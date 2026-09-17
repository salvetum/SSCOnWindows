# Robustness / Sağlamlık Planı

> Durum: **PLAN — henüz uygulanmadı.** Tarih: 2026-09-18.
> Kaynak: kod denetimi (thread/lifecycle, error handling, tasarım/altyapı).
> Satır numaraları `d9cf809` commit'ine göredir.

Bu doküman, yeni özellik (QoL) eklemeden önce yapılması gereken sağlamlık
çalışmalarını öncelik sırasıyla listeler. Her madde: kanıt (`file:line`), hata
senaryosu ve somut çözüm içerir.

---

## 0. Samsung SSC blob'u — lisans / dağıtım planı

Repo public ve blob şu an commit'li:
`tools/ssc_payload/blob/libScalable_Encoder.so` ve
`tools/ssc_daemon/rootfs/blob/libScalable_Encoder.so` (Samsung'a ait tescilli
binary). Risk ve proaktif çözüm:

1. Blob'lar repodan çıkarılır, `.gitignore`'a eklenir:
   `tools/**/blob/libScalable_Encoder.so`
2. `tools/setup_blob.ps1` ile kullanıcı blob'u kendi WSL payload'ından / cihazından
   çıkarıp doğru yere koyar.
3. README'ye "blob dahil değildir, kendiniz edinin" adımı eklenir.
4. App'e `check_blob_present()` eklensin; yoksa anlaşılır hata/toast verir.

Not: Şimdilik push edildi; Samsung DMCA gönderirse içerik kaldırılıp bu plan
uygulanarak repo tekrar açılabilir (reaktif). Proaktif uygulamak önerilir.

---

## 1. Kritik (gerçek bug'lar)

### C1 — UHQ (96 kHz) GUI yolunda heap taşması
- **Kanıt:** `app/src/a2dp_service.cpp:295` (`src_buf.resize(in_frames * 8)`)
  vs `app/src/resampler.cpp:26-27` (çıkışa `4*in_frames` float = 16 B/kare yazar).
  480 karede ~3.8 KB heap taşması; ardından `read_buf.swap(src_buf)` (`:300`) ve
  `:314` okumasında ~3.8 KB OOB okuma.
- **Senaryo:** WinUI + Rate=96k + UHQ-destekleyen cihaz
  (`encode_sr==96000 && channels==2 && bits_per_sample==32`). Buds3 FE UHQ
  desteklemediği (48k fallback) için canlıda patlamıyor; UHQ cihazda çöker.
- **Çözüm:** `src_buf.resize(in_frames * 16)` (veya `std::vector<float>`; CLI
  yolu `main.cpp` zaten güvenli).

### C2 — WASAPI endpoint kopması sessizce ölüyor
- **Kanıt:** `app/src/wasapi_capture.cpp:351-355, 364-371` (`break`), servis
  döngüsü yalnız `check_disconnected()` izler (`a2dp_service.cpp:1368`). Tüm
  ağaçta `IMMNotificationClient` / `AUDCLNT_E_DEVICE_INVALIDATED` yok.
- **Senaryo:** Cihaz çıkarılır/disable edilir → `running_` true kalır, UI
  "Streaming" gösterir, sonsuz sessizlik; hata/reconnect yok.
- **Çözüm:** invalidation'ı yakalayıp fatal-capture callback; ana döngüde
  `ring.available_read()` büyümüyorsa (~1-2 s) capture hatası → `State::Error` /
  yeniden init. `IMMNotificationClient` ile `DefaultDeviceChanged` izle.

### C3 — `~A2dpService` worker thread'i join etmiyor
- **Kanıt:** `app/src/a2dp_service.cpp:534-536`; `stop_streaming()` 5 sn sonra
  thread canlıyken döner (`:982-989`); `worker_thread_` join edilmez.
- **Senaryo:** Pencere kapanışında thread `this`'e erişir → use-after-free.
  `start_streaming` join'i UI thread'inde (`:956-959`) → ~35 sn donma.
- **Çözüm:** `stop_streaming` sonunda gerçekten join et (bounded wait + abandon);
  dtor'da join garantile.

### C4 — BTstack run-loop çökerse kurtarılamaz
- **Kanıt:** `app/src/btstack_transport.cpp:217-426` run-loop thread'i SEH'siz
  (servis thread'i `a2dp_service.cpp:993-1004` ile sarılı). `instance_`/
  `hci_ready_` set kalır; sync çağrılar 5 sn, reconnect denemesi 30 sn timeout →
  ~5.5 dk donma, sadece app restart çözer. WinUI `reset_btstack()`'i hiç çağırmıyor.
- **Çözüm:** run-loop'u SEH/exception ile sar; dispatch timeout'unda transport'u
  ölü işaretle; `ensure_btstack_init()` ölü transport'u yeniden kursun;
  WinUI `State::Error` alınca `reset_btstack()` çağırsın.

---

## 2. Yüksek

- **H1 — scan/transport UAF:** `shutdown_btstack` transport'u resetlerken
  detached scan thread `transport_`'a kilitsiz erişiyor
  (`a2dp_service.cpp:826-838` vs `:888-890`). Çözüm: transport_mutex_ altında
  al veya scan iptalini bekle.
- **H2 — suspend/release reconnect tetiklemiyor:**
  `STREAM_SUSPENDED`/`STREAM_RELEASED` `disconnect_occurred_` set etmiyor
  (`btstack_transport.cpp:1482-1490`). Çözüm: set et + kalıcı send-fail'de
  reconnect.
- **H3 — daemon/WSL timeout'suz:** `ssc_encoder.cpp:133` `run_wsl` `ReadFile`
  timeout'suz; WSL takılırsa Pro Audio encode thread'i süresiz bloklanır.
  Ardından `g_ctx.ring.destroy()` canlı thread varken (`a2dp_service.cpp:1420-1424`)
  → use-after-close. Çözüm: pipe okumaya deadline (overlapped/PeekNamedPipe);
  `WaitForSingleObject(encode_thread)` sonucunu **kontrol et**.
- **H4 — `RunLoopRequest` stack'te + timeout sonrası UAF:**
  `btstack_transport.cpp:59-82`; 5 sn timeout sonra `done_event` kapatılıp sonuç
  okunuyor. Çözüm: heap-allocate (doğru desen `set_absolute_volume` `:1574-1588`).
- **H5 — WinUI lifetime:** callbacks (`MainWindow.xaml.cpp:65-123`), detached
  driver-switch thread (`:524-542`), dtor (`:154-159`) yalnız stream'i durduruyor,
  queued lambda'lar drain edilmiyor. Çözüm: `closing_`/`isClosed_` bayrağı +
  enqueue edilen lambda başında kontrol.

---

## 3. Orta

- **M1 — frame sessizce düşüyor:** `accum[2048]` (`a2dp_service.cpp:249,422`),
  `MediaPacket.data[1024]` ama SSC sözleşmesi 4096. Çözüm: init'te config'i
  doğrula, oversize'ı `send_failure_count_`'a say + logla.
- **M2 — bilinmeyen bit depth:** `a2dp_service.cpp:338-340` `else break` → sessiz
  sessizlik. Çözüm: 24-bit container'ı normalize et veya init'te reddet/hata ver.
- **M3 — daemon recover stall:** `recover()` encode thread'inde 5-15 sn stall
  (`ssc_encoder.cpp:289-318`). Çözüm: recover'ı worker thread'e taşı, `encode()`
  "recovering" döndürsün.
- **M4 — `WSAStartup`/`WSACleanup` dengesiz:** `ssc_encoder.cpp:147/426`. Çözüm:
  ctor'da bir kez startup, dtor'da bir kez cleanup.

---

## 4. Tasarım / altyapı

- **Ortak çekirdek:** CLI (`main.cpp`) ve GUI (`a2dp_service.cpp`) aynı ses yolunu
  iki kez implement ediyor (codec fallback, sample-bytes, int32 scale, PCM
  conversion). Wire sabitleri (`864/4096/20248/2^29`) dağınık. Öneri: tek paylaşımlı
  header/modül (`ssc_constants.h`, ortak codec seçimi).
- **Hardcoded değerler:** `/home/kaan5/...`, `C:\Projects\SSCOnWindows`, UB500
  VID/PID (0x2357/0x0604) 5 tabloda. Öneri: config/parametreleştirme.
- **Test:** CTest/unit test yok. `snap_bitrate`, codec fallback, AVRCP volume
  mapping, resampler, profile JSON test edilmiyor. Öneri: CTest + ilgili unit
  testler; golden'ı CMake test olarak kaydet; core'u `/W4 /WX`.
- **CI:** hiç yok. Öneri: GitHub Actions (MSVC build + JSON lint + Python golden
  dry-run).

---

## 5. Önerilen sıra

1. `patches/btstack-win-usb-logs.patch` zaten alındı; worktree'yi sıfırla (temiz statü).
2. **C1** (tek satır, en kritik).
3. **C2** (WASAPI invalidation + watchdog).
4. **C3** (thread lifecycle/join).
5. **C4 + H5** (BTstack dead-transport + WinUI lifetime).
6. **H1, H2, H3, H4**.
7. **M1-M4**.
8. Ortak çekirdek + sabitler; CTest + `/W4 /WX`.
9. Blob lisans planı (§0) — public repo için.

## 6. Doğrulama

- Her düzeltmeden sonra: `cmake --build build_msvc --config Release --target A2DPWB`
  ve WinUI MSBuild; CLI `--help` / 48k SSC canlı smoke.
- C1 için: UHQ destekleyen cihaz yoksa, en azından `in_frames*16` resize'ı ve
  resampler çıkış sınırını unit test ile doğrula.
- C2 için: streaming sırasında varsayılan çıkış cihazını değiştir/çıkar; app
  hata gösterip toparlanmalı.
