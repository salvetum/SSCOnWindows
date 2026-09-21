# Changelog

All notable changes to this fork are documented here, following
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/).

Types of changes: **Added** / **Changed** / **Deprecated** / **Removed** /
**Fixed** / **Security**.

## [Unreleased]

### Added

- **`SscEncodeBackend` isolation** (Faz 6a) — the closed-source encoder blob is now
  reached only through the `app/src/ssc_encode_backend.h` interface. All WSL2/qemu +
  Qiling daemon, socket and wire-protocol logic moved into the default
  `DaemonSscBackend` (`app/src/ssc_daemon_backend.cpp`); `SscEncoder` is now a thin
  facade with unchanged public behavior (verified by the 6-profile golden
  regression, still byte-exact). A future open implementation (Faz 6b) can be
  swapped in as a drop-in.
- **README "Legal status" section** — SSC is Samsung's proprietary format: this
  project targets interoperability, not the format definition, and neither binary
  releases nor the setup flow distribute the blob (user-supplied `-BlobFrom`
  remains the recommended legal path).
- **LICENSE** now also carries the fork copyright line
  (`Copyright (c) 2026 Salvetum (SSC On Windows fork)`).

## [0.2.0] — 2026-09-21

### Added

- **Codec policy module** (`app/src/codec_policy.h`) — pure, header-only
  decision helpers shared by the CLI and the service backend: `resolve_codec`
  (fallback priority SSC > AAC > SBC), `snap_bitrate_bps` (mode-gated
  48 kHz basic / 96 kHz UHQ sets), `pick_bitrate_bps`, `resolve_encode_sr`
  (UHQ capability gate + `fell_back` flag), `resolve_stream`.
- **Unit + integration tests** (doctest, `tests/`) — 14 cases / 83 assertions
  covering codec fallback, bitrate snapping edges, the UHQ gate and an
  end-to-end negotiation via a mock transport. `a2dpwb_tests` is hermetic
  (does not link `a2dpwb_core`); CI builds it and runs `ctest` for each
  config.
- **Daemon `CMD_SHUTDOWN`** — the wire protocol now supports a 4-byte magic
  (`0x44434853`, LE `'S''H''C''D'`): `SscEncoder::shutdown()` sends it via
  `request_shutdown()`, and both `sscblobd.c` (WSL2) and `sscblobd.py`
  (Qiling) exit cleanly instead of hanging on SIGTERM with a client connected.
  The watchdog (`recover()`) still closes its own socket first for crash paths.
- **Docs**: `docs/architecture.md` expanded with a thread model, detailed data
  flow + timing budgets, and an error-handling/resilience matrix; new
  `docs/compatibility.md` device matrix.

### Changed

- **SemVer adoption** — version policy documented in `CONTRIBUTING.md`;
  `CHANGELOG.md` migrated to the Keep a Changelog format (unreleased entries
  predating 0.1 folded into `[0.1]`). Version bumped **0.1.1 → 0.2.0**
  (new features, backward compatible): CLI exe
  `SSCOnWindows-0.1.1.exe` → `SSCOnWindows-0.2.0.exe`, WinUI `APP_VERSION`,
  manifest + header subtitle, README/docs and `release.yml` updated.
- Encoder decision logic de-duplicated: `ssc_encoder.cpp`, `main.cpp` and
  `a2dp_service.cpp` now delegate to `codec_policy` (behavior preserved —
  golden regression 6/6 PASS).
- GUI design system (engineering/design pass, Faz B–D): typography policy,
  design tokens, and CLI-tagged severity colors in the WinUI log pane,
  enforced by `tools/ui_style_check.py` against the CLI and `setup.ps1`.

### Fixed

- SSC daemon shutdown is now graceful and over the wire (previously the client
  just dropped the socket and relied on the watchdog).

## [0.1.1] — 2026-09-20

### Changed

- **CLI console readability**: a minimal ANSI color layer
  (`app/src/console_style.h`); colors emitted only on a real console
  (pipes/redirection stay byte-clean), `NO_COLOR` env and `--no-color`
  disable colors outright. Diagnostic rows share one aligned tag/color
  language (`INFO`/`OK`/`WARN`/`ERROR`/`DATA`) used by the CLI, the WinUI log
  pane and `setup.ps1`; unhealthy values are highlighted. Field keywords are
  unchanged so existing parsers (golden tooling, telemetry) keep working.
- **Version bump 0.1 → 0.1.1**: CLI exe renamed
  `SSCOnWindows-0.1.exe` → `SSCOnWindows-0.1.1.exe` (`project(VERSION 0.1.1)`);
  WinUI `APP_VERSION`, header subtitle and manifest `0.1.1.0`; wx fallback
  defines, docs/SDK names and `release.yml` bumped.

## [0.1] — 2026-09-18

### Changed

- **Codec set reduced to SSC / AAC / SBC**: removed LDAC, aptX HD and aptX Low
  Latency entirely (encoder sources, stream endpoints, codec caps/SEIDs,
  settings, the ABR path, and the `libldac` / `libopenaptx` submodule
  dependencies, `compat/msvc/`). Codec selection is now
  **SSC (default) / AAC / SBC** everywhere; fallback priority **SSC > AAC > SBC**.
  Language files purged of dead codec keys.
- **Volume slider drives AVRCP absolute volume** (0-100% → 0-127) instead of a
  software pre-gain. `A2dpService` gained `set_device_volume` /
  `get_device_volume` / `set_volume_changed_callback`; device-initiated volume
  changes are mirrored back to the UI.
- **App identity**: renamed to **"SSC On Windows" v0.1** (author Salvetum) —
  CLI exe `A2DPWB-1.0.1.exe` → `SSCOnWindows-0.1.exe`; WinUI exe
  `A2DPWBWinUI.exe` → `SSCOnWindows.exe`. Internal identifiers intentionally
  unchanged (CMake targets, WinUI project file/namespace, `%APPDATA%\A2DPWB`,
  driver signer cert). CLI banner, wx title/About, WinUI Title + header
  subtitle (`APP_VERSION`-driven), `gap_set_local_name`, `app/lang/*.json`
  updated.
- **Docs & repo hygiene**: README rewritten for the fork; mobile progress;
  BTstack local modification captured as `patches/btstack-win-usb-logs.patch`;
  `.gitignore` updated (`build_msvc/`, `dist/`, `winui3/bin|obj|.vs`,
  `config.json`, `__pycache__`); planning notes moved to `docs/dev/`.

### Fixed

- **WinUI 3 crash on launch (ACCESS_VIOLATION in `InitializeComponent`)**:
  the bitrate-snap fix attached `ApplyBitrateSnap()` to `OnRateChanged`;
  `RateCombo` has `IsSelected="True"` in XAML, so XBF load raised
  `SelectionChanged` during initialization, before `BitrateSlider` existed →
  null `IRangeBase` → `0xC0000005`. Fixed with a `uiReady_` guard + null
  checks. Rule: never touch a named control from a handler that can fire
  during `InitializeComponent()` (see AGENTS.md Gotchas).
- **SSC bitrate slider sent mode-invalid bitrates** (e.g. 447 kbps → garbled
  audio): `SscEncoder::snap_bitrate_kbps()` now snaps to the
  rate-appropriate set at `init()`; GUI re-snaps on rate change; CLI parity
  `--bitrate <kbps>`.
- **"Failed to launch makecat.exe"** when enabling WinUSB streaming: SDK tools
  resolved by full path were launched by bare name. Fixed to use the resolved
  path; also hardened all `std::string`→`std::wstring` conversions in
  `driver_switch.cpp` (`CP_ACP`) for non-ASCII `%LOCALAPPDATA%`/`%TEMP%`.
- **SSC produced no audio from the WinUI GUI (`encode_accum=0`)**: the core
  forced int16 (`bytes_per_sample=2`) for every codec. SSC now gets
  `bytes_per_sample=4` (int32), the encode buffer grew 2048 → 4096 bytes, and
  `convert_f32_to_i32` uses the global `g_pcm_int32_scale` (2^29 for SSC, 2^31
  otherwise) to match the CLI. Verified: `ret=576`, capture advancing at 48 kHz.
- **SSC audio slowed/garbled (capture ~38%)**: Nagle buffering on the daemon
  held the encoded frame ~40 ms per request. Fixed with `TCP_NODELAY` on the
  daemon's accepted socket; round-trip went ~47 ms → ~1.2 ms, capture at full
  48000 fs with `queue=0 fail=0`.
- **White-noise / crackle at low volume**: pipeline was down-converting to
  int16 (1–2 bits of quantization near silence). SSC now sends **int32
  (24-bit) PCM** end-to-end at the same 2^29 scale the daemon used to build
  via `int16 << 14` — playback level unchanged, noise gone.

### Added

- **WinUI 3 GUI gets the Windows 11 look**: `SystemBackdrop = MicaBackdrop` set
  in the constructor C++ code (a XAML attribute fails under C++/WinRT with
  WMC0055). Requires WindowsAppSDK ≥ 1.3 (repo pins 1.8.x).
- **Diagnostics** (stderr, ~2 s cadence, used by regression checks): `CAP:`
  capture rate/queue/send-fail; `SSC: enc…` timing split `t_send/t_hdr/t_data/
  total`; `WasapiCapture: CLK` device-clock vs consumed frames; `BTstack: CTX`
  `can_send/ok/err/queue`; `CB:` WASAPI callback pacing.

### Deprecated

- Upstream `v1.0.0` / `v1.0.1` tags: legacy A2DP-Windows-Bridge versioning,
  superseded by this fork's SemVer scheme.

### Notes for future work

- Verify the SSC 16-bit source path (`bits_per_sample == 16` in `main.cpp`)
  still matches the 2^29 int32 scale used by the float32 path.
- Windows volume does not affect captured level (loopback is pre-volume mix);
  a capture-side gain stage may be added later.

---

## [v1.0.1] — upstream

Last upstream release of A2DP Windows Bridge before this fork. See the
upstream repository for its changelog.

[0.2.0]: https://github.com/salvetum/SSCOnWindows/releases/tag/v0.2.0
[0.1.1]: https://github.com/salvetum/SSCOnWindows/releases/tag/v0.1.1
[0.1]: https://github.com/salvetum/SSCOnWindows/releases/tag/v0.1
[v1.0.1]: https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/releases/tag/v1.0.1