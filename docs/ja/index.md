---
title: 日本語
layout: default
nav_order: 7
has_children: true
---

# SSC On Windows

Samsung Galaxy Buds へ **Samsung Scalable Codec (SSC)** で Windows のシステム音声をストリーミングします。
{: .fs-6 .fw-300 }

[A2DP Windows Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge)（Seiya Funaoka 作）のフォークで、**AAC** と **SBC** に加えてフルの SSC エンコーダーパイプラインを追加しています。バージョン 0.1、作者 **Salvetum**。

[セットアップ](setup){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[GitHub](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge){: .btn .fs-5 .mb-4 .mb-md-0 }

---

## 対応コーデック

| コーデック | ビットレート | サンプルレート | ステータス |
|:------|:--------|:------------|:-------|
| **SSC**（既定） | High 229 / Standard 192 / Mobile 128 kbps / UHQ 584・442・250 kbps | 48 kHz（UHQ: 96 kHz） | プライマリターゲット |
| AAC | 128/192/256 kbps | 48 kHz | FDK-AAC 経由 |
| SBC | 最大約 345 kbps | 48 kHz | ベースラインのフォールバック |

フォールバック優先度は **SSC > AAC > SBC** です。SSC UHQ（96 kHz）は UHQ 対応のシンクでのみ使用できます（Galaxy Buds3 FE は UHQ 非対応のため 48 kHz にフォールバックします）。

## オーディオキャプチャモード

音声は WASAPI を使用して次の 2 つのモードのいずれかでキャプチャします。

| モード | 説明 | 用途 |
|:-----|:------------|:---------|
| **システムループバック** | 既定の再生デバイスから全システム音声出力をキャプチャ | シンプルな構成 -- すべての音声がストリーミングされる |
| **仮想デバイス** | 選択した仮想オーディオデバイス（VB-CABLE、VoiceMeeter 等）からキャプチャ | 特定のアプリだけ Bluetooth に送り、他の音声はスピーカーに出力 |

**仮想デバイス**モードでは、Windows の既定の再生デバイスを選択した仮想デバイスに切り替え、そのループバック出力をキャプチャします。

## 動作の仕組み

```
オーディオソース（既定の出力または仮想デバイス）
  |
  v
WASAPI ループバックキャプチャ (48 kHz、float32)
  |
  v
エンコーダー (SSC  |  AAC  |  SBC)
  |
  |  SSC のみ: TCP :20248 -> WSL2/Qiling デーモン -> Samsung libScalable_Encoder.so (aarch64)
  |
  v
BTstack (A2DP Source -> AVDTP -> L2CAP -> HCI)
  |
  v
WinUSB -> USB Bluetooth アダプター -> ヘッドホン
```

Windows の Bluetooth スタックは完全にバイパスされます。専用の USB Bluetooth アダプターを汎用 **WinUSB** ドライバーに切り替え、[BTstack](https://github.com/bluekitchen/btstack) がユーザーモードで直接ドライブします。SSC エンコーダーは実際の Samsung aarch64 バイナリで、ローカル TCP デーモンの背後でエミュレーター内で実行されます。

## 主な機能

- **SSC (Samsung Scalable Codec)**: High / Standard / Mobile、および対応シンクでは 96 kHz UHQ
- **AAC / SBC** フォールバックコーデック
- **アプリ内ドライバートグル**: ドングルを WinUSB と Windows Bluetooth の間で切替（Zadig 不要）
- **2 つのキャプチャモード**: システムループバックまたは仮想オーディオデバイスルーティング
- **自動再接続**: 切断時に自動再接続（最大 10 回）
- **AVRCP 絶対ボリューム**: アプリからヘッドホンの音量を制御
- **WinUI 3 GUI + CLI**
- **ライブ統計**: レイテンシー、エラー率、ロス、キューの深さ、スパークライン
- **プロファイル管理**、**Realtek ファームウェア**ヘルパー、**英語 / 日本語 UI**