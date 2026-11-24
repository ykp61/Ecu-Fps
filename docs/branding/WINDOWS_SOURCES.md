# Windows entegrasyonu için birincil kaynaklar

İnceleme tarihi: 2026-11-04. Aşağıdaki bağlantılar Microsoft'un kendi belgeleridir. Bu paket içindeki boyut ve entegrasyon notlarının dayanağıdır; uygulamanın desteklediği Windows sürümünde son davranış ayrıca denenmelidir.

| Belge | Bu paketteki kullanım |
| --- | --- |
| [Construct your Windows app's icon](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction) | Çoklu çözünürlük, temel 16/24/32/48/256 px boyutları ve DPI varyantları |
| [About Icons](https://learn.microsoft.com/en-us/windows/win32/menurc/about-icons) | İkon kaynakları, alfa ve sistem boyutları |
| [ICON resource](https://learn.microsoft.com/en-us/windows/win32/menurc/icon-resource) | ICO'nun uygulama kaynaklarına gömülmesi |
| [Notifications and the Notification Area](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area) | Tray yaşam döngüsü, tooltip, sürüm ve klavye davranışı |
| [LoadIconMetric](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-loadiconmetric) | Uygun boyut seçimi ve HICON temizliği |
| [The Taskbar](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar) | Explorer/TaskbarCreated sonrası tray ikonunun yeniden eklenmesi |
| [SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow) | Yüksek kontrast ve hareket tercihlerini okuma |

Renkler, wordmark geometrisi, optik küçük çizim ve kısa animasyon süreleri Ecu Fps tasarım kararlarıdır; Microsoft zorunluluğu gibi yorumlanmamalıdır. Bu belgeler kullanıcı ayarlarını değiştirme izni vermez; marka entegrasyonu ilgili tercihleri yalnız okur.
