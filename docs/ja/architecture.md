---
title: アーキテクチャ
layout: default
parent: 日本語
nav_order: 4
---

# アーキテクチャ
{: .no_toc }

## 目次
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## 概要

**SSC On Windows** は A2DP Windows Bridge のフォークです。Windows のシステム音声を **Samsung Scalable Codec (SSC)**、**AAC**、**SBC** で Bluetooth ヘッドホンへ、カーネルドライバーなしでストリーミングします。Windows は A2DP でネイティブには SBC と AAC のみ対応していますが、このプロジェクトは同じユーザーモードトランスポートの上に SSC を追加します。

**BTstack + WinUSB** を使用 -- 完全にユーザーモードで動作し、ドライバー署名は不要です。ストリーミングに Windows Bluetooth スタックは関与しません。

## トランスポート: BTstack + WinUSB

専用の USB Bluetooth アダプターを Microsoft Bluetooth ドライバー（`BTHUSB`）から汎用 **WinUSB** ドライバーに切り替えます（アプリ内トグルが最小限の WinUSB INF を生成・署名・インストールします。Zadig は不要）。BTstack は HCI、L2CAP、AVDTP、A2DP、AVRCP をユーザーモードで実装します。

**利点**:
- ドライバー署名コスト不要（アプリ内の自己署名 WinUSB INF + カタログを使用）
- テスト署名モード不要
- Secure Boot の無効化不要
- すべてのコードがユーザーモードで動作（デバッグが容易）

**トレードオフ**:
- 専用の USB Bluetooth アダプターが必要（Windows 内蔵 Bluetooth とは別）
- WinUSB に占有されている間、そのアダプターは Windows の通常の Bluetooth として使用不可
- デバイスアドレスは手動入力か、Windows のペアリング済みデバイス一覧から選択

**動作の流れ**:
1. ユーザーがアプリからドングルを WinUSB に切り替える（ドライバースイッチ）
2. BTstack が WinUSB API 経由で USB デバイスを開く
3. BTstack が HCI コマンドで Bluetooth コントローラーを初期化
4. (Realtek アダプター) 必要に応じてファームウェアをアップロード
5. BTstack が ACL 接続を確立し、L2CAP チャネル (PSM 0x0019) を開く
6. AVDTP シグナリングでリモート SEP を検出し、コーデックをネゴシエーション
7. エンコード済み音声が L2CAP 経由の AVDTP メディアパケットとして送信

## モジュール構成

```
SSCOnWindows.exe (WinUI 3)  /  SSCOnWindows-0.1.exe (CLI)
├── WinUI レイヤー (winui3/、C++/WinRT)
│   ├── App / MainWindow     スキャン、接続、コーデック/品質/レート、音量、統計、ログ
│   ├── Streaming Mode パネル  WinUSB / BTHUSB ドライバートグル
│   └── Setup & Help         ペアリングガイド + FAQ
│
├── レガシー GUI レイヤー (wxWidgets)
│   ├── wx_app / wx_main_frame / wx_profile_dialog
│   └── wx_about_dialog / ローカライゼーション (en、ja -- 埋め込み JSON)
│
├── コアレイヤー
│   ├── a2dp_service        A2DP 接続ライフサイクル & ステートマシン
│   ├── btstack_transport   BTstack 統合 (HCI, L2CAP, AVDTP, A2DP, AVRCP)
│   ├── wasapi_capture      WASAPI ループバックオーディオキャプチャ + 自動ミュート
│   ├── audio_encoder       エンコーダーインターフェース（抽象基底）
│   │   ├── ssc_encoder         SSC (Samsung バイナリを TCP デーモン経由)
│   │   ├── aac_encoder         AAC-LC (fdk-aac、LATM トランスポート)
│   │   └── a2dp_sbc_encoder    SBC (BTstack Bluedroid)
│   ├── resampler           SSC UHQ（96 kHz）用の 2x SRC
│   ├── driver_mode         ドングルのドライバーモード検出 (WinUSB vs BTHUSB)
│   ├── driver_switch       WinUSB INF 生成 + 署名 + インストール/削除
│   ├── bt_adapter_enum     USB Bluetooth アダプター列挙 (WinUSB)
│   ├── audio_device_enum   WASAPI オーディオデバイス列挙
│   └── profile_manager     接続プロファイル永続化 (JSON)
│
├── サポート
│   ├── app_settings        永続アプリケーション設定 (JSON)
│   ├── config_path         設定ファイルパス解決
│   └── debug_log           デバッグログマクロ
│
└── CLI モード
    └── main.cpp            CLI 引数パース、ヘッドレスストリーミング
```

## データフロー

```
システム音声出力
       │
       ▼
 WASAPI ループバックキャプチャ (float32、48 kHz)
       │
       ▼
 エンコーダー (SSC / AAC / SBC)
       │
       │  SSC: TCP :20248 → WSL2 (qemu-aarch64) または Qiling → libScalable_Encoder.so
       ▼
 A2DP Service → BtStackTransport::send_media()
       │
       ▼
 BTstack A2DP Source → AVDTP → L2CAP → HCI
       │
       ▼
 WinUSB → USB Bluetooth アダプター → Bluetooth 無線
       │
       ▼
 ヘッドホン / スピーカー
```

## 主要モジュール

### A2DP Service (`a2dp_service.cpp`)

接続ライフサイクルの中央管理:
- コーデックネゴシエーション（要求コーデック、フォールバック優先度 SSC > AAC > SBC）
- 全対応コーデックのストリームエンドポイント登録
- 接続ステートマシン（idle → connecting → streaming → disconnecting）
- 予期しない切断時の自動再接続ロジック
- コーデック固有のフレーミングによるメディアパケット送信
- AVRCP 絶対ボリューム（デバイスボリュームコールバック）

### BTstack Transport (`btstack_transport.cpp`)

アプリケーションの同期モデルと BTstack のイベント駆動 API のブリッジ。

**役割**:
- WinUSB HCI トランスポートで BTstack を初期化
- 専用スレッドで BTstack イベントループを実行
- コーデックストリームエンドポイント（SSC ベンダー固有、AAC、SBC）を登録
- 非同期→同期ラッパーで A2DP 接続ライフサイクルを管理
- SSP ペアリング（Just Works モード）を処理し、リンクキーを永続化
- エンコードスレッドからのスレッドセーフなメディア送信を提供
- AVRCP ボリュームと Realtek ファームウェアロード

**使用する主な BTstack API**:
- `a2dp_source_create_stream_endpoint()` -- コーデックエンドポイント登録
- `a2dp_source_establish_stream()` -- A2DP シンクに接続
- `a2dp_source_set_config_other()` -- SSC ベンダー固有コーデック設定
- `a2dp_source_stream_send_media_payload_rtp()` -- エンコード済み音声送信

### WASAPI Capture (`wasapi_capture.cpp`)

Windows Audio Session API を使用してシステム音声出力をリアルタイムでキャプチャ。
- `IAudioClient` を `AUDCLNT_STREAMFLAGS_LOOPBACK` モードで使用
- float32 → int32/int16 変換、チャネルダウンミックス
- `mute_output()` が既定のレンダーエンドポイントをミュート（ループバックはプレミックスなので、スピーカーのミュートはヘッドホンに影響しません）
- デバイス選択に対応

### オーディオエンコーダー

| エンコーダー | バックエンド | ビットレート | 備考 |
|:-------------|:-----------|:-------------|:-----|
| SSC | Samsung `libScalable_Encoder.so`（aarch64、デーモン経由） | 128/192/229 kbps (48k) / 250/442/584 kbps (96k UHQ) | 既定。モードで決まるビットレートセット |
| AAC | fdk-aac | 最大 256 kbps | AAC-LC、LATM トランスポート |
| SBC | BTstack Bluedroid | 最大約 345 kbps | A2DP 必須ベースライン |

すべてのエンコーダーは `AudioEncoder` インターフェースの `encode()` と `get_frame_size()` メソッドを実装しています。

## ベンダーコーデック情報要素

非標準コーデックは AVDTP で Vendor Specific として登録:

| コーデック | Vendor ID | Codec ID |
|:-----------|:----------|:---------|
| SSC | Samsung (0x00000075) | 0x0001 |

AAC と SBC は A2DP 仕様で定義された標準のコーデック ID を使用します。

## ビルドシステム

- **CMake** + MSVC（Visual Studio 2022 以降）でコア / CLI をビルド
- **MSBuild** で WinUI 3 プロジェクトをビルド（`winui3/A2DPWBWinUI.vcxproj`）
- サードパーティライブラリはサブモジュールとしてソースから静的ライブラリとしてビルド
- wxWidgets はビルド設定時に CMake FetchContent で取得 (v3.2.6)
- 言語ファイル (JSON) はビルド設定時に実行ファイルに埋め込み

## 重要な注意事項

- **アダプター互換性**: TP-Link UB500（Realtek RTL8761BU）でテスト済み。Realtek アダプターは起動時にファームウェアアップロードが必要
- **ペアリング**: SSP Just Works を使用。リンクキーはローカルに永続化
- **専用アダプター推奨**: Windows には内蔵 Bluetooth、ストリーミングには専用 USB アダプターを使用
- **SSC UHQ** はリモートのケーパビリティビットでゲートされる（Buds3 FE: `0x3C`、UHQ なし）
- コーデックフォールバック優先度: **SSC > AAC > SBC**