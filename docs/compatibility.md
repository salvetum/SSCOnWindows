---
title: Compatibility
layout: default
nav_order: 8
---

# Compatibility Matrix
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Why this page exists

SSC is a vendor-private codec: the app relies on **remote capability
information** negotiated over AVDTP, and the practical result (audio or
silence) depends on what the specific device actually advertises — not just on
what the app is configured to send. With every firmware update the advertised
SSC capability byte can change. **Only devices that have been physically
verified with this app are listed as "tested".** Everything else is
"expected / not verified".

How to read the SSC capability byte (`cap=0x3C` in the logs): per
`sachk/openssc` `pipewire/a2dp-codec-ssc.c`:
- high nibble (`0x30`) = supported rate/mode bits,
- `0x08` = bitrate-limit bit,
- `0x04` = 24-bit support,
- `0x02` = **UHQ2 / 96 kHz** (`SSC_CAP_UHQ2`) — when absent the app refuses
  96 kHz and falls back to 48 kHz to avoid silent playback.

## Tested on this repository

| Device | Address | Dongle / chipset | Tested (SSC) | Sample rate / ch | SSC cap | Result |
|--------|---------|------------------|--------------|------------------|---------|--------|
| Samsung Galaxy **Buds3 FE** | `78:C1:1D:A7:BC:EE` | TP-Link UB500 (RTL8761BU, `VID_2357&PID_0604`) | High 229 / Standard 192 / Mobile 128 kbps | 48 kHz / stereo | `0x3C` (no UHQ) | Audio confirmed; UHQ request automatically falls back to 48 kHz |
| Samsung Galaxy **Buds3 FE** | `78:C1:1D:A7:BC:EE` | TP-Link UB500 | AAC 256 / SBC | 48 kHz / stereo | — | Codec paths present; exercise for spot checks |

Additional verification (not a different device):
- WSL2 daemon (qemu-aarch64) and Qiling native daemon (`--ssc-native`) both
  stream 48 kHz SSC solidly with the Buds3 FE.
- UHQ 96 kHz (584/442/250 kbps) is validated **byte-exact against frozen
  goldens** and encodes correctly, but the Buds3 FE cannot decode it — it is
  only usable with a UHQ-capable sink.

## Expected but not yet verified

| Device class | Expected | Why we expect it |
|--------------|----------|------------------|
| Other **Galaxy Buds** / Samsung TWS (Buds proper, Buds2/2 Pro, Buds3/3 Pro, Buds Pro, Buds Live, Buds+ …) | SSC works; UHQ only on models that advertise the `0x02` bit | Samsung's own Scalable Codec IP runs on-device; the negotiated capability byte decides UHQ eligibility |
| Windows 11 (current builds) | Same as Windows 10 tested baseline | User-mode stack + WinUSB is OS-version independent |

None of the above have been physically tested with this app yet. If you test one,
open a PR adding a row (see *How to contribute a row* below) — keep the address
short or redacted for privacy.

## Platform / capture matrix

| Layer | Support | Notes |
|-------|---------|-------|
| Windows 10 x64 | ✅ used during development | — |
| Windows 11 x64 | ✅ WinUI 3 GUI (Mica) | requires WindowsAppSDK 1.3+ (repo pins 1.8.x) |
| WASAPI capture | 48 kHz stereo | loopback is **pre-volume-mix**; UHQ = 2x SRC from 48 kHz (not a true 96 kHz capture) |
| USB dongles | TP-Link UB500 verified | Intel/CSR dongles expected fine (no firmware upload); Realtek needs `rtl8761*` firmware in `%APPDATA%\A2DPWB` |

## How to contribute a row

1. Build & run against your sink.
2. Capture the diagnostic block (`CAP:` / `SSC:` / `BTstack:` lines + the
   `cap=0xXX` capability byte) into the issue or PR.
3. State the exact model + firmware generation (e.g. "Buds2 Pro"),
   dongle/chipset, and codec/bitrate/rate combos you confirmed.
4. Mark clearly whether it produced **audio**, **silence**, or **garbled** —
   all three are useful datapoints for the gate logic.

Keep the device address private (use `AA:BB:…:FF` or a short prefix).

## Known limitations (affect compatibility)

- Windows **default output volume** does not change the captured/game-audio
  level (loopback is pre-mix). Use the app's AVRCP volume slider.
- Without the remote `0x02` (UHQ2) capability bit, the app **falls back to
  48 kHz** — the sink accepts the 0x0E config but mutes 96 kHz audio, so the
  gate is deliberate.
- SSC bitrates are mode-gated; the app snaps to the valid set (447 kbps → 442
  on UHQ / 328 on basic) to avoid malformed frames.
- The native Qiling daemon is ~5x slower than WSL2; acceptable at 48 kHz,
  marginal at 96 kHz UHQ.