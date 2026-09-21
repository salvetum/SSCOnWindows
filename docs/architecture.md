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
SSCOnWindows.exe (WinUI 3)  /  SSCOnWindows-0.2.0.exe (CLI)
├── WinUI Layer (winui3/, C++/WinRT)
│   ├── App / MainWindow     Scan, connect, codec/quality/rate, volume, stats, log
│   ├── Streaming Mode panel WinUSB / BTHUSB driver toggle
│   └── Setup & Help         Pairing guide + FAQ
│
├── Legacy GUI Layer (wxWidgets)
│   ├── wx_app / wx_main_frame / wx_profile_dialog
│   └── wx_about_dialog / localization (en, ja — embedded JSON)
│
├── Core Layer (a2dpwb_core, shared by CLI + WinUI)
│   ├── a2dp_service        A2DP connection lifecycle & state machine
│   ├── codec_policy        Pure codec/bitrate/rate decision logic (header-only)
│   ├── btstack_transport   BTstack integration (HCI, L2CAP, AVDTP, A2DP, AVRCP)
│   ├── wasapi_capture      WASAPI loopback audio capture + auto-mute
│   ├── audio_encoder       Encoder interface (abstract base)
│   │   ├── ssc_encoder         SSC facade -> SscEncodeBackend (see below)
│   │   ├── aac_encoder         AAC-LC (fdk-aac, LATM transport)
│   │   └── a2dp_sbc_encoder    SBC (BTstack Bluedroid)
│   ├── ssc_encode_backend  Encoder-engine interface (Faz 6a isolation)
│   │   └── ssc_daemon_backend TCP daemon backend (blob via WSL2/Qiling) + watchdog
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
├── CLI Mode
│   └── main.cpp            CLI argument parsing, headless streaming
│
├── SSC Daemons (not part of the exe)
│   ├── tools/ssc_payload/  WSL2 sscblobd.c + ssc_blob_helper (qemu-aarch64)
│   └── tools/ssc_daemon/   Windows sscblobd.py (Qiling) + minimal rootfs
│
└── Tests (tools/golden + tests/)
    ├── tools/golden/       Byte-exact SSC regression goldens (6 profiles)
    └── tests/              doctest: unit (codec_policy) + integration (mock)
```

> The core is UI-free; the CLI (`main.cpp`) and the WinUI GUI drive the same
> `A2dpService` backend. `codec_policy.h` holds the fallback/bitrate/UHQ
> decisions in pure, header-only, unit-testable form (see *Testing*).

## Thread Model

The pipeline is multi-threaded. Each thread is created once per stream/run:

| Thread | Owner | Created in | Responsibility |
|--------|-------|-----------|----------------|
| **UI thread(s)** | WinUI 3 (C++/WinRT) or wx (legacy) | — | Window, controls, `State/StreamInfo/Stats` callbacks (dispatcher-enqueued), driver toggle triggers a worker |
| **BTstack run loop** | `btstack_transport.cpp` | `BtStackTransport::init` (`CreateThread`) | Owns the BTstack event loop: HCI, L2CAP, AVDTP, A2DP, AVRCP. All cross-thread BTstack calls are marshalled with `btstack_run_loop_execute_on_main_thread()` into synchronous wrappers that signal an event |
| **WASAPI capture** | `wasapi_capture.cpp` | `WasapiCapture::start` (`CreateThread`) | COM (MTA per-thread), MMCSS `"Audio"` class, 1 ms timer resolution. Pumps `IAudioCaptureClient`, converts float32 → i32 (2^29 SSC / 2^31 otherwise), downmixes, and pushes PCM frames into the encode queue via the callback |
| **Streaming worker** | `a2dp_service.cpp` | `A2dpService::start_streaming` (`std::thread`) | `streaming_thread_func`: connect, spawn the encode thread, supervise the main loop, auto-reconnect (≤ 10 attempts). Wrapped in a SEH guard (`seh_call`) so a BTstack crash becomes `State::Error` instead of a hang |
| **Encode** | `a2dp_service.cpp` / `main.cpp` | `CreateThread(encode_thread_func)` | Pops PCM frames from the queue, runs the codec encoder (SSC does a synchronous TCP round-trip per 864-sample frame), then `transport->send_media()` (thread-safe, marshalled to the BTstack thread). Writes telemetry + ~2 s stats |

**Synchronization notes**
- BTstack's event loop is **single-threaded** — the transport layer is the only
  writer to BTstack state, via the run-loop marshalling wrappers; media sending
  from the encode thread uses `btstack_run_loop_execute_on_main_thread` on a
  triggered callback.
- WASAPI callbacks register `AvSetMmThreadCharacteristicsW(L"Audio")` so the
  capture thread gets low-latency scheduling; the encode budget has large
  headroom against the frame period.
- `SscEncoder` keeps its own socket + a watchdog (`recover()`, 5 s cooldown) and
  is only ever touched by the encode thread.

## Data Flow

```
System Audio Output (default device, loopback = pre-volume-mix)
        |
        v
   WASAPI Capture (float32, 48 kHz -- limited by WASAPI shared mode)
        |
        v  capture callback (capture thread)
   [downmix to stereo] -> [f32 -> i32 scale 2^29 SSC / 2^31 else]
        |                    (UHQ only: 2x SRC 48k -> 96k, Catmull-Rom)
        v
   PCM frame queue (864 samples / frame)
        |
        v  encode thread
   Encoder (SSC | AAC | SBC)
        |   SSC path: TCP :20248 (864 samples -> int32 frame @2^29)
        |      -> WSL2: sscblobd -> qemu-aarch64 -> libScalable_Encoder.so
        |      -> (or) native: sscblobd.py -> Qiling (--ssc-native)
        |      <- int32 ret + encoded SSC frame (TCP_NODELAY)
        v
   A2dpService -> BtStackTransport::send_media()  (marshalled to BTstack thread)
        |
        v
   BTstack A2DP Source -> AVDTP -> L2CAP -> HCI
        |
        v
   WinUSB -> USB Bluetooth Adapter -> Bluetooth Radio
        |
        v
   Headphones / Speaker (AVRCP absolute volume)
```

**Timing budgets (48 kHz / stereo)**
- Capture callback pacing: ~480 samples ≈ **10 ms** (healthy `CB: last-call < 15 ms`).
- SSC encode frame: `kFrameSamples = 864` ≈ **18 ms** of audio at 48 kHz.
- So frame budget is 18 ms at 48 kHz, **9 ms** at 96 kHz (UHQ).
- Measured encode round-trip: **~1.2 ms** (WSL2/qemu) vs **~5-6 ms** (Qiling
  native, spikes to ~20 ms in UHQ). Headroom ≥ 87% at 48 kHz (both), ~33% at
  96 kHz on Qiling — hence "prefer WSL2 for UHQ".

**Key invariants (do not regress)**
1. `TCP_NODELAY` must stay set on the daemon's accepted socket (Nagle would hold
   the encoded frame ~40 ms and stall the capture thread to ~38%).
2. SSC PCM is **int32 @ 2^29** end-to-end (int16 quantization was removing the
   low-level noise floor). CLI and WinUI share the same `g_pcm_int32_scale`.
3. SSC needs `bytes_per_sample = 4` in `a2dp_service.cpp`
   (`(use_24bit || codec == SSC) ? 4 : 2`), else the encoder returns
   `out_size = 0` and the stream is silent.
4. A graceful daemon shutdown goes over the wire (`CMD_SHUTDOWN`, `0x44434853`)
   in `SscEncoder::shutdown()`; the watchdog (`recover()`) closes its own socket
   *first* only for crash paths.

## Key Modules

### Codec policy (`codec_policy.h`)

Pure, header-only decision helpers used by both CLI and service:
- `resolve_codec(requested, caps)` — fallback priority **SSC > AAC > SBC**.
- `snap_bitrate_bps(bps, sample_rate)` — snaps to the **mode-gated** valid set
  (48 kHz basic: 88/96/128/192/229/256/328 kbps; 96 kHz UHQ: 152/250/291/308/
  442/584/886 kbps) so the blob can never be fed an invalid rate.
- `pick_bitrate_bps(quality, sample_rate)` — High/Standard/Mobile × 48/96 kHz.
- `resolve_encode_sr(...)` — enforces the **UHQ capability gate** (remote
  `ssc_uhq` bit) and reports fallback via a `fell_back` flag.
- `resolve_stream(...)` — end-to-end composition used by the negotiation path.

### A2DP Service (`a2dp_service.cpp`)

Central connection lifecycle manager. Coordinates:
- Codec negotiation (requested codec, fallback priority SSC > AAC > SBC)
- Stream endpoint registration for all supported codecs
- Connection state machine (idle → connecting → streaming → disconnecting)
- Auto-reconnect logic on unexpected disconnection (≤ 10 attempts)
- Media packet sending with codec-specific framing
- AVRCP absolute volume (device volume callback)
- Auto-mute of the default render endpoint while streaming (loopback is
  pre-mix, so muting speakers does not affect the headphones)

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
- float32 → int32 conversion at `g_pcm_int32_scale`, channel downmix
- `mute_output()` mutes the default render endpoint (loopback is pre-mix, so
  muting speakers does not affect the headphones)
- Configurable device selection

### SSC Encoder (`ssc_encoder.cpp` → `SscEncodeBackend`)

Faz 6a isolation: the closed-source encoder engine is reached only through the
`SscEncodeBackend` interface (`ssc_encode_backend.h`). The default
`DaemonSscBackend` (`ssc_daemon_backend.cpp`) is a TCP client to the encoder
daemon, plus resilience:
- Synchronous request/response per 864-sample frame (split timing
  `t_send / t_hdr / t_data / total` for diagnostics)
- `pick_bitrate()` + `snap_bitrate_kbps()` → both delegate to `codec_policy`
- `recover()` watchdog: on transport failure, closes its own socket and
  restarts the daemon (WSL or native) with a 5 s cooldown; recovery counter
  exposed for diagnostics
- `shutdown()` → `request_shutdown()` sends `CMD_SHUTDOWN` (0x44434853) so the
  daemon exits cleanly instead of hanging on SIGTERM with a client connected
- A future open implementation (Faz 6b) can be provided as another
  `SscEncodeBackend` drop-in

### Audio Encoders

| Encoder | Backend | Bitrate | Notes |
|---------|---------|---------|-------|
| SSC | Samsung `libScalable_Encoder.so` (aarch64 via daemon) | 128/192/229 kbps (48k); 250/442/584 kbps (96k UHQ) | Default; mode-gated bitrates, capability-gated UHQ |
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

## Error Handling & Resilience

| Failure | Detection (diagnostic) | Recovery |
|---------|------------------------|----------|
| HCI init timeout | `BTstack: HCI initialization timed out` | `init()` fails; caller shows error |
| Dongle not in WinUSB | driver-mode panel shows `BTHUSB` | auto-connect is skipped with a log hint; user toggles via the panel |
| Daemon down / TCP failure | `SSC: daemon lost, restarting (recovery #N)...` | `SscEncoder::recover()` watchdog: closes socket, re-spawns daemon (5 s cooldown), reconnects |
| Encode transport inconsistency | `SSC: enc… ret=0` / short reads | socket marked `connected_=false` → watchdog path |
| A2DP connection loss | `BTstack: Connection lost — ready for reconnect` | capture paused; auto-reconnect ≤ 10 attempts, then `State::Error` (CLI exits) |
| BTstack crash in stream | `A2dpService: SEH exception … in streaming thread` | SEH guard → `State::Error`, state `btstack_crash_stream` |
| UHQ request on 48k-only sink | `cap=0x3C` (no `0x02`), status toast | fall back to 48 kHz SSC + warn (silent audio otherwise) |
| Invalid bitrate request | snapped value logged | `snap_bitrate_bps` keeps the blob in its supported set (malformed frames otherwise) |
| Capture overrun/latency | `CAP: rate` < 48000, `queue/fail > 0` | encoder round-trip too slow → reduce load (see timing budgets) |
| Driver toggle while streaming | blocked | `RunDriverSwitch` refuses while `running` |

**Diagnostics invariants** (stderr, ~2 s cadence; golden-tested)
- `CAP: rate=48000fs queue≈0 fail≈0`
- `SSC: total≈1.2 ms ret=576` (High) / `ret=484` (Std) / `ret=324` (Mobile)
- `BTstack: CTX ok≈can_send err=0 q=0`
- `CB: last-call < 15 ms`

> `ret=576/484/324` correspond to High/Standard/Mobile 48 kHz frame sizes
> (229/192/128 kbps × 864 samples / 8000 × 8 bits ≈ 247/207/138 bytes… the
> exact frozen values live in `tools/golden/*.golden`). Use the goldens, not
> this table, as the source of truth.

## Testing

- **doctest** (`tests/`, hermetic — does not link `a2dpwb_core`):
  - `unit/codec_policy_tests.cpp` — fallback order, bitrate snapping edges,
    UHQ gate, quality×rate matrix, end-to-end `resolve_stream`. 14 cases /
    83 assertions.
  - `integration/negotiation_tests.cpp` — a `MockTransport` caps provider
    driving the same negotiation decisions the CLI/service use (mocking the
    real btstack run-loop events would need a transport-interface refactor;
    the policy seam is where the decisions live).
- **Golden regression** (`tools/golden/ssc_golden.py check --all`) — byte-exact
  frozen SSC blob output for 6 profiles (128k/192k/229k @48k, 250k/442k/584k
  @96k). Re-freeze only when the Samsung blob intentionally changes.
- CI (`build.yml`) builds `a2dpwb_tests` and runs `ctest` for Debug+Release.

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
- Versioning follows **SemVer** (see `CHANGELOG.md` and `CONTRIBUTING.md`).