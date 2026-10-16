# OSD/OS performans ve mantık incelemesi

İnceleme tarihi: 16 Ekim 2026  
İncelenen sürüm: `b9c8eff`  
Hedef donanım: Raspberry Pi 4 ve Raspberry Pi 5 (8 GB), CRT televizyon ve arcade kabini

## Kısa sonuç

Projenin temel mimarisi hedef donanıma uygun. Arayüz ile video/emülatör işini ayırması, videoyu `mpv`'ye, arcade oyunlarını MAME'e devretmesi ve Pi 4/Pi 5 için ayrı görüntü profilleri kullanması doğru kararlar. DRM/VT devri, atomik ayar yazımı, oyun kumandası/JAMMA desteği ve oynatma sırasında efektlerin durdurulması özellikle güçlü.

En önemli performans riski RAM değil; ana iş parçacığında yapılan büyük katalog işlemleri, yavaş SD/USB depolama ve tam ekran shader maliyeti. 8 GB RAM her iki kart için de fazlasıyla yeterli olsa da daha fazla RAM, tek çekirdek üzerinde JSON ayrıştırma/sıralama veya exFAT dizin tarama gecikmesini çözmez. Pi 5 bu yükleri Pi 4'ten daha rahat taşır; yine de büyük Plex/Jellyfin/Emby ve tam MAME koleksiyonlarında aynı mimari sınırlar geçerlidir.

Statik incelemede doğrudan veri kaybına veya sürekli çökmeye yol açan kritik bir hata görülmedi. Aşağıdaki ilk üç başlık öncelikli iyileştirme alanlarıdır:

1. Büyük medya kütüphanelerini sayfalama/lazy model ile yüklemek.
2. Arcade klasör taraması, `games.json` işleme ve katalog indeksini GUI iş parçacığından çıkarmak.
3. Plex sezon açılımındaki sınırsız paralel istekleri kaldırmak.

> Bu rapor bir statik kod incelemesidir. Pi 4/Pi 5 üzerinde FPS, sıcaklık, güç tüketimi veya oyun başına MAME performansı ölçülmedi. Ölçülmeyen noktalar aşağıda “ölçüm/test açığı” olarak belirtilmiştir.

## Öncelikli bulgular

### 1. Büyük medya kütüphaneleri tek seferde belleğe ve QML modeline alınıyor

**Önem:** Yüksek  
**Tür:** Doğrulanmış performans/mimari riski

- Jellyfin tüm sonuçları limitsiz istiyor, JSON'u ana iş parçacığında ayrıştırıp tamamını `QVariantList` yapıyor: `src/modules/jellyfin/JellyfinBackend.cpp:668-705`.
- Emby aynı yaklaşımı kullanıyor: `src/modules/emby/EmbyBackend.cpp:845-862`.
- Plex `/library/sections/{id}/all` sonucunun tamamını işliyor: `src/modules/plex/PlexBackend.cpp:1536-1553`.
- Sonuç daha sonra QML tarafında topluca atanıyor ve harf indeksi kuruluyor: `modules/jellyfin/views/Items.qml:79-84`, `modules/plex/views/Items.qml:172-179`.

Binlerce filmlik/dizilik bir arşivde ağ yanıtından sonra JSON ayrıştırma, nesne kopyalama, sıralama ve QML model yenileme aynı anda gerçekleşebilir. Bu durum özellikle Pi 4'te birkaç saniyelik “donmuş menü” hissi yaratabilir. Pi 5 süreyi azaltır fakat ana iş parçacığının bloklanmasını ortadan kaldırmaz.

**Öneri:** Sunucu tarafı sayfalama veya harf dilimleri kullanmak; uzun vadede satırları ihtiyaç oldukça sağlayan bir `QAbstractListModel` kurmak. Harf ile atlama korunacaksa ilk ekran hemen gösterilip kalan dilimler arka planda alınabilir.

### 2. Arcade koleksiyonunun önemli işleri GUI iş parçacığında

**Önem:** Yüksek (tam MAME seti ve yavaş USB/SD için)  
**Tür:** Doğrulanmış performans riski

- ROM klasörü senkron dolaşılıyor ve bazı dosyalar için ek varlık kontrolü yapılıyor: `src/modules/arcade/ArcadeBackend.cpp:317-371`.
- `games.json` dosyasının tamamı okunup ayrıştırılıyor ve kayıt sırasında tamamı yeniden oluşturuluyor: `src/modules/arcade/ArcadeBackend.cpp:462-496`.
- Tüm oyunların adı sıralanıyor; tür, oyuncu sayısı, yıl ve yapımcı indeksleri tek seferde kuruluyor: `src/modules/arcade/ArcadeBackend.cpp:632-665`.
- Bunlar `refresh()`/`prepare()` ve filtrelerin ilk kullanımı üzerinden GUI iş parçacığında çalışıyor: `src/modules/arcade/ArcadeBackend.cpp:389-421`.

Tam bir MAME seti on binlerce girdiye ulaşabilir. exFAT USB disklerde dizin ve “dosya var mı?” sorguları da pahalıdır. Arcade ekranının ilk açılışında veya yeniden taramada giriş gecikmesi oluşabilir.

**Öneri:** Dizin taraması ve indeks üretimini worker üzerinde yapmak, hazır sonucu ana iş parçacığına tek seferde taşımak. Önce ucuz bir oyun listesi gösterilip yanlış ad kontrolü ve ayrıntı indeksi kademeli tamamlanabilir. `games.json` kaydı da tarama grupları arasında değil, debounce edilmiş tek işlem olarak tutulmalıdır.

### 3. Plex sezonları sınırsız paralel HTTP isteğine dönüşebiliyor

**Önem:** Yüksek  
**Tür:** Doğrulanmış mantık/performans riski

`flattenSeasons()` listedeki her sezon için ayrı `/children` isteğini hemen başlatıyor: `src/modules/plex/PlexBackend.cpp:819-859`. Bu işlev hem hub listelerinde hem de tüm kütüphane yüklemesinde çağrılıyor: `src/modules/plex/PlexBackend.cpp:1517-1553`.

Sezon sayısı yüksek bir TV arşivi çok sayıda eşzamanlı istek, çoklu JSON ayrıştırma ve sonuç kopyalama üretebilir. Kullanıcı ancak son istek bitince tam sonucu görür. Wi-Fi, Plex sunucusu ve Pi aynı anda gereksiz yük altında kalır.

**Öneri:** Kütüphane taramasında sezonları episode listesine açmamak; yalnızca kullanıcı bir sezonu veya “Tümünü Oynat”ı seçtiğinde genişletmek. Genişletme şartsa eşzamanlı istek sayısını 4-8 ile sınırlamak ve sonuçları kademeli sunmak.

### 4. Arcade ekran ayarı bozuk/elle değiştirilmiş kayda karşı korunmuyor

**Önem:** Orta  
**Tür:** Doğrulanmış dayanıklılık hatası

Normal ayar ekranı minimum alanı koruyor: `modules/arcade/views/ArcadeScreen.qml:146-169`. Ancak kaydedilmiş `screen_adjustment` değerleri başlangıçta doğrulanmadan yükleniyor: `modules/arcade/views/Root.qml:205-210`. Aşırı bir değer liste alanını küçültebilir; `perPage` sıfır veya bir olduğunda `pitch` hesabındaki `(perPage - 1)` bölümü geçersiz sonuç üretebilir: `modules/arcade/views/ArcadeScreen.qml:70-85`.

Bu, normal tuş kullanımıyla kolayca oluşmuyor; yarım kalmış eski bir ayar, elle düzenlenmiş `config.json` veya gelecekteki ayar biçimi değişiklikleri için gerçek bir sağlamlık açığıdır.

**Öneri:** Kayıt yüklenirken dört kenarı minimum 600×420 alanı koruyacak biçimde clamp etmek ve `perPage <= 1` için güvenli bir `pitch` değeri kullanmak.

### 5. Arcade görseli aranırken seçim değişimi GUI'de disk erişimi yapıyor

**Önem:** Orta  
**Tür:** Doğrulanmış performans riski

Her yeni oyun seçimi `gameDetails()` çağırıyor: `modules/arcade/views/ArcadeScreen.qml:130-133`. İlk ziyarette `pictureOf()` PNG/JPG varlık kontrolleri ve ekran görüntüsü klasörü listelemesi yapıyor: `src/modules/arcade/ArcadeBackend.cpp:776-831`. Sonuçlar, bulunamayan görseller dahil, oturum içinde önbelleğe alındığı için ikinci ziyaret ucuzdur.

Yavaş exFAT/USB ortamında ilk kez hızlı gezinirken her başlık kısa bir takılma yaratabilir.

**Öneri:** `snap` klasörünü bir kez indekslemek veya görünür sayfadaki görselleri worker üzerinde önceden hazırlamak. Mevcut negatif sonuç önbelleği korunmalıdır.

### 6. Asenkron liste yanıtları istek kimliği taşımıyor

**Önem:** Orta  
**Tür:** Mantık ve gereksiz iş riski

Jellyfin ve Emby `itemsLoaded` yanıtını yalnızca ekran moduna göre kabul ediyor; hangi `parentId` isteğine ait olduğunu denetlemiyor: `modules/jellyfin/views/Items.qml:79-84`, `modules/emby/views/Items.qml:79-84`. Plex tarafında da bölüm/hub kimliğiyle korelasyon yok: `modules/plex/views/Items.qml:172-179`.

Kullanıcı hızlı gezinirse eski ve büyük bir yanıt yeni ekran açıldıktan sonra modeli tekrar kurabilir veya kısa süre yanlış listeyi gösterebilir.

**Öneri:** Her yüklemeye artan bir istek kimliği eklemek, sinyalde bu kimliği geri döndürmek ve eski yanıtları yok saymak. Aynı kategoride yeni istek başladığında önceki `QNetworkReply` mümkünse iptal edilmelidir.

### 7. YouTube abonelik yenilemesi bütün kanalları aynı anda çağırıyor

**Önem:** Orta  
**Tür:** Ölçeğe bağlı performans riski

15 dakikalık önbellek eskidiğinde bütün kanallar `refreshChannel()` ile aynı anda başlatılıyor: `src/modules/youtube/YouTubeBackend.cpp:160-190`. Her tamamlanma RSS XML'ini ana iş parçacığında ayrıştırıyor: `src/modules/youtube/YouTubeBackend.cpp:264-293`.

Az sayıda abonelikte sorun beklenmez. Yüzlerce kanalda Pi 4 Wi-Fi ve CPU üzerinde kısa süreli yoğunluk oluşur.

**Öneri:** 3-5 eşzamanlı istekli kuyruk, kademeli `listingReady` ve görünür kanalın önceliklendirilmesi.

### 8. Ayar dosyası oynatma başlangıcında tekrar tekrar okunuyor

**Önem:** Orta-düşük  
**Tür:** Doğrulanmış I/O verimsizliği

`AppCore::get_setting()` her çağrıda `config.json` dosyasını okuyup ayrıştırıyor: `src/AppCore.cpp:58-71`, `src/AppCore.cpp:164-181`. `MpvController` bir oturumun ölçekleme, crop, altyazı, arka plan ve video profil ayarları için bu işlevi birden fazla kez çağırıyor; örnekler `src/player/MpvController.cpp:1018-1028` ve `src/player/MpvController.cpp:1100-1138`.

Dosya küçükken etkisi sınırlıdır; yine de her video başlangıcında tekrarlanan SD erişimi gereksizdir.

**Öneri:** Bellekte yapılandırma kopyası tutmak; `save_setting()` sonrası güncellemek ve kullanıcının dosyayı elle değiştirebilmesi isteniyorsa mtime/`QFileSystemWatcher` ile geçersiz kılmak. `mpv_video_args` değerinin sonraki oynatmada anında uygulanması korunmalıdır.

### 9. Local Files önbelleği ekran her açıldığında temizleniyor

**Önem:** Orta-düşük  
**Tür:** Doğrulanmış I/O verimsizliği

`TreeBrowser.rootPath` binding'i her ekran kuruluşunda `clearDirectoryCache()` çağırıyor: `modules/local_files/views/Items.qml:102-109`. Böylece aynı USB/NAS dizinlerine dönüşte `AsyncDirectoryCache` yeniden disk/ağ erişimi yapıyor.

**Öneri:** Önbelleği yalnızca medya kökü değişince, sürücü takılıp çıkarılınca veya kullanıcı açıkça yenileyince temizlemek.

### 10. TreeBrowser oturum önbelleği sınırsız büyüyebiliyor

**Önem:** Orta-düşük  
**Tür:** Uzun oturuma bağlı bellek riski

Her ziyaret edilen yol `listings` JavaScript nesnesinde saklanıyor: `views/Components/TreeBrowser.qml:172-180`, `views/Components/TreeBrowser.qml:222-247`. Derin yerel klasörlerde ve çok sayfalı çevrim içi kataloglarda modül kapanana kadar büyüyebilir.

8 GB RAM nedeniyle kısa oturumlarda pratik sorun beklenmez; ancak günlerce açık kalan bir arcade kabininde sınırsız büyüyen yapı tercih edilmemelidir.

**Öneri:** Yol sayısı veya tahmini satır sayısına göre LRU sınırı; aktif gezinme izi ve yakın dallar korunup eski yollar atılabilir.

### 11. Arcade teması Pi 4 için pahalı bir varsayılan kombinasyon

**Önem:** Orta (yüksek çözünürlüklü çıkışta)  
**Tür:** Ölçüm gerektiren performans riski

Arcade teması aynı anda `Stars`, `Rainbow` metin, `Rainbow` seçici ve `Cube` geçişini açıyor: `assets/themes/arcade/theme.json:1-11`. Rainbow metin, ekranı tam çözünürlükte bir shader katmanına alıyor: `Main.qml:936-947`. Hareketli efekt zamanı yaklaşık 30 Hz güncelleniyor: `Main.qml:239-250`.

Fiziksel 480i CRT'de piksel sayısı düşük olduğu için maliyet makul olabilir. Pi 4'ün 1080p HDMI üzerinden bir dönüştürücüye veya LCD arcade ekrana bağlandığı durumda tam ekran shader belirgin yük yaratabilir. Oyun/video sırasında efektlerin durdurulması bu riski ciddi biçimde azaltıyor.

**Öneri:** Pi 4 için “Arcade Lite” seçeneği (statik metin + Fade) sunmak veya ilk kurulumda çıkış çözünürlüğüne göre hafif temayı önermek. Varsayılanı değiştirmeden önce gerçek donanımda frame-time ölçmek gerekir.

### 12. Pi 5 composite raster/aspect davranışı yalnızca yorum ve kullanıcı ayarına bırakılmış

**Önem:** Düşük-orta  
**Tür:** Donanım doğrulama açığı

Kod, Pi 5 composite çıkışın bazen 720×480i yerine 704×432 raporladığını açıkça belirtiyor ve çözümü `monitorpixelaspect`/sistem ayarına bırakıyor: `src/player/MpvController.cpp:1055-1063`.

**Öneri:** Pi 5 composite için doğrulanmış PAL/NTSC örnek ayarını belgelerde ayrı bir reçete olarak vermek. Otomatik düzeltme ancak farklı TV/dönüştürücülerde ölçülerek eklenmelidir.

## Pi 4 ve Pi 5 açısından değerlendirme

### Raspberry Pi 4 (8 GB)

- Video için `--vo=drm --hwdec=drm-copy,v4l2m2m-copy` seçimi yapılmış: `src/player/MpvController.cpp:1031-1043`. Kod yorumuna göre düzgün cadence ve crop karşılığında dört çekirdekte yaklaşık %50-70 CPU maliyeti hedefli bir tercihtir.
- 1080p kaynak + Transparent Background + ağır tema shader'ları aynı anda Pi 4'ün sınırına yaklaşabilir. Normal tam ekran oynatmada efektlerin dinlenmesi doğru korumadır.
- Büyük MAME/medya kataloglarında 8 GB RAM'den önce tek çekirdek, JSON kopyaları ve depolama gecikmesi hissedilecektir.
- CRT/arcade kullanımında 480 satırlık çıkış performans avantajıdır. 1080p'de CRT shader uygulayıp sonra analog dönüştürmek gereksiz GPU işi olabilir.

### Raspberry Pi 5 (8 GB)

- Full KMS için `--vo=drm --hwdec=auto-safe` ve seçili DRM device/connector/mode aktarımı kullanılıyor: `src/player/MpvController.cpp:1055-1075`.
- Pi 5 menü shader'larını ve katalog işlemlerini daha rahat yürütür; buna rağmen sınırsız HTTP fan-out ve GUI-thread işlemleri mantıksal olarak aynı kalır.
- 3.5 mm ses çıkışı olmadığı için HDMI veya USB ses kartı akışı önemlidir. MAME başlatılırken kullanılabilir ALSA kartı yoksa `-sound none` verilmesi güvenli davranıştır: `src/modules/arcade/ArcadeBackend.cpp:1180-1183`.
- Composite raster/aspect konusu gerçek Pi 5 + gerçek CRT üzerinde PAL ve NTSC ayrı ayrı doğrulanmalıdır.

## Güçlü tasarım kararları

1. **Doğru iş bölümü:** Arayüz tarama işini yapıyor; video `mpv`, arcade MAME tarafından çalıştırılıyor.
2. **Pi'ye özel oynatma profilleri:** Pi 4 ve Pi 5'in farklı KMS/decode davranışı tek genel ayara zorlanmamış: `src/player/MpvController.cpp:1003-1098`.
3. **DRM/VT devri:** `DisplayHandoff`, harici süreç görüntüyü alırken Qt çizimini ve DRM master durumunu düzenli devrediyor.
4. **Oynatma sırasında efektleri dinlendirme:** Video, MAME veya takeover görünümünde shader/geçiş ve menü müziği duruyor: `Main.qml:225-250`.
5. **Arcade sayfalaması:** QML'ye bütün oyunlar verilmek yerine yalnızca mevcut sayfa alınıyor: `src/modules/arcade/ArcadeBackend.cpp:749-769`.
6. **MAME taramasını oyun sırasında durdurma:** Sonraki `-listxml` grubu MAME oyunu bittiğinde devam ediyor: `src/modules/arcade/ArcadeBackend.cpp:580-588`.
7. **Görsel decode sınırı:** Arcade ekran görüntüsü asenkron yükleniyor ve ekrandaki boyuta göre çözülüyor: `modules/arcade/views/ArcadeScreen.qml:533-536`.
8. **Atomik dosya yazımı:** Ayarlar ve durum dosyaları yarım JSON bırakmayacak biçimde yazılıyor.
9. **Sınırlandırılmış işler:** Yerel arama 200 sonuçla, script konsolu satır/bayt limitleriyle ve asenkron dizin okuma iki eşzamanlı işle sınırlandırılmış.
10. **Giriş mimarisi:** SDL/gamepad/JAMMA girdilerinin merkezi yönetimi QML ekranlarını donanım tipine bağımlı hale getirmiyor.
11. **CRT odaklı düzen:** `root.sh`/`root.sw`, overscan ayarı ve 960×720 tasarım birimleri kullanılıyor.
12. **Mevcut test seviyesi:** Arcade tarama grupları, controller eşlemesi, görüntü çıkışı ve hızlı video değiştirme gibi zor mantıklar için kayda değer regresyon testleri bulunuyor.

## Test ve ölçüm açıkları

Otomatik testler iş mantığında güçlü; ancak gerçek GPU/DRM davranışını taklit etmiyor:

- Pi 4/Pi 5 `mpv` argümanlarını kart modeli bazında doğrulayan birim testi yok.
- `DisplayHandoff` gerçek VT/DRM master geçişi yalnızca fiziksel Pi'de sınanabiliyor.
- QML testleri software backend ile çalıştığı için CRT/Rainbow/Stars shader frame-time maliyetini ölçmüyor.
- Pi 5 composite 704×432/720×480i farkı otomatik doğrulanmıyor.
- Tam MAME seti açılış süresi, snap klasörü gecikmesi ve katalog indeksleme süresi için performans eşiği yok.

`tests/README.md:122-139` zaten iyi bir fiziksel Pi kontrol listesi içeriyor. Buna aşağıdaki ölçümler eklenebilir:

| Senaryo | Ölçülecek değer | Önerilen kabul ölçütü |
|---|---|---|
| Arcade ilk açılış, tam ROM seti | Tuştan ilk çizilebilir listeye süre | Pi 4'te kullanıcıya ilerleme göstergesi; menü girdisi kaybolmamalı |
| 100 hızlı oyun seçimi | En kötü frame süresi ve disk bekleme | Gözle görülür seçim atlaması olmamalı |
| Arcade ve hafif tema, 480i/1080p | Ortalama ve %99 frame süresi | Çıkış yenileme süresini düzenli kaçırmamalı |
| Pi 4, 1080p H.264/HEVC | CPU, dropped frame, sıcaklık | 30 dakika sonunda sürekli dropped frame olmamalı |
| Pi 5 composite PAL/NTSC | Aktif raster ve geometri | Daireler daire, menü kenarları güvenli alanda kalmalı |
| 5.000 öğelik Plex/Jellyfin | Yanıt sonrası UI blok süresi | Navigasyon girdisi kaybolmamalı; hedef sayfalama sonrası ilk ekran hızlı gelmeli |
| 200 YouTube aboneliği | Eşzamanlı istek, ilk içerik süresi | Sabit concurrency limiti ve kademeli sonuç |
| 24 saat menü gezintisi | RSS/bellek eğrisi | Sürekli yükselen trend olmamalı |

## Önerilen uygulama sırası

### Aşama 1 — Kullanıcı tarafından hissedilen takılmalar

1. Arcade ekran ayarı yükleme clamp'i ve `perPage` koruması.
2. Plex sezon isteklerine concurrency limiti veya lazy açılım.
3. Jellyfin/Emby/Plex listelerinde istek kimliği ve eski yanıt iptali.
4. Local Files açılışında gereksiz cache temizliğini kaldırma.

### Aşama 2 — Büyük koleksiyon ölçeği

1. Arcade klasör taraması ve indeksini worker'a taşıma.
2. Medya kütüphanelerini sayfalama/lazy `QAbstractListModel` yapısına geçirme.
3. Snap klasörünü bir kez indeksleme.
4. YouTube RSS istek kuyruğuna concurrency limiti.
5. `TreeBrowser` için LRU sınırı.

### Aşama 3 — Donanım profili ve ölçüm

1. Pi 4/Pi 5 `mpv` argüman testleri.
2. Pi 4 480i ve 1080p için ağır/hafif tema frame-time karşılaştırması.
3. Pi 5 PAL/NTSC composite geometri reçetesi.
4. Tam MAME seti için tekrarlanabilir açılış/gezinti benchmark'ı.

## Son değerlendirme

Kod tabanı “masaüstü uygulamasını Pi'ye taşımak” yaklaşımından daha olgun; Pi, CRT ve kabin gereksinimleri mimaride gerçekten düşünülmüş. Pi 4 için performans marjı doğru yerlerde korunuyor, Pi 5'in Full KMS ve ses farkları hesaba katılıyor. Projeyi bir sonraki seviyeye taşıyacak çalışma yeni özellikten çok, büyük koleksiyon işlerini ana iş parçacığından çıkarmak ve gerçek cihaz ölçümlerini tekrarlanabilir hale getirmek olacaktır.
