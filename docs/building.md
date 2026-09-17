---
title: Building
layout: default
nav_order: 4
---

# Building from Source
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Requirements

- **Visual Studio 2022 or later** with the "Desktop development with C++" workload
- **CMake** 3.16+ (with CMake 4.x, pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`)
- **Git** (for submodules and the wxWidgets FetchContent)
- **Windows App SDK 1.8.x** (restored by NuGet when building the WinUI project)

## Quick Build (core + CLI)

```powershell
git clone --recursive <this-repo-url> SSCOnWindows
cmake -S SSCOnWindows -B SSCOnWindows\build_msvc -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build SSCOnWindows\build_msvc --config Release --target A2DPWB -j 8
```

The CLI executable is output to `build_msvc\app\Release\SSCOnWindows-0.1.exe`.

{: .note }
The first build takes several minutes because CMake FetchContent downloads and compiles wxWidgets (v3.2.6).

## WinUI 3 GUI

Build the core library first, then the MSBuild project:

```powershell
cmake --build build_msvc --config Release --target a2dpwb_core -j 8
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find "MSBuild\**\Bin\MSBuild.exe"
& $msb winui3\A2DPWBWinUI.vcxproj -p:Configuration=Release -p:Platform=x64 -m:8 -t:Build
```

Output: `winui3\bin\x64\Release\SSCOnWindows.exe`. The app is **unpackaged**, so the
whole output folder (bootstrap DLL, WebView2 DLL, `.xbf`/`.pri`/`.winmd`) must be
copied next to the executable or it will exit silently.

## Submodule Patches

The BTstack submodule carries a small local patch (error logging in the Windows
WinUSB transport). Apply it once after cloning:

```powershell
git -C extern/btstack apply ..\..\patches\btstack-win-usb-logs.patch
```

## If You Already Cloned Without `--recursive`

```bash
git submodule update --init --recursive
```

## Project Structure

```
SSCOnWindows/
├── app/                    Application source
│   ├── src/                C++ source files
│   ├── lang/               Localization (en.json, ja.json)
│   └── resources/          Icon, manifest, resource script
├── winui3/                 WinUI 3 GUI (MSBuild project)
├── tools/                  SSC daemon + payload + golden tests
│   ├── ssc_daemon/         Native Qiling daemon + minimal aarch64 rootfs
│   ├── ssc_payload/        WSL2 blob payload (sscblobd, helper)
│   └── golden/             Byte-exact SSC regression goldens
├── patches/                Local patches for third-party submodules
├── docs/                   Documentation (this site)
├── extern/                 Third-party libraries (git submodules)
│   ├── btstack/            BTstack Bluetooth stack
│   ├── fdk-aac/            Fraunhofer AAC encoder
│   └── json/               nlohmann/json (header-only)
├── CMakeLists.txt          Root build config
└── build.bat.example       Build helper script
```

## Dependencies

All dependencies are git submodules or fetched at build time. No manual
installation required.

| Library | Method | License |
|:--------|:-------|:--------|
| BTstack | git submodule | BSD-3-Clause (non-commercial) |
| fdk-aac | git submodule | FDK AAC License |
| nlohmann/json | git submodule (header-only) | MIT |
| wxWidgets v3.2.6 | CMake FetchContent | wxWindows Library Licence |
| Samsung SSC blob | included in `tools/` | Proprietary (Samsung) |

See [THIRD_PARTY_LICENSES.md](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/THIRD_PARTY_LICENSES.md)
for full license details.
