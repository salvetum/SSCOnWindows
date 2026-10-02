# SSCOnWindows — Ürün Tasarımı ve Kullanıcı Deneyimi Planı

## Hedef

Arayüz, Bluetooth hakkında teknik bilgisi olmayan bir kullanıcının da şu üç
soruyu ilk bakışta cevaplayabilmesini sağlamalıdır:

1. Hangi cihaz bağlı?
2. Ses şu anda çalışıyor mu?
3. Bir sorun varsa ne yapmalıyım?

Görsel dil daha expressive ve modern olacak; ancak tanılayıcı bilgiler,
konsol çıktısı ve ayarlar işlevsel sadeliğini koruyacak. WinUI birincil kullanıcı
yüzeyi, CLI ise teknik tanılama yüzeyi olarak kalır. Ortak renk, etiket ve metrik
sözleşmesi için `PLAN_DESIGN_CONSISTENCY.md` referans alınır.

## Tasarım ilkeleri

- **Durum önce:** Bağlantı durumu, cihaz adı, codec, gecikme ve hata oranı ana
	ekranda görünür; kullanıcı bilgiyi menülerde aramaz.
- **Teknik terim + açıklama:** `A2DP`, `SSC` veya `UHQ` gibi terimlerin yanında
	kısa ve kullanıcı dostu açıklama gösterilir. Ham hata yerine çözüm önerisi
	verilir.
- **Güvenilir geri bildirim:** Her işlem bekleme, başarılı, uyarı ve hata
	durumlarını açıkça gösterir. Renk tek başına anlam taşımaz; simge ve metin de
	kullanılır.
- **Erişilebilirlik:** Klavye ile gezinme, yeterli kontrast, ekran okuyucu için
	anlamlı adlar ve azaltılmış hareket tercihi desteklenir.
- **Tutarlılık:** WinUI, wx arayüzü, konsol ve kurulum ekranları aynı durum
	semantiğini kullanır. Renk tokenları ve metrik birimleri tekrarlanmaz.

## Kapsam

### Faz 1 — Bilgi mimarisi ve durum ekranı

- Ana ekranı `Bağlı cihaz`, `Ses durumu`, `Canlı istatistikler` ve `Sorun giderme`
	bölümlerine ayır.
- Bağlantı durumunu metin, ikon ve kısa açıklamayla göster: bağlı, bağlanıyor,
	bağlantı kesildi, desteklenmiyor.
- Konsol/log panelini açıp kapatılabilir yap. Panel kapalıyken son durum özeti
	görünür kalmalı; panel açıldığında önceki kayıtlar kaybolmamalı.
- Birincil eylemleri görünür tut: yeniden bağlan, taramayı yenile, ayarları aç.

**Kabul kriterleri:** Kullanıcı cihaz adını ve bağlantı durumunu ana ekranda
tek bakışta görür; bağlantı kesildiğinde hangi eylemi yapacağı açıkça yazılır;
log paneli açılıp kapatıldığında pencere düzeni bozulmaz.

### Faz 2 — Cihaz bilgisi ve canlı sağlık göstergeleri

- `Devices` bölümünde cihaz adı, model/ürün görseli, pil seviyesi ve şarj durumu
	gösterilir. Görsel bulunamazsa nötr bir cihaz simgesi kullanılır.
- Bağlantı kalitesi ölçülebilir bir veri varsa gösterilir; veri yoksa tahmin
	üretilmez ve alan gizlenir veya `Bilinmiyor` olarak işaretlenir.
- Codec, örnekleme hızı, bitrate, latency ve error rate değerlerini ortak birim
	ve biçimle göster. Değerlerin yanında kısa sağlık durumu etiketi kullan.
- Pil ve bağlantı kalitesi gibi dinamik değerler eski veriyi yeniymiş gibi
	göstermemeli; güncellenme zamanı veya belirsizlik durumu belirtilmelidir.

**Kabul kriterleri:** Gerçek veri yokken sahte pil/kalite değeri gösterilmez;
cihaz görseli yüklenemese de düzen bozulmaz; CLI ve GUI aynı metriği aynı birimle
gösterir.

### Faz 3 — Görsel dil ve etkileşim

- Light/Dark temalar için merkezi renk paleti ve durum tokenları tanımla:
	normal, başarılı, uyarı, hata ve bilgi.
- Windows 11 uyumlu Mica/saydamlık efektini desteklenen yüzeylerde kullan;
	okunabilirlik ve düşük donanım için kapatma/fallback davranışı ekle.
- Tekrarlanan işlevlerde anlamlı simgeler kullan; her simgenin erişilebilir adı
	ve gerektiğinde araç ipucu olsun.
- Animasyonları işlevsel geri bildirim için sınırla: sayfa geçişi, bağlantı
	durumu değişimi ve yükleme. Sürekli dikkat dağıtan animasyon ekleme.
- `prefers-reduced-motion` veya Windows hareket azaltma tercihi etkinse geçişleri
	azalt veya kapat.

**Kabul kriterleri:** Renkler tema değişiminde okunabilir kalır; Mica
desteklenmeyen ortamda opak yüzeyle çalışır; animasyon kapatıldığında bilgi ve
işlev kaybolmaz.

### Faz 4 — Yerelleştirme ve metin kalitesi

- Türkçe ve İngilizce kaynak metinleri ayrı localization kaynaklarında tut.
- Teknik terimler için küçük bir sözlük oluştur; aynı kavram her ekranda aynı
	şekilde adlandırılsın.
- Metinleri pencere genişliği, uzun cihaz adı ve hata mesajı taşmalarına karşı
	test et. Tarih, sayı ve yüzde biçimleri yerel ayarlara uygun olsun.
- Kullanıcıya görünen metinleri kod içine sabitleme; eksik çeviride güvenli bir
	İngilizce fallback kullan.

**Kabul kriterleri:** Dil değişimi uygulamayı yeniden kurmayı gerektirmez;
Türkçe ve İngilizce ana akışlar taşma olmadan tamamlanır; eksik çeviri anahtarları
geliştirme sırasında tespit edilir.

### Faz 5 — Marka karakteri (opsiyonel keşif)

- Maskot ancak ürünün temel akışları tamamlandıktan sonra araştırılır.
- Maskot, bağlantı veya hata bilgisinin yerine geçmez; yalnızca boş durumlarda
	ve onboarding gibi uygun noktalarda kullanılabilir.
- Marka çalışması başlamadan önce kullanım amacı, ton ve erişilebilirlik kuralları
	yazılı hale getirilir.

## Uygulama sırası

| Öncelik | Faz | Gerekçe |
|---|---|---|
| 1 | Bilgi mimarisi ve durum ekranı | Kullanıcının temel sorularını çözer |
| 2 | Cihaz bilgisi ve sağlık göstergeleri | Bağlantıyı görünür ve ölçülebilir yapar |
| 3 | Görsel dil ve etkileşim | Tutarlı ve modern yüzeyi oluşturur |
| 4 | Yerelleştirme | Metinler ve ekranlar oturduktan sonra daha düşük risklidir |
| 5 | Marka karakteri | Ürün değerini destekleyen, zorunlu olmayan çalışma |

## Doğrulama

- Bağlı, bağlanıyor, bağlantısı kesilmiş ve veri bulunamayan cihaz durumlarını
	manuel veya otomatik kontrol et.
- WinUI'yi küçük ve geniş pencere, Light/Dark tema ve Mica kapalı senaryolarında
	kontrol et.
- Türkçe/İngilizce metin taşmalarını ve klavye erişimini kontrol et.
- Azaltılmış hareket tercihi açıkken animasyonların gerçekten azaltıldığını
	doğrula.
- Tasarım değişikliklerini `docs/ui-style.md` içindeki token ve etiket
	sözleşmesiyle karşılaştır.