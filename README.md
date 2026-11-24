# Ecu Fps

Önceki adı: iTunex Pro

Türkçe, hafif bir Windows oyun performansı uygulaması (C++20, Win32, Direct2D/DirectWrite, statik CRT). Ücretsiz **imzasız alpha**: dağıtım henüz dijital olarak imzalı değildir, Windows SmartScreen uyarı gösterebilir. Ürün sahibi kararıyla kapsam ve durum `docs/` ve `state/` altında tutulur; bu dosya yalnız yön gösterir.

## Ne yapar

- Tek kullanıcı modu: **Performans Açık / Kapalı**. Uygulanacak her işlem "Evet / Hayır" ile onaylanır; Windows UAC ayrı bir OS iznidir.
- **Kapalı**, uygulamanın kaydettiği önceki değerleri geri yüklemesidir; fabrika sıfırlaması değildir.
- Her yazma işlemi önce kalıcı başlangıç kaydı, sonra uygulama, geri okuma ve doğrulanabilir geri alma ister.
- Ölçüm: Uygulama Testi, darboğaz testi, internet hızı ve gecikme; önce/sonra karşılaştırma. Oyunda Test kaynağı, doğrulanmış korumasız oyun satırı olmadığı için kapalıdır (`docs/19_GAME_ACCEPTANCE_ROUND.md`).
- Etki **vaat edilmez**: ölçülmemiş FPS, sensör, başarı veya benchmark sonucu gösterilmez. Şimdiye kadar yapılan gerçek ölçümlerde ölçülebilir bir fark çıkmadı ("Fark yok"); ayrıntı `docs/audit/2026-11-18-vaat-denetimi.md`.
- Güvenlik/anti-cheat kapatma, açık sürücü, oyuna enjeksiyon, oyun belleği okuma ve genel BIOS/voltaj yazımı **yoktur**.

## Dağıtım biçimi

Tek EXE: `EcuFps-Setup-<sürüm>.exe` aynı programdır; Program Files altına işlemsel kurulum (UAC bir kez) ve bir yardımcı hizmet (`EcuFpsHelper`, yalnız izin listesindeki işlemleri yürütür) kurar. Yanında loose DLL/config/asset yoktur. Açılışta imzalı sürüm bildirimi denetlenir ve yeni sürüm zorunludur (ADR-028, ADR-033). Ağ kullanımının tam listesi: `site/gizlilik-kvkk.html`. Üçüncü taraf bileşenler: `docs/THIRD_PARTY_NOTICES.md`, `third_party/NOTICES`.

## Klasörler

| Yol | İşlev |
|---|---|
| `AGENTS.md`, `CLAUDE.md` | Ajan talimatları ve ürün değişmezleri |
| `docs/` | Ürün, mimari, güvenlik, test, dağıtım ve karar (ADR) belgeleri; `docs/audit/` denetimler |
| `src/`, `tests/` | Kaynak kod ve testler (`tests/unit` birim, `tests/windows` Windows/sistem testleri) |
| `scripts/` | Derleme (`dev.ps1`), kanıtlı test (`ctest_evidence.ps1`), yayın ve oyun turu (`game_round.ps1`) betikleri |
| `site/` | Statik tanıtım ve hukuk sayfaları (sunucuya yükleme sahibin kararıdır) |
| `design/` | Renk/ölçü belirteçleri ve metin katalogları |
| `tasks/`, `state/` | Görev kaydı ve oturum durumu (yetkili kaynak) |
| `third_party/` | Sabitlenmiş üçüncü taraf kod/başlık ve lisansları |
| `artifacts/evidence/` | Gerçek komut çıktıları ve kanıt notları |

## Derleme ve test

Visual Studio Build Tools 2022 (C++ iş yükü) gerekir; sürümler `docs/13_DEPENDENCIES_LICENSES.md` içinde sabittir.

```
.\scripts\dev.ps1 build -Config debug     # yapılandır + derle + ctest
.\scripts\dev.ps1 build -Config dist      # dağıtım derlemesi
.\scripts\ctest_evidence.ps1              # kanıtlı tam kapı
```

Mock testi gerçek Windows veya GPU testi sayılmaz; gerçek cihaz/oyun kanıtı olmayan işlev etkin gösterilmez.

## Oyun turu

Performans Açık/Kapalı etkisinin gerçek bir oyunda ölçülmesi için protokol ve araç hazırdır (`docs/18_CALIBRATION_PROTOCOL.md` bölüm 7, `scripts/game_round.ps1`). Gerçek tur yalnız sahibin yanında, çevrimdışı ve korumasız bir oyunla yapılır; betik hiçbir ayar yazmaz. Henüz gerçek oyun sonucu yoktur.

## Lisans

Kaynak kodu **GPL-3.0-or-later** ile lisanslıdır (`LICENSE.txt`). Üçüncü taraf bileşenler `third_party/NOTICES` içindedir. "Ecu Fps" adı ve logosu GPL kapsamında değildir (`TRADEMARK.md`). Kod imzalama politikası: `CODE_SIGNING_POLICY.md`. Resmi sürümler: https://github.com/ykp61/Ecu-Fps-releases

## Lisans

Kaynak kodu **GPL-3.0-or-later** ile lisanslıdır (`LICENSE.txt`). Üçüncü taraf bileşenler `third_party/NOTICES` içindedir. "Ecu Fps" adı ve logosu GPL kapsamında değildir (`TRADEMARK.md`). Kod imzalama politikası: `CODE_SIGNING_POLICY.md`. Resmi sürümler: https://github.com/ykp61/Ecu-Fps-releases

## Hukuk metinleri

`site/eula.html`, `site/gizlilik-kvkk.html` ve `site/ucuncu-taraf-lisanslari.html` **taslaktır; hukukçu onayı yoktur** ve ürün sahibi onaylayana kadar öyle etiketli kalır.

## Öncelik

Kullanıcının güncel talimatı > güncel ürün sözleşmeleri > kayıtlı mimari kararlar > görevler > arşiv.
