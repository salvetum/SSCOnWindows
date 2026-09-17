---
title: Usage
layout: default
nav_order: 3
---

# Usage
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## GUI Mode (Default)

```
SSCOnWindows.exe
```

The WinUI 3 interface provides:

- **Scan / Connect / Direct connect** -- find and connect to your headphones
- **Codec selection** -- SSC (default) / AAC / SBC
- **Quality** -- High / Standard / Mobile
- **Sample rate** -- 48 kHz, or 96 kHz (SSC UHQ, UHQ-capable sinks only)
- **Bitrate** -- auto or explicit; snapped to the valid set for the active mode
- **Device volume** -- AVRCP absolute volume (0-100%)
- **Streaming Mode (Dongle)** -- switch the dongle between WinUSB and Windows Bluetooth
- **Live Stats** -- latency, error rate, loss, queue depth + sparklines
- **Setup & Help** -- first-time pairing guide and FAQ

## CLI Mode

Add `--cli` to run without a GUI window.

### Basic

```bash
# SSC (default)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF
```

### Codec and Quality

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc       # SSC
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c aac       # AAC
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c sbc       # SBC

SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q hq   # 229 kbps (48k, default)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q std  # 192 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q mq   # 128 kbps
```

Fallback priority when the requested codec is unavailable: **SSC > AAC > SBC**.

### SSC UHQ (96 kHz)

UHQ applies 2x SRC to the 48 kHz capture and uses the 96 kHz bitrate set. It is
only usable on a sink that advertises the UHQ capability bit; otherwise the app
falls back to 48 kHz.

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq        # 584 kbps (default)
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq -q std # 442 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq -q mq  # 250 kbps
```

### Explicit Bitrate

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --bitrate 192
```

Values outside the active mode's valid set are snapped automatically (feeding an
unsupported bitrate to the blob produces garbled audio).

### Native SSC Daemon (experimental)

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --ssc-native
```

Runs the aarch64 SSC blob under Qiling on Windows (`py -3.14`) instead of WSL2.
No WSL2 required, but slower; prefer WSL2 for UHQ.

### Device Discovery

```bash
# List paired Bluetooth audio devices
SSCOnWindows-0.1.exe --cli -l
```

{: .note }
Device listing uses the Windows Bluetooth API via your **built-in** Bluetooth adapter, not the WinUSB adapter.

## Capture Modes

Two audio capture modes are supported:

| Mode | Description |
|:-----|:------------|
| System Loopback | Captures all system audio from the default output device |
| Virtual Device | Captures from a specific virtual audio device (e.g., VB-CABLE) for per-app routing |

{: .warning }
Both modes use WASAPI shared mode, so the capture sample rate follows the device's **Default Format** in Windows Sound settings (typically 48 kHz). SSC UHQ does not change the Windows capture rate; it applies 2x SRC to the 48 kHz capture.

## Volume

The volume slider controls the **headphone's** volume via AVRCP absolute volume
(0-100% mapped to 0-127). The Windows output volume does not affect the captured
level, because WASAPI loopback captures pre-volume-mix audio. With auto-mute
enabled, the default speakers are muted while streaming.

## Codec Comparison

| Codec | Best For | Trade-off |
|:------|:---------|:----------|
| SSC (High) | Best quality/robustness for Samsung devices | Requires the SSC blob daemon |
| SSC (Mobile) | Congested RF environments | Lower bitrate |
| SSC UHQ | Highest quality on UHQ-capable sinks | Needs WSL2; UHQ sink required |
| AAC | General-purpose fallback | Moderate quality |
| SBC | Maximum compatibility | Lowest audio quality |
