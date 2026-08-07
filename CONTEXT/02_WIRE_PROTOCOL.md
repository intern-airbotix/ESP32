# 2. Wire protocol & the broadcast filter

## Port scheme (v2.0 — the mavesp8266 scheme)

```
14555  drone side   — the radio BINDS here; GCS unicast + swarm broadcasts land here
14550  GCS side     — the radio SENDS here (telemetry + discovery broadcast)
5760   TCP          — MAVLink over TCP (Mission Planner etc.)
80     HTTP         — web GUI + REST API (no auth)
1606   internal     — DroneBridge RSSI telemetry, multicast 232.10.11.12
```

This mirrors mavesp8266 (CPORT 14555 / HPORT 14550), which is *why* mavesp never needed a broadcast
filter: drones only ever **send to** 14550 and only ever **listen on** 14555, so a drone can never
hear another drone. The separation is structural, not filtered.

Skybrush's stock preset — `udp-listen://:14550?broadcast_port=14555` — fits this exactly with no
configuration.

## State machine (STA mode = show mode)

```
boot → join STA → no UDP client known
      → broadcast full telemetry to <subnet>.255:14550 (from source port 14555)
      → any accepted packet from IP X → register X at the fixed GCS port
      → unicast telemetry to X:14550
      → X silent 30 s → entry expires → discovery broadcast resumes
pinned host (udp_client_ip / udp_client_port set) → never expires, no discovery
```

Client list: 8 slots. In STA mode the sender's source port is **rewritten to the fixed GCS port**
before registration, so every socket of one GCS host collapses into a single entry — this is also
what lets the server's broadcast socket refresh the same entry its unicast traffic created.

## The broadcast filter (`main/db_esp32_control.c`, STA mode only)

Datagrams whose **IP destination** is a subnet or global broadcast are classified:

```c
if (proto == MAVLINK) {
    src_sysid = (buf[0] == 0xFD) ? buf[5]     // MAVLink v2: sysid at byte 5
              : (buf[0] == 0xFE) ? buf[3]     // MAVLink v1: sysid at byte 3
              : 0;
    is_gcs_broadcast = src_sysid > 250;       // GCS software 251-255; drones 1-250
} else {
    is_gcs_broadcast = src_port != own_listen_port;   // non-MAVLink fallback
}
if (!is_gcs_broadcast) { drop entire datagram; }
```

**Accepted** → forwarded to the FC **and** the sender is registered as a telemetry client.
**Dropped** → whole datagram discarded (`recv_length = 0`), silently.

Why sysid and not source port: the Skybrush server broadcasts **from the same socket it listens on**,
so its source port is 14550 — indistinguishable from a drone's discovery broadcast by port alone.
That single misclassification is what broke RTK, GCS heartbeat, LED control, RC override and
broadcast commands in v1.0.

Constraints worth knowing:
- It only runs in **STA mode**. In AP mode every broadcast is forwarded (harmless — each AP-mode
  drone owns its own subnet), so **do not validate the filter in AP mode**.
- It depends on `IP_PKTINFO`, which needs **`CONFIG_LWIP_NETBUF_RECVINFO=y`**. Without it the
  `setsockopt` fails, the code logs a warning and continues, and the filter **fails open** — every
  peer broadcast reaches the FC. Treat that config line as load-bearing.
- It inspects only the **first frame's first byte**. A datagram that is not MAVLink-framed
  (0xFD/0xFE) gets sysid 0 and is dropped. Do not introduce raw non-MAVLink broadcast sources.
- `sysid > 250` is hard-coded, while the Skybrush server's system_id is operator-configurable
  (defaults 254/255). Pin the server's system_id and cap fleet sysids at ≤ 250.

## Consequences for Skybrush traffic

| Class | How the server sends it | v1.0 | v2.0 |
|---|---|---|---|
| Telemetry, per-drone commands, takeoff | unicast | works | works |
| RTK / RTCM | **broadcast only**, no fallback | **dead** | works |
| GCS heartbeat 1 Hz | **broadcast only** (unicast variant is dead code) | **dead** | works |
| LED control, RC override | broadcast only | **dead** | works |
| Broadcast-toggle commands | broadcast, `target_system=0` | **dead**, silently | works |
| Show start time / authorization | broadcast **+ per-drone PARAM_SET fallback** | degraded | works |
| TIMESYNC | never sent by the server at all | n/a | n/a |

Note on Skybrush error code **67 (TIMESYNC_ERROR)**: it is set from a flag inside the drone's own
DATA16 show-status packet (`IS_GPS_TIME_BAD`) — it means the drone has no valid GPS time, which is
normal indoors with 0 satellites. It is **not** a link or firmware problem and clears outdoors.
