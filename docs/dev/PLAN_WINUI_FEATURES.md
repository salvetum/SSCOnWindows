# WinUI 3 — İleri Dönük Özellik Planı

> Durum: **TAMAMLANDI** — 6 özellikten 6'sı kodlandı ve GUI'de mevcut;
> cihazda canlı test bekleniyor (buds `78:C1:1D:A7:BC:EE`). Tarih: 2026-09-15.

## Amaç

WinUI 3 GUI'ye aşağıdaki özelliklerin eklenmesi. Ana çekirdek mantığı
(`a2dpwb_core.lib`) korunur; sadece görsel katman (`winui3\*.xaml`) ve gerekli
core istatistik altyapısı değişir.

## 1. Bitrate Changer ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟢 Kolay
- **Mevcut durum:** GUI'de `BitrateSlider` (0–990 kbps, `0 = auto`) +
  `OnBitrateChanged` eklendi. Preset'ler (`QualityCombo`) ve `RateCombo`
  (48k/96k = UHQ) mevcut.
- **Yapılacak:** Slider seçilirse `EncoderQuality` → bitrate map'i genişletilir.
  - ✅ **KARAR: Slider** — `BitrateSlider` 0–990, `ValueChanged` ile
    `settings_.bt_bitrate` → stream öncesi uygulanıyor.
- **Karar notu:** Slider seçildi (preset'ler zaten ayrıca duruyor).

## 2. Ortalama Latency ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟢 Kolay
- **Veri zaten mevcut:** `SSC: enc... total=` log satırı her frame'de encode
  round-trip'ini (0.7–1.9 ms) ölçüyor.
- **Yapılacak:**
  - ✅ Core'da stats aggregator: `stats.latency_ms` (EWMA).
  - ✅ `set_stats_callback()` ile ~2 s'de UI'ya `latency_ms` aktarılıyor.
  - ✅ UI'da `LatencyText` ("Latency: 1.2 ms") + sparkline (0–25 ms ekseni).

## 3. Ortalama Error Oranı ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟡 Orta
- **Mevcut durum:** Kalıcı sayaç eklendi:
  toplam send / toplam fail → `stats.error_rate`.
- **Yapılacak:**
  - ✅ Transport kalıcı sayaç + EWMA yumuşatma.
  - ✅ UI'da `ErrorRateText` ("Error Rate: 0.4%") + kırmızı sparkline (0–5 %).

## 4. Ortalama Loss Oranı ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟡 Orta
- **Mevcut durum:** `ring_overflow_dropped / captured_frames` → `stats.loss_rate`.
- **Yapılacak:**
  - ✅ Core'da kalıcı toplayıcı.
  - ✅ UI'da `LossRateText` ("Loss Rate: 0.02%").

## 5. Bit Changer (Bit Depth) ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟠 Riskli
- **Mevcut durum:** SSC hattı **zorunlu int32** — `bytes_per_sample = 4`
  (`(use_24bit || codec == SSC) ? 4 : 2`). Encoder 864×2×4 = 6912 byte ister;
  düşürülürse `out_size = 0` → **sessizlik** (bu geçmişte fix'lendi, AGENTS.md
  landmine #3).
- **Yapılacak:** GUI'de bit depth seçeneği (16/24/32) ancak **codec bazlı**
  sınırlamayla:
  - SSC için yalnızca int32 desteklenir (down-convert SSC'de yapılamaz veya
    daemon tarafında dönüşüm gerekir).
  - SBC/AAC/LDAC için int16 mevcut; dönüşüm core'da yapılır.
- **⚠️ UYARI NOTU:**
  - Bit changer eklenirse **bug riski yüksek** kabul edilir.
  - Yanlış bit depth eşleşmesi: `out_size = 0`, `encode_accum = 0`, bağlantı
    açık görünürken **sessizlik** üretir (test edilmiş regresyon).
  - Down-convert (int32 → int16) kuantizasyon gürültüsü yaratır (düşük volümde
    beyaz gürültü — daha önce fix'lendi, AGENTS.md landmine #2).
  - Her bit depth değişiminde: `g_ctx.bytes_per_sample`,
    `g_pcm_int32_scale`, `g_encoder_sample_bytes` ve encode buffer boyutu
    **birlikte** güncellenmeli; biri atlanırsa sessizlik/gürültü beklenir.
  - SSC + daemon hattı değiştirilirse `sscblobd` (WSL2) da dönüşümü
    desteklemeli; Windows tarafı tek başına çözemez.

- **UYGULAMA (2026-09-15):**
  - 🟢 **Güvenli kapsam** (core zaten koruyor): SSC her zaman int32 (2^29),
    SBC/AAC/aptX her zaman int16; çekirdek eşleme `a2dp_service.cpp`
    `streaming_thread_func_inner` içinde GUI'deki `profile.bit_depth`'i
    codec bazlı `sample_bytes`'a çevirir. Non-LDAC codec'ler kullanıcı
    seçimini yok sayar (sessizlik/landmine riski yok).
  - GUI: `BitDepthCombo` (Auto/16/24/32) + `OnBitDepthChanged` +
    `UpdateBitDepthForCodec()` codec bazlı kısıtlama:
    - **LDAC:** Auto/16/24 seçilebilir (24 ve 32 aynı S32 container'a eşlenir).
    - **SSC:** kilitli → zorunlu 32-bit (int32, ipucu gösterir).
    - **aptX HD/LL, SBC, AAC:** kilitli → zorunlu 16-bit (int16, ipucu).
    - **Auto:** kullanıcı seçimi korunur; core çözülen codec'e göre eşler.
  - 16-bit WASAPI yakalama → LDAC 24: encode thread'de int16→int32 upscale
    yolu eklendi (`<< 16`), CLI ile aynı.
  - CLI: `--bit-depth 16|24|32` (yalnız LDAC; default 32).
  - `ProfileManager::bit_depth_to_index` / `index_to_bit_depth` 32-bit
    desteği (index 3).

## 6. Volume Changer ✅ (kodu tamam, canlı test bekliyor)

- **Zorluk:** 🟡 Orta
- **Mevcut durum:** GUI'de `VolumeSlider` (0–200%, default 100) +
  `OnVolumeChanged` → core gain scalar (f32 → i32 öncesi `f32 * gain * scale`).
  Ayrıca `AutoMuteCheck` ("Mute speakers while streaming", default ON) →
  `mute_output()` WASAPI endpoint mute'u.
- **Yapılacak (eklendi):**
  - ✅ Core'da gain scalar.
  - ✅ GUI'de slider + `g_pcm_int32_scale` kombinasyonu.

## Ortak Mimari (tüm özellikleri destekler) ✅ uygulandı

```
Core (a2dpwb_core)
  ├─ stats aggregator struct     { latency_ms, err_rate, loss_rate, bitrate_kbps }  ✅
  ├─ EWMA istatistik toplayıcı    (encode loop + transport içinde)                  ✅
  ├─ set_stats_callback()         (~2 s → UI thread'e dispatcher enqueue)           ✅
  └─ gain scalar (volume)         (f32 → i32 öncesi)                                ✅

UI (WinUI 3 — XAML only)
  ├─ Bitrate slider / combobox     ✅
  ├─ Volume slider                 ✅
  ├─ 4 istatistik TextBlock + sparkline (latency/error) ✅
  └─ MainWindow.xaml.cpp'de callback wiring ✅
```

Not: mevcut `set_state_callback` / `set_stream_info_callback` deseni
(`MainWindow.xaml.cpp:49,57`) aynen bu yeni callback için yeniden kullanıldı —
yeni bir desen gerekmedi.

## Öncelik Sırası (durum)

1. ✅ **Bitrate changer** — tamamlandı (slider + preset)
2. ✅ **Ortalama latency** — tamamlandı (EWMA + UI + sparkline)
3. ✅ **Volume changer** — tamamlandı (gain scalar + auto-mute)
4. ✅ **Error / Loss oranı** — tamamlandı (stats struct + sparkline)
5. ✅ **Bit changer** — tamamlandı (codec bazlı kısıtlama + GUI + CLI)
6. ✅ **Dongle driver-mode paneli** (WinUSB vs BTHUSB, `driver_mode.cpp`)

## Log Doğrulama Referansları (regresyon kontrolü)

| Log | Sağlıklı değer |
|-----|----------------|
| `SSC: enc... total=` | ≈ 1.2 ms, `ret=576` (High) |
| `DIAG: encode_accum=` | > 0 ve dalgalanıyor (0/576) |
| `DIAG: send_fails=` | 0 |
| `CAP: ... rate=48000fs` | tam capture |