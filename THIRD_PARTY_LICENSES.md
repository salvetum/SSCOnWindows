# Third-Party Licenses

This document lists the third-party components used by **SSC On Windows**, a fork of
**A2DP Windows Bridge** by Seiya Funaoka, together with their licenses and
compliance obligations.

---

## Summary

| Component | License | Copyright / Owner | Usage |
|-----------|---------|-------------------|-------|
| A2DP Windows Bridge (upstream) | MIT | Seiya Funaoka | Base project |
| BTstack | BSD-3-Clause (dual) | BlueKitchen GmbH | User-mode Bluetooth stack (incl. SBC codec) |
| Fraunhofer FDK AAC | FDK AAC License | Fraunhofer IIS | AAC-LC encoding |
| wxWidgets | wxWindows Library Licence (LGPL-2.0 + exception) | wxWidgets Team | Legacy GUI |
| nlohmann/json | MIT | Niels Lohmann | JSON parsing (settings, profiles, localization) |
| Samsung SSC encoder (`libScalable_Encoder.so`) | Proprietary (Samsung) | Samsung Electronics | SSC audio encoding (aarch64 blob) |
| openssc | see upstream | sachk | SSC codec integration reference |
| Qiling | GPL-2.0 | Qiling Framework | aarch64 emulation for the native SSC daemon |
| Windows App SDK | Microsoft EULA | Microsoft Corporation | WinUI 3 runtime |
| Windows SDK / Win32 APIs | Microsoft EULA | Microsoft Corporation | WASAPI, COM, WinUSB, SetupAPI, Bluetooth |

---

## 1. A2DP Windows Bridge (upstream project)

- **Source**: <https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge>
- **License**: MIT
- **Copyright**: Copyright (c) 2026 Seiya Funaoka
- This fork adds the SSC encoder pipeline, the WinUI 3 frontend, the in-app driver
  toggle, and related tooling. The fork itself is distributed under the same MIT
  license (see `LICENSE`).

---

## 2. BTstack (Bluetooth Stack)

- **Source**: <https://github.com/bluekitchen/btstack>
- **Path in project**: `extern/btstack/` (git submodule)
- **License**: Dual-licensed: BSD-3-Clause (non-commercial) / Commercial
- **Copyright**: Copyright (C) BlueKitchen GmbH
- **SPDX**: `BSD-3-Clause`

BTstack bundles a **Bluedroid SBC encoder/decoder** (`3rd-party/bluedroid/`)
originally from AOSP, licensed under Apache-2.0.

### Local modifications

The submodule carries a small local patch adding error logging to the Windows
WinUSB transport. It is kept as `patches/btstack-win-usb-logs.patch` and is not
committed inside the submodule.

### Obligations

- Non-commercial use is permitted under BSD-3-Clause.
- Commercial use requires a separate license from BlueKitchen GmbH.
- Include the BSD license and copyright notice in distributions.
- Do not use the name "BlueKitchen" or "BTstack" to endorse derived products.

---

## 3. Fraunhofer FDK AAC (AAC Encoder)

- **Source**: <https://github.com/mstorsjo/fdk-aac>
- **Path in project**: `extern/fdk-aac/` (git submodule)
- **License**: Fraunhofer FDK AAC Codec Library for Android (Software License)
- **Copyright**: Fraunhofer-Gesellschaft zur Foerderung der angewandten Forschung e.V.
- **SPDX**: `FDK-AAC`

### Obligations

- Permitted for non-commercial and development use.
- Redistribution and use in source and binary forms permitted with conditions.
- Include the license and copyright notice in distributions.
- Modified versions must be clearly identified as such.
- No use of the Fraunhofer name to endorse derived products without permission.

### Patent Notice

AAC is covered by patent licenses managed by Via Licensing. Commercial products
using AAC encoding should obtain the appropriate patent licenses.

---

## 4. wxWidgets (Legacy GUI Framework)

- **Source**: <https://github.com/wxWidgets/wxWidgets>
- **Version used**: v3.2.6 (fetched via CMake FetchContent at build time)
- **License**: wxWindows Library Licence (LGPL-2.0 with exception)
- **Copyright**: Copyright (C) 1992-2024 wxWidgets Team
- **SPDX**: `LGPL-2.0-or-later WITH WxWindows-exception-3.1`

The exception clause permits binary distribution of works that link wxWidgets
without releasing the application under LGPL.

### Obligations

- Include the wxWindows Library Licence and copyright notice in distributions.
- Modified wxWidgets source must be made available under the same licence.

---

## 5. nlohmann/json

- **Source**: <https://github.com/nlohmann/json>
- **Path in project**: `extern/json/` (header-only, git submodule)
- **Version**: 3.11.3
- **License**: MIT
- **Copyright**: Copyright (C) 2013-2023 Niels Lohmann
- **SPDX**: `MIT`

### Obligations

- Include the MIT license and copyright notice in distributions.

---

## 6. Samsung SSC Encoder (`libScalable_Encoder.so`)

- **Owner**: Samsung Electronics Co., Ltd.
- **Path in project**: `tools/ssc_payload/blob/libScalable_Encoder.so`,
  `tools/ssc_daemon/rootfs/blob/libScalable_Encoder.so`
- **License**: Proprietary. The library is the on-device SSC encoder extracted
  from a Samsung device; it is **not** open source.

### Notes / obligations

- This blob is proprietary Samsung software and is not covered by this project's
  MIT license. Redistribution may be restricted; verify you have the right to
  redistribute it before publishing binaries or sources that contain it.
- The SSC wire format and codec parameters were implemented with reference to
  the open-source **openssc** project (see below).
- The aarch64 blob is executed in an emulator; it is never linked into the
  Windows process.

---

## 7. openssc (SSC codec integration reference)

- **Source**: <https://github.com/sachk/openssc>
- **Usage**: Reference for SSC capabilities (`0x3C`), mode-gated bitrate sets,
  and A2DP codec negotiation rules. No code is copied into this project; it is an
  interoperability reference.

---

## 8. Qiling (Binary Emulation Framework)

- **Source**: <https://github.com/qilingframework/qiling>
- **License**: GNU General Public License v2.0 (GPL-2.0)
- **Usage**: Runs the aarch64 SSC blob natively on Windows in the experimental
  `--ssc-native` mode, inside a separate `py -3.14` process
  (`tools/ssc_daemon/sscblobd.py`).

### Notes

- Qiling is **not** redistributed with this project; users install it themselves.
  Because it runs as a separate process (not linked into the application), the
  project code is not a derived work of Qiling. If you redistribute Qiling
  together with this app, GPL-2.0 obligations apply to that distribution.

---

## 9. Windows App SDK / Windows SDK and APIs

- **Provider**: Microsoft Corporation
- **License**: Microsoft Software License Terms
- **Windows App SDK**: WinUI 3 runtime (`Microsoft.WindowsAppRuntime.Bootstrap.dll`,
  WebView2, XAML `.xbf`/`.pri`), version 1.8.x.
- **Windows SDK APIs used**: COM (`ole32`), WASAPI (`audioclient.h`,
  `mmdeviceapi.h`), AVRT (`avrt`), WinUSB, SetupAPI, Property Store, Shell,
  DPI/scaling, version info, multimedia timer.

### Obligations

- The SDK is licensed for developing Windows applications.
- Distributed binaries must run on licensed copies of Windows.
- SDK headers/libraries may not be redistributed.

---

## 10. Bluetooth Specifications

This project implements protocols defined by the Bluetooth SIG: **A2DP**, **AVDTP**,
**AVRCP**, and **L2CAP**. Implementation does not require licensing fees for
open-source projects, but commercial products must obtain Bluetooth qualification
via the Bluetooth Qualification Process (BQP). See <https://www.bluetooth.com/>.

---

## 11. Project License

The **SSC On Windows** project code (excluding the third-party components above,
and excluding the proprietary Samsung SSC blob) is licensed under the **MIT
License**. See [`LICENSE`](LICENSE) for the full text.

---

## License Compatibility Matrix

| Component | License | Compatible with MIT? | Notes |
|-----------|---------|----------------------|-------|
| SSC On Windows (fork) | MIT | — | Project license |
| A2DP Windows Bridge (upstream) | MIT | Yes | — |
| BTstack | BSD-3-Clause | Yes | Non-commercial use |
| fdk-aac | FDK AAC License | Yes | Permissive with conditions |
| wxWidgets | wxWindows Lib Licence | Yes | LGPL + exception (effectively permissive for binaries) |
| nlohmann/json | MIT | Yes | Same license |
| Samsung SSC blob | Proprietary | **Check** | Not MIT; redistribution may be restricted |
| Qiling | GPL-2.0 | Separate process | Not linked; do not bundle unless complying with GPL |
| Windows App SDK / Windows SDK | Proprietary | Yes (system library) | Platform dependency |
