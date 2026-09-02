#!/usr/bin/env python3
"""Open the USB-Serial/JTAG port, optionally hard-reset the chip via RTS/DTR, capture output for N seconds."""
import sys, time, serial
port, secs, do_reset, out = sys.argv[1], float(sys.argv[2]), sys.argv[3] == "reset", sys.argv[4]
s = serial.Serial(port, 115200, timeout=0.2)
if do_reset:
    # USB-Serial/JTAG: RTS=1 & DTR=0 pulls EN low (reset); release with both low.
    s.setDTR(False); s.setRTS(False); time.sleep(0.05)
    s.setRTS(True); s.setDTR(False); time.sleep(0.15)
    s.setRTS(False); s.setDTR(False)
t0 = time.time(); buf = b""
with open(out, "wb") as f:
    while time.time() - t0 < secs:
        try:
            chunk = s.read(4096)
        except serial.SerialException:
            # USB-Serial/JTAG re-enumerates on chip reset: reopen and continue
            time.sleep(0.5)
            try:
                s.close(); s = serial.Serial(port, 115200, timeout=0.2)
            except Exception:
                pass
            continue
        if chunk:
            f.write(chunk); f.flush(); buf += chunk
s.close()
txt = buf.decode("utf-8", "replace")
print(f"captured {len(buf)} bytes, {txt.count(chr(10))} lines")
