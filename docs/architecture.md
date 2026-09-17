---
title: Architecture
layout: default
nav_order: 5
---

# Architecture
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Overview

**SSC On Windows** is a fork of A2DP Windows Bridge. It streams Windows system
audio to Bluetooth headphones over **Samsung Scalable Codec (SSC)**, **AAC**, or
**SBC**, without a kernel driver. Windows natively supports only SBC and AAC for
A2DP; this project adds SSC on top of the same user-mode transport.

Uses **BTstack + WinUSB** — entirely user-mode, no driver signing needed. The
Windows Bluetooth stack is not involved in streaming.

## Transport: BTstack + WinUSB

A dedicated USB Bluetooth adapter is switched from the Microsoft Bluetooth driver
(`BTHUSB`) to a generic **WinUSB** driver (an in-app toggle generates, signs and
installs a minimal WinUSB INF; no Zadig required). BTstack then implements HCI,
L2CAP, AVDTP, A2DP and AVRCP in user mode.

**Advantages**:
- No driver signing cost (an in-app self-signed WinUSB INF + catalog is used)
- No test signing mode required
- No Secure Boot disabling
- All code runs in user-mode (easier debugging)

**Trade-offs**:
- Requires a dedicated USB Bluetooth adapter (separate from Windows' built-in
  Bluetooth)
- While claimed by WinUSB the adapter is unavailable for normal Windows Bluetooth
- The device address must be entered manually or selected from the Windows paired
  device list

**How it works**:
1. The user toggles the dongle to WinUSB from the app (driver switch)
2. BTstack opens the USB device via the WinUSB API
3. BTstack sends HCI commands to initialize the controller
4. (Realtek adapters) Firmware is uploaded if needed
5. BTstack establishes an ACL connection and opens L2CAP channels (PSM 0x0019)
6. AVDTP signaling discovers remote SEPs and negotiates the codec
7. Encoded audio is sent as AVDTP media packets over L2CAP

## Module Structure

```
SSCOnWindows.exe (WinUI 3)  /  SSCOnWindows-0.1.exe (CLI)
├── WinUI Layer (winui3/, C++/WinRT)
│   ├── App / MainWindow     Scan, connect, codec/quality/rate, volume, stats, log
│   ├── Streaming Mode panel WinUSB / BTHUSB driver toggle
│   └── Setup & Help         Pairing guide + FAQ
│
├── Legacy GUI Layer (wxWidgets)
│   ├── wx_app / wx_main_frame / wx_profile_dialog
│   └── wx_about_dialog / localization (en, ja — embedded JSON)
│
├── Core Layer
│   ├── a2dp_service        A2DP connection lifecycle & state machine
│   ├── btstack_transport   BTstack integration (HCI, L2CAP, AVDTP, A2DP, AVRCP)
│   ├── wasapi_capture      WASAPI loopback audio capture + auto-mute
│   ├── audio_encoder       Encoder interface (abstract base)
│   │   ├── ssc_encoder         SSC (Samsung blob over TCP daemon)
│   │   ├── aac_encoder         AAC-LC (fdk-aac, LATM transport)
│   │   └── a2dp_sbc_encoder    SBC (BTstack Bluedroid)
│   ├── resampler           2x SRC for SSC UHQ (96 kHz)
│   ├── driver_mode         Dongle driver-mode detection (WinUSB vs BTHUSB)
│   ├── driver_switch       WinUSB INF generation + signing + install/remove
│   ├── bt_adapter_enum     USB Bluetooth adapter enumeration (WinUSB)
│   ├── audio_device_enum   WASAPI audio device enumeration
│   └── profile_manager     Connection profile persistence (JSON)
│
├── Support
│   ├── app_settings        Persistent application settings (JSON)
│   ├── config_path         Config file path resolution
│   └── debug_log           Debug logging macros
│
└── CLI Mode
    └── main.cpp            CLI argument parsing, headless streaming
```

## Data Flow

```
System Audio Output
       │
       ▼
 WASAPI Loopback Capture (float32, 48 kHz)
       │
       ▼
 Encoder (SSC / AAC / SBC)
       │
       │  SSC: TCP :20248 → WSL2 (qemu-aarch64) or Qiling → libScalable_Encoder.so
       ▼
 A2DP Service → BtStackTransport::send_media()
       │
       ▼
 BTstack A2DP Source → AVDTP → L2CAP → HCI
       │
       ▼
 WinUSB → USB Bluetooth Adapter → Bluetooth Radio
       │
       ▼
 Headphones / Speakers
```

## Key Modules

### A2DP Service (`a2dp_service.cpp`)

Central connection lifecycle manager. Coordinates:
- Codec negotiation (requested codec, fallback priority SSC > AAC > SBC)
- Stream endpoint registration for all supported codecs
- Connection state machine (idle → connecting → streaming → disconnecting)
- Auto-reconnect logic on unexpected disconnection
- Media packet sending with codec-specific framing
- AVRCP absolute volume (device volume callback)

### BTstack Transport (`btstack_transport.cpp`)

Bridge between the application's synchronous model and BTstack's event-driven API.

**Responsibilities**:
- Initialize BTstack with the WinUSB HCI transport
- Run the BTstack event loop in a dedicated thread
- Register codec stream endpoints (SSC vendor-specific, AAC, SBC)
- Handle A2DP connection lifecycle via async-to-sync wrappers
- Manage SSP pairing (Just Works); persist link keys
- Provide thread-safe media sending from the encode thread
- AVRCP volume and Realtek firmware loading

**Key BTstack APIs used**:
- `a2dp_source_create_stream_endpoint()` — register codec endpoints
- `a2dp_source_establish_stream()` — connect to an A2DP sink
- `a2dp_source_set_config_other()` — configure the SSC vendor-specific codec
- `a2dp_source_stream_send_media_payload_rtp()` — send encoded audio

### WASAPI Capture (`wasapi_capture.cpp`)

Captures system audio in real time via the Windows Audio Session API.
- `IAudioClient` in `AUDCLNT_STREAMFLAGS_LOOPBACK` mode
- float32 → int32/int16 conversion, channel downmix
- `mute_output()` mutes the default render endpoint (loopback is pre-mix, so
  muting speakers does not affect the headphones)
- Configurable device selection

### Audio Encoders

| Encoder | Backend | Bitrate | Notes |
|---------|---------|---------|-------|
| SSC | Samsung `libScalable_Encoder.so` (aarch64 via daemon) | 128/192/229 kbps (48k); 250/442/584 kbps (96k UHQ) | Default; mode-gated bitrates |
| AAC | fdk-aac | up to 256 kbps | AAC-LC, LATM transport |
| SBC | BTstack Bluedroid | up to ~345 kbps | Mandatory A2DP baseline |

All encoders implement the `AudioEncoder` interface with `encode()` and
`get_frame_size()` methods.

## Vendor Codec Information Elements

Non-standard codecs are registered as Vendor Specific in AVDTP:

| Codec | Vendor ID | Codec ID |
|-------|-----------|----------|
| SSC | Samsung (0x00000075) | 0x0001 |

AAC and SBC use standard A2DP codec IDs defined in the A2DP specification.

## Build System

- **CMake** with MSVC (Visual Studio 2022 or later) for the core / CLI
- **MSBuild** for the WinUI 3 project (`winui3/A2DPWBWinUI.vcxproj`)
- Third-party libraries built as static libraries from source (submodules)
- wxWidgets fetched at configure time via CMake FetchContent (v3.2.6)
- Language files (JSON) embedded into the executable at configure time

## Important Considerations

- **Adapter compatibility**: tested with the TP-Link UB500 (Realtek RTL8761BU).
  Realtek adapters require a firmware upload at startup; Intel/CSR do not.
- **Pairing**: SSP Just Works. Link keys are persisted locally.
- **Dedicated adapter recommended**: keep built-in Bluetooth for Windows and use a
  separate USB adapter for streaming.
- **SSC UHQ** is gated on the remote capability bit (Buds3 FE: `0x3C`, no UHQ).
- Codec fallback priority: **SSC > AAC > SBC**.
