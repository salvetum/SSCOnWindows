# Changelog

All notable changes to this fork are documented here.

## [0.1] — 2026-09-18

### Changed: codec set reduced to SSC / AAC / SBC

- Removed **LDAC**, **aptX HD**, and **aptX Low Latency** entirely: encoder
  sources, stream endpoints, codec caps/SEIDs, settings, the ABR path, and the
  `libldac` / `libopenaptx` submodule dependencies (`libldac` / `libopenaptx`
  removed from `.gitmodules`; `compat/msvc/` deleted).
- Codec selection is now **SSC (default) / AAC / SBC** everywhere (CLI, WinUI,
  wx); fallback priority is **SSC > AAC > SBC**.
- Language files purged of dead `codec.ldac` / `codec.aptx_*` / `*.abr` keys.

### Changed: volume slider drives AVRCP absolute volume

- The WinUI volume slider now sets the **headphone** volume via AVRCP absolute
  volume (0-100% → 0-127) instead of a software pre-gain.
- `A2dpService` gained `set_device_volume` / `get_device_volume` /
  `set_volume_changed_callback`; device-initiated volume changes are mirrored
  back to the UI.

### Docs & repo hygiene

- `README.md` rewritten for the **SSC On Windows** fork (forks/credits, SSC
  pipeline, driver toggle, WSL2/Qiling backends); docs and
  `THIRD_PARTY_LICENSES.md` updated (Samsung blob, Qiling, Windows App SDK).
- The BTstack local modification is now captured as
  `patches/btstack-win-usb-logs.patch` (no longer only in the submodule worktree).
- `.gitignore` updated (`build_msvc/`, `dist/`, `winui3/bin|obj|.vs`,
  `config.json`, `__pycache__`), root build artifacts removed, planning notes
  moved to `docs/dev/`.

## [Unreleased] — 2026-09-17

### Fixed: WinUI 3 GUI crashed on launch (ACCESS_VIOLATION in `InitializeComponent`)

- **Root cause:** the bitrate-snap fix added `ApplyBitrateSnap()` to
  `MainWindow::OnRateChanged`. `RateCombo` has a `ComboBoxItem ... IsSelected="True"`
  in XAML, so XBF load raised `SelectionChanged` **during**
  `InitializeComponent()` — before the later-declared `BitrateSlider` had been
  connected. `ApplyBitrateSnap` then called `BitrateSlider()` (null) →
  `vcall'{112}'` on a null `IRangeBase` → `0xC0000005`, no window, silent exit.
- **Fix:** a `uiReady_` flag (false until the end of the ctor) now guards
  `OnRateChanged` / `OnBitrateChanged` / `OnVolumeChanged` / `OnDeviceSelectionChanged`
  / `OnCodecChanged`, and `ApplyBitrateSnap` uses null checks
  (`if (auto slider = BitrateSlider()) { ... }`).
- **Rule for future XAML edits:** never touch a named control from an event
  handler that can fire during `InitializeComponent()`; see `AGENTS.md` Gotchas.

### Changed: app is now "SSC On Windows" v0.1 (author: Salvetum)

- CLI output renamed `A2DPWB-1.0.1.exe` → **`SSCOnWindows-0.1.exe`**
  (`app/CMakeLists.txt` `OUTPUT_NAME`); WinUI exe `A2DPWBWinUI.exe` →
  **`SSCOnWindows.exe`** (`TargetName=SSCOnWindows`, manifest
  `assemblyIdentity name="SSCOnWindows.app"`).
- User-facing strings updated: CLI banner + window title, wx title/About,
  WinUI `Title` + header subtitle (`v0.1 · by Salvetum`, now code-driven from
  `APP_VERSION`), `gap_set_local_name("SSC On Windows")`, `app/lang/*.json`.
- **Internal identifiers were intentionally left unchanged** (CMake targets
  `A2DPWB`/`a2dpwb_core`, the `winui3\A2DPWBWinUI.vcxproj` file, the
  `winrt::A2DPWBWinUI` namespace, `%APPDATA%\A2DPWB`, the `A2DPWB` registry value,
  the driver signer subject) to avoid breaking the build, saved settings, and the
  existing driver-signing cert.

### Fixed: SSC bitrate slider could send mode-invalid bitrates (garbled audio)

- The SSC blob only supports a mode-gated set of bitrates; the old continuous
  0–990 slider could send e.g. 447 kbps → malformed frames → garbled audio.
  `SscEncoder::snap_bitrate_kbps()` now snaps to the rate-appropriate set at
  `init()`, and the GUI re-snaps on rate change. CLI parity: `--bitrate <kbps>`.

### Fixed: "Failed to launch makecat.exe" when enabling WinUSB streaming

- `find_sdk_tool()` resolved the full path of `makecat.exe` / `signtool.exe` under
  the Windows SDK, but the code then launched the **bare name** (not on `PATH`) —
  `ShellExecuteExW` failed with `ERROR_FILE_NOT_FOUND` (2). Both are now launched
  by their resolved full path.
- Also hardened all `std::string`→`std::wstring` conversions in `driver_switch.cpp`
  (`to_wide()` using `CP_ACP`) so paths under a non-ASCII `%LOCALAPPDATA%` /
  `%TEMP%` don't get mangled.

## [Unreleased] — 2026-09-10

### Fixed: SSC produced no audio from the WinUI 3 GUI (`encode_accum=0`)

- **Root cause:** the WiGig-era core set CODEC `bytes_per_sample = 2` (int16) for
  every codec unless `use_24bit`. The SSC encoder (TCP daemon over WSL2) always
  expects int32: `expect_bytes = 864 × 2 × 4 = 6912`, but the encode thread fed it
  only `864 × 2 × 2 = 3456` bytes → the encoder bailed out with `out_size = 0` on
  every frame. `encode_accum` stayed 0 despite a healthy HCI link (no audio).
- **Fix 1:** `a2dp_service.cpp` now forces
  `g_ctx.bytes_per_sample = 4` for SSC (`(use_24bit || codec == SSC) ? 4 : 2`).
- **Fix 2:** encode buffer enlarged 2048 → **4096** (SSC frames can be up to
  4096 bytes), so large frames aren't truncated.
- **Fix 3 (scale mismatch):** `convert_f32_to_i32` was hardcoded to 2^31; it now
  uses a global `g_pcm_int32_scale` set to **2^29 for SSC** (matching the CLI
  `main.cpp` behavior and the daemon's original `int16 << 14`) and 2^31 otherwise,
  keeping SSP levels identical between CLI and GUI.
- **Verified in `%APPDATA%\A2DPWB\winui_debug.log`:** `SSC: enc… total≈0.7–1.9 ms,
  ret=576` (High), `DIAG: encode_accum=576, queue_depth=1, send_fails=0`,
  capture `ts` advancing at ~96 k/s ≈ 48 kHz → healthy streaming, audio confirmed.

### Added: WinUI 3 GUI gets the Windows 11 look (Mica)

- `MainWindow.xaml.cpp` now sets `SystemBackdrop = MicaBackdrop` **in the
  constructor C++ code** (a `SystemBackdrop="MicaBackdrop"` XAML attribute fails
  under C++/WinRT with compiler error WMC0055).
- Requires WindowsAppSDK ≥ 1.3; repo pins 1.8.260804001, so no version bump needed.
- Note: `winui3.pch.h` already included `winrt/Microsoft.UI.Xaml.Media.h`.

### Moved: repo relocated from Temp workspace to `C:\Projects\SSCOnWindows`

- Robocopy `/E /MOVE` of the checkout (incl. `.git`, `build_msvc`, `winui3`) from
  `C:\Users\kaan5\AppData\Local\Temp\opencode\a2dp-windows-bridge`.
- `start.ps1` `$Repo` and `AGENTS.md` paths updated.
- `build_msvc` re-configured at the new location (wiped `CMakeCache.txt` +
  `_deps`); CMake 4.4 + wxWidgets 3.2.6 requires
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, else `cmake_minimum_required` fails.
- `winui3\obj\*` cleaned (stale `*.embed.manifest` referenced the old temp path,
  causing `RC2135` in Debug); Debug+Release core libs and WinUI exes rebuilt and
  deployed to `dist\SSCOnWindows.exe`.

### Notes for future work

- Verify the SCC 16-bit source path (`bits_per_sample == 16` in `main.cpp`) still
  matches the 2^29 int32 scale used by the float32 path.
- Windows volume does not affect captured level (loopback is pre-volume mix);
  may want a capture-side gain stage later.

---

## [Unreleased] — 2026-09-09

### Fixed: SSC audio was slowed/garbled (capture only reached ~38%)

- **Root cause 1 (throughput):** the SSC daemon (`sscblobd.c`, running in WSL2)
  served each frame over TCP without `TCP_NODELAY`. Nagle buffering delayed the
  second write (the encoded SSC frame) by ~40 ms per request, blocking the WASAPI
  capture thread. Capture rate dropped to ~18.4 kfps (38% of 48 kHz), producing
  barely-intelligible audio.
- **Fix:** added `TCP_NODELAY` on the daemon's accepted client socket. Encode
  round-trip went from ~47 ms to ~1.2 ms; capture now runs at the full 48000 fs
  with zero drops (`queue=0 fail=0`).

### Fixed: white-noise / crackle at low volume

- **Root cause 2 (low-level noise):** the pipeline converted WASAPI float32 PCM
  to **int16** before encoding. At low signal levels the signal fell to 1–2 bits
  of quantization, making the noise floor audible as white-noise/crackle.
- **Fix:** SSC now sends **int32 (24-bit) PCM** end-to-end. The daemon reads int32
  directly (`ssc_blob_helper` already consumed int32 at `bit_depth=24`); the
  Windows side converts float→int32 at the same **2^29 scale** the daemon used to
  create via `int16 << 14`, so playback level is unchanged. White-noise is gone.

### Diagnostics added (stderr, ~2 s cadence, kept for regression checks)

- `CAP:` capture rate/queue/send-fail counters (`main.cpp`).
- `SSC: enc...` per-encode timing split `t_send / t_hdr / t_data / total`
  (`ssc_encoder.cpp`).
- `WasapiCapture: CLK` device-clock vs consumed-frame check (`wasapi_capture.cpp`).
- `BTstack: CTX` transport `can_send / ok / err / queue` (`btstack_transport.cpp`).
- `CB:` WASAPI callback pacing (`main.cpp`).

### Notes for future work

- Verify the SCC 16-bit source path (`bits_per_sample == 16` in `main.cpp`) still
  matches the 2^29 int32 scale used by the float32 path.
- Windows volume does not affect captured level (loopback is pre-volume mix);
  may want a capture-side gain stage later.