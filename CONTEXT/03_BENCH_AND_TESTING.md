# 3. Bench setup, testing & traps

## Fleet configuration scheme

Drone *n* = hostname & AP-SSID **`A BOT N2 n`**, static IP **192.168.1.n**/24, gateway 192.168.1.254.
Common to all: STA SSID **`GCS 2`**, own AP password `Emax23Air&`, **baud 921600**, FC on
**GPIO16 (TX) / GPIO17 (RX)**, protocol MAVLink, `udp_listen_port` **14555** (v2.0).
FC system IDs are independent of the IP scheme (bench FCs seen: 104, 1).

Known boards: ESP MAC `98:a3:16:61:0f:94` = N2 1 · `98:a3:16:61:1d:e4` = N2 4 ·
`98:a3:16:61:2a:94` = original bench unit.

## Bench hardware (as of 2026-08-06)

- **ESP32-C6** on USB (`/dev/ttyACM*`) — **assignments shuffle on every replug**, always check
  `udevadm info --query=property /dev/ttyACM0 | grep ID_MODEL`.
- **MicoAir743v2** — ArduCopter V4.6.3 with Skybrush show firmware, sysid 104, SERIAL1 = MAVLink2 @
  921600 wired to the ESP. Also exposes its own USB port.
- A **Matek H743** may be present belonging to a different drone / another agent — leave it alone
  unless told otherwise.
- FCs on the bench will **not arm** (prearm: no GPS fix indoors, log storage full/ENOSPC, compass).
  An ARM attempt returning a *refusal reason* is a successful uplink test.

## Network recipes (Linux, nmcli)

**Real show AP** — SSID `GCS 2`, BSSID `40:ed:00:8a:ff:29`, device 192.168.1.246. It bridges its
ethernet port onto the drones' 192.168.1.0/24 Wi-Fi LAN, but its wired DHCP hands out 10.223.x.x,
so give the laptop a static alias on the drone subnet:
```
nmcli con modify <eth-con> +ipv4.addresses "192.168.1.200/24" && nmcli device reapply <iface>
```

**Impersonating the show AP** (when the real router isn't present) — drones join it believing it is
the real one, since SSID + password is all they check:
```
nmcli device wifi hotspot ifname wlo1 con-name gcs2-test ssid "GCS 2" password 'North9Gondola%' band bg channel 6
```
Only **one** "GCS 2" may be on the air at a time — take the hotspot down (`nmcli con down gcs2-test`)
before powering the real router near the bench, or drones will join whichever is stronger.

**Subnet collision trap:** the office ethernet is also 192.168.1.0/24 **and the office router is
192.168.1.1 — the same address as drone 1**. Add /32 host routes for the drone IPs on the Wi-Fi
interface, and for *broadcast* tests disconnect the ethernet entirely (the local broadcast route
beats a /32).

## Skybrush

Installed at `~/Documents/drone_show/skybrush-test-server/`:
- `skyenv-2.25/` → **server 2.25.2 — the production version, use this one**
- `skyenv/` → 2.49.1 (newer, for comparison)
- `sky_stock.jsonc` → stock preset (drones on 14555) · `sky.jsonc` → 14550 scheme (v1.0-era)
- `check_drone.py` → visibility + telemetry + decoded error badges; `--arm` adds an uplink round trip

Run: `./skyenv-2.25/bin/skybrushd -c sky_stock.jsonc`

Installing 2.25.2 is non-trivial (not on PyPI; deps on a private Gemfury index; one dependency must
come from a GitHub tag) — the working venv already exists, don't rebuild it casually.

Server internals worth knowing: UAVs are only created from **autopilot** heartbeats (a bare ESP with
no FC never appears); the TCP JSON API is on 127.0.0.1:5001 with newline-delimited
`{"$fw.version":"1.0","id":…,"body":{…}}` envelopes (`UAV-LIST`, `UAV-INF`, `UAV-MOTOR`, …);
error codes decode via `flockwave.spec.errors.FlockwaveErrorCode`.

## Traps that cost real time

1. **`idf.py fullclean` wipes the build target** → the next build silently produces an **esp32**
   image instead of esp32c6, and the USB-JTAG console option is dropped without error. Always
   `idf.py set-target esp32c6` first and verify the three gate flags.
2. **Opening the ESP's USB serial port resets the board**, and DTR/RTS asserted holds it in reset —
   open with `dtr=False, rts=False` (pyserial). Plain `cat`/`screen` makes the board look dead.
3. **`pkill -f <pattern>` matches its own wrapping shell** (exit 144). Kill by exact PID, or use a
   pattern that cannot match itself (`sky[.]jsonc`).
4. **Only one process can bind UDP 14550** — Mission Planner, a stray checker, or a second server
   will block skybrushd.
5. **Flockwave subscriptions need a settle moment** — a reused TCP connection can report ~1
   telemetry update where a fresh connection reports 50+. Not a firmware fault; re-measure cleanly.
6. **`read_bytes` counts FC→ESP telemetry**, which flows continuously — it cannot isolate whether a
   specific uplink packet arrived. Use client registration or an FC *response* (e.g. the banner from
   `MAV_CMD_DO_SEND_BANNER = 42428`) as the observable.
7. In v2.0 an FC reply goes to the **fixed GCS port**, not your ephemeral test socket — bind 14550
   or command through the server.

## Useful probes

```
curl -s http://192.168.1.1/api/system/stats      # read_bytes, serial_dec_mav_msgs, udp_clients, rssi
curl -s http://192.168.1.1/api/settings          # full config (POST reboots the board)
~/dronebridge-c6-report/provision_drone.py --check --range 1-20
```
