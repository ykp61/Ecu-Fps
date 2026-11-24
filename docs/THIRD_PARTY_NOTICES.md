# Üçüncü taraf bildirimleri (REL-03)

Durum: taslak, hukukçu onayı yok (ürün sahibi onayına kadar böyle kalır). Bu belge `third_party/` dizinlerinden, CMake dosyalarından ve ürün EXE'sinin gerçek import tablosundan derlenir; `b11_*` birim testleri (`tests/unit/core/test_third_party_inventory.cpp`) bileşenlerin, sabitlenmiş sürüm/commit değerlerinin ve DLL listesinin bu belge, `third_party/NOTICES`, `docs/13_DEPENDENCIES_LICENSES.md` ve site sayfasıyla (`site/ucuncu-taraf-lisanslari.html`) uyumunu denetler.

Ecu Fps tek EXE olarak dağıtılır (yanında loose DLL/config/asset yok). Bildirim dosyası
`third_party/NOTICES`'tır: gömülü/yanında kullanılan üçüncü taraf bileşen
listesini ve NVAPI, PresentMon (MIT) ile JetBrains Mono (SIL OFL 1.1) lisans metinlerinin tamamını içerir.
Yayın tek kurulum EXE'si olduğundan ayrı bir `NOTICE.txt` teslim edilmez; CMake `install` paketi
(`ctest -R windows.package`) onu `NOTICE.txt` olarak yerleştirmeye devam eder. Uygulama içinde
Ayarlar > "Üçüncü taraf lisansları" kartı telif satırlarını ve lisans
metinlerini gösterir (EXE içine gömülü katalog metni; yan dosya yok).

| Bileşen | Sürüm | Lisans | Kullanım | Lisans metni |
|---|---|---|---|---|
| NVIDIA NVAPI SDK (başlıklar) | `third_party/nvapi`, commit `70d337db9186e968eab622f7e786de7e437faf3d` (2026-09-18) | MIT (SPDX, © 2024 NVIDIA CORPORATION & AFFILIATES) | Yalnız başlık; `nvapi64.dll` sürücüyle gelir, System32'den imza doğrulamalı yüklenir | third_party/nvapi/License.txt |
| JetBrains Mono (Medium ve SemiBold alt kümesi) | 2.304 (`third_party/fonts/jetbrains-mono`, kaynak: github.com/JetBrains/JetBrainsMono, v2.304 yayını; arşiv SHA-256 `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf`) | SIL Open Font License 1.1 (Copyright 2020 The JetBrains Mono Project Authors) | EXE'ye bayt dizisi olarak gömülü; DirectWrite bellekten yükler, diske yazılmaz | third_party/fonts/jetbrains-mono/OFL.txt |
| Intel PresentMon — PresentData | v2.6.0 / `e13fce6acdb55a808fd8318175a56863e532d95f` | MIT (Copyright (C) 2017-2024 Intel Corporation) | Statik `ef_presentdata`; yalnız ölçüm/test araçlarında, `EcuFps.exe`'ye bağlı değil | third_party/presentmon/LICENSE.txt |
| MSVC CRT/STL | 14.44.35207 | Microsoft VC++ çalışma zamanı (uygulamaya statik bağlama izinli) | Statik `/MT` | — |
| 02/BOOST marka varlıkları | v1.0.0 | Ürün sahibine ait, kullanıcı onaylı (ADR-017) | Gömülü ICO/SVG | docs/branding/PROVENANCE.md |

Kullanılmayan / eklenmeyen: **AMD ADLX SDK** (ADLX SDK License Agreement
kabul edilmedi, B-006; başlık repoda yok, kod bağlanmıyor), **Intel IGCL**
(Intel cihaz yok, B-004; başlık/runtime yok), cereal/concurrentqueue/
PresentMonAPI2/servis (PresentMon'un yalnız PresentData kısmı alındı),
Windows fontları (sistem fontu kullanılır, kopyalanmaz). Tam paket içeriği
ve doğrulaması: docs/09_BUILD_RELEASE.md, `ctest -R windows.package`.

## Ağ ve harici bileşenler

Üçüncü taraf ağ kitaplığı yoktur. Ağ erişimi yalnız Windows'un kendi WinHTTP, DNS sorgusu ve ICMP işlevleriyle,
yalnız `src/platform/windows/net/` ve `src/platform/windows/readiness/` altında yapılır. Bağlanılan hizmetler
(GitHub yayın deposu, Cloudflare hız testi, DNS sunucuları, Windows Update) bu belgenin konusu olan gömülü kod
değil, çalışma zamanı hizmetleridir; adresleri ve davranışı `site/gizlilik-kvkk.html` içinde listelenir.
Cloudflare hız testi için yazılı bir kullanım izni alınmamıştır (ADR-027 açık risk).
