# SSC On Windows — UI / çıktı stili sözleşmesi (kural kitabı)

Bu dosya `docs/dev/PLAN_DESIGN_CONSISTENCY.md` (Faz D) kapsamında, tüm kullanıcı
yüzeylerinde **aynı görsel dili** korumak için yazılmış referanstır. Gelecekteki
her değişiklik bu sözleşmeye uymalıdır.

Kapsam: CLI, WinUI 3 GUI, `setup.ps1` kurulum betiği. wxWidgets GUI, kaynak
kodu `app/src/theme_manager.h` içindeki dokümante edilmiş renk eşlemesiyle
"servis/legacy" yüzeydir — birebir kopya değil, aynı anlam dilini kullanır.

## 1. Etiket dili (tek çıktı dili)

Beş etiket her yüzeyde aynı sözcük + renk semantiğini taşır (renk yalnızca TTY'de):

| Etiket  | Renk    | Anlam                                             | Örnek yerler                          |
|---------|---------|---------------------------------------------------|---------------------------------------|
| `INFO`  | varsayılan | düz bilgi, ilerleme, adım                        | banner, Stage adımları (`Step N: ...`) |
| `OK`    | yeşil   | başarılı sonuç                                    | "Done.", "Setup complete", bağlantı sonu |
| `WARN`  | sarı/amber | olumsuz olmayan uyarı, düşük değer / eşik       | "no devices found", `rate<48000`, `Fix:` ipucu |
| `ERROR` | kırmızı | başarısızlık, koşul ihlali                        | `Failed to ...`, `Die`, sayaç `fail>0` |
| `DATA`  | camgöbeği (cyan) | diyagnostik/ölçüm satırı                  | `CAP:`, `SSC:`, `BTstack:`, `CB:`, `[State]`, `[DriverMode]` |

Test budur: `python tools\ui_style_check.py check --all --exe <exe>` (bkz. 5).

## 2. Metrik formatı (tek metrik dili)

- Birimler sabit kısaltmayla: `kbps`, `ms`, `%`, `fs` (başına boşluk yok).
- Ondalık ayracı nokta; yalnızca gereken basamak (ör. `2.50 ms`).
- Gizli padding/boşluk eklenmez; CLI satırı ile WinUI Live Stats **aynı değeri
  aynı birimle** gösterir (ör. CLI `error 0.59%` ↔ GUI `Error rate 0.59%`).
- Sparkline eksen etiketleri de aynı birimi taşır (`Latency (0-25 ms)`).

## 3. Tipografi

- **Yüzler (kurulum dayatması yok):** kullanıcıya font "yükletilmez"; sistemde
  yoksa aile falback listesi kullanılır.
  - Sans: `Segoe UI Variable Text` → `Segoe UI`.
  - Mono (log/sayaç): `Cascadia Mono` → `Consolas`.
- **Ölçek:** title/body/caption ayrımı WinUI'de `App.xaml` stillerinde
  tanımlıdır (`SectionLabelTextStyle` 13, `CaptionGrayTextStyle` 12,
  `HintGrayTextStyle` 11); XAML içinde rastgele `FontSize` değme yok (kural 5).
- **Sayılar:** istatistik değerleri `Typography.NumeralAlignment="Tabular"`
  kullanır (yanıp sönme yok). CLI'da bu, sabit genişlikte glif üretilerek sağlanır.
- Mono log satırları `MonoLogTextStyle` (12px / line-height 16) kullanır.

## 4. Renk tokenları ve boşluk

- **Severity renkleri** tema değişmez (`TagOkBrush` #3FB950, `TagWarnBrush`
  #D29922, `TagErrorBrush` #F85149, `TagDataBrush` #39B9D2) — yukarıdaki tabloyla
  birebir. Atama yalnızca `ThemeResource`/`StaticResource` üzerinden.
- **Metin-tonu** renkleri tema değişir (`SubtleTextBrush`, `StatusTextBrush`).
- **Boşluk:** 4pt ızgarası — marjin/padding 4'ün katıdır (ör. `Padding="12,4"`).
- WinUI görünümü: Mica (Win11) korunur; tipografi/renk tema sözlüklerinden gelir.

## 5. Kural kontrolleri (Faz D)

Her Faz D değişikliği `python tools\ui_style_check.py check --all --exe <exe>`
ile doğrulanır (çıktı 0 = PASS):

- **cli:** çıktı bir pipe'a yazılırken (TTY değilse) asla `\x1b` içermez;
  `--no-color` da aynı garantiyi verir.
- **xaml:** `winui3/*.xaml` içinde el yazısı inline
  `Foreground=`/`FontSize=`/`FontFamily=` değeri yok (yalnızca `{...}` kaynakları).
- **setup:** `setup.ps1` `Stage`/`Die`/bitiş satırları `[INFO]  Step N:` /
  `[ERROR]` / `[WARN]  Fix:` / `[ OK ]` etiketlerini kullanır.

## 6. Kapsam dışı (bilinçli)

- Marka/logotip araştırması, komple yeniden tasarım.
- Kullanıcıya font kurulumu dayatmak.
- wx veya WinUI'den birini bırakmak; wx yalnızca "servis/legacy" modu.
- Terminal fontunu değiştirmek (kullanıcının terminali kullanıcının seçimidir).