# SSC On Windows

Stream Windows system audio to Samsung Galaxy Buds over **Samsung Scalable Codec (SSC)**, using a USB Bluetooth dongle in WinUSB mode (external HCI via BTstack). No kernel driver, no test signing.

> **Version 0.1** — by **Salvetum**
> A fork of [A2DP Windows Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge) by Seiya Funaoka, adding a full **SSC (Samsung Scalable Codec)** encoder pipeline alongside AAC and SBC.

---

## What this is

Windows' built-in Bluetooth stack cannot stream over SSC. This project bypasses it entirely:

- A **USB Bluetooth dongle** is switched from the Microsoft Bluetooth driver (`BTHUSB`) to a generic **WinUSB** driver, giving the app raw HCI access. The dongle is then fully owned by the app and invisible to Windows Bluetooth.
- Audio is captured from the Windows default output device via **WASAPI loopback** and encoded with the selected codec.
- The SSC encoder is the **real Samsung on-device encoder blob** (`libScalable_Encoder.so`, aarch64), executed inside an emulator (WSL2 by default, or [Qiling](https://qiling.io/) natively on Windows) behind a small TCP daemon. This is what makes proper SSC bitstreams possible on Windows.
- Encoded frames are sent over A2DP/AVDTP/L2CAP by **BTstack** in external-HCI mode.

Target device used during development: **Samsung Galaxy Buds3 FE** (`78:C1:1D:A7:BC:EE`), 48 kHz / stereo.

## Supported codecs

| Codec | Modes / bitrate | Sample rate | Status |
|-------|-----------------|-------------|--------|
| **SSC** (default) | High 229 / Standard 192 / Mobile 128 kbps — UHQ 584 / 442 / 250 kbps | 48 kHz (UHQ: 96 kHz) | Primary target |
| AAC | 128 / 192 / 256 kbps | 48 kHz | Via Fraunhofer FDK-AAC |
| SBC | up to ~345 kbps | 48 kHz | Baseline fallback |

Codec fallback priority when the requested codec is unavailable: **SSC > AAC > SBC**.

> **SSC UHQ (96 kHz)** is gated on the remote capability bit. Galaxy Buds3 FE advertises cap `0x3C`, which does **not** include UHQ — the app detects this and falls back to 48 kHz to avoid silent playback. UHQ is only used with a UHQ-capable sink.

## Architecture

```
+-------------------------------------------------------------+
|  SSCOnWindows.exe (WinUI 3 GUI)  /  SSCOnWindows-0.1.exe (CLI) |
|  +-- a2dpwb_core (shared C++ backend) --------------------+  |
|  |  WASAPI loopback capture (48 kHz, float32)             |  |
|  |  Encoder:  SSC  |  AAC (fdk-aac)  |  SBC (bluedroid)   |  |
|  |  A2DP service / connection lifecycle / telemetry       |  |
|  |  BTstack  (HCI / L2CAP / AVDTP / A2DP / AVRCP)         |  |
|  +--------------------------------------------------------+  |
|                         WinUSB API                           |
+-----------------------------+-------------------------------+
                              |
                   USB Bluetooth dongle (BTHUSB -> WinUSB)
                              |
              SSC encoder daemon:  TCP :20248
                              |
        +---------------------+----------------------+
        | WSL2: sscblobd -> qemu-aarch64 (default)   |
        |   or                                       |
        | Windows: sscblobd.py -> Qiling (--ssc-native)
        +--------------------------------------------+
                              |
                  Samsung libScalable_Encoder.so
```

| Path | Description |
|------|-------------|
| `app/src/` | C++ core: capture, encoders, A2DP service, BTstack transport, driver switching |
| `app/lang/` | Localization (JSON, embedded at build time) |
| `winui3/` | WinUI 3 GUI (C++/WinRT, MSBuild project) |
| `tools/ssc_daemon/` | Native Qiling daemon (`sscblobd.py`) + minimal aarch64 rootfs |
| `tools/ssc_payload/` | WSL2 blob payload: `sscblobd.c`, `ssc_blob_helper.c`, build shims |
| `tools/golden/` | Byte-exact SSC regression goldens + `ssc_golden.py` |
| `patches/` | Small local patches applied to third-party submodules (currently BTstack) |
| `extern/btstack/` | BTstack — user-mode Bluetooth stack (git submodule) |
| `extern/fdk-aac/` | Fraunhofer FDK AAC encoder (git submodule) |
| `extern/json/` | nlohmann/json (git submodule) |
| `docs/` | Documentation; `docs/dev/` holds design/planning notes |

## Requirements

- **Windows 10/11 (x64)**
- **Visual Studio 2022 or newer** with the Desktop C++ workload
- **CMake** 3.16+ (with CMake 4.x, configure with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for wxWidgets 3.2.6)
- **Git** (submodules + wxWidgets FetchContent)
- A **dedicated USB Bluetooth dongle** (do not reuse the PC's built-in Bluetooth)
- For the default SSC path: **WSL2** with a Linux distro (Ubuntu) and the prebuilt blob payload
- For the experimental native path: **Python 3.14** + **Qiling** (`py -3.14 -m pip install qiling`)

## Setup

### 1. Dedicated dongle drivers

The app includes an in-app **WinUSB ↔ BTHUSB toggle** (no Zadig needed) in the WinUI *Streaming Mode (Dongle)* panel:

- **Enable Streaming (WinUSB)** — signs and installs a generic Microsoft WinUSB INF for the dongle (raises a UAC prompt; blocked while streaming), then re-evaluates the device.
- **Restore Windows BT (BTHUSB)** — removes only the WinUSB-flavored driver package and lets Windows re-install its own Bluetooth driver.

Tested dongle: **TP-Link UB500** (`VID_2357&PID_0604`), a Realtek RTL8761BU adapter.

> **Warning:** Never switch the driver on your PC's built-in Bluetooth — doing so disables all normal Bluetooth and may require Device Manager or system recovery to restore. Always use a **separate, dedicated** dongle.

### 2. Realtek firmware (Realtek adapters only)

Realtek-based dongles need proprietary firmware. Intel and CSR adapters do not.

1. Download the firmware/config files for your chipset (e.g. `rtl8761bu_fw.bin`, `rtl8761bu_config.bin`) from [linux-firmware/rtl_bt](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt).
2. Put both in `%APPDATA%\A2DPWB` (or use the app's **Firmware** dialog to open the folder).

Firmware files are not included (proprietary Realtek binaries).

## Building

### Core (CLI + shared library)

```powershell
git clone --recursive <this-repo-url> SSCOnWindows
cmake -S SSCOnWindows -B SSCOnWindows\build_msvc -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build SSCOnWindows\build_msvc --config Release --target A2DPWB -j 8
```

Output: `build_msvc\app\Release\SSCOnWindows-0.1.exe`.

### WinUI 3 GUI

Build the core library first, then the MSBuild project:

```powershell
cmake --build build_msvc --config Release --target a2dpwb_core -j 8
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find "MSBuild\**\Bin\MSBuild.exe"
& $msb winui3\A2DPWBWinUI.vcxproj -p:Configuration=Release -p:Platform=x64 -m:8 -t:Build
```

Output: `winui3\bin\x64\Release\SSCOnWindows.exe`. The app is **unpackaged**, so the whole output folder (bootstrap DLL, WebView2 DLL, `.xbf`, `.pri`, `.winmd`) must travel with the exe.

### Third-party submodule patches

The BTstack submodule carries a small local patch (error logging in the Windows WinUSB transport). Apply it once after cloning:

```powershell
git -C extern/btstack apply ..\..\patches\btstack-win-usb-logs.patch
```

## Usage

### GUI (default)

Run `SSCOnWindows.exe`. The interface provides device scan/connect, codec (SSC/AAC/SBC), quality (High/Standard/Mobile), sample rate (48 kHz / 96 kHz UHQ), bitrate slider (auto/snapped to valid values), device volume (AVRCP absolute volume), streaming-mode driver toggle, live stats/sparklines, and a log pane.

### CLI

```powershell
# SSC (default) to a device
SSCOnWindows-0.1.exe --cli -d 78:C1:1D:A7:BC:EE

# Pick a codec and quality
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q hq   # 229 kbps (48k)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q std  # 192 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q mq   # 128 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c aac
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c sbc

# SSC UHQ 96 kHz (2x SRC from the 48 kHz capture; UHQ-capable sinks only)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq -q hq  # 584 kbps

# Explicit bitrate override (snapped to the active mode's valid set)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --bitrate 192

# Native SSC daemon instead of WSL2 (experimental)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --ssc-native

# List paired Bluetooth audio devices / show help
SSCOnWindows-0.1.exe --cli -l
SSCOnWindows-0.1.exe --help
```

> Windows output volume does not change the captured level: WASAPI loopback captures pre-volume-mix audio. Use the device volume slider (AVRCP) to change headphone loudness.

## SSC encoder pipeline

The SSC encoder is aarch64-only, so it runs behind a TCP daemon on port `20248`.

- **Wire protocol:** client sends `uint32 frame_samples` (LE) + `frame_samples * channels * 4` bytes of int32 PCM; daemon replies `int32 ret` + `ret` bytes of encoded SSC frame. `kFrameSamples = 864`, max encode 4096 bytes, `TCP_NODELAY` required.
- **WSL2 backend (default):** `tools/ssc_payload` builds `sscblobd` + `ssc_blob_helper`, run under `qemu-aarch64`. Encode round-trip ~1.2 ms.
- **Native backend (`--ssc-native`, experimental):** `tools/ssc_daemon/sscblobd.py` runs the same helper under Qiling directly on Windows via `py -3.14`. No WSL2 required, ~5x slower (48 kHz solid; 96 kHz UHQ marginal).
- **PCM scale:** int32 at 2^29 for SSC (2^31 otherwise). The CLI and GUI share the same scale — they must stay in sync.

### Golden-file regression

Frozen, byte-exact references for the SSC blob:

```powershell
python tools\golden\ssc_golden.py check --all     # regenerates via the daemon and byte-compares
python tools\golden\ssc_golden.py gen --rate 96000 --bitrate 584   # re-freeze a profile
```

## Known limitations

- SSC UHQ requires a UHQ-capable sink (remote cap bit); otherwise the app falls back to 48 kHz.
- SSC bitrates are mode-gated; the app snaps any requested bitrate to the active mode's valid set to avoid garbled audio.
- WASAPI capture is 48 kHz unless the Windows default format is changed; UHQ applies 2x SRC to that 48 kHz capture.
- The native Qiling backend is experimental; prefer WSL2 for UHQ.
- The SSC daemon does not exit on SIGTERM while a client is connected; the app's watchdog closes its own socket first.
- Windows crossfade/volume only affects headphones through AVRCP, not the captured stream.

## Forks, credits and licenses

This project builds on the work of many others. Upstream code and libraries:

| Component | Origin | License |
|-----------|--------|---------|
| **A2DP Windows Bridge** (this project's upstream) | [SeiyaFunaokaJP/A2DP-Windows-Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge) | MIT |
| **SSC On Windows** (this fork) | Salvetum | MIT |
| **BTstack** — user-mode Bluetooth stack | [bluekitchen/btstack](https://github.com/bluekitchen/btstack) | BSD-3-Clause (dual; commercial license from BlueKitchen GmbH) |
| **SSC codec integration reference** — A2DP SSC codec definitions (capabilities, bitrate/mode rules) | [sachk/openssc](https://github.com/sachk/openssc) | see upstream |
| **Samsung `libScalable_Encoder.so`** — the actual SSC encoder blob | Samsung (distributed via the SSC/openssc ecosystem) | proprietary; not included in this repository |
| **Qiling** — aarch64 user-mode emulation for the native daemon | [qilingframework/qiling](https://github.com/qilingframework/qiling) | GPL-2.0 |
| **Fraunhofer FDK AAC** | [mstorsjo/fdk-aac](https://github.com/mstorsjo/fdk-aac) | FDK AAC License (non-commercial) |
| **wxWidgets** — legacy GUI | [wxWidgets/wxWidgets](https://github.com/wxWidgets/wxWidgets) | wxWindows Library Licence (LGPL-2.0 + exception) |
| **nlohmann/json** | [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| **Windows App SDK** — WinUI 3 runtime | Microsoft | Microsoft EULA |

- Project license: **MIT** — see [LICENSE](LICENSE).
- Full third-party notices: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
- Local modifications to submodules are kept as patches under `patches/`.

## References

- [BTstack](https://github.com/bluekitchen/btstack) — open-source Bluetooth stack with WinUSB support
- [A2DP Windows Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge) — upstream project
- [openssc](https://github.com/sachk/openssc) — SSC codec integration reference
- [Qiling](https://qiling.io/) — binary emulation framework
- [fdk-aac](https://github.com/mstorsjo/fdk-aac), [wxWidgets](https://www.wxwidgets.org/), [nlohmann/json](https://github.com/nlohmann/json)
