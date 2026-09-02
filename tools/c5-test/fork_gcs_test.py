#!/usr/bin/env python3
"""GCS-side test for the mavesp8266-style fork: send GCS heartbeats (sysid 255) to <esp>:<send_port>,
listen on <listen_port> for telemetry, and optionally read TCP 5760. Reports frames by sysid/msgid."""
import sys, time, socket, struct, threading
from collections import Counter
def x25(data, crc=0xFFFF):
    for b in data:
        tmp = b ^ (crc & 0xFF); tmp ^= (tmp << 4) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc
CRC_EXTRA = {0: 50, 109: 185, 30: 39, 1: 124, 33: 104, 24: 24, 20: 214, 22: 220}
NAMES = {0: "HEARTBEAT", 1: "SYS_STATUS", 24: "GPS_RAW_INT", 30: "ATTITUDE", 33: "GLOBAL_POSITION_INT", 109: "RADIO_STATUS", 74: "VFR_HUD", 253: "STATUSTEXT"}
seq = 0
PARAMS = []
def frame(msgid, payload, sysid, compid=190):
    global seq
    hdr = bytes([0xFD, len(payload), 0, 0, seq & 0xFF, sysid, compid]) + msgid.to_bytes(3, "little"); seq += 1
    crc = x25(hdr[1:] + payload); crc = x25(bytes([CRC_EXTRA[msgid]]), crc)
    return hdr + payload + struct.pack("<H", crc)
def heartbeat(sysid): return frame(0, struct.pack("<IBBBBB", 0, 6, 8, 0, 4, 3), sysid)  # type GCS, autopilot invalid
def parse(buf):
    i = 0; out = []
    while i < len(buf):
        b = buf[i]
        if b == 0xFD and i + 12 <= len(buf):
            ln = buf[i+1]; end = i + 12 + ln + (13 if buf[i+2] & 1 else 0)
            if end > len(buf): break
            mid = int.from_bytes(buf[i+7:i+10], "little"); ln2 = buf[i+1]
            if mid == 22 and ln2 >= 24:
                pl = buf[i+10:i+10+ln2]; val, cnt, idx = struct.unpack("<fHH", pl[:8]); PARAMS.append((pl[8:24].split(b"\0")[0].decode(errors="replace"), val))
            out.append((buf[i+5], mid)); i = end
        elif b == 0xFE and i + 8 <= len(buf):
            ln = buf[i+1]; end = i + 8 + ln
            if end > len(buf): break
            out.append((buf[i+3], buf[i+5])); i = end
        else: i += 1
    return out
def summarize(tag, data):
    fr = parse(data); c = Counter(fr)
    print(f"{tag}: {len(data)} bytes, {len(fr)} MAVLink frames, sysids={sorted(set(s for s,_ in fr))}")
    for (s, m), n in sorted(c.items(), key=lambda kv: -kv[1])[:8]: print(f"   sysid={s:3d} msgid={m:5d} {NAMES.get(m,'?'):20s} x{n}")
host = sys.argv[1]; send_port = int(sys.argv[2]); listen_port = int(sys.argv[3]); secs = float(sys.argv[4]); do_tcp = len(sys.argv) > 5 and sys.argv[5] == "tcp"
GCS = 255
TARGET_SYSID = int(sys.argv[6]) if len(sys.argv) > 6 else 0
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
udp.bind(("0.0.0.0", listen_port)); udp.settimeout(0.2)
udp_rx = b""; tcp_rx = b""; stop = False; srcs = Counter()
def rd_udp():
    global udp_rx
    while not stop:
        try: d, a = udp.recvfrom(65535); udp_rx += d; srcs[a] += 1
        except socket.timeout: pass
def rd_tcp():
    global tcp_rx
    try: t = socket.create_connection((host, 5760), timeout=3); t.settimeout(0.2)
    except OSError as e: print("TCP 5760 connect failed:", e); return
    while not stop:
        try: tcp_rx += t.recv(65535)
        except socket.timeout: pass
        except OSError: break
    t.close()
ths = [threading.Thread(target=rd_udp, daemon=True)] + ([threading.Thread(target=rd_tcp, daemon=True)] if do_tcp else [])
[t.start() for t in ths]
t0 = time.time(); n = 0
while time.time() - t0 < secs:
    udp.sendto(heartbeat(GCS), (host, send_port)); n += 1
    if n == 3 and TARGET_SYSID:  # round trip through the UART: PARAM_REQUEST_READ -> FC -> PARAM_VALUE
        for nm in ("SERIAL2_BAUD", "SERIAL2_PROTOCOL", "SYSID_THISMAV"):
            udp.sendto(frame(20, struct.pack("<h", -1) + bytes([TARGET_SYSID, 1]) + nm.encode().ljust(16, b"\0"), GCS), (host, send_port))
    time.sleep(1.0)
time.sleep(0.5); stop = True; time.sleep(0.3)
print(f"sent {n} GCS heartbeats (sysid {GCS}) to {host}:{send_port}; UDP sources seen: {dict(srcs)}")
summarize(f"UDP {listen_port} rx", udp_rx)
if do_tcp: summarize("TCP 5760 rx", tcp_rx)
print("PARAM_VALUE replies via bridge:", PARAMS[:6])
