# OSD/OS performans ve mantık incelemesi

İnceleme tarihi: 16 Ekim 2026<br>
İncelenen sürüm: `b9c8eff`<br>
Hedef donanım: Raspberry Pi 4 ve Raspberry Pi 5 (8 GB), CRT televizyon ve arcade kabini

## Kısa sonuç

Projenin temel mimarisi hedef donanıma uygun. Arayüz ile video/emülatör işini ayırması, videoyu `mpv`'ye, arcade oyunlarını MAME'e devretmesi ve Pi 4/Pi 5 için ayrı görüntü profilleri kullanması doğru kararlar. DRM/VT devri, atomik ayar yazımı, oyun kumandası/JAMMA desteği ve oynatma sırasında efektlerin durdurulması özellikle güçlü.

En önemli performans riski RAM değil; ana iş parçacığında yapılan büyük katalog işlemleri, yavaş SD/USB depolama ve tam ekran shader maliyeti. 8 GB RAM her iki kart için de fazlasıyla yeterli olsa da daha fazla RAM, tek çekirdek üzerinde JSON ayrıştırma/sıralama veya exFAT dizin tarama gecikmesini çözmez. Pi 5 bu yükleri Pi 4'ten daha rahat taşır; yine de büyük Plex/Jellyfin/Emby ve tam MAME koleksiyonlarında aynı mimari sınırlar geçerlidir.

İkinci, daha saldırgan mantık ve hata-yolu taramasında performans sorunlarının yanında önemli doğruluk problemleri de bulundu. En ciddisi, Plex/Jellyfin/Emby oynatma sinyallerinin istek kimliği taşımaması: iptal edilmiş A videosunun geç gelen cevabı B ekranı tarafından kabul edilip yanlış içerik ve yanlış izleme oturumu başlatabiliyor. Güç kesintisi sırasında güncelleme, VT geri dönüşü ve Pi 5 CRT çıkış seçimi de ayrıca güçlendirilmelidir.

İlk öncelikler:

1. Bütün medya backend cevaplarına istek/öğe kimliği eklemek ve eski cevapları iptal etmek.
2. Güncelleme ve DRM/VT geri dönüşünü kesinti durumunda güvenli hale getirmek.
3. Büyük medya kütüphanelerini sayfalama/lazy model ile yüklemek.
4. Arcade klasör taraması, `games.json` işleme ve katalog indeksini GUI iş parçacığından çıkarmak.
5. Plex sezon açılımındaki sınırsız paralel istekleri kaldırmak.

> Bu rapor bir statik kod incelemesidir. Pi 4/Pi 5 üzerinde FPS, sıcaklık, güç tüketimi veya oyun başına MAME performansı ölçülmedi. Ölçülmeyen noktalar aşağıda “ölçüm/test açığı” olarak belirtilmiştir.

## Öncelikli bulgular

### 1. Büyük medya kütüphaneleri tek seferde belleğe ve QML modeline alınıyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış performans/mimari riski

- Jellyfin tüm sonuçları limitsiz istiyor, JSON'u ana iş parçacığında ayrıştırıp tamamını `QVariantList` yapıyor: `src/modules/jellyfin/JellyfinBackend.cpp:668-705`.
- Emby aynı yaklaşımı kullanıyor: `src/modules/emby/EmbyBackend.cpp:845-862`.
- Plex `/library/sections/{id}/all` sonucunun tamamını işliyor: `src/modules/plex/PlexBackend.cpp:1536-1553`.
- Sonuç daha sonra QML tarafında topluca atanıyor ve harf indeksi kuruluyor: `modules/jellyfin/views/Items.qml:79-84`, `modules/plex/views/Items.qml:172-179`.

Binlerce filmlik/dizilik bir arşivde ağ yanıtından sonra JSON ayrıştırma, nesne kopyalama, sıralama ve QML model yenileme aynı anda gerçekleşebilir. Bu durum özellikle Pi 4'te birkaç saniyelik “donmuş menü” hissi yaratabilir. Pi 5 süreyi azaltır fakat ana iş parçacığının bloklanmasını ortadan kaldırmaz.

**Öneri:** Sunucu tarafı sayfalama veya harf dilimleri kullanmak; uzun vadede satırları ihtiyaç oldukça sağlayan bir `QAbstractListModel` kurmak. Harf ile atlama korunacaksa ilk ekran hemen gösterilip kalan dilimler arka planda alınabilir.

### 2. Arcade koleksiyonunun önemli işleri GUI iş parçacığında

**Önem:** Yüksek (tam MAME seti ve yavaş USB/SD için)<br>
**Tür:** Doğrulanmış performans riski

- ROM klasörü senkron dolaşılıyor ve bazı dosyalar için ek varlık kontrolü yapılıyor: `src/modules/arcade/ArcadeBackend.cpp:317-371`.
- `games.json` dosyasının tamamı okunup ayrıştırılıyor ve kayıt sırasında tamamı yeniden oluşturuluyor: `src/modules/arcade/ArcadeBackend.cpp:462-496`.
- Tüm oyunların adı sıralanıyor; tür, oyuncu sayısı, yıl ve yapımcı indeksleri tek seferde kuruluyor: `src/modules/arcade/ArcadeBackend.cpp:632-665`.
- Bunlar `refresh()`/`prepare()` ve filtrelerin ilk kullanımı üzerinden GUI iş parçacığında çalışıyor: `src/modules/arcade/ArcadeBackend.cpp:389-421`.

Tam bir MAME seti on binlerce girdiye ulaşabilir. exFAT USB disklerde dizin ve “dosya var mı?” sorguları da pahalıdır. Arcade ekranının ilk açılışında veya yeniden taramada giriş gecikmesi oluşabilir.

**Öneri:** Dizin taraması ve indeks üretimini worker üzerinde yapmak, hazır sonucu ana iş parçacığına tek seferde taşımak. Önce ucuz bir oyun listesi gösterilip yanlış ad kontrolü ve ayrıntı indeksi kademeli tamamlanabilir. `games.json` kaydı da tarama grupları arasında değil, debounce edilmiş tek işlem olarak tutulmalıdır.

### 3. Plex sezonları sınırsız paralel HTTP isteğine dönüşebiliyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış mantık/performans riski

`flattenSeasons()` listedeki her sezon için ayrı `/children` isteğini hemen başlatıyor: `src/modules/plex/PlexBackend.cpp:819-859`. Bu işlev hem hub listelerinde hem de tüm kütüphane yüklemesinde çağrılıyor: `src/modules/plex/PlexBackend.cpp:1517-1553`.

Sezon sayısı yüksek bir TV arşivi çok sayıda eşzamanlı istek, çoklu JSON ayrıştırma ve sonuç kopyalama üretebilir. Kullanıcı ancak son istek bitince tam sonucu görür. Wi-Fi, Plex sunucusu ve Pi aynı anda gereksiz yük altında kalır.

**Öneri:** Kütüphane taramasında sezonları episode listesine açmamak; yalnızca kullanıcı bir sezonu veya “Tümünü Oynat”ı seçtiğinde genişletmek. Genişletme şartsa eşzamanlı istek sayısını 4-8 ile sınırlamak ve sonuçları kademeli sunmak.

### 4. Arcade ekran ayarı bozuk/elle değiştirilmiş kayda karşı korunmuyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış dayanıklılık hatası

Normal ayar ekranı minimum alanı koruyor: `modules/arcade/views/ArcadeScreen.qml:146-169`. Ancak kaydedilmiş `screen_adjustment` değerleri başlangıçta doğrulanmadan yükleniyor: `modules/arcade/views/Root.qml:205-210`. Aşırı bir değer liste alanını küçültebilir; `perPage` sıfır veya bir olduğunda `pitch` hesabındaki `(perPage - 1)` bölümü geçersiz sonuç üretebilir: `modules/arcade/views/ArcadeScreen.qml:70-85`.

Bu, normal tuş kullanımıyla kolayca oluşmuyor; yarım kalmış eski bir ayar, elle düzenlenmiş `config.json` veya gelecekteki ayar biçimi değişiklikleri için gerçek bir sağlamlık açığıdır.

**Öneri:** Kayıt yüklenirken dört kenarı minimum 600×420 alanı koruyacak biçimde clamp etmek ve `perPage <= 1` için güvenli bir `pitch` değeri kullanmak.

### 5. Arcade görseli aranırken seçim değişimi GUI'de disk erişimi yapıyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış performans riski

Her yeni oyun seçimi `gameDetails()` çağırıyor: `modules/arcade/views/ArcadeScreen.qml:130-133`. İlk ziyarette `pictureOf()` PNG/JPG varlık kontrolleri ve ekran görüntüsü klasörü listelemesi yapıyor: `src/modules/arcade/ArcadeBackend.cpp:776-831`. Sonuçlar, bulunamayan görseller dahil, oturum içinde önbelleğe alındığı için ikinci ziyaret ucuzdur.

Yavaş exFAT/USB ortamında ilk kez hızlı gezinirken her başlık kısa bir takılma yaratabilir.

**Öneri:** `snap` klasörünü bir kez indekslemek veya görünür sayfadaki görselleri worker üzerinde önceden hazırlamak. Mevcut negatif sonuç önbelleği korunmalıdır.

### 6. Asenkron liste yanıtları istek kimliği taşımıyor

**Önem:** Orta<br>
**Tür:** Mantık ve gereksiz iş riski

Jellyfin ve Emby `itemsLoaded` yanıtını yalnızca ekran moduna göre kabul ediyor; hangi `parentId` isteğine ait olduğunu denetlemiyor: `modules/jellyfin/views/Items.qml:79-84`, `modules/emby/views/Items.qml:79-84`. Plex tarafında da bölüm/hub kimliğiyle korelasyon yok: `modules/plex/views/Items.qml:172-179`.

Kullanıcı hızlı gezinirse eski ve büyük bir yanıt yeni ekran açıldıktan sonra modeli tekrar kurabilir veya kısa süre yanlış listeyi gösterebilir.

**Öneri:** Her yüklemeye artan bir istek kimliği eklemek, sinyalde bu kimliği geri döndürmek ve eski yanıtları yok saymak. Aynı kategoride yeni istek başladığında önceki `QNetworkReply` mümkünse iptal edilmelidir.

### 7. YouTube abonelik yenilemesi bütün kanalları aynı anda çağırıyor

**Önem:** Orta<br>
**Tür:** Ölçeğe bağlı performans riski

15 dakikalık önbellek eskidiğinde bütün kanallar `refreshChannel()` ile aynı anda başlatılıyor: `src/modules/youtube/YouTubeBackend.cpp:160-190`. Her tamamlanma RSS XML'ini ana iş parçacığında ayrıştırıyor: `src/modules/youtube/YouTubeBackend.cpp:264-293`.

Az sayıda abonelikte sorun beklenmez. Yüzlerce kanalda Pi 4 Wi-Fi ve CPU üzerinde kısa süreli yoğunluk oluşur.

**Öneri:** 3-5 eşzamanlı istekli kuyruk, kademeli `listingReady` ve görünür kanalın önceliklendirilmesi.

### 8. Ayar dosyası oynatma başlangıcında tekrar tekrar okunuyor

**Önem:** Orta-düşük<br>
**Tür:** Doğrulanmış I/O verimsizliği

`AppCore::get_setting()` her çağrıda `config.json` dosyasını okuyup ayrıştırıyor: `src/AppCore.cpp:58-71`, `src/AppCore.cpp:164-181`. `MpvController` bir oturumun ölçekleme, crop, altyazı, arka plan ve video profil ayarları için bu işlevi birden fazla kez çağırıyor; örnekler `src/player/MpvController.cpp:1018-1028` ve `src/player/MpvController.cpp:1100-1138`.

Dosya küçükken etkisi sınırlıdır; yine de her video başlangıcında tekrarlanan SD erişimi gereksizdir.

**Öneri:** Bellekte yapılandırma kopyası tutmak; `save_setting()` sonrası güncellemek ve kullanıcının dosyayı elle değiştirebilmesi isteniyorsa mtime/`QFileSystemWatcher` ile geçersiz kılmak. `mpv_video_args` değerinin sonraki oynatmada anında uygulanması korunmalıdır.

### 9. Local Files önbelleği ekran her açıldığında temizleniyor

**Önem:** Orta-düşük<br>
**Tür:** Doğrulanmış I/O verimsizliği

`TreeBrowser.rootPath` binding'i her ekran kuruluşunda `clearDirectoryCache()` çağırıyor: `modules/local_files/views/Items.qml:102-109`. Böylece aynı USB/NAS dizinlerine dönüşte `AsyncDirectoryCache` yeniden disk/ağ erişimi yapıyor.

**Öneri:** Önbelleği yalnızca medya kökü değişince, sürücü takılıp çıkarılınca veya kullanıcı açıkça yenileyince temizlemek.

### 10. TreeBrowser oturum önbelleği sınırsız büyüyebiliyor

**Önem:** Orta-düşük<br>
**Tür:** Uzun oturuma bağlı bellek riski

Her ziyaret edilen yol `listings` JavaScript nesnesinde saklanıyor: `views/Components/TreeBrowser.qml:172-180`, `views/Components/TreeBrowser.qml:222-247`. Derin yerel klasörlerde ve çok sayfalı çevrim içi kataloglarda modül kapanana kadar büyüyebilir.

8 GB RAM nedeniyle kısa oturumlarda pratik sorun beklenmez; ancak günlerce açık kalan bir arcade kabininde sınırsız büyüyen yapı tercih edilmemelidir.

**Öneri:** Yol sayısı veya tahmini satır sayısına göre LRU sınırı; aktif gezinme izi ve yakın dallar korunup eski yollar atılabilir.

### 11. Arcade teması Pi 4 için pahalı bir varsayılan kombinasyon

**Önem:** Orta (yüksek çözünürlüklü çıkışta)<br>
**Tür:** Ölçüm gerektiren performans riski

Arcade teması aynı anda `Stars`, `Rainbow` metin, `Rainbow` seçici ve `Cube` geçişini açıyor: `assets/themes/arcade/theme.json:1-11`. Rainbow metin, ekranı tam çözünürlükte bir shader katmanına alıyor: `Main.qml:936-947`. Hareketli efekt zamanı yaklaşık 30 Hz güncelleniyor: `Main.qml:239-250`.

Fiziksel 480i CRT'de piksel sayısı düşük olduğu için maliyet makul olabilir. Pi 4'ün 1080p HDMI üzerinden bir dönüştürücüye veya LCD arcade ekrana bağlandığı durumda tam ekran shader belirgin yük yaratabilir. Oyun/video sırasında efektlerin durdurulması bu riski ciddi biçimde azaltıyor.

**Öneri:** Pi 4 için “Arcade Lite” seçeneği (statik metin + Fade) sunmak veya ilk kurulumda çıkış çözünürlüğüne göre hafif temayı önermek. Varsayılanı değiştirmeden önce gerçek donanımda frame-time ölçmek gerekir.

### 12. Pi 5 composite raster/aspect davranışı yalnızca yorum ve kullanıcı ayarına bırakılmış

**Önem:** Düşük-orta<br>
**Tür:** Donanım doğrulama açığı

Kod, Pi 5 composite çıkışın bazen 720×480i yerine 704×432 raporladığını açıkça belirtiyor ve çözümü `monitorpixelaspect`/sistem ayarına bırakıyor: `src/player/MpvController.cpp:1055-1063`.

**Öneri:** Pi 5 composite için doğrulanmış PAL/NTSC örnek ayarını belgelerde ayrı bir reçete olarak vermek. Otomatik düzeltme ancak farklı TV/dönüştürücülerde ölçülerek eklenmelidir.

## İkinci tur: somut mantık ve güvenilirlik hataları

### 13. Geç gelen oynatma cevabı yanlış videoyu başlatabiliyor

**Önem:** Kritik<br>
**Tür:** Doğrulanmış yarış/mantık hatası

Plex'in `streamUrlReady(url, token)` sinyali hangi istek veya videoya ait olduğunu taşımıyor. `Extras.qml` üzerinde bir Extra başlatıp cevap gelmeden Back yapılırsa istek yaşamaya devam ediyor: `modules/plex/views/Extras.qml:121-143`. Geç cevap, artık açık olan ana `Item.qml` tarafından koşulsuz kabul ediliyor: `modules/plex/views/Item.qml:152-187`. Ana öğenin başlık, rating key, resume ve oturum bilgileriyle Extra'nın URL'si birleştiriliyor.

Jellyfin/Emby/Plex liste, detay ve oynatma cevaplarında aynı genel tasarım sorunu var: backend sinyalleri istek kimliği taşımıyor ve yeni ekranlar eski cevabı ayırt edemiyor. Örnek backend yolları: `src/modules/jellyfin/JellyfinBackend.cpp:1128-1246`, `src/modules/emby/EmbyBackend.cpp:1321-1508`, `src/modules/plex/PlexBackend.cpp:2553-2584`.

**Öneri:** Her oynatma isteğine generation/request ID ve media ID eklemek; sinyalde geri döndürmek; QML yalnızca beklediği kimliği kabul etmeli. Back/logout/yeni istek önceki `QNetworkReply`'ı iptal etmeli. Oynatma oturumu ancak cevap doğru ekran tarafından kabul edildikten sonra aktif sayılmalı.

### 14. Autoplay veya fallback hatası kullanıcıyı siyah/ölü Player ekranında bırakabiliyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış hata-yolu eksikliği

Bir bölüm bitince sonraki bölüm veya transcode fallback isteği başarısız olursa Player görünümü açık kalıyor; Plex genel backend hatasını yalnızca logluyor: `modules/plex/views/Player.qml:232-253`. Jellyfin/Emby de sadece bazı retry durumlarını ele alıyor: `modules/jellyfin/views/Player.qml:445-493`, `modules/jellyfin/views/Player.qml:637-647`, `modules/emby/views/Player.qml:416-465`, `modules/emby/views/Player.qml:554-564`.

Bu sırada mpv zaten sona ermiş olabileceği için Back komutu çalışmayan mpv'ye gönderilebilir. Kumandasız/klavyesiz kabinde kullanıcı ölü ekranda kalabilir.

**Öneri:** Her pending işlem için ayrı hata durumu; hata geldiğinde flag'i temizleme, sunucu oturumunu kapatma ve güvenli `goBack()`. Hatalar da request ID taşımalıdır.

### 15. DRM devrinde kısmi VT geçişi ekranı yanlış terminalde bırakabilir

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış sistem çağrısı hata yolu

`VT_ACTIVATE` başarılı, `VT_WAITACTIVE` kesilmiş/başarısız olursa `switchToVt()` bütün işlemi başarısız sayıyor: `src/util/DisplayHandoff.cpp:261-279`. Böylece `m_switchedVt` false kalıyor ve restore kodu eski VT'ye dönmüyor: `src/util/DisplayHandoff.cpp:176-183`.

Ayrıca aktif VT `/sys/class/tty/tty0/active` üzerinden okunamazsa `m_previousVt=-1` iken yine başka VT'ye geçiş denenebiliyor: `src/util/DisplayHandoff.cpp:90-105`, `src/util/DisplayHandoff.cpp:190-228`. Dönülecek terminal bilinmediği için restore atlanıyor.

**Öneri:** Activate ile wait sonucunu ayrı tutmak, `EINTR` için `VT_WAITACTIVE` tekrar etmek ve activate başarılıysa her koşulda geri dönüş denemek. Aktif VT bilinmiyorsa handoff başlamamalı veya `VT_GETSTATE` ile doğrulanmalıdır.

### 16. Güç kesintisi staged güncellemeyi karışık kurulum halinde bırakabilir

**Önem:** Kritik<br>
**Tür:** Doğrulanmış güç-kesintisi dayanıklılık hatası

Launcher `bin` ve `share` klasörlerini ayrı ayrı taşıyor: `scripts/install.sh:150-177`. Kurtarma yalnızca `bin/osdos` yoksa çalışıyor. Yeni `bin` yerleştirildikten sonra, yeni `share` yerleşmeden güç kesilirse çalıştırılabilir dosya mevcut olduğu için recovery tetiklenmiyor; yeni binary eski/eksik QML ve assetlerle açılıyor.

Elle yeniden kurulum da tarball'ı doğrudan canlı `/opt/osdos` üzerine açıyor: `scripts/install.sh:100-116`. Disk dolması veya kesinti eski sağlam sürümü korumuyor ve kaldırılan eski dosyaları temizlemiyor.

**Öneri:** Sürümleri ayrı dizinlere açıp doğrulamak; fsync edilmiş küçük transaction journal tutmak; tek `current` symlink/rename ile aktif sürümü atomik değiştirmek. `.new` veya `.old` varlığı her başlangıçta recovery sebebi olmalıdır.

### 17. Pi 5 CRT/JAMMA çıkışında erken boot yarışı HDMI'ı seçebilir

**Önem:** Yüksek<br>
**Tür:** Donanıma bağlı doğrulanmış yarış

`wait-for-display`, istenen preset konektörünü değil herhangi bir DRM konektörünü görünce çıkıyor: `os/stage-osdos/02-system/files/wait-for-display:7-14`. Launcher daha sonra Pi 5 presetindeki Composite/DPI konektörünü yalnızca bir kez arıyor; henüz kayıt olmadıysa ilk bağlı konektöre, çoğunlukla HDMI'a düşüyor: `scripts/install.sh:190-247`.

**Öneri:** Bekleme betiği önce preseti okuyup istenen konektör ve mod görünene kadar beklemeli. Timeout olduğunda sessiz HDMI fallback yerine açık hata/log ve kontrollü fallback uygulanmalıdır.

### 18. NFC kartı yeniden eşlemede yeni yazım başarısızsa eski eşleme kayboluyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış veri kaybı

Kart başka başlığa eşlenirken eski dosya önce siliniyor, yeni dosya sonra atomik yazılıyor: `src/modules/nfc_reader/NfcReaderBackend.cpp:499-516`. Disk dolu veya klasör read-only ise yeni yazım başarısız oluyor ve eski geçerli kart eşlemesi geri getirilemiyor.

**Öneri:** Önce yeni dosyayı başarıyla commit etmek, sonra eski dosyayı kaldırmak. Aynı UID'yi iki dosyada kısa süre görme ihtimali, eski eşlemeyi kalıcı kaybetmekten daha güvenlidir; işlem sonunda yeniden scan yapılabilir.

### 19. Ayar kaydı başarısız olsa da sistem ayar değişmiş gibi sinyal yayıyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış çalışma zamanı/disk tutarsızlığı

`saveConfig()` başarısız olduğunda uyarı yazılıyor fakat hemen ardından `appSettingChanged` veya `moduleSettingChanged` yine emit ediliyor: `src/AppCore.cpp:220-231`. Disk dolu/read-only durumda ses, input ve modül backendleri yeni değeri o oturumda uygular; `get_setting()` ve yeniden başlatma eski değeri kullanır.

**Öneri:** Değişim sinyalini yalnızca başarılı commit sonrasında yaymak ve QML'ye bool/hata döndürmek. Başarısız kayıtta arayüz değeri de eski haline alınmalıdır.

### 20. Playlist dosyası yazılamasa bile başarı sonucu dönüyor

**Önem:** Yüksek<br>
**Tür:** Doğrulanmış hata kontrolü eksikliği

`PlaylistsBackend::prepare()` klasör oluşturma, `QSaveFile::open`, `write` ve `commit` sonuçlarını zorunlu başarı koşulu yapmıyor; eski M3U'ları önce siliyor ve her durumda yeni yolu/madde sayısını döndürüyor: `src/modules/playlists/PlaylistsBackend.cpp:506-530`.

Disk dolu/read-only durumda QML geçersiz veya eksik M3U yolunu mpv'ye verir.

**Öneri:** Eski dosyayı yeni dosya commit edilene kadar korumak; tam write ve commit doğrulanmadan `m_prepared` güncellememek ve açık hata döndürmek.

### 21. MAME'den çıkış yalnızca Player 1 joystickine bağlı

**Önem:** Yüksek (klavyesiz kabin)<br>
**Tür:** Doğrulanmış kontrol kilidi

Controller eşlemesi oyuncu sırasını koruyor, fakat `UI_CANCEL` yalnızca Esc veya `JOYCODE_1_SELECT + JOYCODE_1_START` kabul ediyor: `src/modules/arcade/Mame.cpp:473-499`. Player 1 olarak ayarlı kol/encoder takılı değil, Player 2 bağlıysa menüler çalışabilse bile oyundan normal çıkış yok.

**Öneri:** İlk bağlı joysticki çıkış için kullanmak veya bağlı tüm atanmış oyuncuların Coin/Select + Start kombinasyonunu `UI_CANCEL` dizisine eklemek.

### 22. İptal edilen veya başlayamayan oyun “Recently Played” listesine giriyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış mantık hatası

Arcade root oyunu, bilgi ekranı gecikmesinden ve `arcadeBackend.play()` sonucundan önce recent listesine ekliyor: `modules/arcade/views/Root.qml:64-73`. Kullanıcı bilgi ekranında Back yaparsa veya MAME preflight başarısız olursa hiç oynanmayan oyun Recent ve High Scores akışında görünür.

**Öneri:** Recent kaydını yalnızca `play()` başarıyla süreç başlattıktan sonra yapmak.

### 23. Kaybolan harici ROM diski boş klasör olarak yeniden oluşturulabiliyor

**Önem:** Orta-yüksek<br>
**Tür:** Doğrulanmış depolama güvenliği riski

Arcade `refresh()` yapılandırılmış oyun klasöründe koşulsuz `mkpath()` çağırıyor: `src/modules/arcade/ArcadeBackend.cpp:389-394`. Harici disk mount edilmediyse aynı yol sistem diskinde sıradan klasör olarak oluşabilir; arayüz boş kütüphane gösterir ve kullanıcı bu konuma ROM kopyalarsa SD karta yazmış olur.

**Öneri:** Kullanıcı tarafından seçilmiş harici yolun beklenen mount altında olduğunu doğrulamak; kayıp volume için klasör oluşturmamak ve “disk bağlı değil” hatası göstermek.

### 24. Jellyfin başarılı auth kontrolü gezinmeyi beklenmedik biçimde sıfırlıyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış gezinme yarışı

Modül geçerli token ile Libraries ekranını hemen açıyor, sonra asenkron `check_auth()` yapıyor: `modules/jellyfin/views/Root.qml:112-120`. Başarılı kontrol de `authStateChanged` yayıyor ve root mevcut stack'i temizleyip Libraries'e dönüyor: `modules/jellyfin/views/Root.qml:90-108`.

Sunucu yavaşsa kullanıcı bu arada bir kütüphane/öğe açabilir ve cevap geldiğinde aniden başlangıca atılır.

**Öneri:** Yalnızca gerçek auth durumu değiştiğinde sinyal üretmek veya zaten authed ve auth ekranında değilken başarılı doğrulamayı navigation reset sebebi yapmamak.

### 25. Plex logout temizlenen stack'e korumalı ekranı yeniden ekliyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış gezinme hatası

Logout handler önce `navStack=[]` yapıyor, sonra `navigateTo("PinAuth.qml")` çağırıyor: `modules/plex/views/Root.qml:76-87`. `navigateTo` mevcut korumalı görünümü yeniden stack'e ittiği için PinAuth üzerinde Back eski authenticated ekrana dönebilir.

**Öneri:** Auth ekranına stack'e push etmeyen `replaceWith` ile geçmek ve stack'i geçişten sonra da boş tutmak.

### 26. Weather, başka bir oynatmanın screen-saver sahipliğini bozabiliyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış ortak durum sahipliği hatası

Weather açılırken ortak `idleTracker.mpvActive=true`, kapanırken koşulsuz false yazıyor: `modules/weather/views/Weather.qml:41-46`, `modules/weather/views/Weather.qml:83-87`. Arka planda embedded video halen oynuyorsa Weather'dan çıkış gerçek mpv aktifliğini siliyor ve ekran koruyucu videonun üstüne gelebiliyor.

**Öneri:** Weather için ayrı suppression/ref-count sahibi kullanmak veya kapanışta değeri gerçek `mpvController.videoActive` durumuna geri yüklemek.

### 27. Script başlatma timeout'u geç başlayan süreci serbest bırakabiliyor

**Önem:** Orta-yüksek<br>
**Tür:** Doğrulanmış nadir süreç yaşam döngüsü hatası

`QProcess` beş saniyeden uzun `Starting` durumunda kalırsa watchdog `finish()` çağırıyor; fakat süreç öldürülmüyor ve sinyalleri ayrılmıyor. Daha sonra gelen `started` sinyali PGID kaydedip scripti, ekran ve menü müziği çoktan geri verilmişken çalıştırabilir: `src/modules/scripts/ScriptLauncher.cpp:330-357`, `src/modules/scripts/ScriptLauncher.cpp:495-569`.

**Öneri:** Startup timeout'u gerçek cancellation yapmalı: süreç terminal duruma gelene kadar terminate/kill akışı, sonra display/music release ve tek sonuç.

### 28. Ambient Mode ses durdurma GUI'yi bloklayıp sesi yaşamaya bırakabiliyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış süreç hata yolu

`stopAudio()` GUI thread üzerinde bir saniye `waitForFinished()` yapıyor; timeout sonucunu kontrol etmiyor, kill göndermeden nesneyi `deleteLater()` yapıyor ve ses kartı sahipliğini serbest bırakıyor: `src/modules/ambient_mode/AmbientModeBackend.cpp:119-130`.

**Öneri:** Asenkron terminate → süreli kill → `finished` sonrasında ownership release. Böylece menü bir saniye donmaz ve eski mpv yeni sese engel olmaz.

### 29. Güncelleme kontrolü eski sürümü “güncelleme” olarak kabul ediyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış sürüm karşılaştırma hatası

UpdateManager yalnızca tag mevcut sürüme tam eşitse “up to date” diyor: `src/update/UpdateManager.cpp:232-284`. Feed/GitHub yanlışlıkla daha eski bir sürüm döndürürse geçerli asset bulunduğu sürece downgrade teklif ediliyor.

**Öneri:** Normalize edilmiş semantic version karşılaştırması; yalnızca `latest > current` için update. Geri dönüş gerekirse ayrı ve açık bir rollback yolu olmalıdır.

### 30. İlk boot media partition işlemi yarıda kalırsa kendini onarmıyor

**Önem:** Orta<br>
**Tür:** Doğrulanmış güç-kesintisi hata yolu

`resize_early`, üçüncü partition varsa bütün işlemi atlıyor: `os/stage-osdos/05-media/files/resize_early:87-105`. Güç, `mkpart` sonrasında fakat `mkfs.exfat` tamamlanmadan kesilirse sonraki boot partition'ı var sayıp formatlamıyor.

**Öneri:** `blkid TYPE` ile dosya sistemini kontrol etmek; partition mevcut ve formatsızsa formatlamayı güvenle tamamlamak, beklenmeyen filesystem varsa dokunmadan açık hata vermek.

### 31. Boot progress ilk kareyi 2,5 saniyeye kadar geciktirebiliyor

**Önem:** Orta-düşük<br>
**Tür:** Doğrulanmış startup bloklaması

QML yüklenmeden önce `systemctl is-system-running` için GUI/startup thread üzerinde iki saniye, kill sonrası 500 ms daha bekleniyor: `src/boot/BootProgress.cpp:40-51`. Stall watchdog bu aşamadan sonra kurulduğu için gecikmeyi raporlayamaz.

**Öneri:** Probe'u asenkron başlatmak; boot overlay'i cevap veya kısa timeout ile kapatmak.

### 32. Kurulum sahibi mevcut systemd kullanıcısıyla ayrışabiliyor

**Önem:** Orta-yüksek<br>
**Tür:** Doğrulanmış kurulum/işletim hatası

Mevcut autostart kurulumu başka kullanıcıyla çalışırken installer yeniden çalıştırılıp “N” seçilirse servis kaldırılmıyor; fakat `/opt/osdos` çağıran kullanıcıya `chown -R` ediliyor: `scripts/install.sh:83-116`. Servis dosyaları okuyabilir ama uygulama içi update için install tree'ye yazamayabilir.

**Öneri:** Mevcut unit'in `User=` değerini algılayıp varsayılan owner olarak korumak; autostart kapatma/silme kararını ayrı sormak ve bitişte servis kullanıcısının yazabildiğini doğrulamak.

### 33. Release tag'i regresyon testleri çalışmadan yayınlanabiliyor

**Önem:** Orta<br>
**Tür:** CI güvence açığı

Regresyon workflow'u PR ve `main` pushlarında çalışıyor fakat tag pushunda değil: `.github/workflows/regression-tests.yml:6-30`. Release workflow'u yayınlamayı test job'una bağlamıyor: `.github/workflows/release.yml:436-439`.

**Öneri:** Release workflow içinde reusable regression job çalıştırmak ve bütün artefact/publish adımlarını buna bağımlı yapmak.

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

- Plex/Jellyfin/Emby için gecikmiş veya sırası değişmiş liste/detail/stream cevaplarını sınayan QML/backend testi yok.
- Autoplay/fallback isteği hata verdiğinde Player'dan güvenli çıkış testi yok.
- Staged update sırasında her rename noktasında güç kesintisi/recovery testi yok.
- Pi 5 boot sırasında HDMI ile Composite/DPI konektörlerinin farklı sırada gelmesini taklit eden launcher testi yok.
- Disk dolu/read-only durumda config sinyali, NFC remap ve playlist hazırlama rollback testleri yok.
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

### Aşama 1 — Yanlış içerik, siyah ekran ve kurtarılamayan kurulumlar

1. Plex/Jellyfin/Emby liste, detay, stream ve error sinyallerine request/media ID ekleme; eski reply iptali.
2. Autoplay ve transcode fallback hata yollarında Player'dan güvenli çıkış.
3. Staged update'i versioned-directory + atomik pointer ve transaction recovery modeline geçirme.
4. `DisplayHandoff` kısmi VT geçişi ve bilinmeyen aktif VT düzeltmesi.
5. Pi 5 launcher'ın preset konektörünü gerçekten beklemesi.
6. NFC remap, playlist prepare ve config save için başarısızlık/rollback düzeltmeleri.
7. MAME çıkışını bağlı controller'ların tümünde kullanılabilir yapma.

### Aşama 2 — Günlük kullanım mantığı ve hata yolları

1. Jellyfin auth ve Plex logout navigation yarışlarını düzeltme.
2. Weather/screen-saver sahipliğini ref-count veya ayrı flag ile çözme.
3. Script ve Ambient süreçlerini tamamen asenkron terminate/kill akışına alma.
4. Arcade recent kaydını başarılı launch sonrasına taşıma ve kayıp ROM mount'unu ayırt etme.
5. Arcade ekran ayarı clamp'i ve `perPage` koruması.
6. Installer owner/service-user tutarlılığı ve first-boot partition recovery.
7. Release'i regresyon job'una bağlama.

### Aşama 3 — Büyük koleksiyon ölçeği ve performans

1. Arcade klasör taraması ve indeksini worker'a taşıma.
2. Medya kütüphanelerini sayfalama/lazy `QAbstractListModel` yapısına geçirme.
3. Plex sezon isteklerine concurrency limiti veya lazy açılım.
4. Snap klasörünü bir kez indeksleme.
5. YouTube RSS istek kuyruğuna concurrency limiti.
6. Local Files gereksiz cache temizliğini kaldırma ve `TreeBrowser` için LRU sınırı.

### Aşama 4 — Donanım profili ve ölçüm

1. Pi 4/Pi 5 `mpv` argüman testleri.
2. Pi 4 480i ve 1080p için ağır/hafif tema frame-time karşılaştırması.
3. Pi 5 PAL/NTSC composite geometri ve konektör kayıt sırası testi.
4. Tam MAME seti için tekrarlanabilir açılış/gezinti benchmark'ı.

## Son değerlendirme

Kod tabanı “masaüstü uygulamasını Pi'ye taşımak” yaklaşımından daha olgun; Pi, CRT ve kabin gereksinimleri mimaride gerçekten düşünülmüş. Buna rağmen ikinci tarama, mutlu yolun iyi test edilmesine karşın iptal, geç cevap, disk dolması, güç kesintisi ve kısmi sistem çağrısı gibi hata yollarında önemli boşluklar olduğunu gösterdi. Önce request kimlikleri, update transaction'ı ve VT/konektör geri dönüşleri güvenli hale getirilmeli; ardından büyük koleksiyon işleri ana iş parçacığından çıkarılmalı ve gerçek cihaz ölçümleri tekrarlanabilir hale getirilmelidir.
