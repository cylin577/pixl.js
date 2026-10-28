# BLE Control Plane (Always-On)

This document describes the **BLE Control Plane**, a small, always-on BLE interface on Pixl.js.
It is independent of the Nordic UART Service (NUS) file-transfer protocol documented in
`05+1-ble_protocol.md` and stays available even while the device is not in file-transfer mode.

It exposes exactly **two commands**:

1. Enter normal BLE file transfer mode.
2. Enter control mode, where the host sends 3 inputs that map to the 3 physical buttons.

## Service

- Service name: Pixl Control Plane
- Service UUID: `7e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Characteristic (write + write-without-response): `7e400002-b5a3-f393-e0a9-e50e24dcca9e` (Control TX)
- Characteristic (notify): `7e400003-b5a3-f393-e0a9-e50e24dcca9e` (Control RX)

The control-plane UUID is advertised in Pixl.js and AmiLoop modes (it fits the 31-byte
advertising data). In the amiibolink modes the advertisement is full, so the service is not
advertised there, but it still exists on the connection. It is never disabled by entering
file transfer or control mode; only the active mode changes.

## Packet format

Same 4-byte header as the file-transfer protocol:

| Field | Type | Length | Description |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | Command id |
| status | uint8 | 1 | 0 in requests; status code in responses |
| chunk | uint16 | 2 | 0 (no chunking; control packets are single ATT write) |
| data | byte | N | Command-specific payload |

Numeric types are unsigned, little-endian. No chunk transfer is used; each command fits in one ATT write.

## Status codes

| Code | Meaning |
| ---- | ---- |
| 0x00 | OK |
| 0x01 | Generic error |
| 0x02 | Invalid mode transition (e.g. already in requested mode) |
| 0xFF | Unknown command |

## Command 0x01: Enter normal BLE file transfer

Switches the device into the normal BLE file-transfer app (the NUS-based protocol in
`05+1-ble_protocol.md`). After success the device starts advertising the file-transfer
service and the control plane remains available for mode switching.

Request:

| Field | Type | Length | Value |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x01 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |

Response:

| Field | Type | Length | Value |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x01 |
| status | uint8 | 1 | 0x00 on success |
| chunk | uint16 | 2 | 0 |

## Command 0x02: Enter control mode

Switches the device into control mode. In this mode the host drives the 3 physical buttons
via software. While in control mode the device does not process physical button presses
from the buttons themselves (they are shadowed by software input) until control mode exits.

Request:

| Field | Type | Length | Value |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x02 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |

Response:

| Field | Type | Length | Value |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x02 |
| status | uint8 | 1 | 0x00 on success |
| chunk | uint16 | 2 | 0 |

### Button input (control mode only)

After entering control mode, the host sends button-state updates on the same Control TX
characteristic using cmd `0x03` (a sub-command of the control mode). Each update carries 3
bytes, one per physical button, in fixed order:

| Byte | Button | Value |
| ---- | ---- | ---- |
| 0 | Button A (left) | 0 = released, 1 = pressed |
| 1 | Button B (middle) | 0 = released, 1 = pressed |
| 2 | Button C (right) | 0 = released, 1 = pressed |

Packet:

| Field | Type | Length | Description |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x03 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |
| btnA | uint8 | 1 | 0/1 |
| btnB | uint8 | 1 | 0/1 |
| btnC | uint8 | 1 | 0/1 |

The device applies the state immediately and debounces in software. No response is sent for
`0x03` updates (fire-and-forget), to keep latency low. Sending `0x03` while not in control
mode is ignored.

## Exiting modes

- To leave control mode and return to idle, send command `0x01` (enter file transfer) or
  power-cycle. There is no separate "exit control mode" command; mode transitions are
  always initiated by one of the two top-level commands.
- Physical button long-press behavior is unchanged; the control plane only shadows presses
  while in control mode.

## Notes

- The control plane is always on; mode state is held in RAM and resets on reboot.
- Only one client should drive control mode at a time; concurrent writers are not
  arbitrated.
- Values are applied as-is; the host is responsible for debouncing if needed.