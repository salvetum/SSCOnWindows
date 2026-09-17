---
title: セットアップ
layout: default
parent: 日本語
nav_order: 1
---

# セットアップ
{: .no_toc }

## 目次
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## 前提条件

- **Windows 10/11** (x64)
- **専用 USB Bluetooth アダプター**（内蔵 Bluetooth とは別）
- SSC ストリーミング用: **WSL2** に Linux ディストロ（既定のパス）、または **Python 3.14 + Qiling**（ネイティブパス）

## 手順 1: ドングルを WinUSB に切り替える

SSC On Windows は WinUSB 経由で USB Bluetooth アダプターと直接通信します。**専用のアダプター**が必要です。内蔵 Bluetooth は通常の Windows Bluetooth として引き続き使用できます。Zadig は不要です -- アプリがドライバーのインストールと削除を行います。

1. USB Bluetooth アダプターを接続
2. `SSCOnWindows.exe` を起動
3. **Streaming Mode (Dongle)** パネルで **Enable Streaming (WinUSB)** をクリック
4. UAC プロンプトを受け入れる（自己署名 WinUSB INF + カタログが生成・署名・インストールされます）

戻すには **Restore Windows BT (BTHUSB)** をクリックします。

{: .warning }
PC の**内蔵** Bluetooth アダプターでは絶対にドライバーを切り替えないでください。すべての通常 Bluetooth（キーボード、マウス、オーディオ）が使えなくなり、デバイスマネージャーやシステムの復元が必要になる場合があります。必ず別の専用ドングルを使用してください。

## 手順 2: ヘッドホンの Bluetooth アドレスを確認

ヘッドホン/スピーカーの Bluetooth MAC アドレスが必要です。

**Windows 設定から確認:**
1. **設定 > Bluetooth とデバイス** を開く
2. オーディオデバイスをクリック
3. **プロパティ** をクリック -- アドレスが `AA:BB:CC:DD:EE:FF` の形式で表示されます

**GUI から確認:** スキャン後、ペアリング済みの Bluetooth オーディオデバイスがデバイス一覧に表示されます。

**CLI から確認:**
```
SSCOnWindows-0.1.exe --cli -l
```

## 手順 3: 起動

**GUI:**
```
SSCOnWindows.exe
```

**CLI:**
```
SSCOnWindows-0.1.exe --cli -d AA:BB:CC:DD:EE:FF
```

詳細は[使い方](usage)をご覧ください。

---

## ファームウェア（Realtek アダプター）

Realtek ベースのアダプター（TP-Link UB500、RTL8761BU ドングル等）は専用ファームウェアが必要です。**Intel および CSR アダプターにはこの手順は不要です。**

### 手動ダウンロード

1. [linux-firmware/rtl_bt](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt) からチップセット用のファームウェアと設定の `.bin` ファイルをダウンロード（例: `rtl8761bu_fw.bin`、`rtl8761bu_config.bin`）
2. 両ファイルを設定フォルダー `%APPDATA%\A2DPWB` に配置

{: .note }
これらのファームウェアファイルは linux-firmware プロジェクト経由で配布される Realtek 独自のバイナリです。本リポジトリには含まれていません。再配布条件については [WHENCE](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/WHENCE) を参照してください。

---

## SSC エンコーダーデーモン

SSC エンコーダーは Samsung の aarch64 バイナリのため、ローカル TCP デーモン（`:20248`）の背後で実行されます。バックエンドは 2 つです:

| バックエンド | 方式 | 備考 |
|:--------|:----|:------|
| WSL2（既定） | WSL2 内部の `qemu-aarch64` でバイナリ + ヘルパーを実行 | 最速（約 1.2 ms RTT）。UHQ に必要 |
| Qiling ネイティブ（`--ssc-native`） | `py -3.14` で `tools/ssc_daemon/sscblobd.py` を実行 | WSL2 不要。約 5 倍遅く、96 kHz は限界 |

Windows 側がデーモンを自動的に起動します。WSL2 パスの場合は、インストール済みで実行中のディストロが必要です。

---

## 推奨アダプター

| チップセット | 製品例 | 備考 |
|:-------------|:-------|:-----|
| Realtek | TP-Link UB500、RTL8761BU | テスト済み。ファームウェアのダウンロードが必要 |
| Intel | Intel AX200/AX210 | ファームウェア不要 |
| CSR | CSR8510 汎用ドングル | ファームウェア不要 |

USB Bluetooth 5.0 以上のアダプターを推奨します。