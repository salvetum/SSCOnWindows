# Denetim Raporu — Faz 0 (2026-09-18)

`docs/dev/PLAN_PROFESSIONALIZATION.md` Faz 0 kapsamında yapılan envanter ve lisans
denetimi. Kod değişikliği yapılmadı; yalnızca kanıt toplandı ve tutarsızlıklar
raporlandı.

---

## Yönetici özeti

1. **README'nin "blob repoda yok" iddiası YANLIŞ.** Proprietary Samsung kodlayıcı
   blob'u (`libScalable_Encoder.so`) `d9cf809` commit'iyle bilerek repoya eklendi ve
   şu an iki kopya halinde tracked. Bu, Faz 1 ve Faz 6a'yı **yüksek önceliğe** taşır.
2. **Lisans tabloları üç yerde yanıltıcı/eksik:** BTstack "ticari kullanıma kapalı",
   fdk-aac "patent + MIT-uyumsuz", openssc ise **lisanssız (tüm hakları saklı)**.
   Ayrıca `rootfs` içinde dağıtılan **glibc** ikilileri `THIRD_PARTY_LICENSES.md`'de
   hiç geçmiyor.
3. **Hiçbir otomatik koruma yok:** `.github/` yok (CI/PR check yok), pre-commit hook
   yok, `.gitignore`'da `*.so` / `libScalable_Encoder*` deseni yok. Yani proprietary
   binary yarın yine farkında olmadan commit edilebilir.

---

## Bulgu 1 — Proprietary blob repoda (kritik)

### Kanıt

```powershell
git log --all --full-history --source --remotes --oneline -- "*.so" "*Scalable_Encoder*"
# d9cf809  refs/heads/main  SSC On Windows 0.1: SSC/AAC/SBC fork, docs and repo cleanup

git ls-files | Select-String -Pattern "Scalable_Encoder"
# tools/ssc_daemon/rootfs/blob/libScalable_Encoder.so
# tools/ssc_payload/blob/libScalable_Encoder.so
```

- İki kopya, her biri ~103.4 KB, ikisi de **tracked**.
- Blob **upstream (SeiyaFunaokaJP) geçmişinde hiç yer almamış**; yalnızca bizim
  `d9cf809` commit'imizde eklenmiş. Yani "upstream'den miras" değil, bizim kararımız.
- `*.bin` / `*.fw` (Realtek firmware) **hiç** commit edilmemiş → firmware iddiası doğru.

### Sorun

- `README.md:211` şöyle diyor:
  > `Samsung libScalable_Encoder.so` — ... — *proprietary; not included in this repository*

  Bu ifade **gerçekle çelişiyor**. README, aynı repoda tracked duran bir dosyayı
  "repoda yok" diye beyan ediyor. Bu hem yanlış beyan hem de kullanıcı için
  yanıltıcı (setup sırasında blob'u aramaya gerek olmadığını sanır).
- `.gitignore` yalnızca `*.fw` / `*.bin` içeriyor; `*.so`, `libScalable_Encoder*`
  veya blob imzası için koruma yok.

### Etki

Projenin çekirdek işlevi, Samsung'a ait lisanssız bir binary'e dayanıyor ve bu
binary public repo'da dağıtılıyor. Yeniden dağıtım riski (DMCA/telif) gerçek.
Kullanıcı 2026-09-18'de "blob şimdilik kalsın" kararı verdi; bu bilinçli bir
kabul. Ancak README'nin yanlış beyanı **ayrı ve düzeltilmesi zorunlu** bir kusur.

---

## Bulgu 2 — Diğer tracked binary'ler ve eksik atıflar (yüksek)

`git ls-files tools` ile bulunan tracked ELF ikilileri:

| Dosya | Ne | Lisans durumu |
|-------|----|---------------|
| `tools/ssc_daemon/rootfs/blob/libScalable_Encoder.so` | Samsung blob | Proprietary (Bulgu 1) |
| `tools/ssc_payload/blob/libScalable_Encoder.so` | Samsung blob (kopya) | Proprietary |
| `tools/ssc_daemon/rootfs/helper/ssc_blob_helper` | Derlenmiş helper | Proje kaynağı (`ssc_blob_helper.c`) var |
| `tools/ssc_payload/build_blob/ssc_blob_helper` | Aynı helper (kopya) | Proje kaynağı var |
| `tools/ssc_payload/build_blob/sscenc-blob-run` | Derlenmiş runner | Derlenmiş artefakt |
| `tools/ssc_daemon/rootfs/shims/lib{c,dl,log,m}.so` | Android shim'leri | Kaynak: `android_libc_shim.c` / `android_log_shim.c` / `empty_shim.c` |
| `tools/ssc_payload/build_blob/lib{c,dl,log,m}.so` | Aynı shim'ler (kopya) | Aynı |
| `tools/ssc_daemon/rootfs/lib/ld-linux-aarch64.so.1` | **glibc** dinamik yükleyici | **LGPL-2.1+ — atıf YOK** |
| `tools/ssc_daemon/rootfs/lib/aarch64-linux-gnu/lib{c,dl,m,pthread,gcc_s}.so*` | **glibc/libgcc** çalışma zamanı | **LGPL-2.1+ / GPL-3+exception — atıf YOK** |
| `tools/ssc_payload/build_blob/ssc_blob_helper` | (yukarıda) | - |

### Sorun

- **`rootfs/lib/` altındaki glibc + libgcc dağıtılıyor** ama `THIRD_PARTY_LICENSES.md`
  ve `docs/licenses.md` bunları hiç anmıyor. LGPL, ikili dağıtımda lisans metninin
  ve telif bildiriminin verilmesini şart koşar.
- Shim `.so` ve helper ikilileri proje kaynaklı; sorun yok ama **derlenmiş çıktı**
  oldukları için kaynakla eşleşme/bayatlık riski var (Faz 1'de karar verilmeli:
  binary tutulsun mu, yoksa setup'ta mı derlensin?).

---

## Bulgu 3 — Lisans tablosu tutarsızlıkları (yüksek)

`THIRD_PARTY_LICENSES.md` ile gerçek dosyalar karşılaştırıldı:

| Bileşen | `THIRD_PARTY_LICENSES.md` diyor | Gerçek | Sonuç |
|---------|--------------------------------|--------|-------|
| BTstack | "BSD-3-Clause (dual)", "MIT ile uyumlu: Yes" | BSD-3-Clause **+ 4. madde: yalnızca ticari olmayan kullanım** (`extern/btstack/LICENSE`) | **Yanıltıcı.** Proje MIT ama birleşik eser ticari kullanılamaz; ticari kullanım BlueKitchen'dan ayrı lisans ister. |
| fdk-aac | "FDK AAC License", "permissive with conditions", "MIT ile uyumlu: Yes" | Fraunhofer FDK AAC lisansı (`extern/fdk-aac/NOTICE`, `MODULE_LICENSE_FRAUNHOFER`), **AAC patent uyarısı** (Via Licensing) | **Fazla iyimser.** Ne OSI onaylı ne MIT uyumlu; ticari dağıtımda ayrı patent lisansı gerekir. |
| openssc | "see upstream" | GitHub API: **lisans yok (404)** → varsayılan **tüm hakları saklı** | Eksik/yanlış. Kod kopyalanmıyor olsa bile referans olarak açıkça belirtilmeli. |
| Qiling | "GPL-2.0", "Separate process" | Doğru | Qiling repoda **vendored değil** (`tools/ssc_daemon/qiling_patches.py` yalnızca yama uygular, `py -3.14` ile pip'ten gelir) → dağıtılmıyor, yükümlülük doğmuyor. Not yeterli. |
| glibc / libgcc | **listede yok** | LGPL-2.1+ / GPL-3+exception, `rootfs/lib` içinde dağıtılıyor | **Eksik.** Bulgu 2. |
| Qiling / Python çalışma zamanı | - | `py -3.14` sistem bağımlılığı | Kullanıcı kuruyor; dağıtılmıyor. |

### Proje LICENSE notu (düşük)

`LICENSE` hâlâ `Copyright (c) 2026 Seiya Funaoka` diyor; fork yazarı eklenmemiş.
MIT, mevcut bildirimi korumayı şart koşar ama fork telifi eklemek iyi uygulamadır:
`Copyright (c) 2026 Seiya Funaoka` **+** `Copyright (c) 2026 Salvetum (SSC On Windows fork)`.

---

## Bulgu 4 — Otomatik koruma / CI yok (yüksek)

- `.github/` dizini **yok** → CI, PR check, issue/PR şablonu, release workflow yok.
- pre-commit hook yok → proprietary binary commit'i teknik olarak engellenmiyor.
- `.gitignore`'da `*.so` / `libScalable_Encoder*` / blob imzası deseni yok.

---

## Faz önceliği sonucu

Faz 0'ın amacı "blob committed ise Faz 6a'nın acil temizlik maddesi öncelik kazanır"
demekti. Sonuç:

1. **Blob committed.** → Faz 6a / Faz 1 öncelik kazandı.
2. **README yanlış beyanı** bulgu 1 ile bağımsız bir düzeltme gerektiriyor
   (blob kalsa bile ifade yanlış; ya blob çıkarılmalı ya README düzeltilmeli).
3. Kullanıcı kararı: blob **şimdilik repoda kalsın**. Bu durumda minimum doğru
   adım: README'yi gerçeğe uydur + LICENSE/atıf tutarsızlıklarını düzelt + `.gitignore`
   ve CI korumasını ekle (blob'u ileride çıkarmayı kolaylaştırır).
4. Faz 6b (clean-room) bu denetimden **etkilenmiyor**; uzun vadeli Ar-Ge olarak kalır.

---

## Kanıt komutları (tekrar üretilebilir)

```powershell
git log --all --full-history --source --remotes --oneline -- "*.so" "*Scalable_Encoder*"
git log --all --full-history --oneline -- "*.bin" "*.fw"
git ls-files | Select-String -Pattern "\.so$|Scalable_Encoder|rtl.*\.bin$"
git submodule status
git ls-files tools | Select-String -Pattern "\.so$|helper$|sscenc-blob-run$"
Get-Content extern\btstack\LICENSE -TotalCount 25
Get-Content extern\fdk-aac\NOTICE -TotalCount 30
Test-Path .github
```
