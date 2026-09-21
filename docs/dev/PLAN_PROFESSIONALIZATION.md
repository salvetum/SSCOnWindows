# SSCOnWindows — Profesyonelleştirme ve Açık-SSC Yol Haritası

> Bu dosya bir **opencode plan promptu**dur. Agent'a bu dosyayı referans göstererek
> "PLAN_PROFESSIONALIZATION.md'deki Faz X'i uygula" şeklinde görev verebilirsin.
> Her faz bağımsız bir PR/branch olacak şekilde tasarlandı; sırayla veya paralel yürütülebilir.

## Durum

| Faz | Konu | Durum |
|-----|------|-------|
| 0 | Envanter ve Denetim | ✅ `docs/dev/audit-2026.md` (commit c894219) |
| 1 | Depo Hijyeni ve Lisans Uyumu | ✅ 44a3327 (gitignore + pre-commit + setup -BlobFrom + CONTRIBUTING) |
| 2 | CI/CD | ✅ 96ea97a (build/lint/golden/release workflow'ları; vcxproj portability) |
| 3 | Kurulum Otomasyonu | ✅ yerelde parse + dry-run doğrulandı (committed) |
| 4 | Kod Kalitesi ve Test Kapsamı | ✅ `codec_policy.h` + doctest unit/integration testler + daemon `CMD_SHUTDOWN` |
| 5 | Dokümantasyon ve Sürümleme | ✅ `f087899` + `528b887` (architecture/compatibility docs, SemVer + Keep a Changelog, 0.2.0) |
| 6 | Kapalı Kaynak SSC Bağımlılığı | ✅ 6a (2026-09-21, `SscEncodeBackend` + Legal status; blob kullanıcı kararı ile tracked kaldı) — 6b açık kalır |
| 7 | Topluluk ve Sürdürülebilirlik | ⏳ |

## Bağlam

SSCOnWindows, Samsung'un kapalı kaynak SSC (Samsung Scalable/Seamless Codec) codec'ini
Windows'ta kullanılabilir kılan bir proje (A2DP Windows Bridge fork'u). Mimari zaten sağlam:
WASAPI loopback capture → SSC/AAC/SBC encode → BTstack üzerinden A2DP/AVDTP. Şu anki
sorun alanları iki kategoride: (1) proje olgunluğu — kurulum, CI, test, dokümantasyon
eksiklikleri; (2) **stratejik risk** — projenin çekirdek işlevi Samsung'a ait, kapalı,
lisanslanmamış bir binary blob'a (`libScalable_Encoder.so`) bağımlı.

Bu plan her iki eksenle de ilgileniyor. Faz 0–5 "profesyonelleştirme", Faz 6 ise
"kapalı-kaynak bağımlılığı" konusunu ele alıyor.

---

## Faz 0 — Envanter ve Denetim (ilk yapılacak, kısa)

Agent görevi:
1. `git log --all --full-history --source --remotes -- '*.so' '*Scalable_Encoder*'`
   ile blob'un **git geçmişinde herhangi bir noktada commit edilip edilmediğini** doğrula.
   README "proprietary; not included in this repository" diyor — bunun gerçekten doğru
   olduğunu, blob'un ne çalışma zamanında indirilip ne de repo içine gömülü hiçbir yerde
   (tools/, extern/, patches/ dahil) bulunmadığını teyit et.
2. `extern/` altındaki tüm submodule'lerin lisanslarını (`BTstack` dual-license,
   `fdk-aac` non-commercial FDK lisansı) `THIRD_PARTY_LICENSES.md` ile karşılaştır,
   tutarsızlık varsa raporla.
3. Sonucu `docs/dev/audit-2026.md` olarak yaz.

Bu fazın çıktısı sonraki fazların önceliğini belirleyecek: blob gerçekten repo'da
committed ise Faz 6'nın "acil temizlik" alt maddesi öncelik kazanır.

---

## Faz 1 — Depo Hijyeni ve Lisans Uyumu

- `.gitignore`'a proprietary blob/firmware dosyalarının asla commit edilemeyeceğini
  garanti eden pattern'ler ekle (`*.so`, `libScalable_Encoder*`, `rtl8761*firmware*`).
- Bir **pre-commit hook / CI check** ekle: diff'te bilinen proprietary dosya imzaları
  (dosya adı veya SHA256 allowlist) varsa commit/PR'ı reddet.
- `setup.ps1`'i, kullanıcının blob'u **kendi sahip olduğu** bir Galaxy cihazının
  Wearable uygulaması/firmware paketinden yasal biçimde çıkarmasına yardımcı olacak
  şekilde genişlet (indirme değil, yerel çıkarma — README'de zaten bu yön ima ediliyor).
- `CONTRIBUTING.md`'ye "asla proprietary binary submit etmeyin" kuralını netçe yaz.

## Faz 2 — CI/CD

- `.github/workflows/build.yml`: Windows runner'da CMake configure + `a2dpwb_core`
  + WinUI3 build matrix (Debug/Release).
- `.github/workflows/golden.yml`: `tools/golden/ssc_golden.py check --all`'ı
  (blob mevcutsa) veya mock/stub encoder ile (blob yoksa, açık kaynak CI runner'da
  proprietary blob bulunmayacağı için) skip-with-warning mantığıyla çalıştır.
- Lint: clang-format + clang-tidy core için, xaml-style-check WinUI3 için.
- Release workflow: tag push → GitHub Release + derlenmiş `.exe` artifact (blob hariç).

## Faz 3 — Kurulum Otomasyonu

- `setup.ps1`'i tek komutla: dongle sürücü kontrolü → WinUSB toggle rehberliği →
  Realtek firmware kontrolü/indirme → WSL2 kontrolü/kurulumu → blob varlık kontrolü
  akışına dönüştür. Her adımda idempotent olsun (tekrar çalıştırılabilir).
- Hata mesajlarını kullanıcı dostu hale getir (şu an muhtemelen ham exception/log).
- Opsiyonel: basit bir WPF/WinUI "ilk kurulum sihirbazı" ekranı.

## Faz 4 — Kod Kalitesi ve Test Kapsamı

- `app/src/` için unit testler: codec fallback mantığı (SSC>AAC>SBC), bitrate
  snapping, UHQ capability-bit fallback — bunlar şu an sadece golden-file ile
  dolaylı test ediliyor, izole unit testler ekle (Catch2 veya doctest).
- A2DP/AVDTP negotiation için mock BTstack transport ile integration testleri.
- Daemon graceful-shutdown sorunu (SIGTERM'de kapanmama) için: TCP protokolüne
  açık bir `CMD_SHUTDOWN` mesajı ekle, watchdog yerine düzgün kapanma sağla.

### Uygulandı (2026-09-20)

- **`app/src/codec_policy.h`**: saf (I/O'suz) karar yardımcıları — `Caps`,
  `resolve_codec` (SSC>AAC>SBC fallback), `snap_bitrate_bps` (48k basic /
  96k UHQ mode-gated setler, 229/328/442/584 vd.), `pick_bitrate_bps`,
  `resolve_encode_sr` (UHQ 0x02 capability gate + fell_back bayrağı),
  `resolve_stream` (uçtan uca). `ssc_encoder.cpp`, `main.cpp`,
  `a2dp_service.cpp` bunlara delege edecek şekilde refactor edildi (kod
  kopyası kaldırıldı, davranış korundu — golden regression 6/6 PASS).
- **doctest test süiti**: `tests/` (harness `a2dpwb_tests`, `a2dpwb_core`'a
  bağlanmaz — hermetic). `unit/codec_policy_tests.cpp` 14 test case / 83
  assertion; `integration/negotiation_tests.cpp` MockTransport ile
  negotiation kararını sınar (gerçek btstack run-loop seviyesinde event
  mock'u büyük transport refactor'ü gerektirirdi — sınır belgelendi).
  CMake: `enable_testing()` + `add_test`, CI `build.yml`'de her config'de
  `a2dpwb_tests` build + `ctest` çalışır.
- **Daemon `CMD_SHUTDOWN`**: TCP protokolüne 4-byte magic
  (`0x44434853`, LE 'S','H','C','D') eklendi. `sscblobd.c` (WSL) ve
  `sscblobd.py` (Qiling) client'ın frame_samples header'ında bu değeri
  görmesiyle temiz çıkıyor; `SscEncoder::shutdown()` artık socket'i
  kapatmadan önce `request_shutdown()` ile magic gönderiyor. Watchdog
  (`recover`) crash/bağlantı-kopma yolları için korunuyor. Her iki daemon
  üzerinde canlı smoke test doğrulandı.

## Faz 5 — Dokümantasyon ve Sürümleme

- `docs/architecture.md`: README'deki ASCII diyagramı genişletip veri akışını,
  thread modelini, hata durumlarını belgele.
- Semver'e geç, `CHANGELOG.md`'yi Keep a Changelog formatına taşı.
- Desteklenen/test edilmiş cihaz matrisi (Buds3 FE dışında hangi modeller
  denendi) için `docs/compatibility.md`.

---

## Faz 6 — Kapalı Kaynak SSC Bağımlılığı: Yol Haritası

Bu projenin gerçek stratejik zayıflığı bu. İki paralel, birbirini dışlamayan yol var:

### 6a. Kısa vadede: bağımlılığı şeffaflaştır ve izole et (hemen yapılabilir)

- Blob her zaman kullanıcı tarafından **kendi cihazından** temin edilsin, repo veya
  release asla dağıtmasın (Faz 1 ile örtüşüyor).
- Encoder arayüzünü bir `IScscEncoderBackend` soyutlamasının arkasına al, böylece
  ileride açık bir implementasyon drop-in replacement olarak eklenebilsin.
- README'ye net bir "Legal status" bölümü ekle: SSC formatı Samsung'a ait, bu proje
  format tanımını değil, format ile **birlikte çalışabilirliği (interoperability)**
  hedefler; blob dağıtılmaz, patent/telif riskleri kullanıcıya açıkça belirtilir.

### 6b. Orta/uzun vadede: açık, temiz-oda (clean-room) reimplementasyon araştırması

**Amaç:** Samsung blob'una hiç ihtiyaç duymayan, tamamen açık kaynak bir SSC
encoder/decoder. Bu ciddi bir mühendislik + hukuki dikkat gerektiren bir araştırma
hattı — "birkaç haftalık görev" değil, ayrı bir alt-proje olarak planlanmalı.

**Önemli çerçeve notu (agent'a ve sana):** Bir codec'i *sadece davranışını gözlemleyerek*
tersine mühendislik yapıp yeniden yazmak (kod kopyalamadan), birlikte çalışabilirlik
amacıyla birçok yargı bölgesinde tanınan bir uygulamadır (AB Yazılım Direktifi Md. 6,
ABD'de DMCA §1201(f) — ama bunlar teknik koruma önlemi/EULA bypass senaryolarına göre
değişir). Ancak **ses codec'leri genellikle patentlidir** (AAC, aptX vb. örneklerinde
olduğu gibi) — kod kopyalamasanız bile aynı algoritmayı yeniden üretmek patent ihlali
riski taşıyabilir. Bu, telif hakkından tamamen ayrı bir risk ve ben (ya da herhangi bir
kod asistanı) bunu senin için değerlendiremez. **Bu hattı kamuya (özellikle ticari
ölçekte) yayınlamadan önce bir fikri mülkiyet avukatına danışmanı öneririm.**

Bununla birlikte, *araştırma ve kişisel/hobi amaçlı interoperability* için makul bir
mühendislik metodolojisi şu şekilde kurgulanabilir:

**Adım 1 — Davranışsal (black-box) veri toplama.**
Zaten elinizde bir varlık var: blob'u WSL2/Qiling üzerinden çalıştıran TCP daemon.
Bunu genişletip sistematik bir test seti çalıştırın: bilinen sinüs sweepleri, beyaz
gürültü, sessizlik, gerçek müzik örnekleri — geniş bir PCM girdi/SSC çıktı çifti
kümesi toplayın (golden-file altyapınız zaten bu mantıkta, sadece amacı test
doğrulamasından spesifikasyon çıkarımına genişletin).

**Adım 2 — Statik analiz ile "davranış spesifikasyonu" yazımı.**
Blob'u Ghidra ile disassemble edip fonksiyon sınırlarını, sabit tabloları
(quantization/filter coefficient tabloları gibi) ve genel akışı (subband
decomposition var mı, entropy coding tipi ne, vb.) **belgeleyin** — hedef, kodu
kopyalamak değil, "bu girdi verildiğinde bu adımlar izleniyor" diyen bağımsız bir
spesifikasyon dokümanı üretmek (openssc projesinin zaten açtığı capability/bitrate/
mode alanlarını temel alarak).

**Adım 3 — Temiz-oda (clean-room) reimplementasyon.**
Spesifikasyonu **hiç orijinal koda bakmamış** ayrı bir "yazar" (ya da zaman içinde
ayrı bir zihin durumuyla siz) okur ve sıfırdan bir implementasyon yazar. Bu ayrım —
"analiz eden" ile "yazan" arasında — temiz-oda metodolojisinin özü ve telif hakkı
riskini azaltan tek şey (patent riskini azaltmaz, yukarıdaki notu tekrar hatırlatırım).

**Adım 4 — Doğrulama.**
Yeni implementasyonu golden-file test setinizle karşılaştırın: bit-exact eşleşme
beklemeyin (bu muhtemelen mümkün olmayacak ve zaten hedef de bu değil), bunun yerine
gerçek Galaxy Buds donanımında **perceptually** çalışıp çalışmadığını (ses kalitesi,
senkron, dropout yok) test edin.

**Adım 5 — İzolasyon.**
Bu araştırmayı ana repodan ayrı bir `openssc-research` (ayrı repo veya en azından
ayrı, deneysel bir branch) altında tutun; ana projenin MIT lisanslı, kullanıma hazır
statüsünü riske atmayın. Sonuç olgunlaşırsa Faz 6a'daki `IScscEncoderBackend`
soyutlamasının arkasına opsiyonel bir backend olarak eklenebilir.

**Gerçekçi beklenti yönetimi:** Bu araştırma hattının başarı ihtimali belirsizdir —
codec'in entropy coding ve psikoakustik model detayları olmadan davranışsal
gözlemden tam bir yeniden üretim çok zor olabilir. Kısa vadede Faz 6a'yı (şeffaflık +
izolasyon) tamamlamak, projenin "profesyonellik" hedefine daha hızlı ve garantili
katkı sağlar; Faz 6b uzun soluklu, sonucu garanti olmayan bir Ar-Ge hattı olarak
görülmeli.

---

## Faz 7 — Topluluk ve Sürdürülebilirlik

- Issue/PR şablonları (`bug_report.md`, `feature_request.md`, `hardware_compat.md`).
- `ROADMAP.md`: yukarıdaki fazları GitHub Projects board'una dönüştür.
- Discussions sekmesini aç, donanım uyumluluk raporları için ayrı kategori.

---

## opencode için kullanım notu

Her faz başlığını ayrı bir görev olarak ver, örn:
```
opencode: PLAN_PROFESSIONALIZATION.md dosyasındaki "Faz 0" bölümünü uygula,
sonucu docs/dev/audit-2026.md olarak commit et.
```
Faz 6b'yi agent'a verirken **"kod kopyalama, sadece davranışsal spesifikasyon
dokümanı üret"** kısıtını görev tanımına açıkça yazın — agent'ın orijinal blob'un
disassembly çıktısını doğrudan "yeniden yazılmış kod" olarak sunmasını istemezsiniz,
adım 2 ile adım 3 arasındaki ayrımın gerçek bir ayrım olması metodolojinin bütünlüğü
için önemli.
