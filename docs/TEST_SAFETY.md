# Gerçek ayar testi güvenlik prosedürü (ENV-06)

Kullanıcı kararı (2026-11-04): geliştirici ana PC'sinde sistem ayarı değiştiren her gerçek testten önce Sistem Geri Yükleme noktası alınır ve kullanıcıdan Evet/Hayır onayı istenir.

## Kapsam

| Test türü | Prosedür gerekli mi |
|---|---|
| Birim testleri, mock adaptörler, parser testleri | Hayır |
| Salt okuma envanter/ölçüm (INV, MEAS okuma) | Hayır |
| Smoke aç/kapat (sistem değişikliği yok) | Hayır |
| Windows/GPU/oyun ayarı yazan her test (WIN, GPU, GAME, OPT, REC gerçek yolları) | **Evet** |
| UAC ile yükseltilmiş rol testi (SEC) | Evet, yazma içeriyorsa |

## Adımlar

1. Ajan testin kapsamını kullanıcıya listeler: hangi ayar, eski değer nasıl kaydedilecek, nasıl geri alınacak, yeniden başlatma gerekip gerekmediği.
2. Kullanıcı Evet derse: `.\scripts\restore-point.ps1 -Create "Ecu Fps <GÖREV> testi öncesi"` (UAC onayı kullanıcıdadır).
3. Sonuç kodu 0 değilse (koruma kapalı, 24 saat sınırı, UAC reddi) test **yapılmaz**; durum kullanıcıya bildirilir. 24 saat içinde alınmış bir nokta varsa kullanıcı onu kabul edebilir.
4. Test uygulamanın kendi journal'ıyla yapılır: başlangıç kaydı → uygulama → geri okuma → geri alma → geri okuma.
5. Kanıt dosyasına geri yükleme noktası numarası ve geri alma sonucu yazılır.

## Betik

| Komut | İş |
|---|---|
| `.\scripts\restore-point.ps1 -Status` | Koruma durumu ve son nokta |
| `.\scripts\restore-point.ps1 -List` | Tüm noktalar |
| `.\scripts\restore-point.ps1 -Create "<açıklama>"` | Yeni nokta (yönetici) |

Betik Sistem Korumasını kendisi açmaz ve Windows'un nokta sıklığı sınırını değiştirmez; bunlar kullanıcı kararıdır.

Çıkış kodları: 0 başarılı, 1 hata, 2 koruma kapalı, 3 UAC reddedildi, 4 yeni nokta oluşmadı (sıklık sınırı).

## Geri dönüş yolu (acil)

Uygulama ve journal başarısız olursa: Ayarlar > Sistem > Kurtarma > Gelişmiş başlangıç veya `rstrui.exe` ile ilgili geri yükleme noktası seçilir.

## Ek karar (2026-11-04): kullanıcı yokken kullanıcı kapsamlı gerçek testler

Kullanıcı açıkça "tüm işlemlere onay veriyorum, asla sorma" dedi ve PC başında değil. Geri yükleme noktası UAC ister; ajan UAC'a tıklayamaz. Bu nedenle:

| Kapsam | Gerçek test | Koşul |
|---|---|---|
| Kullanıcı (HKCU, yönetici gerekmez) | Evet | Journal'lı uygula → geri oku → geri al → ilk değerle karşılaştır; test sonunda değer ilk halinde olmalı (değilse test başarısız ve kayıt korunur) |
| Makine (HKLM, güç planı yazımı, servis, DRS kaydetme) | Hayır (PENDING) | ENV-06 geri yükleme noktası + kullanıcı varlığı |

## Ek: P9 temiz VM turu (sahip yapar; B4, 2026-11-18)

Amaç: 0.9.0 yazma adaptörlerinin (ayar kataloğu HKCU/HKLM, Grafik tercihi, IFEO önceliği, Defender dışlaması, Store paketi kaldırma, özellik hizmetleri, arka plan önceliği) **gerçek** uygula/geri al sonucunu ve kurulum → kaldırma turunu izole bir VM'de görmek. Birim testleri (`tests/unit/recovery/test_adapter_contract.cpp`) yalnız **MOCK** arka uçla koşar; gerçek cihaz kanıtı değildir.

Kurallar:
- Yalnız sanal makine veya Windows Sandbox. `scripts/clean_machine_smoke.ps1` ana PC'de reddeder (çıkış 3) ve hiçbir şey yazmaz.
- Betik **salt okuma**: kurulum EXE'sini başlatmaz, UAC istemez, UAC'ye tıklamaz, ayar/kayıt/hizmet değiştirmez; yalnız `-OutDir` altına durum fotoğrafı ve günlük/journal **kopyası** yazar.
- Evet/Hayır ve UAC onayı sahibindedir. Ajan bu turu yapamaz (sahip listesi 4 ve 5).
- Betik yönetici olmadan çalışırsa Defender dışlama listesi "okunamadı" yazılır (Defender gizler); tam kanıt için betiği VM içinde yönetici PowerShell'den çalıştırın.

Denetim listesi (her satır işaretlenir; sonuç ve `OutDir` klasörü kanıt olarak saklanır):

| # | Adım | Kim | Komut / eylem | Kabul |
|---|---|---|---|---|
| 1 | Temiz Win11 (ve ayrı tur: Win10 22H2) VM, ağ açık, anlık görüntü (snapshot) alındı | Sahip | VM aracı | Snapshot adı not edildi |
| 2 | Kurulum EXE'si indirildi, SHA-256 yayın sayfasıyla karşılaştırıldı | Sahip | `-Checkpoint before -SetupExe <exe> -ExpectedSha256 <sha>` | `setup_exe.match = true` |
| 3 | Kurulum: EXE çift tık, UAC'yi sahip onaylar | Sahip | — | Kurulum bitti mesajı |
| 4 | Kurulum sonrası durum | Sahip | `-Checkpoint installed` | ARP var, `DisplayVersion` = kurulan sürüm; `EcuFpsHelper` RUNNING; hata eylemleri 3×RESTART |
| 5 | Performans Açık: plandaki 0.9.0 gruplarına Evet (her grup ayrı satır) | Sahip | Uygulama | Uygulama "Açık"; hata yok |
| 6 | Açık durum fotoğrafı | Sahip | `-Checkpoint on` | Hedefler değişti (beklenen) |
| 7 | Performans Kapalı | Sahip | Uygulama | "Kapalı"; RecoveryNeeded yok |
| 8 | Kapalı durum fotoğrafı | Sahip | `-Checkpoint off` | — |
| 9 | Geri alma karşılaştırması | Sahip | `-Compare before,off` | Çıkış 0: adaptör hedeflerinde fark yok (Store paketi: kurulu/kurulu değil düzeyinde; yeniden kayıt provisioned sürümü getirebilir) |
| 10 | (İsteğe bağlı) Uygulama içi güncelleme 0.9.x → 0.9.y; sonra `-Checkpoint installed` yeniden (ayrı `OutDir`) | Sahip | Uygulama | ARP `DisplayVersion` yeni sürüm; hizmet RUNNING |
| 11 | (İsteğe bağlı) Hizmet durmuş vakası: güncelleme sonrası hizmet durmuşsa uygulamadaki onarım sorusuna Evet (UAC sahip) | Sahip | Uygulama | Hizmet RUNNING; ARP `DisplayVersion` çalışan sürüm |
| 12 | Kaldırma: Ayarlar > Kaldır, UAC sahip | Sahip | Uygulama | Kaldırma bitti |
| 13 | Kaldırma sonrası | Sahip | `-Checkpoint uninstalled` sonra `-Compare before,uninstalled` | Çıkış 0: hedef farkı yok, ARP/hizmet/klasör/kısayol yok (klasör yeniden başlatma sonrası silinebilir: yeniden başlatıp 13'ü tekrarlayın) |
| 14 | Kanıt | Sahip | `OutDir` klasörünü (state.json, files\, compare-*.md) saklayın | Fark varsa `files\` altındaki `restore.log` ve journal kopyaları hata raporuna eklenir |
| 15 | VM snapshot'a geri dönüldü | Sahip | VM aracı | — |

Çıkış kodları: 0 tamam, 1 hata, 2 kabul dışı fark, 3 sanal makine değil (çalışmadı).
