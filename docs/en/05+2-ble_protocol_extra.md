# PixLink (Always-On)

This document describes **PixLink**, a small, always-on BLE interface on Pixl.js.
It is independent of the Nordic UART Service (NUS) file-transfer protocol documented in
`05+1-ble_protocol.md` and stays available even while the device is not in file-transfer mode.

It exposes two top-level commands, plus one sub-command that is only valid in control mode:

1. Enter normal BLE file transfer mode.
2. Enter control mode, where the host sends 3 inputs that map to the 3 physical buttons.
3. Button state update (`0x03`), a sub-command of control mode.

## Service

- Service name: PixLink
- Service UUID: `7e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Characteristic (write + write-without-response): `7e400002-b5a3-f393-e0a9-e50e24dcca9e` (Control TX)
- Characteristic (notify): `7e400003-b5a3-f393-e0a9-e50e24dcca9e` (Control RX)

The control-plane UUID is advertised in Pixl.js and AmiLoop modes (it fits the 31-byte
advertising data). In the amiibolink modes the advertisement is full, so the service is not
advertised there, but it still exists on the connection. It is never disabled by entering
file transfer or control mode; only the active mode changes.

All responses are sent as notifications on Control RX (`7e400003`). The host must subscribe
to that characteristic (enable notifications) before sending commands, otherwise the device
has nowhere to send the answer and drops it.

## Packet format

Same 4-byte header as the file-transfer protocol:

| Field | Type | Length | Description |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | Command id |
| status | uint8 | 1 | 0 in requests; status code in responses |
| chunk | uint16 | 2 | 0 (no chunking; control packets are single ATT write) |
| data | byte | N | Command-specific payload |

Numeric types are unsigned, little-endian. No chunk transfer is used; each command fits in one ATT write.

Requests for the known commands must carry `status = 0` and `chunk = 0`. A request with a
non-zero status or chunk field is malformed: `0x01` and `0x02` are rejected with status `0x01`,
`0x03` (which has no response at all) is silently ignored, and an unknown command is always
answered with `0xFF`.

## Status codes

| Code | Meaning |
| ---- | ---- |
| 0x00 | OK |
| 0x01 | Generic error (malformed request, or the command could not be queued) |
| 0x02 | Invalid mode transition (e.g. already in requested mode) |
| 0xFF | Unknown command |

An unknown command is answered with the same command byte as the request and status `0xFF`.

## Mode and command matrix

| Current mode | Command | Action | Response |
| ---- | ---- | ---- | ---- |
| idle | 0x01 | Starts the BLE file transfer app, mode becomes file transfer | 0x00 |
| control | 0x01 | Kills and restarts the BLE file transfer app (the launcher kills the running app first, so the app UI restarts), mode becomes file transfer | 0x00 |
| file transfer | 0x01 | None, the device is already in that mode | 0x02 |
| idle | 0x02 | Mode becomes control | 0x00 |
| file transfer | 0x02 | Mode becomes control | 0x00 |
| control | 0x02 | None, the device is already in that mode | 0x02 |
| control | 0x03 | Applies the 3 button values, no answer | none |
| any other mode | 0x03 | Ignored, no answer | none |

Responses are 4-byte frames: `{cmd, status, 0x00, 0x00}`, where `cmd` is the command byte of
the request.

Command `0x01` from idle or control mode goes through the app launcher: the app that is
currently running is killed first. From file-transfer mode the command is rejected with
`0x02` and nothing is restarted.

## Command 0x01: Enter normal BLE file transfer

Switches the device into the normal BLE file-transfer app (the NUS-based protocol in
`05+1-ble_protocol.md`). The NUS file-transfer service is used over the connection that is
already open, the device does not advertise a second service or open a second connection.
Advertising resumes once the connection is closed. PixLink remains available for mode
switching.

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
| status | uint8 | 1 | 0x00 on success, 0x02 if already in file transfer mode |
| chunk | uint16 | 2 | 0 |

## Command 0x02: Enter control mode

Switches the device into control mode. In this mode the host drives the 3 physical buttons
via software. A button that was already pressed before control mode started still owns the
button state and delivers its release as usual, so the UI never stays stuck with a held
button. A physical press that happens during control mode is ignored completely (press,
release, long press and repeat are all suppressed) and does not block virtual input.

The file transfer app, if it is running, keeps running: only the input mode changes.

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
| status | uint8 | 1 | 0x00 on success, 0x02 if already in control mode |
| chunk | uint16 | 2 | 0 |

### Button input (control mode only)

After entering control mode, the host sends button-state updates on the same Control TX
characteristic using cmd `0x03` (a sub-command of the control mode). Each update carries 3
bytes, one per physical button, in fixed order:

| Byte | Button | Value |
| ---- | ---- | ---- |
| 0 | Button A (left) | 0 = released, non-zero = pressed |
| 1 | Button B (middle) | 0 = released, non-zero = pressed |
| 2 | Button C (right) | 0 = released, non-zero = pressed |

Packet:

| Field | Type | Length | Description |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x03 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |
| btnA | uint8 | 1 | 0 = released, non-zero = pressed |
| btnB | uint8 | 1 | 0 = released, non-zero = pressed |
| btnC | uint8 | 1 | 0 = released, non-zero = pressed |

The packet must be exactly 7 bytes long. Each button byte is a boolean: 0 means released,
any non-zero value means pressed. There is no software debouncing: only a change of a
button state produces an event. A 0 -> 1 change produces a press, a 1 -> 0 change produces
a release, which also counts as a short press, exactly like a physical button. Sending the
same value again produces no event.

When a button was already pressed before control mode started, the physical press owns the
button state: the virtual press does not produce a second press event, and the matching
virtual release does not produce a release or short press. A physical press that happens
during control mode is ignored, so it does not own the button state and does not block the
virtual press.

No response is sent for `0x03` updates (fire-and-forget), to keep latency low. Sending `0x03`
while not in control mode, or with a malformed packet, is ignored without any answer.

## Exiting modes

- To leave control mode, send command `0x01` (enter file transfer): the device leaves control
  mode and enters file-transfer mode. There is no separate "exit control mode" command; mode
  transitions are always initiated by one of the two top-level commands. A power cycle also
  resets the mode to idle.
- Killing the file transfer app (for example when the user closes it) resets the PixLink mode
  to idle.
- Leaving control mode releases the virtual buttons that are still held (release only, no
  short press). A virtual button whose press was not delivered to the UI, because a physical
  press already owned the button state, is not released.
- Outside control mode, physical button behavior including long-press and repeat is
  unchanged. While control mode is active, all physical button events are suppressed,
  except for the release of a button that was already pressed before control mode started.

## Notes

- PixLink is always on; mode state is held in RAM and resets on reboot.
- Only one client should drive control mode at a time; concurrent writers are not
  arbitrated.
- Non-zero values mean pressed; the host is responsible for debouncing if needed.
- Responses are best effort: if the device's notification queue is saturated, a
  response can be dropped, so the host should time out.
