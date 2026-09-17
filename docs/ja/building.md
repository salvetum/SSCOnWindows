---
title: ビルド
layout: default
parent: 日本語
nav_order: 3
---

# ソースからビルド
{: .no_toc }

## 目次
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## 必要なもの

- **Visual Studio 2022 以降**（「C++ によるデスクトップ開発」ワークロード）
- **CMake** 3.16 以上（CMake 4.x の場合は `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` を指定）
- **Git**（サブモジュールおよび wxWidgets FetchContent 用）
- **Windows App SDK 1.8.x**（WinUI プロジェクトのビルド時に NuGet が復元）

## クイックビルド（コア + CLI）

```powershell
git clone --recursive <this-repo-url> SSCOnWindows
cmake -S SSCOnWindows -B SSCOnWindows\build_msvc -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build SSCOnWindows\build_msvc --config Release --target A2DPWB -j 8
```

CLI 実行ファイルは `build_msvc\app\Release\SSCOnWindows-0.1.exe` に出力されます。

{: .note }
初回ビルドは CMake FetchContent が wxWidgets (v3.2.6) をダウンロード・コンパイルするため、数分かかります。

## WinUI 3 GUI

先にコアライブラリをビルドし、その後に MSBuild プロジェクトをビルドします:

```powershell
cmake --build build_msvc --config Release --target a2dpwb_core -j 8
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find "MSBuild\**\Bin\MSBuild.exe"
& $msb winui3\A2DPWBWinUI.vcxproj -p:Configuration=Release -p:Platform=x64 -m:8 -t:Build
```

出力: `winui3\bin\x64\Release\SSCOnWindows.exe`。このアプリは**アンパッケージ**のため、出力フォルダー全体（ブートストラップ DLL、WebView2 DLL、`.xbf`/`.pri`/`.winmd`）を実行ファイルの隣にコピーする必要があります。これを省くと、ウィンドウなしでサイレントに終了します。

## サブモジュールパッチ

BTstack サブモジュールには小さなローカルパッチ（Windows WinUSB トランスポートのエラーロギング）があります。クローン後に 1 回適用します:

```powershell
git -C extern/btstack apply ..\..\patches\btstack-win-usb-logs.patch
```

## `--recursive` なしでクローン済みの場合

```bash
git submodule update --init --recursive
```

## プロジェクト構成

```
SSCOnWindows/
├── app/                    アプリケーションソース
│   ├── src/                C++ ソースファイル
│   ├── lang/               ローカライゼーション (en.json、ja.json)
│   └── resources/          アイコン、マニフェスト、リソーススクリプト
├── winui3/                 WinUI 3 GUI（MSBuild プロジェクト）
├── tools/                  SSC デーモン + ペイロード + ゴールデンテスト
│   ├── ssc_daemon/         ネイティブ Qiling デーモン + 最小 aarch64 rootfs
│   ├── ssc_payload/        WSL2 バイナリペイロード (sscblobd、ヘルパー)
│   └── golden/             バイト完全一致 SSC 回帰ゴールデン
├── patches/                サードパーティサブモジュール向けローカルパッチ
├── docs/                   ドキュメント（このサイト）
├── extern/                 サードパーティライブラリ（git サブモジュール）
│   ├── btstack/            BTstack Bluetooth スタック
│   ├── fdk-aac/            Fraunhofer AAC エンコーダー
│   └── json/               nlohmann/json（ヘッダーオンリー）
├── CMakeLists.txt          ルートビルド設定
└── build.bat.example       ビルドヘルパースクリプト
```

## 依存関係

すべての依存関係は git サブモジュールか、ビルド時に取得されます。手動インストールは不要です。

| ライブラリ | 取得方法 | ライセンス |
|:--------|:-------|:--------|
| BTstack | git サブモジュール | BSD-3-Clause（非商用） |
| fdk-aac | git サブモジュール | FDK AAC License |
| nlohmann/json | git サブモジュール（ヘッダーオンリー） | MIT |
| wxWidgets v3.2.6 | CMake FetchContent | wxWindows Library Licence |
| Samsung SSC バイナリ | `tools/` に同梱 | プロプライエタリ（Samsung） |

ライセンスの全文は [THIRD_PARTY_LICENSES.md](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/THIRD_PARTY_LICENSES.md) を参照してください。