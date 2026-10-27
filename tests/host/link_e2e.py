#!/usr/bin/env python3
"""End-to-end test of umcub link: several simulated devices on one bus.

  link_e2e.py <build/host dir> [--python .venv/bin/python]

Starts device simulators (tests/host/sim_device.c: real mux, umcub link,
boot_serial on emulated flash) and a bus hub that gives the `umcub` tool
(tools/host/umcub) a serial port (pty): every byte from the host reaches every
device, device lines are merged back. Checks discovery, addressing (only the
addressed device answers), selection by UID, the SECURE handshake, a wrong admin
key, SMP upload through the link, and standard smpmgr through `umcub serve`.
Then SMP on a plain device (umcub_sim_plain: smpmgr and umcub), on the CAN
transport (umcub_sim_can, python-can "serial" bus on a pty) and lite upload.
"""
import argparse
import base64
import os
import pty
import select
import subprocess
import sys
import tempfile
import threading
import time
import tty

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools/host"))
from umcub import link as ulink  # noqa: E402  (frame format)
LINK_LINE = bytes([0x05, 0x0B])
ENV = dict(os.environ, PYTHONPATH=os.path.join(ROOT, "tools/host"))
KEYS = os.path.join(ROOT, "tools/keys")

failures = 0


def check(cond, what):
    global failures
    print(("  ok    " if cond else "  FAIL  ") + what, flush=True)
    if not cond:
        failures += 1


class Hub(threading.Thread):
    """The bus: host pty <-> all simulator stdin/stdout, line by line."""

    def __init__(self, sims):
        super().__init__(daemon=True)
        self.sims = sims
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self.slave = slave
        self.sent = {i: [] for i in range(len(sims))}   # frames (type, dst) each device sent
        self.bufs = [b""] * len(sims)
        self.stop = False

    def run(self):
        outs = {p.stdout.fileno(): i for i, p in enumerate(self.sims)}
        while not self.stop:
            r, _, _ = select.select([self.master] + list(outs), [], [], 0.1)
            for fd in r:
                if fd == self.master:
                    data = os.read(self.master, 4096)
                    for p in self.sims:
                        p.stdin.write(data)
                        p.stdin.flush()
                    continue
                i = outs[fd]
                data = os.read(fd, 4096)
                if not data:
                    continue
                self.bufs[i] += data
                while b"\n" in self.bufs[i]:
                    line, self.bufs[i] = self.bufs[i].split(b"\n", 1)
                    if line[:2] == LINK_LINE:
                        try:
                            f = ulink.parse_frame(base64.b64decode(line[2:], validate=True))
                        except ValueError:
                            f = None
                        if f:
                            self.sent[i].append((f["type"], f["dst"]))
                    os.write(self.master, line + b"\n")     # one whole line at a time


def tool(py, args, timeout=60):
    """The umcub command line tool; returns the CompletedProcess."""
    return subprocess.run([py, "-m", "umcub"] + args, capture_output=True, text=True, timeout=timeout, env=ENV)


def run(py, args, timeout=30):
    p = tool(py, args, timeout)
    return p.returncode, p.stdout + p.stderr


class ByteHub(threading.Thread):
    """A pty passing raw bytes to and from one simulator (python-can "serial" CAN bus)."""

    def __init__(self, sim):
        super().__init__(daemon=True)
        self.sim = sim
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self.slave = slave
        self.stop = False

    def run(self):
        out = self.sim.stdout.fileno()
        while not self.stop:
            for fd in select.select([self.master, out], [], [], 0.1)[0]:
                data = os.read(fd, 4096)
                if fd == self.master:
                    self.sim.stdin.write(data)
                    self.sim.stdin.flush()
                elif data:
                    os.write(self.master, data)


def can_tools(build, py):
    """umcub over CAN ISO-TP (smpclient on the umcub connection) against the real umcub CAN transport."""
    print("[e2e] CAN: umcub echo / upload / list / verify / hash over ISO-TP", flush=True)
    sim = subprocess.Popen([os.path.join(build, "umcub_sim_can")], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    hub = ByteHub(sim)
    hub.start()
    can = ["-d", f"can:serial:{hub.path}", "--timeout", "10"]
    try:
        p = tool(py, can + ["echo", "hello-can"])
        check(p.returncode == 0 and "hello-can" in p.stdout, "umcub echo over CAN")
        with tempfile.TemporaryDirectory() as d:
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/sim_can_post.h"), "--version", "3.1.4",
                            os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app")],
                           check=True, capture_output=True)
            p = tool(py, can + ["upload", os.path.join(d, "app.signed.bin")], timeout=300)
            check(p.returncode == 0 and "uploaded" in p.stdout, "umcub upload over CAN")
            p = tool(py, can + ["verify", "0", "0"])
            check(p.returncode == 0 and "valid" in p.stdout, "umcub verify 0 0 over CAN")
            p = tool(py, can + ["hash", "0", "0", "--file", os.path.join(d, "app.signed.bin")])
            check(p.returncode == 0 and "MATCH" in p.stdout, "umcub hash --file over CAN: MATCH")
        p = tool(py, can + ["list"])
        check(p.returncode == 0 and "slot 0: 3.1.4" in p.stdout, "umcub list over CAN: 3.1.4 in slot 0")
    finally:
        hub.stop = True
        sim.stdin.close()
        sim.wait(5)


def lite_tools(build, py):
    """Bootloaders without SMP: umcub upload --lite on a stream (pty) and on CAN."""
    print("[e2e] without SMP: umcub upload --lite over a stream and over CAN, verify", flush=True)
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                        "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                        "--post", os.path.join(ROOT, "tests/host/sim_lite_post.h"), "--version", "4.0.1",
                        os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app")],
                       check=True, capture_output=True)
        img = os.path.join(d, "app.signed.bin")
        bad = os.path.join(d, "bad.bin")
        data = bytearray(open(img, "rb").read())
        data[0x800] ^= 1
        open(bad, "wb").write(data)

        sim = subprocess.Popen([os.path.join(build, "umcub_sim_lite")], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, env=dict(os.environ, UMCUB_SIM_ADDR="0", UMCUB_SIM_UID="9"))
        hub = Hub([sim])
        hub.start()
        try:
            port = ["-d", hub.path]
            p = tool(py, port + ["upload", "--lite", img], timeout=120)
            check(p.returncode == 0 and "uploaded" in p.stdout, "umcub upload --lite over a stream")
            p = tool(py, port + ["cmd", "verify 0 0"])
            check("ok valid" in p.stdout, "text command verify 0 0: valid")
            p = tool(py, port + ["cmd", "i"])
            check("image 0 slot 0: 4.0.1+0" in p.stdout, "text command i: 4.0.1 in slot 0")
            p = tool(py, port + ["upload", "--lite", bad], timeout=120)
            p = tool(py, port + ["cmd", "verify 0 0"])
            check("bad hash" in p.stdout, "tampered upload: verify reports it invalid")
        finally:
            hub.stop = True
            sim.stdin.close()
            sim.wait(5)

        sim = subprocess.Popen([os.path.join(build, "umcub_sim_can_lite")], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE)
        hub = ByteHub(sim)
        hub.start()
        try:
            can = ["-d", f"can:serial:{hub.path}"]
            p = tool(py, can + ["upload", "--lite", img, "--chunk", "512"], timeout=300)
            check(p.returncode == 0 and "uploaded" in p.stdout, "umcub upload --lite over CAN ISO-TP")
            p = tool(py, can + ["cmd", "verify 0 0"])
            check("ok valid" in p.stdout, "text command over CAN: verify 0 0 valid")
        finally:
            hub.stop = True
            sim.stdin.close()
            sim.wait(5)


def plain_tools(build, py, smpmgr):
    """Standard SMP on one plain device: smpmgr upload, umcub upload / verify / hash / read."""
    print("[e2e] plain SMP: smpmgr upload, umcub upload / verify / hash / read", flush=True)
    sim = subprocess.Popen([os.path.join(build, "umcub_sim_plain")], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, env=dict(os.environ, UMCUB_SIM_ADDR="0", UMCUB_SIM_UID="7"))
    hub = Hub([sim])
    hub.start()
    port = ["-d", hub.path, "--timeout", "5"]
    try:
        with tempfile.TemporaryDirectory() as d:
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/sim_plain_post.h"), "--version", "2.0.1",
                            os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app")],
                           check=True, capture_output=True)
            img = os.path.join(d, "app.signed.bin")
            p = subprocess.run([smpmgr, "--port", hub.path, "--timeout", "5", "--line-buffers", "8", "image",
                                "upload", img], capture_output=True, text=True, timeout=300)
            check(p.returncode == 0, "smpmgr image upload (plain)")
            p = tool(py, port + ["verify", "0", "0"])
            check(p.returncode == 0 and "valid" in p.stdout, "umcub verify 0 0")
            p = tool(py, port + ["hash", "0", "0", "--file", img])
            check(p.returncode == 0 and "MATCH" in p.stdout, "umcub hash --file: MATCH")
            p = tool(py, port + ["verify", "0", "1"])
            check(p.returncode != 0 and "no image" in p.stdout + p.stderr, "umcub verify of an empty slot")
            dump = os.path.join(d, "dump.bin")
            p = tool(py, port + ["read", "0", "0", "--len", "1000", "--out", dump], timeout=120)
            check(p.returncode == 0 and open(dump, "rb").read() == open(img, "rb").read()[:1000],
                  "umcub read: readback equals the file")
            p = tool(py, port + ["upload", img], timeout=300)
            check(p.returncode == 0 and "uploaded" in p.stdout, "umcub upload (SMP over NLIP lines)")
            if p.returncode:
                print(p.stdout[-500:], p.stderr[-1500:])
            p = tool(py, port + ["list"])
            check(p.returncode == 0 and "2.0.1" in p.stdout, "umcub list: 2.0.1")
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/sim_plain_post.h"), "--version", "2.0.2",
                            os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app2")],
                           check=True, capture_output=True)
            app2 = os.path.join(d, "app2.signed.bin")
            p = tool(py, port + ["upload", "--slot", "secondary", app2], 300)
            p = tool(py, port + ["list"])
            check(any("slot 1: 2.0.2" in l and "pending" not in l for l in p.stdout.splitlines()),
                  "umcub upload --slot secondary (SMP direct upload): 2.0.2 in slot 1, not marked")
            p = tool(py, port + ["mark", app2])
            check(p.returncode == 0, "umcub mark <image file>")
            p = tool(py, port + ["list"])
            check(any("slot 1: 2.0.2" in l and "pending" in l and "permanent" not in l for l in p.stdout.splitlines()),
                  "umcub list: 2.0.2 pending for a test boot")
            p = tool(py, port + ["reset"])          # last: the simulator exits on the reset
            check(p.returncode == 0 and "reset" in p.stdout, "umcub reset (boot_serial answers rc 0)")
    finally:
        hub.stop = True
        sim.stdin.close()
        sim.wait(5)


def secure_tools(build, py, smpmgr):
    adm = os.path.join(KEYS, "dev-admin-p256.pem")
    dev = os.path.join(KEYS, "dev-device-p256.pem")

    # node 1, node 2, unassigned (0) - all SECURE with payload encryption
    nodes = [(1, 0x1001), (2, 0x2002), (0, 0x3003)]
    sims = [subprocess.Popen([os.path.join(build, "umcub_sim_secure")], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, env=dict(os.environ, UMCUB_SIM_ADDR=str(n),
                                                             UMCUB_SIM_UID=str(u))) for n, u in nodes]
    hub = Hub(sims)
    hub.start()
    port = ["-d", hub.path]
    sec = ["--admin-key", adm, "--device-key", dev]
    try:
        print("[e2e] discovery of 3 devices on one bus", flush=True)
        rc, out = run(py, port + ["discover", "--slots", "8", "--slot-ms", "30"])
        check(rc == 0 and "3 device(s)" in out, "discover finds 3 devices")
        uids = [l.split("uid ")[1].split()[0] for l in out.splitlines() if l.startswith("node ") and
                l.split()[1] == "0"]
        check(len(uids) == 1, "one unassigned device")

        print("[e2e] SECURE handshake, commands by address and by UID", flush=True)
        for k in hub.sent:
            hub.sent[k].clear()
        rc, out = run(py, port + ["--node", "1"] + sec + ["info"])
        check(rc == 0 and "authenticated" in out, "info --addr 1: authenticated session")
        rc, out = run(py, port + ["--node", "2"] + sec + ["cmd", "i"])
        check(rc == 0 and "board type" in out, "cmd i --addr 2 (encrypted DATA)")
        data_from = {k: sum(1 for t, _ in v if t == 7) for k, v in hub.sent.items()}
        check(data_from[0] == 0 and data_from[1] == 1 and data_from[2] == 0,
              f"only node 2 answered the DATA ({data_from})")
        if uids:
            rc, out = run(py, port + ["--uid", uids[0]] + sec + ["cmd", "i"])
            check(rc == 0 and "node 0" in out, "cmd i --uid <unassigned>")

        print("[e2e] an image for another board type is refused before the transfer", flush=True)
        with tempfile.TemporaryDirectory() as d:
            post = os.path.join(d, "other.h")
            open(post, "w").write(f'#include "{ROOT}/tests/host/test_link_secure_post.h"\n'
                                  "#undef UMCUB_CFG_BOARD_TYPE\n#define UMCUB_CFG_BOARD_TYPE 0x12345678\n")
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", post, "--version", "1.0.0", os.path.join(build, "payload.bin"),
                            "-o", os.path.join(d, "other")], check=True, capture_output=True)
            rc, out = run(py, port + ["--node", "1"] + sec + ["upload", os.path.join(d, "other.signed.bin")])
            check(rc != 0 and "is for board type" in out, "umcub upload of another board's image: refused")

        print("[e2e] wrong admin key is refused", flush=True)
        with tempfile.TemporaryDirectory() as d:
            bad = os.path.join(d, "bad.pem")
            subprocess.run([os.path.join(os.path.dirname(py), "imgtool"), "keygen", "-t", "ecdsa-p256", "-k", bad],
                           check=True, capture_output=True)
            rc, out = run(py, port + ["--node", "1", "--admin-key", bad, "--device-key", dev, "cmd", "i"])
            check(rc != 0 and "authentication failed" in out, "AUTH with another admin key fails")

        print("[e2e] smpmgr through `umcub serve` (SECURE, encrypted), umcub upload through the link", flush=True)
        with tempfile.TemporaryDirectory() as d:
            link = os.path.join(d, "smp")
            srv = subprocess.Popen([py, "-m", "umcub"] + port + ["--node", "1"] + sec + ["serve", "--pty-link", link],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=ENV)
            for _ in range(100):
                if os.path.exists(link):
                    break
                time.sleep(0.05)
            p = subprocess.run([smpmgr, "--port", link, "--timeout", "5", "os", "echo", "hello-umcub"],
                               capture_output=True, text=True, timeout=60)
            check("hello-umcub" in p.stdout, "smpmgr os echo")
            # Image for the simulator's configuration (board type TLV, encrypted with the device key),
            # signed the way an IDE project would do it.
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/test_link_secure_post.h"), "--version",
                            "1.2.3", os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app")],
                           check=True, capture_output=True)
            img = os.path.join(d, "app.encrypted.bin")
            p = subprocess.run([smpmgr, "--port", link, "--timeout", "5", "--line-buffers", "8", "image", "upload",
                                img], capture_output=True, text=True, timeout=300)
            check(p.returncode == 0, "smpmgr image upload of an encrypted image")
            p = subprocess.run([smpmgr, "--port", link, "--timeout", "5", "image", "state-read"],
                               capture_output=True, text=True, timeout=60)
            check("1.2.3" in p.stdout, "state-read: 1.2.3 decrypted in place, signature and board type valid")
            srv.terminate()
            srv.wait(5)
            subprocess.run([py, os.path.join(ROOT, "tools/host/umcub"), "image", "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/test_link_secure_post.h"), "--version",
                            "1.2.4", os.path.join(build, "payload.bin"), "-o", os.path.join(d, "app2")],
                           check=True, capture_output=True)
            p = tool(py, port + ["--node", "2"] + sec + ["upload", os.path.join(d, "app2.encrypted.bin")], 300)
            check(p.returncode == 0 and "uploaded" in p.stdout, "umcub upload through the link (SECURE, encrypted)")
            if p.returncode:
                print(p.stdout[-500:], p.stderr[-1500:])
            rc, out = run(py, port + ["--node", "2"] + sec + ["list"])
            check(rc == 0 and "1.2.4" in out, "umcub list through the link: 1.2.4")
    finally:
        hub.stop = True
        for s in sims:
            s.stdin.close()
            s.wait(5)


SCENARIOS = {"secure": secure_tools, "plain": plain_tools,
             "can": lambda build, py, smpmgr: can_tools(build, py),
             "lite": lambda build, py, smpmgr: lite_tools(build, py)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--python", default=sys.executable)
    ap.add_argument("--scenario", choices=sorted(SCENARIOS), help="run only this one (default: all, in parallel)")
    a = ap.parse_args()
    py = a.python
    smpmgr = os.path.join(os.path.dirname(py), "smpmgr")
    if a.scenario:
        SCENARIOS[a.scenario](a.build, py, smpmgr)
        sys.exit(1 if failures else 0)
    # Every scenario has its own simulators and bus: run them side by side,
    # print their output in order.
    procs = [subprocess.Popen([py, __file__, a.build, "--python", py, "--scenario", n], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True) for n in SCENARIOS]
    failed = 0
    for p in procs:
        out, _ = p.communicate(timeout=900)
        sys.stdout.write(out)
        failed += p.returncode != 0
    print(f"\n{failed} scenario(s) failed" if failed else "\nALL E2E TESTS PASSED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
