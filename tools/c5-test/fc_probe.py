#!/usr/bin/env python3
"""Read-only probe of an ArduPilot FC over USB: heartbeat/sysid, message mix, and SERIALn_BAUD/_PROTOCOL params."""
import sys, time, struct, serial
from collections import Counter
def x25(data, crc=0xFFFF):
    for b in data:
        tmp = b ^ (crc & 0xFF); tmp ^= (tmp << 4) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc
CRC_EXTRA = {0: 50, 20: 214, 22: 220}
seq = 0
def frame(msgid, payload, sysid=255, compid=190):
    global seq
    hdr = bytes([0xFD, len(payload), 0, 0, seq & 0xFF, sysid, compid]) + msgid.to_bytes(3, "little"); seq += 1
    crc = x25(hdr[1:] + payload); crc = x25(bytes([CRC_EXTRA[msgid]]), crc)
    return hdr + payload + struct.pack("<H", crc)
def frames(buf):
    i = 0; out = []; rest = b""
    while i < len(buf):
        b = buf[i]
        if b == 0xFD and i + 12 <= len(buf):
            ln = buf[i+1]; end = i + 12 + ln + (13 if buf[i+2] & 1 else 0)
            if end > len(buf): break
            out.append(("v2", buf[i+5], buf[i+6], int.from_bytes(buf[i+7:i+10], "little"), buf[i+10:i+10+ln])); i = end
        elif b == 0xFE and i + 8 <= len(buf):
            ln = buf[i+1]; end = i + 8 + ln
            if end > len(buf): break
            out.append(("v1", buf[i+3], buf[i+4], buf[i+5], buf[i+6:i+6+ln])); i = end
        else: i += 1
    return out, buf[i:]
port = sys.argv[1]
s = serial.Serial(port, 115200, timeout=0.2)
buf = b""; allf = []; t0 = time.time(); sysid = None; compid = None
while time.time() - t0 < 4:
    buf += s.read(8192); fs, buf = frames(buf); allf += fs
    for v, sid, cid, mid, pl in fs:
        if mid == 0 and sysid is None:
            sysid, compid = sid, cid
            cm, typ, ap, bm, st, ver = struct.unpack("<IBBBBB", pl[:9])
            print(f"HEARTBEAT from sysid={sid} compid={cid} type={typ} autopilot={ap} base_mode=0x{bm:02x} status={st} mavlink={v}")
if sysid is None: print("no heartbeat seen in 4 s"); sys.exit(1)
c = Counter(m for _, _, _, m, _ in allf); print("msg mix (4 s):", dict(sorted(c.items())))
# ask for serial port params (read-only query)
names = [f"SERIAL{n}_{k}" for n in range(0, 8) for k in ("BAUD", "PROTOCOL")]
for nm in names:
    pid = nm.encode().ljust(16, b"\0")
    s.write(frame(20, struct.pack("<h", -1) + bytes([sysid, compid]) + pid)); time.sleep(0.03)
t0 = time.time(); got = {}
while time.time() - t0 < 6 and len(got) < len(names):
    buf += s.read(8192); fs, buf = frames(buf)
    for v, sid, cid, mid, pl in fs:
        if mid == 22 and len(pl) >= 25:
            val, cnt, idx = struct.unpack("<fHH", pl[:8]); nm = pl[8:24].split(b"\0")[0].decode(errors="replace")
            if nm.startswith("SERIAL"): got[nm] = val
s.close()
for n in range(0, 8):
    b = got.get(f"SERIAL{n}_BAUD"); p = got.get(f"SERIAL{n}_PROTOCOL")
    if b is not None or p is not None: print(f"SERIAL{n}: BAUD={b} PROTOCOL={p}")
print(f"({len(got)}/{len(names)} params answered)")
