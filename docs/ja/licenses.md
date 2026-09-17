---
title: ライセンス
layout: default
parent: 日本語
nav_order: 5
---

# ライセンス
{: .no_toc }

## 目次
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## プロジェクトライセンス

**SSC On Windows**（A2DP Windows Bridge のフォーク）は **MIT License** の下で
ライセンスされています。[LICENSE](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/LICENSE)
を参照してください。ベースプロジェクトの著作権は Copyright (c) 2026 Seiya Funaoka に帰属します。

## サードパーティコンポーネント

| コンポーネント | ライセンス | 用途 |
|:---------------|:-----------|:-----|
| [BTstack](https://github.com/bluekitchen/btstack) | BSD-3-Clause（非商用）/ 商用 | ユーザーモード Bluetooth スタック |
| [fdk-aac](https://github.com/mstorsjo/fdk-aac) | FDK AAC License | AAC-LC エンコーダー |
| [wxWidgets](https://www.wxwidgets.org/) | wxWindows Library Licence | レガシー GUI フレームワーク |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | JSON パーサー |
| [Qiling](https://github.com/qilingframework/qiling) | GPL-2.0 | SSC デーモン用のネイティブ aarch64 エミュレーション |
| Samsung `libScalable_Encoder.so` | プロプライエタリ（Samsung） | SSC エンコーダーブロブ（MIT 非対象） |
| [openssc](https://github.com/sachk/openssc) | 上流を参照 | SSC コーデック統合のリファレンス |
| Windows App SDK / Windows SDK | Microsoft EULA | WinUI 3 ランタイム + システム API |

ライセンス全文とコンプライアンスの詳細は
[THIRD_PARTY_LICENSES.md](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/blob/main/THIRD_PARTY_LICENSES.md)
を参照してください。

## 重要な注意事項

### BTstack デュアルライセンス

BTstack はデュアルライセンスです。BSD-3-Clause ライセンスは**非商用利用**のみ許可されています。商用利用には [BlueKitchen GmbH](https://bluekitchen-gmbh.com/) からの別途ライセンスが必要です。

### Samsung SSC エンコーダー

SSC エンコーダー（`libScalable_Encoder.so`）は Samsung のプロプライエタリソフトウェアであり、MIT ライセンスの対象外です。再配布が制限される場合があるため、同梱したバイナリやソースを公開する前に権利を確認してください。

### Qiling (GPL-2.0)

Qiling は実験的なネイティブ SSC デーモンでのみ使用され、独立した `py -3.14` プロセスとして動作します。アプリケーションにリンクされず、同梱もされません。

### AAC 特許に関する注意

AAC は Via Licensing が管理する特許ライセンスの対象です。商用製品は適切なライセンスを取得する必要があります。

### Bluetooth 認証

商用 Bluetooth 製品は Bluetooth SIG が管理する [Bluetooth 認証プロセス](https://www.bluetooth.com/)を受ける必要があります。
