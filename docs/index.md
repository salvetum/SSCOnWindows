---
title: Home
layout: default
nav_order: 1
---

# SSC On Windows

Stream Windows system audio to Samsung Galaxy Buds over **Samsung Scalable Codec (SSC)**.
{: .fs-6 .fw-300 }

A fork of [A2DP Windows Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge)
by Seiya Funaoka, adding a full SSC encoder pipeline alongside **AAC** and **SBC**.
Version 0.1, by **Salvetum**.

[Get Started](setup){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[GitHub](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge){: .btn .fs-5 .mb-4 .mb-md-0 }

---

## Supported Codecs

| Codec | Bitrate | Sample Rate | Status |
|:------|:--------|:------------|:-------|
| **SSC** (default) | High 229 / Standard 192 / Mobile 128 kbps; UHQ 584 / 442 / 250 kbps | 48 kHz (UHQ: 96 kHz) | Primary target |
| AAC | 128/192/256 kbps | 48 kHz | Via FDK-AAC |
| SBC | up to ~345 kbps | 48 kHz | Baseline fallback |

Fallback priority: **SSC > AAC > SBC**. SSC UHQ (96 kHz) requires a UHQ-capable
sink (Galaxy Buds3 FE advertises no UHQ and falls back to 48 kHz).

## Audio Capture Modes

Audio is captured via WASAPI in one of two modes:

| Mode | Description | Use Case |
|:-----|:------------|:---------|
| **System Loopback** | Captures all system audio output from the default playback device | Simple setup -- all sounds are streamed |
| **Virtual Device** | Captures from a user-selected virtual audio device (e.g., VB-CABLE, VoiceMeeter) | Route specific apps to Bluetooth while other audio stays on speakers |

In **Virtual Device** mode, the app switches the Windows default playback device to
the selected virtual device and captures its loopback output.

## How It Works

```
Audio Source (default output or virtual device)
  |
  v
WASAPI Loopback Capture (48 kHz, float32)
  |
  v
Encoder  (SSC  |  AAC  |  SBC)
  |
  |  SSC only: TCP :20248 -> WSL2/Qiling daemon -> Samsung libScalable_Encoder.so (aarch64)
  |
  v
BTstack  (A2DP Source -> AVDTP -> L2CAP -> HCI)
  |
  v
WinUSB -> USB Bluetooth Adapter -> Headphones
```

The Windows Bluetooth stack is bypassed entirely. A dedicated USB Bluetooth
adapter is switched to a generic **WinUSB** driver and driven directly by
[BTstack](https://github.com/bluekitchen/btstack) in user mode. The SSC encoder is
the actual Samsung aarch64 blob, run inside an emulator behind a local TCP daemon.

## Features

- **SSC (Samsung Scalable Codec)**: High / Standard / Mobile, plus 96 kHz UHQ on capable sinks
- **AAC and SBC** fallback codecs
- **In-app driver toggle**: switch the dongle between WinUSB and Windows Bluetooth (no Zadig needed)
- **Two capture modes**: system loopback or virtual audio device routing
- **Auto-reconnect** on disconnection (up to 10 attempts)
- **AVRCP absolute volume**: control headphone volume from the app
- **WinUI 3 GUI + CLI**
- **Live stats**: latency, error rate, loss, queue depth, sparklines
- **Profile management**, **Realtek firmware** helper, **English/Japanese UI**
