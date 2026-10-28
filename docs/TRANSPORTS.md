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
- Bluetooth: select Bluetooth on Android before starting capture,
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

## Verification record (2026-10-28)

- Local Windows x64 cross-build: PASS, including size/import/resource checks.
- Local JVM protocol tests: 27 passed; Android app unit tests: 77 passed.
- CI helper unit tests: 31 passed; existing integration harness unit tests: 29 passed.
- [Android CI 40652725060](https://github.com/1-1-2/WKB_test/actions/runs/40652725060):
  PASS on commit `ebe4791`. Both debug and R8 release build and emulator runtime pass,
  including `stream_client.py` over actual ADB forwards, STOP and reconnect.
- [Windows CI 40653436393](https://github.com/1-1-2/WKB_test/actions/runs/40653436393):
  PASS on commit `eaab8d7`. Native runtime includes stream fragmentation, legacy peer
  refusal and invalid-length rejection against the real client executable.
- The Windows changes between those tested commits affect Windows labels/forwarded
  commands and screenshot documentation; Android sources and its test action are unchanged.
- Physical Bluetooth, USB unplug/replug, phone hotspot interoperability and real acoustic
  quality: NOT TESTED. No new public release is made by this development change.

## Android connection flow

Choose exactly one of Wi-Fi, phone hotspot, USB or Bluetooth **before** starting.
Only the selected method's preparation and system-settings entry are shown. Start
means “wait for Windows”; Windows then chooses the same method and connects.
Stop first to change methods. The previous separate Bluetooth switch migrates to
this shared choice. USB/Bluetooth listen on IPv4 loopback only, require stream
negotiation, and Bluetooth additionally requires ownership by the RFCOMM relay.
Wi-Fi/hotspot retain the legacy network protocol and diagnostic stream support.

### Connection-flow revision verification

[Android CI 40655787135](https://github.com/1-1-2/WKB_test/actions/runs/40655787135)
passes on `e04e10c`: debug/release builds, both emulator UI/legacy protocol/ADB-stream
runs. The four-choice UI assertion is mandatory. Its release-emulator connection
screenshot was visually reviewed and replaces the README/product-page image.
The subsequent relay source-port registration fix and connected-state hint (`d288fdb`)
pass the local debug build and 104 JVM/app tests. Shell regression: 68 passed.

## Bluetooth HELLO investigation (2026-10-28)

Reported physical-device symptoms: phone reports `bt socket closed, read return -1`;
Windows reports `101 无应答`. Wi-Fi and phone hotspot work on the same setup.
These messages identify a connection closed before the HELLO response; they do not
by themselves establish a pairing failure or a unique root cause.

The previously validated CI APK on `e04e10c` predates the explicit loopback bind in
`d288fdb`. Its Bluetooth relay publishes an unbound Socket and immediately connects,
while the session server independently checks the source port on accept. The server
can observe an unregistered port and reject the internal connection. `d288fdb` fixed
the startup ordering in source but was only locally built/tested; its earlier CI APK
was not a build of that change.

The dedicated fix extracts LoopbackRelay: bind IPv4 loopback, capture the assigned
source port, publish ownership, then connect. A JVM test checks ownership at accept,
forwards a fragmented type-101 HELLO and returns its response; an unrelated connection
must be rejected. This requires no physical radio. App tests now run in APK build CI.

Phone diagnostics log internal source/target ports and the byte counts of each
forwarding direction; rejected internal connections are logged explicitly. Windows
now uses select-based receive deadlines and distinguishes EOF, timeout, WSA error and
oversized replies in HELLO errors. A native-executable fixture tests early peer EOF.

This addresses a confirmed ordering defect. Whether it is the sole cause of the
reported physical Bluetooth failure remains subject to the new package's device test.

Follow-up: the reporter confirmed using the previous CI release APK, rather than a
local build of the later source fix. This matches the package/source discrepancy.
[CI 40663752768](https://github.com/1-1-2/WKB_test/actions/runs/40663752768)
passes: Android debug/release builds with app unit tests (including LoopbackRelay),
both emulator protocol/ADB-stream runs, and Windows runtime checks.
[Windows CI 40664700217](https://github.com/1-1-2/WKB_test/actions/runs/40664700217)
also passes with delayed-frame-header and explicit early-EOF diagnostic fixtures.
Neither run has a physical Bluetooth radio. Device confirmation is still required.
