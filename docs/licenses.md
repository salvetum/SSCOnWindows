---
title: Licenses
layout: default
nav_order: 6
---

# Licenses
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Project License

**SSC On Windows** (a fork of A2DP Windows Bridge) is licensed under the **MIT
License**. See [LICENSE](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/LICENSE).
The base project is Copyright (c) 2026 Seiya Funaoka.

## Third-Party Components

| Component | License | Usage |
|:----------|:--------|:------|
| [BTstack](https://github.com/bluekitchen/btstack) | BSD-3-Clause (non-commercial) / Commercial | User-mode Bluetooth stack |
| [fdk-aac](https://github.com/mstorsjo/fdk-aac) | FDK AAC License | AAC-LC encoder |
| [wxWidgets](https://www.wxwidgets.org/) | wxWindows Library Licence | Legacy GUI framework |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | JSON parser |
| [Qiling](https://github.com/qilingframework/qiling) | GPL-2.0 | Native aarch64 emulation for the SSC daemon |
| Samsung `libScalable_Encoder.so` | Proprietary (Samsung) | SSC encoder blob (not MIT) |
| [openssc](https://github.com/sachk/openssc) | see upstream | SSC codec integration reference |
| Windows App SDK / Windows SDK | Microsoft EULA | WinUI 3 runtime + system APIs |

For full license texts and compliance details, see
[THIRD_PARTY_LICENSES.md](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/THIRD_PARTY_LICENSES.md).

## Important Notes

### BTstack Dual License

BTstack is dual-licensed. The BSD-3-Clause license permits **non-commercial use**
only. Commercial use requires a separate license from
[BlueKitchen GmbH](https://bluekitchen-gmbh.com/).

### Samsung SSC Encoder

The SSC encoder (`libScalable_Encoder.so`) is proprietary Samsung software and is
not covered by the MIT license. Redistribution may be restricted; verify your
rights before publishing binaries or sources that contain it.

### Qiling (GPL-2.0)

Qiling is used only for the experimental native SSC daemon and runs as a separate
`py -3.14` process; it is not linked into the application and is not bundled.

### AAC Patent Notice

AAC is covered by patent licenses managed by Via Licensing. Commercial products
should obtain appropriate licenses.

### Bluetooth Qualification

Commercial Bluetooth products must undergo the
[Bluetooth Qualification Process](https://www.bluetooth.com/) managed by the
Bluetooth SIG.
