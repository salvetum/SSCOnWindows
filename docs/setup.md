---
title: Setup
layout: default
nav_order: 2
---

# Setup
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Prerequisites

- **Windows 10/11** (x64)
- A **dedicated USB Bluetooth adapter** (separate from your built-in Bluetooth)
- For SSC streaming: **WSL2** with a Linux distro (default path) or **Python 3.14 + Qiling** (native path)

## Step 1: Switch the dongle to WinUSB

SSC On Windows talks directly to a USB Bluetooth adapter via WinUSB. You need a
**dedicated adapter**; your built-in Bluetooth keeps working for normal Windows
peripherals. No Zadig is required -- the app installs (and can remove) the driver.

1. Plug in the dedicated USB Bluetooth adapter
2. Launch `SSCOnWindows.exe`
3. In the **Streaming Mode (Dongle)** panel, click **Enable Streaming (WinUSB)**
4. Accept the UAC prompt (a self-signed WinUSB INF + catalog is generated, signed and installed)

To revert, click **Restore Windows BT (BTHUSB)**.

{: .warning }
Never switch the driver on your PC's **built-in** Bluetooth adapter -- doing so disables all normal Bluetooth (keyboard, mouse, audio) and may require Device Manager or system recovery to restore. Always use a separate, dedicated dongle.

## Step 2: Find Your Headphone's Bluetooth Address

You need the Bluetooth MAC address of your headphones/speakers.

**From Windows Settings:**
1. Open **Settings > Bluetooth & devices**
2. Click your audio device
3. Click **Properties** -- the address is shown as `AA:BB:CC:DD:EE:FF`

**From the GUI:** paired Bluetooth audio devices appear in the device list after a scan.

**From the CLI:**
```
SSCOnWindows-0.1.exe --cli -l
```

## Step 3: Run

**GUI:**
```
SSCOnWindows.exe
```

**CLI:**
```
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF
```

See [Usage](usage) for full details.

---

## Firmware (Realtek Adapters)

Realtek-based adapters (e.g. TP-Link UB500, RTL8761BU dongles) require proprietary
firmware. **Intel and CSR adapters do not need this step.**

### Manual Download

1. Download the firmware and config `.bin` files for your chipset from
   [linux-firmware/rtl_bt](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt)
   (e.g. `rtl8761bu_fw.bin`, `rtl8761bu_config.bin`)
2. Place both in the config folder `%APPDATA%\A2DPWB`

{: .note }
These firmware files are proprietary Realtek binaries distributed via the linux-firmware project. They are not included in this repository. See [WHENCE](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/WHENCE) for redistribution terms.

---

## SSC Encoder Daemon

The SSC encoder is the Samsung aarch64 blob, so it runs behind a local TCP daemon
(`:20248`). Two backends are supported:

| Backend | How | Notes |
|:--------|:----|:------|
| WSL2 (default) | Blob + helper under `qemu-aarch64` inside WSL2 | Fastest (~1.2 ms RTT); needed for UHQ |
| Qiling native (`--ssc-native`) | `tools/ssc_daemon/sscblobd.py` under `py -3.14` | No WSL2; ~5x slower, 96 kHz marginal |

The Windows side starts the daemon automatically. For the WSL2 path, a distro must
be installed and running.

---

## Recommended Adapters

| Chipset | Example Products | Notes |
|:--------|:-----------------|:------|
| Realtek | TP-Link UB500, RTL8761BU | Tested; requires firmware download |
| Intel | Intel AX200/AX210 | No firmware needed |
| CSR | Generic CSR8510 dongles | No firmware needed |

USB Bluetooth 5.0+ adapters are recommended.
