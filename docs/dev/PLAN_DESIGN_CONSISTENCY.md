# SSCOnWindows — Tasarım Tutarlılığı ve Konsol Okunabilirliği Planı

> Bu dosya bir **opencode plan promptu**dur. Agent'a bu dosyayı referans göstererek
> "PLAN_DESIGN_CONSISTENCY.md'deki Faz C'yi uygula" şeklinde görev verebilirsin.
> Fazlar bağımsız commit'ler olacak şekilde tasarlandı; CLI, GUI ve setup.ps1'i
> ortak bir görsel/stil dilinde birleştirir. **Kapsam dışı:** marka/logo arayışı ve
> tam yeniden tasarım — amaç tutarlılık, sıfırdan tasarım değil.

## Bağlam / motivasyon

Uzun soak testi (2026-09-19, ~3.5 saat, SSC 229k / WSL2 daemon) çalışma tarafının
sağlıklı olduğunu gösterdi: latency 1–2.5 ms, error %0.59 (uyku/uzaklaşma anlarına
kümeli), ses kalitesi "olağanüstü". Bu faz **işlevselliği değil, algıyı** iyileştirir:
şu an CLI diyagnostikleri ham `printf` akışı (ANSI renk yok, hizalama yok), wx
arayüzü ve WinUI 3 GUI ile setup.ps1 farklı dil/düzenler konuşuyor. Kullanıcı
deneyimi bütünlüğü için üç yüzey aynı "stil kurallarını" paylaşmalı.

Ön denetim bulguları (uygulama öncesi tespit):
- `app/src/main.cpp`: 79 `printf`/`fprintf`; **hiç ANSI/renk yok**; tüm diyag satırları
  stderr'e gidiyor, hizalama elle ama tutarsız (CAP/SSC/BTstack/CB satırları).
- `setup.ps1` zaten `ForegroundColor` kullanmaya başladı (Stage/`Die`) — CLI ile aynı
  dilde değil.
- wx GUI'de `theme_manager` var; WinUI 3 Mica + XAML-only görünüm kullanıyor; iki
  GUI'nin tipografi/renk token'ları birbirinden bağımsız.
- Sağlık hedef değerleri AGENTS.md tablosunda; çıktı bunları görsel olarak
  vurgulamıyor (iyi=yeşil, dikkat=sarı, kötü=kırmızı yok).

---

## Durum

| Faz | Konu | Durum |
|-----|------|-------|
| A | CLI konsol okunabilirliği | ✅ `app/src/console_style.h` + `main.cpp`/`ssc_encoder.cpp`/`btstack_transport.cpp`; `--no-color`/`NO_COLOR`/TTY; build OK |
| B | Yazı (tipografi) politikası | ✅ WinUI: log paneli `Cascadia Mono,Consolas` 12px/1.35 satır arası; istatistik + değer alanlarında `Typography.NumeralAlignment="Tabular"`; font seti XAML başına komut bloğu olarak yazıldı; WinUI build OK |
| C | GUI (WinUI + wx) tutarlılığı | ⏳ |
| D | Referans ve doğrulama (`docs/ui-style.md`) | ⏳ |

## Genel kural tanımı (tüm fazlara ortak)

1. **Tek çıktı dili:** `INFO`, `OK`, `WARN`, `ERROR`, `DATA` (diyagnostik) etiketleri
   CLI + WinUI log paneline + setup.ps1'de aynı önek/renk semantiğiyle kullanılır.
2. **İki stream kuralı:** kullanıcıya yönelik mesaj stdout, diyag/log stderr. `--quiet`
   ve pipe'ta (TTY değilse) ANSI renk otomatik kapanır.
3. **Tek metrik formatı:** birimler tutarlı (`kbps`, `ms`, `%`, `fs`), ondalık aynı
   (`2.50 ms`), gizli padding yok. CLI satırı ile GUI`Live Stats` aynı değeri aynı
   birimle gösterir.
4. **Terminal'e font dayatılmaz** (kullanıcının terminali kullanıcının seçimi); mono
   tutarlılık, tek genişlikte glif (box drawing uyumlu) kullanımıyla sağlanır.

---

## Faz A — CLI konsol okunabilirliği

- ANSI desteği: `ENABLE_VIRTUAL_TERMINAL_PROCESSING` (Windows) ile aç; `NO_COLOR`
   ortam değişkeni + TTY kontrolü (piped ise renksiz). `main.cpp`'ye küçük
  `termcolor`-benzeri yardımcı (bağımlılık eklemeden, ~60 satır).
- **Etiket/renk semantiği:** `INFO`=default, `OK`=yeşil, `WARN`=sarı, `ERROR`=kırmızı,
  `DATA`=camgöbeği (diag satırları).
- **Sağlık satırları hizalı tablo formatına:** sabit genişlikli sütunlar
  (`CAP:`/`SSC:`/`BTstack:`/`CB:` aynı hizada), metrik=değer/birim, bozuk değer renkli
  vurgulanır (ör. `rate=<48000` → kırmızı). Yeni format, elle parse eden
  `tools/golden` gibi araçları kırmamalı (düzen değişir, anahtar sözcükler sabit kalır).
- İnteraktif banner + tek satırlık durum özeti (`#1 scan`, `#2 connect`, ...).
- postlude: `--no-color` bayrağı; `--quiet` zaten var.

## Faz B — Yazı (tipografi) politikası

- **Kabul edilen font seti (sistem dağıtımı, repo'ya font gömülmez):**
  - UI/sans: `Segoe UI Variable Text` (Windows 11) / `Segoe UI` (fallback).
  - Mono/diyag: `Cascadia Mono` (Windows Terminal ile gelen, OFL) / `Consolas` (fallback).
  - Her iki GUI + CLI dokümanlarında bu set kullanılır.
- **Rakam hizalaması (tabular):** istatistik alanları `InsideTextBlock`
  `Typography`/`FontFeatures` (WinUI: tabular figures) ile `1.00/12.50/202.0` hizalı;
  CLI'da tek genişlikli glyph'lerle aynı etki.
- **Log paneli (GUI):** mono font (Cascadia Mono/Consolas), 12–13px, satır arası 1.35.
- CLI kendi fontunu DEĞİŞTİRMEZ; sadece glif genişliği tutarlı seçilir.

## Faz C — GUI (WinUI 3 + wx) tutarlılığı

- **WinUI ResourceDictionary:** tipografi ölçeği (DisplayTitle/Title/Body/Caption),
  renk **ThemeDictionaries** (Light/Dark), accent; Mica korunur. Tüm XAML bu
  dictionary'lerden beslenir (inline değerleri azalt).
- **Spacing:** 4pt grid (`StackPanel`/`Grid` kenar boşlukları 4'ün katı).
- **Kontrol stilleri:** Button/ComboBox/Slider/Volume için ortak stiller; aynı
  yükseklik, min-width, hover/disabled durumları.
- **Live Stats paneli:** CLI diyag satırlarıyla **aynı birim/önek**; ör. CLI
  `error 0.59%` ↔ GUI `Error rate 0.59%`; sparkline eksen etiketleri aynı birim.
- **Log pane render'ı:** CLI `DATA` satırlarını taklit eden önek/renk renk kodları.
- **wx tarafı:** `theme_manager` üzerinden aynı renk/önem semantiği (WinUI'ye
  birebir kopya değil, tutarlı dil) — wx "legacy/servis modu" olarak kalır; CLI ve
  WinUI birincil yüzeydir. (Aşağıdaki 6.2 notuna bak.)

## Faz D — Referans ve doğrulama

- `docs/ui-style.md`: renk/etiket/font/boşluk sözleşmesi (yukarıdaki "Genel kural"
  bölümünün genişletilmiş hali) — gelecek işlerin kural kitabı.
- **Kural kontrolleri:**
  - CLI: rehinli `--no-color` davranışı için küçük auto-test; TTY'siz pipe testi.
  - WinUI: XAML'de inline `Foreground/FontSize` taraması azalsın (grep kuralı).
  - `setup.ps1`: Stage/`Die` çıktısının aynı etiket dilini kullanması.
- **Kapsam dışı (bilinçli):** marka/logotip araştırması, komple yeniden tasarım,
  kullanıcıya font kurulumu dayatma, wx veya WinUI'den birinin bırakılması.

---

## Uygulama sırası önerisi

| Öncelik | Faz | Neden |
|---------|-----|-------|
| 1 | A (CLI) | En düşük risk, en görünür kazanç; diyag okuyucu tek kişi |
| 2 | B (font politikası) | GUI için ön koşul; küçük |
| 3 | C (WinUI) | En uzun; B'den sonra |
| 4 | D (referans/doğrulama) | Kapanış |

> Not: wx (Faz C kapsamında "servis/legacy") güncellemesi opsiyoneldir ve WinUI 3
> birincil GUI olarak kaldığı sürece düşük öncelikli tutulur. CLI + WinUI birlikte
> "birincil yüzey" sayılır ve tutarlılık hedefi oradadır.

## Keşif maddesi — Kulaklık şarj göstergesi (çok düşük öncelik)

**Durum:** açık / araştırma. **Öncelik: çok düşük** — bu planın (ve genel
sıralamanın) en altında; yalnızca yol/güç kullanılmamış bir an omuza alındığında
yapılır. Kullanıcı sadece merak; ANC gösterimi bilinçli olarak süphe dışıdır
(vendor-GATT yazma komutları + Buds3 FE'de oturum kriptosu → durumun salt-okunur
öğrenilmesi pratik değil).

- **Hedef:** stream sırasında Buds3 FE şarj seviyesinin WinUI durum satırında
  görüntülenmesi. Sesin kendisine dokunmaz (A2DP korunur).
- **Yöntem (doğrulama adımı):** BTstack üzerinde ayrı bir **LE GATT bağlantısı** açan
  küçük bir keşif modu (`--probe` benzeri). GATT servis dökümüyle karar verilir:
  - `0x180F` (standart Battery Service) görünürse → okuyup WinUI'de göstermek
    **küçük bir iş** (servis taraması + okuma + UI ama bütçe küçük).
  - Battery yalnızca **vendor UUID'lerde** ise → RE gerekir; bu durumda madde
    kapatılır, token bulunamadığı için yapılmaz.
- **Risk/bağımlılık:** LE yan bağlantısının mevcut BR/EDR stream'ini bozmaması
  (BTstack EHCI'de ikisini de yönetebilir; soak testinde LE denenmedi → önce küçük
  bir kanıt oturumu gerekir).
- **Kapsam dışı:** ANC modu gösterimi/değişimi; kasa şarjı; telefon bağlantı
  durumu gibi "diğer kulaklık telemetrisi".

Bu maddenin yapılması ayrıca kararlanır; öncelikli işlerde (Faz A-D, profesyonelleşme
Faz 4+, robustness) yol alındıkça üst kapağı açılır.

## opencode için kullanım notu

```
opencode: PLAN_DESIGN_CONSISTENCY.md dosyasındaki "Faz A" bölümünü uygula;
CLI çıktısına ANSI renk + hizalı diyag formatını getir, anahtar sözcükleri
(değişmez) koru, TTY/NO_COLOR davranışını test et.
```

Her faz ayrı commit; Faz A'dan başlanması önerilir. Soak testinin uzaklaşma
kesintileri bilinçli olarak kapsam dışıdır — dongle sınıfının beklentisidir, tasarım
planının konusu değildir.