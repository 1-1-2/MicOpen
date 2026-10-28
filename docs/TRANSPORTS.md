# Connection adapters

Source, issues, CI and releases: [1-1-2/MicOpen](https://github.com/1-1-2/MicOpen).

## Design and references

- **Wi-Fi:** the phone listens for control connections; media uses the existing UDP format.
- **Phone hotspot:** Windows joins the phone's SoftAP and uses its actual interface address. This follows WO Mic's [official hotspot scenario](https://wolicheng.com/womic/references.html); it is not negotiated Wi-Fi P2P and has no hard-coded gateway.
- **USB:** ADB forwards a private local TCP port to the installed app. No server binary is pushed. Selection and forwarding cleanup follow the lifecycle documented by [scrcpy](https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md).
- **Bluetooth:** secure paired RFCOMM, following the [Android API](https://developer.android.com/develop/connectivity/bluetooth/connect-bluetooth-devices) and [Microsoft Winsock example](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/winsock/bluetooth/bthcxn.cpp). Pairing stays in system settings; Windows enumerates paired devices without a new radio inquiry.

`client/src/transport.c` owns connection establishment, device selection, ADB process ownership and forwarding rules. It does not access codecs, DSP or the virtual driver. `engine.c` owns one audio pipeline for every method.

On Android, `MediaSender` accepts complete existing media packets. `UdpSender` serves Wi-Fi/hotspot; `StreamMediaSender` serves USB/RFCOMM with a three-packet queue, drops queued packets older than 200 ms, and aborts a connection whose write blocks for 500 ms. Control and media share a serialized writer. Bluetooth relays bytes to the local session server; its loopback source port is bound and registered before connect, preventing ownership races.

## Stream extension v1

The control frame remains `[type:u8][length:u32be][payload]`.

1. A stream peer sets the sixth byte of the six-byte HELLO payload to `0x53`.
2. The phone retains legacy status/format (`0,4`) and sets capability bit `0x02` in the third reply byte. It is required before streaming.
3. Media type `200` carries one unchanged WO Mic format-C packet, at most 2048 bytes. CONFIG / START / STOP / heartbeat retain their meanings.
4. Stream mode sends neither UDP media nor UDP reconciliation. CONFIG's media port remains for legacy validation, not as the stream destination.
5. An old peer lacking capability bit 2 is rejected with an upgrade message. Ordinary legacy peers retain UDP and capability bit 1.

This is a MicLink extension, not a claim of compatibility with WO Mic's proprietary USB/Bluetooth protocols. Use both MicLink clients together. The `tcp` CLI diagnostic mode exercises the same framing without USB/radio hardware.

## Operation

Choose a method on Android **before** tapping Start. Leave the phone waiting, then choose the same method and Connect on Windows. Stop Android before changing methods. USB/Bluetooth listen only on IPv4 loopback and require stream negotiation; Bluetooth additionally requires ownership by its relay.

| Method | Windows CLI / preparation |
|---|---|
| Wi-Fi | `womic.exe --transport wifi --host <phone-ip> --connect` |
| Hotspot | Enable the phone's hotspot, join it from Windows, then `womic.exe --transport hotspot --host <phone-hotspot-ip> --connect`. |
| USB | `womic.exe --transport usb --connect`; optionally `--host <usb-serial>`. Empty / auto selects exactly one USB device. Enable USB debugging and authorize this PC. |
| Bluetooth | Pair in system settings, enable Bluetooth and nearby-device permission on Android, then select the paired phone in Windows. CLI accepts a 12-digit hexadecimal address without separators. |

Obtain Windows [Platform-Tools](https://developer.android.com/tools/releases/platform-tools) for USB. Keep its complete directory on PATH, or put `adb.exe` and companion DLLs (including `AdbWinApi.dll` and `AdbWinUsbApi.dll`) beside `womic.exe`. The client does not download ADB or search arbitrary subdirectories. Use a data-capable cable. Bluetooth service UUID: `ad5c9d21-8a64-487b-9c74-6f3aa069472e`.

## ADB process ownership and exit

If a local ADB service is already listening on the default port 5037, MicLink reuses it. Otherwise it starts a foreground ADB server on an OS-selected private loopback port using the official [`-L` / `server nodaemon` options](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/user/adb.1.md). Device readiness is awaited before forwarding.

All ADB processes launched by the adapter enter a [Windows Job Object](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects), with kill-on-close enabled before their suspended threads resume. Disconnect, setup failure and cancellation remove only the app's forwarding rule, then close the job. Parent process termination also closes its job handle, so owned children cannot remain behind. A pre-existing shared server is outside that job and remains running. The app never runs a global `adb kill-server` or removes unrelated forwarding rules.

An old version may already have left a shared server behind. If no other tool needs it, stop that old service once before testing the upgrade. A service that existed before MicLink started is deliberately preserved; its continued presence is not an owned-process leak.

## Verification

[MicOpen CI 40735612451](https://github.com/1-1-2/MicOpen/actions/runs/40735612451) passes both Android variants, emulator legacy/ADB-stream sessions, Windows protocol/driver checks and the six native ADB lifecycle cases. Details and limits are in the [current validation report](verification/windows-e2e-report.md).

Users have reported working physical Wi-Fi, phone hotspot, Bluetooth and wired ADB / USB links. This does not establish all-device compatibility, real-room AEC quality, repeated unplug/replug coverage or hours-long stability. A simulated stream or process fixture is not reported as a new physical-device test. Official-client interoperability remains a separate manual check.

Screenshots show Windows CI and an Android emulator with test addresses/device names. They illustrate controls, not physical USB/radio proof. Historical runs are retained only as historical records; current acceptance links point to this repository.

Native HarmonyOS remains a future platform adapter. Kotlin/Compose binaries are not directly compatible; the protocol and fixtures can be reused for a future implementation.
