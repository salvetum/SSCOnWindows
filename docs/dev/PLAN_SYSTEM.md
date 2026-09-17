# Sistem Mimarisi Planı — RAM Ayak İzi + Çift Ses Sorunu

> Durum: **AKTİF** — 5.4 (tek-çalıştır kurulum) ve 5.6 (link-key kalıcılığı)
> **tamamlandı**; RAM ayak izi (Bölüm 6) kullanıcı kararıyla **en sona ertelendi**.
> Tarih: 2026-09-15.

## Amaç

İki bağımsız iyileştirme:

1. **Çift ses sorununu çöz** — kulaklık Windows'a ayrı bir çıktı olamıyor
   (dongle WinUSB modunda, BT yığını devre dışı) → müzik varsayılan hoparlörde
   çalarken loopback aynı sesi kulaklığa da gönderiyor.
2. **SSC encoder zincirinin RAM ayak izini küçült** — hedef 100 MB bandı;
   araştırmaya göre WSL2 ile bu imkansız (~700 MB taban), Windows-native Qiling
   yolu ile ulaşılabilir.
   - **⚠️ ERTELENDİ (kullanıcı kararı, 2026-09-15):** RAM işi en sona saklandı.
     Aktif hedef önce 5.4 + 5.6; RAM ayrıntısı Bölüm 6'da.

---

## 1. Çift Ses Sorunu

### Kök neden

- UB500, WinUSB modunda olduğundan **Windows BT yığını devre dışı**.
- Kulaklık hiçbir Windows ses cihazına çıkmıyor; A2DPWB sesi **loopback ile
  yakalayıp** kulaklığa köprü kuruyor.
- Müzik uygulaması varsayılan cihazda (hoparlör) açılıyor → hem hoparlörden
  hem de yakalanıp kulaklığa iletilen kopyadan geliyor.

### Çözüm (mute toggle)

- Streaming başlarken varsayılan oynatma cihazını **otomatik sustur**
  (`IAudioEndpointVolume::SetMute` — `wasapi_capture.cpp`).
- **Kritik nokta:** loopback **pre-mix** yakalar (AGENTS.md'de kayıtlı) —
  mute'dan etkilenmez. Hoparlör susar, kulaklığa gönderilen sinyal aynen devam
  eder.
- Stream kapanınca mute geri açılır.
- UI: "Mute speakers while streaming" checkbox (WinUI).

### İş kalemi

- `wasapi_capture.cpp/.h`: mute uygula/geri al (session-endpoint volume).
- Toggle → `A2dpService` → capture. Efor: ~0.5 gün, düşük risk.

### ✅ YAPILDI (2026-09-15)

- `WasapiCapture::mute_output(bool)` — `IAudioEndpointVolume::SetMute` default
  render endpoint'te; `endpointvolume.h` + `IID_IAudioEndpointVolume_` eklendi.
  Ayrı duran bir harness ile canlı doğrulandı: default cihaz (FxSound Speakers)
  üzerinde MUTED→UNMUTED logları, `mute_test.exe` PASS.
- `A2dpService::set_auto_mute_output(bool)` + `get_auto_mute_output()` —
  `auto_mute_output_` (varsayılan **true**). Stream başlangıcında
  `SystemLoopback` modunda `mute_output(true)`, clear-path'ta (transport
  ayrılırken) `mute_output(false)` — `output_muted` bayrağı ile. VirtualDevice
  modunda mute YOK (default endpoint zaten yakalama kaynağı — susturmak bozar).
- CLI: `run_streaming(..., mute_output)` parametresi + `--no-mute-output` bayrağı
  (varsayılan mute açık). CLI capture başlarken mute, cleanup'ta unmute.
- WinUI: `MainWindow.xaml`'de "Mute speakers while streaming" `CheckBox`
  (`AutoMuteCheck`, `Checked/Unchecked=OnAutoMuteChanged`). **WMC0055 tuzağı:**
  `IsChecked="True"` XAML'de yazılamaz — constructor'da `AutoMuteCheck().IsChecked(true)`
  ile C++'tan set edilir (MicaBackdrop ile aynı pattern).
- Derleme: core 0 hata; WinUI 0 hata; dist'e deploy; GUI dağıtımdan açılıyor.
- Golden: 3/3 PASS (128k/192k/229k).

---

## 2. Sürücü Otomasyonu (Zadig / WinUSB → Uygulama İçi Toggle)

### Mevcut durum

- Dongle (UB500, VID:PID **2357:0604**, RTL8761B) WinUSB moduna **elle** Zadig
  ile geçiriliyor. Tek seferlik ama yeni kurulum yapan herkes için manuel adım.
- Diğer yandan streaming zinciri **kendi kendini başlatıyor**: `ssc_daemon`
  exe tarafından çağrılıyor → WSL `start_sscblobd` → TCP bağlan (ssc_encoder.cpp).
  Bu kısım zaten otomatik.

### Keşif: Zadig = libwdi'nin "örnek uygulaması"

- Gerçek kütüphane **libwdi** (LGPL v3). CLI kardeşi **`wdi-simple.exe`**
  tek komutla WinUSB kurar:
  ```
  wdi-simple.exe --vid 0x2357 --pid 0x0604 --type 0 --progressbar=<hwnd>
  ```
- **Geri dönüş (restore) belgeli:** WinUSB'yi cihazdan uninstall edince
  Windows BT adaptörü orijinal **BTHUSB** sürücüsünü otomatik geri yükler.
  Ayrı bir manuel restore adımı gerekmiyor.

### Yapılacak — WinUI3 içinde "Streaming Modu" toggle'ı

> **YAPILDI (2026-09-16).** Karar: `wdi-simple.exe`/libwdi yerine **inbox-only**
> yaklaşım — WinUSB INF'i uygulama üretiyor (Microsoft'un `Include/Needs
> winusb.inf` kalıbı, Zadig'in ispatlanmış `DeviceInterfaceGUIDs`
> `{3FE6ABE9-5D35-4CC6-9B10-78E6F199C952}`'si ile) ve elevated
> `pnputil /add-driver ... /install /force` ile kuruluyor. Geri dönüş
> `pnputil /delete-driver <oem#.inf> /uninstall`; devnode `pnputil /remove-device`
> + `/scan-devices` ile re-evaluate ediliyor. Sadece **winusb.inf referanslı**
> paketler siliniyor — Realtek'in BTHUSB filter INF'ine (oem28.inf) asla
> dokunulmuyor.

- ~~wdi-simple.exe / libwdi payload~~ → yok, gerek yok (inbox pnputil + üretilen INF).
- **YAPILDI:** `app/src/driver_switch.cpp` — `set_dongle_winusb(vid,pid,enable)`:
  WinUSB INF üretimi (temp'e), elevated pnputil add/delete/remove-device/scan,
  sonuç raporu (`Ok/NoChange/NotPresent/InfWriteError/ElevationCancelled/PnPError`).
- **YAPILDI:** `driver_mode.cpp` — `DongleDriverStatus::instance_id` eklendi
  (`SetupDiGetDeviceInstanceIdA`) → toggle devnode'u hedefliyor.
- **YAPILDI:** WinUI "Streaming Mode (Dongle)" paneline iki buton:
  **"Enable Streaming (WinUSB)"** ve **"Restore Windows BT (BTHUSB)"**
  (ayrı thread'de çalışır, stream açıkken engellenir, UAC prompt'u, sonrası
  otomatik re-detect).
- Test (canlı): UB500 üzerinden enable → stream → disable → Windows BT geri
  dönüşü **kullanıcı testini bekliyor** (dongle şu an WinUSB modda; toggle
  çalıştırıldığında canlı durum değişir).

### İş kalemleri

- [x] libwdi/wdi-simple — **elenen yaklaşım** (INF üretimi + pnputil ile çözüldü).
- [x] `driver_switch.cpp/.h` (core, framework-agnostik) + `instance_id` (driver_mode).
- [x] WinUI: "Streaming Mode" toggle butonları + durum algılama + UAC elevation.
- [ ] Test: UB500 Üzerinden enable → stream → disable → Windows BT'nin geri döndüğü.

---

## 3. Eşleştirme / Bağlantı Kolaylığı ve Anlatım

### Teknik kısıt (önemli, kullanıcıya net anlatılmalı)

WinUSB modunda **Windows Bluetooth yığını adaptörü hiç görmüyor** (BTHUSB
sürücüsü devre dışı). Bu yüzden:

- Windows Ayarlar → Bluetooth'tan cihaz **eşleştirme/silme imkansız** — orada
  adaptör yok.
- Tüm eşleştirme/bağlantı **uygulama içinden** (BTstack: Scan → Connect /
  Direct Connect) yapılıyor. Cihaz adı, MAC, codec, bitrate uygulamada/`profile`
  JSON'da saklanıyor.
- Normal Windows BT'yi kullanmak istenirse "Streaming Mode" toggle'ı ile
  BTHUSB'a dönülür — **iki mod birbirini dışlar** (tek USB interface, tek
  sürücü). Streaming açıkken Windows BT kapalı, streaming kapalıyken Windows
  BT açık.

### Yapılacak — UX iyileştirmeleri (WinUI3)

> **YAPILDI (2026-09-16)** — wizard = uygulama içi **"Setup & Help"** dialogu
> (adım adım ilk kurulum + SSS/FAQ + WinUSB↔BTHUSB anlatımı); otomatik bağlanma =
> "Auto-connect last device on start" ayarı + "Reconnect Last" butonu
> (settings.json'da `last_device_mac/name` + `auto_connect_on_start`).

- [x] İlk çalıştırmada adım adım rehber → **"Setup & Help"** (`ShowSetupGuide`):
  1) dongle WinUSB'ye geçir (toggle'a yönlendirir) → 2) kulaklığı eşleştirme
  moduna al → 3) Scan → 4) seç + codec → 5) Connect; SSS bölümü: no-sound,
  link key / reconnect, SSC bitrate, Windows BT'ye dönüş.
- [x] Son bağlanan cihazı hatırla + otomatik bağlan: connect/direct-connect'te
  `RememberLastDevice`; açılışta dongle WinUSB değilse atlanıp log'a uyarı
  basılır; yoksa stream otomatik başlar.
- [x] Cihaz listesi adlandırma (MAC + ad, mevcut); durum akışı mevcut.
- [ ] tanıdık adlandırma için "Buds3 FE (SSC 229k)" beklenti satırı — gerekirse
  sonraki passta.
- [x] "Nasıl çalışır / SSS" uygulama içi panel (Setup & Help dialogu).
- [ ] geniş "durum akışı" (ör. "Kulaklığı kasanın yakınına al" tipi akıllı
  öneriler) — sonraki pas.

### İş kalemleri

- [x] MainWindow.xaml: Setup & Help + Auto-connect checkbox + Reconnect Last +
      driver toggle butonları.
- [x] MainWindow.xaml.cpp: `ShowSetupGuide`, `RememberLastDevice`,
      `StartStream`, `RunDriverSwitch`, açılışta otomatik-connect.
- [x] app_settings.h/.cpp: `last_device_mac` / `last_device_name` /
      `auto_connect_on_start` (JSON kalıcılık).
- [x] Yardım/SSS içeriği uygulama içi dialog. (README güncellemesi ayrı kalem.)

---

## 4. İleriye Dönük İyileştirmeler (öncelik sıralı)

> Bölüm başlangıçta (2026-09-14) "Hiçbiri başlatılmadı" diye toplanmıştı;
> **5.1–5.7 artık ✅** ve **Bölüm 2 (sürücü toggle) + Bölüm 3 (Setup & Help /
> auto-connect / Reconnect Last) 2026-09-16'da tamamlandı**. Kalan aktif
> kalem: yalnızca RAM ayak izi (Bölüm 6, kullanıcı kararıyla ertelendi) +
> canlı toggle testi.

### 4.1 Encoder altın dosya (golden-file) regresyon testi — 🟢 ön koşul ✅ (2026-09-15)

- **YAPILDI.** `tools\golden\ssc_golden.py` + `tools\golden\{128k,192k,229k}.golden`
  (48 kHz, 2ch, 16 frame; 324/484/576 byte). `check --all` daemon'ı profile göre
  restart edip byte-byte karşılaştırır; exit 0 = PASS. Daemon/rootfs/Qiling
  değişiklikleri bu referansa göre doğrulanır.
- Re-freeze sadece Samsung blob **kasıtlı** değişince (yeni firmware/encoder
  bump); bugfix adayı çalışmalar için asla.

### 4.2 UHQ 96 kHz / 24-bit streaming — 🟢 en büyük kalite atlaması ✅ (2026-09-15)

- **YAPILDI.** WASAPI 48 kHz'de kilitli kaldığı için 48k→96k SRC (2x) + UHQ profil
  hattı kuruldu:
  1. **Resampler:** `app/src/resampler.cpp` — `upsample_2x_stereo_f32()`, Catmull-Rom
     cubic Hermite (t=0.5: `(9*(cur+next) - (prev+nextnext))/16`, uçlarda linear),
     N orijinal + N-1 midpoint → `in_frames*2 - 1` çıktı, sadece stereo. Birim test
     (DC/ramp/sine RMS) PASS.
  2. **CLI:** `--uhq` bayrağı → SSC + 48k'de `encode_sr = 96000`; `a2dp_service`'in
     `StreamingContext.encode_sample_rate`'i codec + encoder + notify_stream_info'ya
     gider; `audio_callback`'te downmix sonrası 2x SRC.
  3. **WinUI:** Codec yanına RateCombo (48k/96k) → index 1 `profile.sample_rate =
     96000`; SRC `encode_thread_func`'ta (channels==2 && bps==32) uygulanır.
  4. **SSC caps:** `btstack_transport.cpp` `SSC_CAP_UHQ (0x0E)` advertise ediliyor +
     negotiation'da `p.sample_rate > 48000` → UHQ cap kullanılıyor.
- Testler: golden 6/6 PASS (48k: 128k/192k/229k; 96k: 250k/442k/584k, frame
  284/500/660 byte), build'ler EXIT 0. **Canlı cihaz testi yapılamadı** (Bud'lar
  erişilemezdi, BT 0x04); kod ve golden kedebilir, bud'lar gelince
  `SSCOnWindows-0.1.exe --cli -d 78:C1:1D:A7:BC:EE -c ssc --uhq` ile doğrula.
- **Canlı test sonucu (2026-09-17): 96 kHz UHQ Buds3 FE'de ÇALIŞMIYOR.** Buds3 FE
  SSC cap=`0x3C` ilan ediyor (UHQ `0x02` biti **yok**). App yine de `0x0E` config +
  96 kHz PCM gönderince link sağlıklı (`err=0`, `ret=660`) ama ses **sessiz**.
  Çözüm: `ssc_uhq` biti yoksa 48 kHz SSC'ye otomatik düş (CLI + A2dpService) +
  `status.ssc_uhq_fallback` uyarısı; `configure_codec` mismatch'te WARNING basar.
  48 kHz fallback canlı doğrulandı: `rate=48000fs`, `ret=576`, `err=0`, ses gelir.

### 4.3 WSL daemon watchdog / otomatik yeniden bağlanma — 🟢 sağlamlık ✅ (2026-09-15)

- **YAPILDI.** `SscEncoder` artık transport hatasında daemon'ı otomatik restart
  edip reconnect ediyor: `recover()` (daemon'ı öldür + `start_sscblobd` + TCP),
  `kRecoveryCooldownMs=5000` cooldown (wsl.exe'yi hamallama yok), `get_recovery_count()`.
  `encode()` içindeki her send/recv hatası watchdog'u tetikler; kayıp frame düşer,
  stream devam eder (ses boşluğu ~1-2s).
- Test: WSL'de daemon SIGKILL → sonraki encode frame'i atar, recovery sonrası
  576 byte geri gelir (standalone harness ile doğrulandı, PASS).
- **Daemon gotcha:** client bağlıyken gelen SIGTERM daemon'u kapatmaz (g_stop
  flag'i sadece `accept()` döngüsünü kırar; `service_client()` client'dan veri
  bekler). Watchdog bu yüzden önce socket'i kapatır, sonra restart eder.

### 4.4 Tek-çalıştır kurulum otomasyonu — 🟡 devam ediyor (2026-09-15)

- `setup.ps1` (repo kökü): WSL durum algılama → `tools\ssc_payload\` (vendor'lı
  daemon/helper/blob) → WSL'e kopya → `gcc` ile `sscblobd` derle → apt
  bağımlılıklarını doğrula (`qemu-user`, `gcc`, `gcc-aarch64-linux-gnu` sysroot)
  → `.wslconfig` (Aşama 1 ayarları) yaz → dongle sürücü durumunu algıla
  (WinUSB vs BTHUSB) → smoke test (daemon start + golden check).
- Manuel ~1 günlük kurulum → ~5 dakika. Bölüm 2'deki toggle ile tamamlayıcı.

### 4.5 Encode pipeline'lama (paralel slice) — 🟢 performans ✅ (2026-09-15, karar)

- **Yapılmadı — gerekmedi.** Analiz: 96 kHz'de 864 sample'lık frame her 9 ms'de
  gelir (864 / 96000 ≈ 9 ms). SSC encode round-trip'i ~1.2 ms. Ring + ayrı encode
  thread (mevcut mimari) bu diff'i zaten karşılıyor → UHQ için ek pipeline
  gerekmedi. Sadece 2x SRC eklendi (4.2), o da ~µs düzeyinde.

### 4.6 Bağlantı anahtarı kalıcılığı (link key store) — 🟢 tamamlandı ✅ (2026-09-15)

- **YAPILDI.** `profile.json`'a opsiyonel `linkKey` (32-hex) + `linkKeyType` alanları
  eklendi (`ConnectionProfile`, `ProfileManager` load/save round-trip).
- `BtStackTransport::seed_link_key()` / `get_link_key_hex()` — `btstack_link_key_db_file`
  üzerinden key besleme/okuma (BTstack BE ↔ caller LE adres çevrimi dahil).
- `A2dpService`: connect **öncesi** profildeki key'i db'ye `seed` eder (pairing'siz
  reconnect), başarılı bağlantı **sonrası** ve disconnect **sonrası** güncel key'i
  `profiles.json`'daki eşleşen profile yazar (`persist_link_key`). Canonical kaynak
  artık hem `link_keys.txt` hem profile — biri silinse diğeri kurtarır.
- CLI (dev yolu) dosya-db'yi zaten kullanıyor; profile-besleme `A2dpService` (WinUI +
  wx GUI ortak) üzerinden.
- Build'ler 0 hata (core / A2DPWB / WinUI), dist'e deploy edildi. **Canlı test**
  cihaz gelince: profil bağlantısında tekrar eşleştirme istememeli.

### 4.7 SSC bitrate snapping (mode-gated) — 🟢 hata düzeltme ✅ (2026-09-17)

- **Sorun:** GUI bitrate slider'ı (0–990, step 1) sürekli; elle seçilen keyfi değer
  (ör. 447 kbps) blob'a geçince **ses bozuk/garbled** oluyordu.
- **Kök neden:** SSC bitrate'leri A2DP mode'una bağlı (`sachk/openssc`
  `pipewire/a2dp-codec-ssc.c`): 48 kHz **basic** (`0x0C`) yalnızca
  `88/96/128/192/229/256/328 kbps`; UHQ değerleri (`152/250/291/308/442/584/886`)
  UHQ2 (`0x02`) biti ister — Buds3 FE'de yok (`cap=0x3C`). Blob geçersiz değeri
  kabul edip **malformed frame** üretiyor (test: 447k → bozuk).
- **Çözüm:** `SscEncoder::snap_bitrate_kbps(kbps, sample_rate)` — istenen değeri
  örnekleme hızına göre geçerli kümeye yuvarlar; `init()`'te override'a uygulanır
  (CLI + WinUI). GUI `ApplyBitrateSnap()` slider'ı anında snap eder ve rate
  değişiminde yeniden uygular. CLI parity: `--bitrate <kbps>` (0 = auto).
- **Canlı doğrulama:** `--bitrate 447` → log `snapped to 328000 bps`,
  `SSC: initialized ... bitrate=328000`, `rate=48000fs`, `err=0`. 96 kHz UHQ'da
  aynı istek 442000'e düşer.

### 4.8 Canlı grafik / csv telemetri — 🟢 görünürlük ✅ (2026-09-15)

- **YAPILDI.** İki parça:
  1. **CSV telemetri (core):** `publish_stats` yanına `telemetry_write()` —
     stream sırasında ~1 Hz satır ekler: `%TEMP%\a2dpwb_telemetry.csv`
     (header + 13 kolon: ts_unix_ms, uptime_ms, codec, latency_ms, error_rate,
     loss_rate, bitrate_kbps, queue_depth, total_sends, send_fails,
     dropped_frames, encode_calls, captured_frames). CLI ve WinUI ortak.
  2. **Sparkline (WinUI):** Live Stats altına iki çizgi — latency (accent,
     0-25 ms eksen) + error rate (kırmızı, 0-5%) (~120 örnek ≈ 4 dk @2Hz).
- CSV biçimi standalone sanity-test ile doğrulandı (13/13 kolon); WinUI
  0 hata derlendi, dist'e deploy, GUI açılıyor.

### Önerilen paketleme

- **Paket A:** 4.1 + 4.3 + 4.7 — ✅ tamamlandı (sağlamlık + ölçüm tabanı).
- **Paket B:** 4.2 + 4.5 — ✅ tamamlandı (kalite + performans; 5.5 için mimari
  kararı: pipeline eklenmedi, SRC yeterli).
- **Paket C:** 4.4 — kurulum otomasyonu, **✅ tamamlandı** (setup.ps1, test PASS, golden 6/6).
- **Paket D:** 4.6 — link-key kalıcılığı, **✅ tamamlandı**.
- **Paket E:** Bölüm 2 + 3 — sürücü toggle + eşleştirme rehberi/otomatik bağlan,
  **✅ tamamlandı** (2026-09-16; canlı toggle testi açık).
- **Paket F (en sona):** RAM ayak izi (Bölüm 6) — kullanıcı kararıyla ertelendi.

---

## Kararlar / Açık sorular

- [x] Mute toggle: otomatik (stream başlatınca sustur, `AutoMuteCheck` default ON).
- [x] 5.x paketler: A, B ✅; **C (4.4) ✅ ve D (4.6) ✅** — tüm paketler tamamlandı.
- [x] Streaming Mode toggle: **üçüncü seçenek seçildi** — wdi-simple/libwdi yerine
      uygulamanın ürettiği WinUSB INF + inbox `pnputil` (elevated); reverse:
      `pnputil /delete-driver` (sadece winusb.inf referanslı paketler). (2026-09-16)
- [x] Eşleştirme rehberi: **uygulama içi "Setup & Help" wizard/SSS dialogu**
      (+ auto-connect + Reconnect Last). README kısaca güncellenir. (2026-09-16)
- [ ] Canlı test bekliyor: Bölüm 2 toggle (enable→stream→disable) — dongle
      şu an WinUSB modda; çalıştırınca canlı durum değişir.
- [ ] RAM ayak izi (Bölüm 6): en sona ertelendi — Aşama 1 başlangıç onayı
      sadece diğerleri bitince alınır.
- [ ] Aşama 2 POC gerekli mi / ne zaman? (RAM bölümüne dönünce karar verilecek)

## İlgili dosyalar

- `app/src/wasapi_capture.cpp/.h` — mute işi (Bölüm 1).
- `wsl -d Ubuntu -- bash -lc 'ssc ...'` / `~/ssc/openssc/build_blob/` — daemon.
- `tools\ssc_payload\` — vendor'lı daemon/helper/blob (setup.ps1 kaynağı).
- `setup.ps1` (repo kökü) — tek-çalıştır kurulum (4.4), test edildi.
- `app/src/profile_manager.*` + `app/src/btstack_transport.*` +
  `app/src/a2dp_service.*` — link-key kalıcılığı (4.6).
- `C:\Users\<user>\.wslconfig` — Aşama 1 ayarları.
- `~/ssc/blob/libScalable_Encoder.so` — Samsung blob (kaldırılamaz, vendor'lı).
- `app/src/driver_switch.cpp/.h` + `driver_mode.*` — sürücü otomasyonu (Bölüm 2),
  inbox pnputil + üretilen WinUSB INF; `instance_id` alanı `driver_mode.h`.
- `winui3/MainWindow.xaml` + `.xaml.cpp` — Setup & Help wizard/SSS, auto-connect,
  Reconnect Last, driver toggle butonları (Bölüm 2+3), mute toggle (Bölüm 1).
- `app/src/app_settings.cpp/.h` — `last_device_mac`/`last_device_name`/
  `auto_connect_on_start` kalıcılığı (Bölüm 3).

---

## 6. RAM Ayak İzi — ERTELENDİ (en sona, kullanıcı kararı 2026-09-15)

> Bu bölüm "yapılacaklar" sırasının **en sonuna** saklandı. Önce 4.4 (kurulum)
> + 4.6 (link-key) bitirilecek; RAM işine ancak onlardan sonra bakılacak.
> İçerik değişmedi, sadece sıra değişti.

### Araştırma sonuçları (2026-09-14, harici agent)

1. **qemu-user Windows'ta yok ve olmayacak** — resmi QEMU duruşu; Windows
   syscall ABI'si stabil/dokümante değil. MSYS2 build'i yalnızca system emulation
   (full-OS), user-mode değil. → **WSL2 (Linux kernel) zincirin şartı** olmaya
   devam ediyor.
2. FEX-Emu yanlış yön (sadece aarch64 host üzerinde x86). Unicorn tek başına
   CPU emülatörü; **Qiling Framework** (Unicorn tabanlı) ELF loader + dynamic
   linker + syscall/IO handler'ları hazır getiriyor ve Windows üzerinde aarch64
   ELF çalıştırabiliyor → WSL2'siz gerçek alternatif.
3. **openssc bit-exact olması kısa/orta vadede gerçekçi değil** — quantization/
   rounding, bit-slicing/entropy sırası, DRC, header/CRC dokümante değil; LDAC/
   aptX örnekleri yıllar sürmüş. → blob'a bağımlılık sürer.
4. `.wslconfig` tuning'i ile idle **~700 MB bandına** inmek mümkün ama altına
   güvenilir inilemiyor (VM kimliği + kernel + init). `memory=` bir tavan,
   taban değildir.

### Yol haritası (önerilen sıra)

#### Aşama 1 — WSL2 ayak izini küçült (1–3 gün, düşük risk, önce)

- Custom minimal rootfs (`wsl --import`): systemd yok, busybox/musl tabanlı;
  yalnızca `sscblobd` + `qemu-aarch64` + blob.
- `.wslconfig`:
  ```ini
  [wsl2]
  memory=1GB
  vmIdleTimeout=5000
  swap=0
  sparseVhd=true
  nestedVirtualization=false
  guiApplications=false

  [experimental]
  autoMemoryReclaim=dropcache
  ```
- Daemon'ı sürekli değil, encode isteği geldiğinde spawn et (idle'da VM tamamen
  idle algılansın → reclaim tetiklensin). `sscblobd` zaten `start_sscblobd`
  üzerinden restart ediyor; `vmIdleTimeout` + on-demand `wsl` call yeterli.
- Beklenen kazanç: ~700 MB–1 GB bandına iniş, mevcut zincir korunur.
- Kabul kriteri: SSC bitstream byte-exact aynı (regresyon: log `ret=576`,
  `total ≈ 1.2 ms`).

#### Aşama 2 — Qiling ile WSL2'yi kaldır (2–4 hafta POC, orta-yüksek risk, opsiyonel)

- Windows-native Qiling process içinde aarch64 blob'u VM yok, TCP köprüsü yerine
  in-process çağrı → **100–200 MB hedefi burada**.
- Zorunlu koşullar:
  - Blob'un syscall yüzeyini çıkar (mmap, brk, futex, clock_gettime, pthread).
  - Qiling syscall tablosunda eksikler kendin implement edilir.
  - **Byte-exact regresyon test seti** şart: orijinal WSL2/qemu çıktısıyla
    her profil (128/192/229 k, 48 kHz) byte-byte karşılaştır. "Sanırım doğru"
    kabul edilemez — payload kesinliği kısıtı var.
  - Riskler: glibc TLS/thread-local init tutmazsa crash ya da **sessiz** yanlış
    hesaplama (bitstream bozulması) — debug WSL2'den zor.
- Bağımlılık: Aşama 1'in ulaştığı ayak izi yetersizse (veya kullanıcı WSL2'den
  tamamen kurtulmak istiyorsa) başlat.
- Karar eşiği: Aşama 1 ölçümü ~1 GB'ın altındaysa Aşama 2'yi ertele.

### Önerilmeyen yollar

- qemu-user'ı Windows'a taşımak (resmi olarak imkansız).
- FEX-Emu (yanlış yön).
- openssc'nin bit-exact olmasını beklemek (açık uçlu, aylar).