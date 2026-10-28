# Connection adapters

Development repository: `1-1-2/WKB_test`. `1-1-2/MicOpen` is distribution-only.

## Scope and reference implementations

- WO Mic's [official transport documentation](https://wolicheng.com/womic/references.html)
  explicitly implements its “Wi-Fi Direct” scenario using the phone's SoftAP.
  MicLink calls this **phone hotspot direct connection**, not negotiated Wi-Fi P2P.
  Enable the phone hotspot in system settings, join it from Windows, and enter the
  phone's actual hotspot interface address. There is no hard-coded gateway address.
- USB follows the ADB prerequisite used by WO Mic and the tunnel lifecycle pattern
  documented in [scrcpy](https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md)
  and [adb_tunnel.c](https://github.com/Genymobile/scrcpy/blob/master/app/src/adb/adb_tunnel.c):
  select a device, create a private forwarding rule, close that rule on teardown.
  Unlike scrcpy we already have an installed Android server; no server binary is pushed.
- Bluetooth uses secure, paired RFCOMM, as in the
  [Android API](https://developer.android.com/develop/connectivity/bluetooth/connect-bluetooth-devices)
  and [Microsoft Winsock example](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/winsock/bluetooth/bthcxn.cpp).
  Bluetooth discovery and bonding remain in system settings; Windows enumerates paired
  devices without launching a new radio inquiry.
- [MicYou](https://github.com/MicYou-Dev/MicYou) was considered as an actively maintained
  USB microphone application; its Rust runtime is not imported into this native C/Kotlin
  project. These references guide architecture; no third-party implementation is copied.

## Boundaries

`client/src/transport.c` owns connection establishment, paired-device enumeration and
ADB rule lifetime. It does not access codecs, DSP or the virtual driver.
`engine.c` still owns one audio decoder and output pipeline for every transport.

Android capture writes complete existing media packets into `MediaSender`:
`UdpSender` for LAN/hotspot, `StreamMediaSender` for USB/RFCOMM. The latter has a
three-packet queue, discards queued packets older than 200 ms, and aborts the owning
connection if a write blocks for 500 ms. Control replies and media share a serialized
writer so their bytes cannot interleave. BluetoothTransport only relays bytes to the
existing local session server; it owns and closes its RFCOMM and loopback sockets.

## Stream extension v1 (MicLink peers only)

The legacy TCP frame remains `[type:u8][length:u32be][payload]`.

1. A stream client sets the sixth byte of the six-byte HELLO payload to `0x53`.
2. The phone replies with legacy status/level unchanged (`0,4`), and capability bit
   `0x02` in the third payload byte. That bit is required before the client starts.
3. Media is type `200`, payload is one **unchanged** WO Mic format-C media packet.
   Maximum payload: 2048 bytes. CONFIG/START/STOP/heartbeat retain their meanings.
4. No UDP media or UDP reconciliation is sent in stream mode. The CONFIG media port
   is retained for legacy session validation, but is not the stream destination.
5. An old phone missing capability bit 2 is rejected with an upgrade message. An
   ordinary legacy client receives the original UDP behavior and capability bit 1.

USB and Bluetooth use the same extension; it is not claimed to interoperate with
WO Mic's proprietary USB/Bluetooth wire protocols. The native engine's `tcp` CLI
mode exercises the same framing without requiring radio/USB hardware.

## Operation

- LAN: `womic.exe --transport wifi --host <phone-ip> --connect`
- Hotspot: `womic.exe --transport hotspot --host <phone-hotspot-ip> --connect`
- USB: `womic.exe --transport usb --connect`; optionally `--host <usb-serial>`.
  `adb.exe` must be on PATH. Enable USB debugging, authorize this PC, start the app's
  capture service. `auto` selects exactly one USB device using `adb -d get-serialno`.
  Forwarding uses an OS-assigned PC port; cleanup attempts to remove only that device/port rule (bounded to one second even when the device is gone).
  MicLink never kills the shared ADB server or removes unrelated forwarding rules.
- Bluetooth: enable the Android Bluetooth transport switch before starting capture,
  grant the nearby-device permission, enable Bluetooth and pair in system settings.
  Choose a paired phone in Windows. CLI accepts a 12-digit hexadecimal Bluetooth
  address without separators. Service UUID: `ad5c9d21-8a64-487b-9c74-6f3aa069472e`.

## Verification limits

Protocol negotiation, fragmentation, length validation and blocked-sender tests run
without hardware. Windows CI runs the actual executable against a fragmented TCP
fixture, including legacy capability refusal and malformed media length.
Android CI additionally exercises the installed APK through ADB forwarding, including
media framing, heartbeats, STOP, reconnect and forwarding cleanup, for both variants.
Physical USB authorization/unplugging, Bluetooth pairing/reconnect and real hotspot/audio
quality still require physical devices. A passing simulated stream is not reported
as a passing physical USB or Bluetooth connection.

Native HarmonyOS is a separate future platform adapter; Kotlin/Compose code is not
binary-compatible. The packet protocol and fixtures can be retained for its tests.

## UI evidence

The README/product page connection screenshots were refreshed from native CI:
Android release run `40651214343` (emulator), Windows run `40651522992`.
These show the newly exposed connection controls, not proof of physical radio/USB operation.
