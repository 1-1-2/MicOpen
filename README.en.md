<div align="center">

# 🎙️ MicLink Open

**Turn your Android phone into a microphone that's easier to use on Windows.**

Your phone captures. Your PC carries the conversation.

[简体中文](README.md) · **English**

[Get the apps](#download) · [Build from source](#build) · [Quick start](#quick-start) · [Find your docs](#docs)

</div>

MicLink Open builds new Android and Windows clients on the established **WO Mic virtual microphone driver**. The focus is on the details you use every day: connecting, adjusting your sound, listening back, and understanding what happened when something goes wrong.

This is an independently maintained client project, unaffiliated with WO Mic. No official partnership or endorsement is implied. We appreciate WO Mic's mature virtual audio driver; it remains maintained and licensed separately by its provider.

## One phone. Connect your way.

📶 **Wi-Fi (router / phone hotspot)** · 📡 **Wi-Fi Direct** · 🔵 **Bluetooth** · 🔌 **USB**

> The development version merges Wi-Fi and hotspot into one option and adds real **Wi-Fi Direct**. Physical-device acceptance is pending; no new release will be published before user confirmation. Downloads below still point to stable v0.1.2.

![Stable Windows client showing input levels, gain and audio settings](site/assets/windows-client-dark.png)

<sub>Screenshot: stable v0.1.2. Connection choices have changed in the development version.</sub>

| Connection | How it works |
|---|---|
| 📶 Wi-Fi | Join the same local network, or connect Windows to the phone's hotspot, then enter the phone's IP address. |
| 📡 Wi-Fi Direct · pending device test | Start Direct on the phone, discover and select it on Windows, then confirm pairing. No IP entry or phone hotspot required. |
| 🔵 Bluetooth | Pair in system settings, then select the paired phone in the Windows client. |
| 🔌 USB | Connect a data cable, enable USB debugging and authorize the PC. External ADB is required. |

**Choose a method on Android → tap Start to wait → select the same method on Windows and Connect.** Stop the phone's service before changing methods. Wi-Fi covers routers and phone hotspots; Wi-Fi Direct creates a separate P2P group.

### 🎚️ Your input, your settings

Android keeps Connection, Source and Advanced controls on separate tabs:

- **Choose a source and device.** Microphone, voice recognition, voice communication, camcorder and system-exposed input devices; unprocessed input where supported.
- **Adjust while watching.** Stream volume and live levels share the connection screen, with capture status close at hand.
- **Use system effects when available.** Noise suppression, acoustic echo cancellation and automatic gain control. Unsupported effects are visibly marked.

<p align="center">
  <a href="site/assets/android-connect-test.png"><img src="site/assets/android-connect-test.png" alt="Android connection screen: choose a method before starting" width="280"></a>
  <a href="site/assets/android-source-test.png"><img src="site/assets/android-source-test.png" alt="Android source screen: capture sources and input devices" width="280"></a>
</p>

<p align="center"><sub>Screenshots are from Windows CI and an Android emulator, with test addresses and device names. Click to enlarge. The screenshots show the Chinese UI.</sub></p>

System effects depend on the phone, ROM and selected source. Android AEC has no reference from the PC's speakers, so it is not end-to-end echo cancellation. Changing capture settings may briefly interrupt audio.

### 🎧 Listen first. Dial it in.

The Windows client offers input gain, extended gain and one-click mute. Its Monitor Lab lets you choose a playback device and adjust listening volume independently.

Optional monitoring modules provide **Ring Out automatic feedback detection and notch filtering**, plus an **experimental SpeexDSP AEC mode**. They process a local monitoring copy: **the processed audio is not sent back to the virtual microphone**. Monitoring and processing start disabled; evaluate the results with your own equipment and room.

<details>
<summary>Take a look inside Monitor Lab</summary>

![Monitor Lab with output device, independent volume, Ring Out and AEC controls](site/assets/windows-monitor-dark.png)

See the [monitoring guide](client/monitor/README.md) for modules, delay adjustment and limitations. Start with headphones before evaluating speaker monitoring.

</details>

### 🧭 A clearer view of the connection

| Everyday task | Useful details |
|---|---|
| Connect and recover | Driver readiness, method-specific address inputs and optional automatic reconnection. |
| Watch your audio | Input level and peaks, sample rate and buffer controls; session duration, jitter and packet-loss statistics. |
| Troubleshoot | Copy Windows event logs; use Android's 440 Hz test tone to check the audio path. The development branch also adds selectable Android logs and Copy all; see the [Direct testing guide](docs/WIFI-DIRECT.md). |
| Make it yours | Light/dark Windows UI; Android system theme, preset palettes, Android 12+ dynamic colors and AMOLED black. |

Wi-Fi, phone hotspot, Bluetooth and wired ADB / USB all have user-reported working results on physical devices. [CI in this repository](https://github.com/1-1-2/MicOpen/actions/runs/40735612451) passes both Android variants, emulator ADB forwarding, Windows protocol/driver checks and ADB exit cleanup. This does not cover every device or long session. See the [release record and validation scope](docs/RELEASES.md).

> [!IMPORTANT]
> **About Windows security prompts**
>
> MicLink Open is an independent, open-source project. Missing code signing or application reputation may cause SmartScreen to display “Windows protected your PC.” A reputation warning is not a confirmed malware finding, and open source alone is not a safety guarantee. Check the download source and published checksums, review the code, or build it yourself. This project does not ask you to turn off Windows security protection. [Microsoft explains application reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

<a id="download"></a>

## 📦 Get the apps

**[Go to MicOpen Releases →](https://github.com/1-1-2/MicOpen/releases)**

Source, issue reports and releases now live together in [`1-1-2/MicOpen`](https://github.com/1-1-2/MicOpen). Release `v0.1.2` offers separate downloads and a combined bundle. See the [release record](docs/RELEASES.md) for changes.

- **Android APK**: `MicOpen-Android-v0.1.2.apk`. Install it, grant microphone access, and allow foreground-service notifications when prompted.
- **Windows x64 ZIP**: `MicOpen-Windows-x64-v0.1.2.zip`. Extract the entire archive and run `womic.exe`. Keep its monitoring DLLs, driver utility and license files together.
- **Android + Windows bundle**: `MicOpen-v0.1.2-Android-Windows.zip`, with both apps, licenses and corresponding-source materials.
- **Checksums**: `SHA256SUMS.txt`. Compare the downloads' SHA-256 hashes. A match establishes agreement with the manifest, not a security certification.

**Before you connect:**

- **Android 8.0+.** Available input devices and system effects depend on the phone.
- **Windows x64 with a compatible WO Mic driver.** Obtain the driver through [WO Mic's official channel](https://wolicheng.com/womic/). The verified baseline is the 2022 driver, version `2.1.0.0`; arbitrary older/newer versions, x86 and ARM64 are not claimed as tested. See the [driver notes](docs/protocol/driver-whitepaper.md).
- **ADB is needed only for USB.** Download Windows [Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools), keep the tools directory intact and add it to `PATH`. Alternatively, place `adb.exe` and its companion DLLs beside `womic.exe`. The client does not download ADB or search arbitrary subdirectories. Once USB debugging is authorized, it creates and cleans up its own forwarding rule. See the [connection guide](docs/TRANSPORTS.md#operation).

To compile the apps yourself, follow the steps below. [GitHub Actions](https://github.com/1-1-2/MicOpen/actions/workflows/ci.yml) also offers short-lived test artifacts: **CI release APKs use a test signing identity, not the distribution identity**. Source archives contain no third-party driver binaries, and public CI driver utilities have no embedded driver payload.

<a id="build"></a>

## 🛠️ Build it yourself

Android uses Kotlin / Compose; Windows uses native C / C++. Build either side independently. Linux is a build host here; **there is no Linux desktop receiver**.

### Android · Windows or Linux

Prepare a full **JDK 17**, Android SDK `37.0`, NDK and CMake `3.22.1`. See the [build guide](docs/BUILD.md) for SDK installation, signing variables and exact versions. From the root of your current source checkout, run the following; on Windows, use Git Bash for source preparation:

```bash
bash .github/scripts/fetch-opus.sh
cd android
./gradlew :core:test :app:testDebugUnitTest \
  :app:assembleDebug :app:assembleRelease
```

After preparing Opus, Windows CMD / PowerShell users can run the same tasks from `android` with `gradlew.bat` / `.\gradlew.bat`. Outputs are in `android/app/build/outputs/apk/`. Release enables R8 and resource shrinking. **Without a configured distribution key, the release APK is unsigned and cannot be installed directly.** Use debug for development; configure your own stable signing identity for distribution.

### Windows x64 client · Cross-compile on Linux

On Ubuntu 24.04, or Ubuntu in WSL2 on Windows, prepare the MinGW-w64 **GCC / G++ POSIX toolchain**, Python 3, 7-Zip, curl and make. From the repository root:

```bash
bash client/tools/build_opus.sh
bash client/build.sh
bash client/monitor/build.sh
```

`client/build/` receives `womic.exe`, the `womic-drvinst.exe` driver utility and two optional monitoring DLLs. The first script prepares Opus for static linking; the third builds the Ring Out / SpeexDSP modules and includes licenses and corresponding source. Full dependency installation commands are in the [build guide](docs/BUILD.md#windows).

`client/build.bat` has not yet caught up with the current monitoring and switch-control build dependencies, so it is not a reliable native entry point for the current client. Windows developers should use the WSL2 route above. CI verifies Linux cross-compilation and native Windows execution. A source-built utility with an empty driver payload cannot install the driver on its own; install the official driver before everyday use.

<a id="quick-start"></a>

## 🚀 Your first connection, over Wi-Fi

1. **Join the same network.** The phone and PC must be able to reach each other. Install a compatible driver on the PC first.
2. **Start on the phone.** Open MicLink Open, choose Wi-Fi, tap Start and leave it waiting. Note the displayed IP address and control port.
3. **Connect from Windows.** Open `womic.exe`, choose Wi-Fi, enter that IP and matching control port (default `8125`), then click Connect.
4. **Choose your microphone.** Select the WO Mic virtual microphone in your meeting app, recorder or OBS. Speak, watch the level and adjust the volume.

Cannot connect? Check that the phone is still waiting, verify the address, then check guest-network isolation and the app's firewall permissions. Connected but silent? Check received packets, input levels and the selected recording device in that order. [Windows troubleshooting](client/README.md#排查) · [The other three connection methods](docs/TRANSPORTS.md#operation)

<a id="docs"></a>

## 🗺️ Find the guide for what you want to do

Most detailed guides are currently in Chinese. The connection guide is in English; both READMEs point to the same maintained documents.

| I want to… | Start here |
|---|---|
| Try another connection | [Connection guide and test coverage](docs/TRANSPORTS.md), including ADB, Bluetooth and hotspot. |
| Adjust capture or monitoring | [Android capture](android/README.md) · [Windows receiver](client/README.md) · [Monitor Lab](client/monitor/README.md). |
| Set up a development environment | [Build and integration walkthrough](docs/STEP-BY-STEP.md) · [Toolchains and signing](docs/BUILD.md). |
| Follow the audio path | [Wire protocol](docs/protocol/womic-wifi-protocol-v1.md) · [Driver interface notes](docs/protocol/driver-whitepaper.md). |
| See what has been tested | [Delivery status and known issues](docs/CLOSEOUT-2026-10-28.md) · [Windows validation](docs/verification/windows-e2e-report.md) · [Manual interoperability checklist](docs/verification/android-interop-checklist.md). |
| Reproduce tests or enter a CI environment | [CI / Lab operations](.github/workflows/README.md) · [Android verification](android/verify/README.md). |

The [engineering documentation map](docs/README.md) also covers maintenance, scripts and contribution conventions. When reporting an issue, include both app versions, connection method, device/OS versions and relevant logs. Remove personal information and credentials first.

## 🤝 Credits and licenses

Thank you to **WO Mic / Wolicheng Tech** for its virtual microphone driver, **Xiph.Org Opus and SpeexDSP** for audio coding and processing, and **Dusk Audio Ring Out** for its feedback detection and notch-filtering core.

MicLink Open's own code is primarily [MIT-licensed](LICENSE); third-party code retains its own terms. **The monitoring test combination containing Ring Out is distributed under GPL-3.0-or-later.** See the [combination license](client/monitor/TEST-LICENSE.md) for corresponding-source requirements. The WO Mic driver is separate third-party software, not covered by this project's MIT license. Tools that accept a locally supplied driver do not grant redistribution rights.

See [NOTICE](NOTICE.md) for sources and licensing boundaries. Tests between MicLink's two clients do not establish interoperability with official WO Mic clients. USB and Bluetooth use MicLink's own streaming extension; use both MicLink clients together.

<div align="center">

**Let the software handle the connection. You bring the voice.**

</div>
