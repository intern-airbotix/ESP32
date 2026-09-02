# C5 bench-test helpers (no pymavlink needed, only pyserial)

- `capture_serial.py PORT SECONDS reset|noreset OUTFILE` - capture the USB console log, optionally hard-resetting the chip first.
- `fc_probe.py /dev/ttyACMx` - read-only probe of an ArduPilot FC over USB: heartbeat/sysid and SERIALn_BAUD/PROTOCOL.
- `fork_gcs_test.py ESP_IP 14555 14550 SECONDS [tcp] [FC_SYSID]` - act as a GCS (sysid 255): send heartbeats to the
  board's listen port, count telemetry frames received on 14550 (discovery broadcast, then unicast) and on TCP 5760,
  and do a PARAM_REQUEST_READ round trip through the UART to the FC.
