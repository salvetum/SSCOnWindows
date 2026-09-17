---
title: 使い方
layout: default
parent: 日本語
nav_order: 2
---

# 使い方
{: .no_toc }

## 目次
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## GUI モード（既定）

```
SSCOnWindows.exe
```

WinUI 3 インターフェースでは以下の操作が可能です:

- **スキャン / 接続 / ダイレクト接続** -- ヘッドホンの検出と接続
- **コーデック選択** -- SSC（既定）/ AAC / SBC
- **品質** -- High / Standard / Mobile
- **サンプルレート** -- 48 kHz、または 96 kHz（SSC UHQ、UHQ 対応シンクのみ）
- **ビットレート** -- 自動または明示指定（アクティブモードの有効なセットにスナップ）
- **デバイス音量** -- AVRCP 絶対ボリューム (0-100%)
- **Streaming Mode (Dongle)** -- ドングルを WinUSB と Windows Bluetooth の間で切替
- **ライブ統計** -- レイテンシー、エラー率、ロス、キューの深さ + スパークライン
- **Setup & Help** -- 初回ペアリングガイドと FAQ

## CLI モード

`--cli` オプションで GUI なしで実行します。

### 基本

```bash
# SSC（既定）
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF
```

### コーデックと品質

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc       # SSC
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c aac       # AAC
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c sbc       # SBC

SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q hq   # 229 kbps（48k、既定）
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q std  # 192 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc -q mq   # 128 kbps
```

要求したコーデックが利用できない場合のフォールバック優先度: **SSC > AAC > SBC**。

### SSC UHQ（96 kHz）

UHQ は 48 kHz キャプチャに 2x SRC を適用し、96 kHz のビットレートセットを使用します。UHQ ケーパビリティビットをアドバタイズするシンクでのみ使用可能で、それ以外の場合はアプリが 48 kHz にフォールバックします。

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq        # 584 kbps（既定）
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq -q std # 442 kbps
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --uhq -q mq  # 250 kbps
```

### ビットレートを明示指定

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --bitrate 192
```

アクティブモードの有効セット外の値は自動的にスナップされます（サポート外のビットレートをバイナリに渡すと音声が乱れます）。

### ネイティブ SSC デーモン（試験的）

```bash
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF -c ssc --ssc-native
```

WSL2 の代わりに、aarch64 SSC バイナリを Windows 上の Qiling（`py -3.14`）で実行します。WSL2 は不要ですが遅く、UHQ では WSL2 を推奨します。

### デバイス検出

```bash
# ペアリング済みの Bluetooth オーディオデバイスを一覧表示
SSCOnWindows-0.1.exe --cli -l
```

{: .note }
デバイス一覧の取得には WinUSB アダプターではなく、**内蔵** Bluetooth アダプター経由の Windows Bluetooth API を使用します。

## キャプチャモード

次の 2 つのオーディオキャプチャモードに対応しています:

| モード | 説明 |
|:-------|:-----|
| システムループバック | 既定の出力デバイスからシステム音声をすべてキャプチャ |
| 仮想デバイス | 特定の仮想オーディオデバイス（VB-CABLE 等）からキャプチャ（アプリ単位のルーティングに使用） |

{: .warning }
どちらのモードも WASAPI 共有モードを使用するため、キャプチャのサンプルレートは Windows サウンド設定のデバイスの**「既定の形式」**に依存します（通常 48 kHz）。SSC UHQ は Windows のキャプチャレートを変更せず、48 kHz キャプチャに 2x SRC を適用します。

## 音量

音量スライダーは AVRCP 絶対ボリュームで**ヘッドホンの**音量を制御します（0-100% を 0-127 にマッピング）。WASAPI ループバックはボリュームミックス前の音声をキャプチャするため、Windows の出力音量はキャプチャレベルに影響しません。自動ミュートを有効にすると、ストリーミング中は既定のスピーカーがミュートされます。

## コーデック比較

| コーデック | 最適な用途 | トレードオフ |
|:-----------|:-----------|:-------------|
| SSC (High) | Samsung デバイスで最高の音質と堅牢性 | SSC バイナリデーモンが必要 |
| SSC (Mobile) | RF が混雑した環境 | 低ビットレート |
| SSC UHQ | UHQ 対応シンクでの最高音質 | WSL2 が必要。UHQ シンクが必要 |
| AAC | 汎用フォールバック | 中程度の音質 |
| SBC | 最大の互換性 | 最低の音質 |