#!/usr/bin/env python3
"""End-to-end test of umcub link: several simulated devices on one bus.

  link_e2e.py <build/host dir> [--python .venv/bin/python]

Starts device simulators (tests/host/sim_device.c: real mux, umcub link,
boot_serial on emulated flash) and a bus hub that gives tools/umcub_link.py a
serial port (pty): every byte from the host reaches every device, device lines
are merged back. Checks discovery, addressing (only the addressed device
answers), selection by UID, the SECURE handshake, a wrong admin key, and
standard smpmgr through `umcub_link.py serve`.
"""
import argparse
import base64
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tty

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LINK = os.path.join(ROOT, "tools/umcub_link.py")
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
                    if line[:2] == b"\x05\x0b":
                        try:
                            f = base64.b64decode(line[2:])
                            self.sent[i].append((f[2], struct.unpack_from("<H", f, 4)[0]))
                        except ValueError:
                            pass
                    os.write(self.master, line + b"\n")     # one whole line at a time


def run(py, args, timeout=30):
    p = subprocess.run([py, LINK] + args, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--python", default=sys.executable)
    a = ap.parse_args()
    py = a.python
    smpmgr = os.path.join(os.path.dirname(py), "smpmgr")
    adm = os.path.join(KEYS, "dev-admin-p256.pem")
    dev = os.path.join(KEYS, "dev-device-p256.pem")

    # node 1, node 2, unassigned (0) - all SECURE with payload encryption
    nodes = [(1, 0x1001), (2, 0x2002), (0, 0x3003)]
    sims = [subprocess.Popen([os.path.join(a.build, "umcub_sim_secure")], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, env=dict(os.environ, UMCUB_SIM_ADDR=str(n),
                                                             UMCUB_SIM_UID=str(u))) for n, u in nodes]
    hub = Hub(sims)
    hub.start()
    port = ["--port", hub.path]
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
        rc, out = run(py, port + ["--addr", "1"] + sec + ["info"])
        check(rc == 0 and "authenticated" in out, "info --addr 1: authenticated session")
        rc, out = run(py, port + ["--addr", "2"] + sec + ["cmd", "i"])
        check(rc == 0 and "board type" in out, "cmd i --addr 2 (encrypted DATA)")
        data_from = {k: sum(1 for t, _ in v if t == 7) for k, v in hub.sent.items()}
        check(data_from[0] == 0 and data_from[1] == 1 and data_from[2] == 0,
              f"only node 2 answered the DATA ({data_from})")
        if uids:
            rc, out = run(py, port + ["--uid", uids[0]] + sec + ["cmd", "i"])
            check(rc == 0 and "node 0" in out, "cmd i --uid <unassigned>")

        print("[e2e] wrong admin key is refused", flush=True)
        with tempfile.TemporaryDirectory() as d:
            bad = os.path.join(d, "bad.pem")
            subprocess.run([os.path.join(os.path.dirname(py), "imgtool"), "keygen", "-t", "ecdsa-p256", "-k", bad],
                           check=True, capture_output=True)
            rc, out = run(py, port + ["--addr", "1", "--admin-key", bad, "--device-key", dev, "cmd", "i"])
            check(rc != 0 and "authentication failed" in out, "AUTH with another admin key fails")

        print("[e2e] smpmgr through `umcub_link.py serve` (SECURE, encrypted)", flush=True)
        with tempfile.TemporaryDirectory() as d:
            link = os.path.join(d, "smp")
            srv = subprocess.Popen([py, LINK] + port + ["--addr", "1"] + sec + ["serve", "--pty-link", link],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for _ in range(100):
                if os.path.exists(link):
                    break
                time.sleep(0.05)
            p = subprocess.run([smpmgr, "--port", link, "--timeout", "5", "os", "echo", "hello-umcub"],
                               capture_output=True, text=True, timeout=60)
            check("hello-umcub" in p.stdout, "smpmgr os echo")
            # Image for the simulator's configuration (board type TLV, encrypted with the device key),
            # signed the way an IDE project would do it.
            subprocess.run([py, os.path.join(ROOT, "tools/umcub_image.py"), "sign", "--board", "nucleo_h755zi_q",
                            "--core", "cm7", "--pre", os.path.join(ROOT, "tests/host/cfg_swap_scratch.h"),
                            "--post", os.path.join(ROOT, "tests/host/test_link_secure_post.h"), "--version",
                            "1.2.3", os.path.join(a.build, "payload.bin"), "-o", os.path.join(d, "app")],
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
    finally:
        hub.stop = True
        for s in sims:
            s.stdin.close()
            s.wait(5)

    print(f"\n{failures} failure(s)" if failures else "\nALL E2E TESTS PASSED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
