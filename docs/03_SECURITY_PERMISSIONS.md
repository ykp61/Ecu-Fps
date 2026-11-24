# Yetki ve güven sınırları

Geniş API matrisi [v2 yetki araştırmasında](../archive/v2/07_Yetkiler_Portable_ve_Koruma.md); bu belge uygulama için zorunlu sınırları belirtir. Kaynaklar S61-S90, research/SOURCES.md içindedir.

## Yetkili işlem protokolü

Normal UI sistemin tamamına yazamaz. Yürütücü yalnız derlenmiş operation_type, hedef kapsamı ve sınırlandırılmış tipli değerleri kabul eder. Rastgele shell, PowerShell, registry yolu, servis adı veya dosya yolu yetkili komuta dönüşemez. Paketler schema/protocol sürümü, uzunluk sınırı, request/transaction kimliği, oturum bağı ve tekrar kullanılamayan nonce taşır.

Named pipe ACL, istemci SID/oturumu, karşı uç süreç kimliği, yaratılma zamanı ve yürütülebilir dosya doğrulaması birlikte tasarlanır. Tahmin edilmesi zor pipe adı tek başına kimlik doğrulama değildir. Uzak istemci reddi, zaman aşımı, replay, malformed payload ve path/handle TOCTOU testleri yapılır. Yönetici süreci istemciden gelen yolu kontrol edip farklı hedefi sonradan açmamalı; doğrulanmış handle/kimlikle işlem yapmalıdır.

Farklı admin hesabıyla UAC yapıldığında oyuncunun SID/oturumu kaybolmaz. HKCU işlemi broker admin hesabına yanlış uygulanmaz. Makine değişiklikleri kullanıcılar arası tek işlem kilidiyle korunur. UI sahibi olmayan kullanıcı keyfi kurtarma/yazma isteği başlatamaz.

## Geliştirme ve yayın kimliği

İmzasız geliştirme EXE'si üretim imza doğrulamasını sessizce kapatmamalı. Uygulama kodlamasında DEV/RELEASE trust policy ayrı ve görünür olmalı; release build dev bypass içeremez. İmzalı üretim broker'ına geçiş sertifika olmadan tamamlandı sayılmaz. Dev testi için izinli test konumu ve aynı binary kimliğiyle sınırlı yöntem ADR ile seçilir.

Üretici DLL'leri yalnız doğrulanmış kurulu sürücü/sistem yollarından yüklenir. Çalışma klasöründeki taklit DLL veya kullanıcı yazılabilir plugin taranmaz. Platform imzası ve cihaz desteği denetlenir. Public API'nin başarısız dönmesi, güvenlik/anti-cheat engelini kaldırma sebebi değildir.

## Kayıt, sırlar ve koruma

Makine ve kullanıcı journal'ı uygun ACL ve DPAPI kapsamıyla saklanır. Machine DPAPI tek başına erişim kontrolü değildir. Sırlar, özel imza anahtarı veya ortak API anahtarı binary içine konmaz. Ürün çevrimdışı temel işlerini ve geri almayı sürdürebilir.

Native derleme, sembol yönetimi, bütünlük, imzalı güncelleme ve dar IPC hedeflenir. Yüzde 100 tersine mühendislik koruması vaat edilmez. Ağır anti-debug/anti-VM, anti-cheat müdahalesi veya açık kernel sürücüsü yoktur. Lisans veya ölçüm servisi çalışmasa da rollback erişilebilir kalır.

## Başlangıç kontrolü

Run ve Startup, görevler, izinli servisler ayrı adaptörlerdir. Değişikliğin kapsamı, 32/64 bit registry görünümü ve eski değer tipi korunur. RunOnce ve kritik bağımlılıklar otomatik paket dışında kalır. Başlangıç girişini kapatma, çalışan uygulamayı öldürme değildir. Paketli uygulama için evrensel aç/kapat yeteneği kanıtlanmadan uygulanmış sayılmaz.

## Yardımcı hizmet ve broker IPC: STRIDE tehdit modeli (P9/B2, ADR-037)

Kapsam: kurulu EXE'nin LocalSystem yardımcı hizmeti (`core/service/**`, `platform/windows/service/**`), UAC broker oturumu (`core/ipc/protocol.*`) ve hizmet üzerinden zorunlu güncelleme alıcısı (`core/update/staged_update.*`). Güven sınırları: (1) UI süreci ↔ named pipe ↔ hizmet/broker; (2) imzalı yayın zarfı ↔ hizmet alıcısı. Her satırın test kimliği birim test adıdır (`ef_unit_tests`; `ctest -R unit`) veya `windows.*` ctest adıdır. Testler saf/in-process'tir; gerçek SCM, hizmet kurulumu veya UAC kullanılmaz. Kanıt: `artifacts/evidence/P9-service-threat-model.md`.

| No | STRIDE | Tehdit | Kontrol | Test kimliği |
|---|---|---|---|---|
| S-1 | Spoofing | Başka bir EXE (kopya, sert bağ, ADS, 8.3 adı, UNC/aygıt yolu, gömülü NUL) kurulu istemci gibi bağlanır | `check_client`: görüntü yolu kanonik eşitliği + dosya kimliği (birim seri + dizin) eşitliği; şüpheli yol fail-closed `WrongImage` | `svc_spoof_image_path_matrix`, `svc_spoof_identity_matrix` |
| S-2 | Spoofing | Broker'a sahte/yeniden kullanılmış nonce, fazladan Hello alanı, farklı PID | Hello tam olarak `{"nonce","pid"}`; nonce tek kullanımlık, sabit zamanlı karşılaştırma; gözlenen PID her çerçevede | `ipc_neg_broker_hello_shape_and_pid`, `ipc_session_*` (test_ipc.cpp) |
| S-3 | Spoofing | Pipe adı gaspı (saldırgan sunucu) | UI: pipe sunucu PID'i == SCM PID'i, aksi halde UAC broker | `svc_spoof_channel_choice_matrix`, `windows.ipc` (squat kipi) |
| S-4 | Spoofing | Hedef kullanıcı SID'i sistem/hizmet hesabı ya da bozuk | `check_client`: kullanıcı SID'i `S-1-5-21-…` / `S-1-12-1-…` biçiminde olmalı; değilse `QueryFailed` | `svc_spoof_identity_matrix` |
| T-1 | Tampering | Yeniden oynatma, sıra dışı çerçeve, seq taşması, kapalı oturuma çerçeve | seq kesin +1; ilk ihlalde `Closed`, sonraki her çerçeve `Error` ve işleyiciye ulaşmaz | `svc_neg_out_of_order_frames`, `svc_neg_replay_after_ready_and_after_close`, `service_session_seq_replay_and_gap` |
| T-2 | Tampering | Bozuk çerçeve/istek yükü ayrıştırıcıyı çökertir veya sınırı aşar | Başlık/uzunluk/tür doğrulaması, JSON sınırları, bilinmeyen alan reddi; sabit tohumlu 10k mutasyon korpusu 0 çökme | `security_ipc_frame_mutations`, `security_fuzz_service_sessions_10k`, `security_fuzz_decode_request_10k` |
| T-3 | Tampering | İmzalı ama acil frenle durdurulmuş (`"halt": true`) sürüm, istemci tarafından hizmete zorla yüklenir | Hizmet: `halt_guarded` imzalı zarfı açar, `halt` true/bozuksa UpdateBegin koordinatöre ulaşmadan oturum kapanır (`.part` açılmaz). Kök neden `UpdateReceiver::begin` içinde `Halted` dalı yok (B4 sahipliği, istek branch-notes'ta) | `svc_halt_flag_blocks_staged_update` |
| T-4 | Tampering | Kurulum klasörü ACL'i zayıf (kullanıcı EXE'yi değiştirebilir) | `install_dir_acl_ok` değilse her istemci `InstallDirInsecure` | `svc_spoof_identity_matrix` |
| R-1 | Repudiation | İşlemin kim tarafından istendiği sonradan inkâr edilir | Her yazma önce journal (oyuncu SID'i + işlem); hizmet ret nedeni günlüğe | Journal testleri (B4 sahipliği); hizmet günlüğü yalnız OutputDebugString — **açık: kalıcı denetim günlüğü yok** |
| I-1 | Info disclosure | Reddedilen istemci ret nedenini veya iç durumu öğrenir | Doğrulama reddinde hiçbir çerçeve gönderilmez (bağlantı kesilir); oturum hatası yalnız kısa neden metni | Platform davranışı (`service_host.cpp`); birim testi yok — `windows.*` hizmet testi gerektirir (açık) |
| D-1 | DoS | Aşırı uzun yük / dev alanlar bellek tüketir | `kMaxPayload` 64 KiB, Hello 256 B, request_id 64, JSON derinlik/öğe sınırı | `svc_neg_oversize_payloads`, `security_ipc_frame_mutations` |
| D-2 | DoS | Bağlantı başına sınırsız istek/çerçeve | Hizmet: 64 istek + 4096 güncelleme çerçevesi; broker: 64 çerçeve (çekirdekte de zorlanır) | `service_session_max_64_requests`, `svc_neg_extra_frame_limit`, `ipc_neg_broker_frame_limit` |
| D-3 | DoS | Asılı/oyalayan istemci hizmeti bekletir | Hello 5 sn, boşta 60 sn, ömür 2 sa | `service_connection_timer_limits` |
| E-1 | Elevation | İzin listesi dışı işlem, bilinmeyen eylem, DNS değiştirme, sürücü/üretici kurulumu hizmetten UAC'siz | `decode_request` + `service_handles_op`; bunlar UAC broker'ına kalır | `svc_neg_outside_allowlist`, `service_session_dns_change_and_driver_install_rejected`, `service_session_vendor_install_rejected` |
| E-2 | Elevation | Güncelleme uzantı noktasına beklenmeyen tür (Cancel, UpdateStatus, bilinmeyen) ulaşır | Uzantı yalnız UpdateBegin/Chunk/Commit/Healthy (8..11) görür; broker bunları hiç kabul etmez | `svc_neg_extra_hook_only_update_frames`, `ipc_neg_broker_update_frames_rejected` |
| E-3 | Elevation | Standart hesap hizmet üzerinden yetki kazanır | K27: istemci BUILTIN\Administrators üyesi olmalı (`NotAdmin`) | `svc_spoof_identity_matrix` |
| E-4 | Elevation | Aynı yönetici hesabındaki kod kurulu UI'ye enjekte olup UAC'siz hizmet işlemi yaptırır | Kabul edilmiş kalan risk (ADR-033): yalnız geri alınabilir izin listesi; geniş etkili işlemler UAC'de | Kalan risk; test yok (tasarım kararı) |

Açık kalanlar dürüstçe: R-1 kalıcı denetim günlüğü ve I-1 platform ret davranışı birim testle kanıtlı değildir; gerçek hizmet üzerinde `windows.*` testi kurulu hizmet ve UAC gerektirir (sahip/VM turu).
