#!/usr/bin/env python3
"""Minimal DHCP server for ONE direct link (PC network port <-> board). Needs root.

  sudo tools/hw/dhcp_server.py --iface eth1                    # DISCOVER/OFFER/REQUEST/ACK
  sudo tools/hw/dhcp_server.py --iface eth1 --lease 30         # renewal every 15 s
  sudo tools/hw/dhcp_server.py --iface eth1 --mode silent      # no answers: the board's static fallback
  sudo tools/hw/dhcp_server.py --iface eth1 --mode nak         # NAK every REQUEST
  sudo tools/hw/dhcp_server.py --iface eth1 --advertise 10     # PC side offers 10 Mbit/s only (or 100)

The PC side gets the first address of --net, the board the 50th; --fallback-net
adds a second PC address so that the board's static fallback address is
reachable too. Answers carry no router and no DNS server.

It leaves the rest of the machine alone: the socket is bound to --iface
(SO_BINDTODEVICE), an interface carrying the default route is refused, and
so is any --net / --fallback-net that overlaps a route on another interface.
NetworkManager is told not to manage --iface while this runs, and when
firewalld runs, --iface alone goes into its "trusted" zone (runtime only; the
default zones drop UDP port 67). On exit (Ctrl-C, SIGTERM, SIGHUP) the added addresses
are removed and the NetworkManager state, the firewalld zone and the advertised
link modes are restored.
"""
import argparse
import ipaddress
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time

MAGIC = b"\x63\x82\x53\x63"
NAMES = {1: "DISCOVER", 2: "OFFER", 3: "REQUEST", 4: "DECLINE", 5: "ACK", 6: "NAK", 7: "RELEASE", 8: "INFORM"}
MODES = {"10baseT/Half": 0x001, "10baseT/Full": 0x002, "100baseT/Half": 0x004, "100baseT/Full": 0x008,
         "1000baseT/Half": 0x010, "1000baseT/Full": 0x020}
ADVERTISE = {"10": 0x003, "100": 0x00C}


def sh(*cmd, check=True):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"{' '.join(cmd)}: {r.stderr.strip()}")
    return r.stdout


def log(f, msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    if f:
        f.write(line + "\n")
        f.flush()


def options(data):
    out, i = {}, 0
    while i + 1 < len(data) and data[i] != 255:
        if data[i] == 0:
            i += 1
            continue
        out[data[i]] = data[i + 2:i + 2 + data[i + 1]]
        i += 2 + data[i + 1]
    return out


def link_modes(iface, which):
    """ethtool's 'Supported' / 'Advertised' link modes as an advertise mask."""
    text = sh("ethtool", iface, check=False)
    m = re.search(rf"{which} link modes:(.*?)\n\s*{which} pause", text, re.S)
    return sum(MODES.get(w, 0) for w in m.group(1).split()) if m else 0


def check_isolated(iface, nets):
    if iface in sh("ip", "-4", "route", "show", "default").split():
        sys.exit(f"{iface} carries the default route - refusing")
    for line in sh("ip", "-4", "route", "show").splitlines():
        p = line.split()
        if p[0] == "default" or "dev" not in p or p[p.index("dev") + 1] == iface:
            continue
        other = ipaddress.ip_network(p[0], strict=False)
        for mine in nets:
            if mine.overlaps(other):
                sys.exit(f"{mine} overlaps {other} on {p[p.index('dev') + 1]} - pick another network")


def reply(pkt, kind, client, host, net, lease):
    body = struct.pack("!BBBB4sHH4s4s4s4s16s64s128s", 2, 1, 6, 0, pkt[4:8], 0, 0x8000, pkt[12:16],
                       socket.inet_aton(client if kind != 6 else "0.0.0.0"), socket.inet_aton(host),
                       b"\0" * 4, pkt[28:34] + b"\0" * 10, b"", b"")
    opt = bytes([53, 1, kind, 54, 4]) + socket.inet_aton(host)
    if kind != 6:
        opt += bytes([1, 4]) + socket.inet_aton(str(net.netmask))
        opt += bytes([51, 4]) + struct.pack("!I", lease)
        opt += bytes([58, 4]) + struct.pack("!I", lease // 2)
        opt += bytes([59, 4]) + struct.pack("!I", lease * 7 // 8)
    return body + MAGIC + opt + b"\xff"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", required=True, help="network interface wired to the board only")
    ap.add_argument("--net", default="172.31.250.0/24", help="DHCP network (PC .1, board .50)")
    ap.add_argument("--lease", type=int, default=600, help="lease time, s (renewal at half of it)")
    ap.add_argument("--mode", choices=("serve", "silent", "nak"), default="serve")
    ap.add_argument("--mac", help="answer only this client MAC (aa:bb:cc:dd:ee:ff)")
    ap.add_argument("--fallback-net", default="192.168.1.1/24",
                    help="extra PC address to reach the board's static fallback ('' = none)")
    ap.add_argument("--advertise", choices=sorted(ADVERTISE), help="link speed offered by the PC side, Mbit/s")
    ap.add_argument("--log", help="also append the log to this file")
    a = ap.parse_args()
    if os.geteuid():
        sys.exit("run as root (port 67, interface addresses)")

    net = ipaddress.ip_network(a.net)
    host, client = str(net.network_address + 1), str(net.network_address + 50)
    extra = [ipaddress.ip_interface(a.fallback_net)] if a.fallback_net else []
    check_isolated(a.iface, [net] + [e.network for e in extra])

    f = open(a.log, "a") if a.log else None
    states = dict(line.split(":", 1) for line in
                  sh("nmcli", "-t", "-f", "DEVICE,STATE", "device", check=False).splitlines() if ":" in line)
    was_managed = states.get(a.iface, "unmanaged") != "unmanaged"
    advertised = link_modes(a.iface, "Advertised") if a.advertise else 0
    firewalld = subprocess.run(["firewall-cmd", "--state"], capture_output=True).returncode == 0 \
        if shutil.which("firewall-cmd") else False
    zone = sh("firewall-cmd", f"--get-zone-of-interface={a.iface}", check=False).strip() if firewalld else ""
    added, trusted = [], False
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda *_: sys.exit(0))
    try:
        if was_managed:
            sh("nmcli", "device", "set", a.iface, "managed", "no")
        for addr in [f"{host}/{net.prefixlen}"] + [str(e) for e in extra]:
            if subprocess.run(["ip", "addr", "add", addr, "dev", a.iface]).returncode == 0:
                added.append(addr)
        sh("ip", "link", "set", a.iface, "up")
        if firewalld and zone != "trusted":
            sh("firewall-cmd", "--zone=trusted", f"--change-interface={a.iface}")
            trusted = True
        if a.advertise:
            sh("ethtool", "-s", a.iface, "autoneg", "on", "advertise", hex(ADVERTISE[a.advertise]))

        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, a.iface.encode())
        s.bind(("0.0.0.0", 67))
        log(f, f"{a.iface}: PC {host}/{net.prefixlen}{' + ' + a.fallback_net if extra else ''}, offer {client}, "
               f"lease {a.lease} s, mode {a.mode}{', advertise ' + a.advertise + ' Mbit/s' if a.advertise else ''}")
        while True:
            pkt, peer = s.recvfrom(1500)
            if len(pkt) < 240 or pkt[236:240] != MAGIC or pkt[0] != 1:
                continue
            ciaddr = socket.inet_ntoa(pkt[12:16])
            mac = ":".join(f"{b:02x}" for b in pkt[28:34])
            o = options(pkt[240:])
            t = o.get(53, b"\0")[0]
            req = socket.inet_ntoa(o[50]) if len(o.get(50, b"")) == 4 else "-"
            log(f, f"<- {NAMES.get(t, t):8} {mac} xid {pkt[4:8].hex()} ciaddr {ciaddr} requested {req} "
                   f"(from {peer[0]})")
            if (a.mac and mac != a.mac.lower()) or a.mode == "silent" or t not in (1, 3):
                continue
            if t == 1:
                kind = 2
            elif a.mode == "nak" or req not in ("-", client) or ciaddr not in ("0.0.0.0", client):
                kind = 6
            else:
                kind = 5
            dest = ciaddr if ciaddr != "0.0.0.0" else "255.255.255.255"
            s.sendto(reply(pkt, kind, client, host, net, a.lease), (dest, 68))
            log(f, f"-> {NAMES[kind]:8} {mac} {client if kind != 6 else ''} (to {dest})")
    except KeyboardInterrupt:
        pass
    finally:
        for addr in added:
            sh("ip", "addr", "del", addr, "dev", a.iface, check=False)
        if trusted:
            if zone:
                sh("firewall-cmd", f"--zone={zone}", f"--change-interface={a.iface}", check=False)
            else:
                sh("firewall-cmd", "--zone=trusted", f"--remove-interface={a.iface}", check=False)
        if a.advertise:
            sh("ethtool", "-s", a.iface, "advertise",
               hex(advertised or link_modes(a.iface, "Supported")), check=False)
        if was_managed:
            sh("nmcli", "device", "set", a.iface, "managed", "yes", check=False)
        log(f, "restored")


if __name__ == "__main__":
    main()
