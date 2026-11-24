add_subdirectory(unit)

# Windows entegrasyon testleri (gercek API, gercek dosya sistemi)
add_executable(ef_crash_probe windows/crash_probe.cpp)
target_link_libraries(ef_crash_probe PRIVATE ef_platform ef_options)
add_executable(ef_bundle_tool windows/bundle_tool.cpp)
target_link_libraries(ef_bundle_tool PRIVATE ef_platform ef_core ef_options)

add_executable(ef_inventory_tool windows/inventory_tool.cpp)
target_link_libraries(ef_inventory_tool PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.inventory
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_inventory.ps1
            -Tool $<TARGET_FILE:ef_inventory_tool>)
# TIMEOUT 180 (P9-B9): bosta ~34 sn, paralel derleme yukunde 60 sn sinirini asti (baska dal notu).
set_tests_properties(windows.inventory PROPERTIES TIMEOUT 180 LABELS "windows")

add_executable(ef_support_matrix windows/support_matrix_tool.cpp)
target_link_libraries(ef_support_matrix PRIVATE ef_platform ef_core ef_options)

add_test(NAME windows.crash_dump
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_crash.ps1
            -Probe $<TARGET_FILE:ef_crash_probe>)
add_test(NAME windows.support_bundle
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_bundle.ps1
            -Tool $<TARGET_FILE:ef_bundle_tool>)
# TIMEOUT 120 (P9-B9): bosta ~23 sn; yukte 60 sn sinirina yaklasiyordu.
set_tests_properties(windows.crash_dump windows.support_bundle PROPERTIES TIMEOUT 120 LABELS "windows")

add_executable(ef_ipc_probe windows/ipc_probe.cpp)
target_link_libraries(ef_ipc_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.ipc
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_ipc.ps1
            -Probe $<TARGET_FILE:ef_ipc_probe>)
set_tests_properties(windows.ipc PROPERTIES TIMEOUT 300 LABELS "windows")

add_executable(ef_scope_probe windows/scope_probe.cpp)
target_link_libraries(ef_scope_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.scope
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_scope.ps1
            -Probe $<TARGET_FILE:ef_scope_probe>)
set_tests_properties(windows.scope PROPERTIES TIMEOUT 120 LABELS "windows")

add_executable(ef_signature_probe windows/signature_probe.cpp)
target_link_libraries(ef_signature_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.trust
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_trust.ps1
            -Probe $<TARGET_FILE:ef_signature_probe> -App $<TARGET_FILE:EcuFps>
            -SignedSample ${CMAKE_COMMAND} -Policy $<IF:$<BOOL:${EF_RELEASE_TRUST}>,release,dev>
            -UnsignedAlpha $<IF:$<BOOL:${EF_UNSIGNED_ALPHA}>,on,off>)
set_tests_properties(windows.trust PROPERTIES TIMEOUT 120 LABELS "windows")

add_test(NAME windows.single_instance
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_single_instance.ps1
            -Exe $<TARGET_FILE:EcuFps> -Probe $<TARGET_FILE:ef_scope_probe>)
set_tests_properties(windows.single_instance PROPERTIES TIMEOUT 120 LABELS "windows" RUN_SERIAL TRUE)

add_executable(ef_journal_probe windows/journal_probe.cpp)
target_link_libraries(ef_journal_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.journal
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_journal.ps1
            -Probe $<TARGET_FILE:ef_journal_probe>)
set_tests_properties(windows.journal PROPERTIES TIMEOUT 120 LABELS "windows")

add_executable(ef_crash_recovery_probe windows/crash_recovery_probe.cpp)
target_link_libraries(ef_crash_recovery_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.crash_recovery
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_crash_recovery.ps1
            -Probe $<TARGET_FILE:ef_crash_recovery_probe>)
set_tests_properties(windows.crash_recovery PROPERTIES TIMEOUT 180 LABELS "windows")

add_test(NAME windows.offline
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_offline.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.offline PROPERTIES TIMEOUT 60 LABELS "windows")

add_executable(ef_uninstall_probe windows/uninstall_probe.cpp)
target_link_libraries(ef_uninstall_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.uninstall
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_uninstall.ps1
            -Probe $<TARGET_FILE:ef_uninstall_probe>)
set_tests_properties(windows.uninstall PROPERTIES TIMEOUT 120 LABELS "windows" RUN_SERIAL TRUE)

add_executable(ef_vendor_probe windows/vendor_probe.cpp)
target_link_libraries(ef_vendor_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.vendor_runtime
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_vendor.ps1
            -Probe $<TARGET_FILE:ef_vendor_probe>)
set_tests_properties(windows.vendor_runtime PROPERTIES TIMEOUT 120 LABELS "windows")

add_executable(ef_nvidia_probe windows/nvidia_probe.cpp)
target_link_libraries(ef_nvidia_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.nvidia
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_nvidia.ps1
            -Probe $<TARGET_FILE:ef_nvidia_probe>)
set_tests_properties(windows.nvidia PROPERTIES TIMEOUT 60 LABELS "windows")

add_executable(ef_power_probe windows/power_probe.cpp)
target_link_libraries(ef_power_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.power_read COMMAND ef_power_probe read)
set_tests_properties(windows.power_read PROPERTIES TIMEOUT 30 LABELS "windows")

add_executable(ef_display_process_probe windows/display_process_probe.cpp)
target_link_libraries(ef_display_process_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.process_adapter COMMAND ef_display_process_probe process)
add_test(NAME windows.display_read COMMAND ef_display_process_probe display)
set_tests_properties(windows.process_adapter windows.display_read PROPERTIES TIMEOUT 60 LABELS "windows")

add_executable(ef_startup_probe windows/startup_probe.cpp)
target_link_libraries(ef_startup_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.startup_list COMMAND ef_startup_probe list)
set_tests_properties(windows.startup_list PROPERTIES TIMEOUT 60 LABELS "windows")

add_executable(ef_tasks_services_probe windows/tasks_services_probe.cpp)
target_link_libraries(ef_tasks_services_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.tasks_services COMMAND ef_tasks_services_probe)
set_tests_properties(windows.tasks_services PROPERTIES TIMEOUT 120 LABELS "windows")

# WIN-09: salt okuma; hizmet durdurmaz, baslangic girdisi degistirmez.
add_executable(ef_background_probe windows/background_probe.cpp)
target_link_libraries(ef_background_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.background COMMAND ef_background_probe)
set_tests_properties(windows.background PROPERTIES TIMEOUT 60 LABELS "windows")

add_test(NAME windows.roles
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_roles.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.roles PROPERTIES TIMEOUT 90 LABELS "windows")

# CHK-06 / NET-01: ag okuyucu probu (salt okuma; ICMP yanki yalniz --ping ile). Ag ortamina bagli
# ve paket gonderdigi icin ctest'e eklenmez.
add_executable(ef_readiness_network_probe windows/readiness_network_probe.cpp)
target_link_libraries(ef_readiness_network_probe PRIVATE ef_platform ef_core ef_options)

# Windows smoke testi: gercek pencere acilir ve kendiliginden kapanir.
add_test(NAME smoke.app_open_close COMMAND EcuFps --smoke)
set_tests_properties(smoke.app_open_close PROPERTIES TIMEOUT 15 LABELS "windows")
# Uyumluluk B9 (bosluk 23): ilk calistirma / bos profil. %TEMP% altinda olmayan, Turkce karakterli veri klasoru
# (EF_DATA_DIR yalniz EF_TEST_BACKGROUND=1 ile kabul edilir): smoke + karsilama gorunur + tercih yonlendirilen klasore.
add_test(NAME windows.first_run
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_first_run.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.first_run PROPERTIES TIMEOUT 120 LABELS "windows;first_run" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

# WIN-07: surucu/guncelleme kuran veya indiren kod yolu yok (kaynak taramasi)
add_test(NAME windows.no_install
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_no_install.ps1)
set_tests_properties(windows.no_install PROPERTIES TIMEOUT 60 LABELS "windows")

# UI-01: ekran disi cizim; 100/125/150/200 DPI, iki pencere boyutu, tum sayfalar, iki dil
add_executable(ef_ui_snapshot windows/ui_snapshot.cpp)
target_link_libraries(ef_ui_snapshot PRIVATE ef_ui_dialogs ef_ui ef_core ef_options ole32 windowscodecs)
# wmain tek buyuk islevdir: yuzlerce durum (UiState / ViewModel yerelleri) tek yigin cercevesinde durur ve varsayilan
# 1 MB yigini asar (0xC00000FD). Yalniz bu denetim aracinin yigini buyutulur; urun EXE'si etkilenmez.
target_link_options(ef_ui_snapshot PRIVATE /SUBSYSTEM:CONSOLE /STACK:8388608)
add_test(NAME windows.ui_layout COMMAND ef_ui_snapshot)
# TIMEOUT 600 (P9-B9): tum sayfa x DPI x dil x boyut (8740 cizim) dist'te 112 sn surer; sinir 120 iken yuk
# yokken bile zaman asimi, debug'da her kosuda zaman asimi. Sinir takilmayi yakalar, sureyi sinirlamaz.
set_tests_properties(windows.ui_layout PROPERTIES TIMEOUT 600 LABELS "windows")

# UI-04: gercek pencerede UI Automation agaci (ad/rol/durum, SelectionItem/Toggle desenleri)
add_test(NAME windows.uia
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_uia.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.uia PROPERTIES TIMEOUT 90 LABELS "windows")

# UI-05: pencere 20 kez ac/kapat; GDI/USER nesnesi ve bellek sizintisi yok; gorunurluk durumu
add_executable(ef_ui_lifecycle windows/ui_lifecycle.cpp)
target_link_libraries(ef_ui_lifecycle PRIVATE ef_ui ef_core ef_options psapi)
target_link_options(ef_ui_lifecycle PRIVATE /SUBSYSTEM:CONSOLE)
add_test(NAME windows.ui_lifecycle COMMAND ef_ui_lifecycle)
# RUN_SERIAL: bellek/GDI olcumu paralel ctest yukunden etkilenmesin (esikler degismedi)
# TIMEOUT 300 (P9-B9): 200 tur dist'te ~22 sn, debug + paralel derleme yukunde 114 sn olculdu (sinir 120 iken zaman asimi
# flake'i); sinir olcumu degil yalniz takilmayi yakalar.
set_tests_properties(windows.ui_lifecycle PROPERTIES TIMEOUT 300 LABELS "windows" RUN_SERIAL TRUE)
# Sizinti-yakalama kaniti (kirmizi/yesil): bilerek sizdiran kosuda test sizintiyi bildirmeli.
# PASS_REGULAR_EXPRESSION cikis kodunu yok sayar; bu yuzden yalniz isaret satirina bakar.
add_test(NAME windows.ui_lifecycle_detects_heap_leak COMMAND ef_ui_lifecycle 100 --leak-heap=65536)
set_tests_properties(windows.ui_lifecycle_detects_heap_leak PROPERTIES TIMEOUT 240 LABELS "windows" RUN_SERIAL TRUE
                     PASS_REGULAR_EXPRESSION "LEAK_DETECTED heap")
add_test(NAME windows.ui_lifecycle_detects_private_leak COMMAND ef_ui_lifecycle 100 --leak-virt=131072)
set_tests_properties(windows.ui_lifecycle_detects_private_leak PROPERTIES TIMEOUT 240 LABELS "windows" RUN_SERIAL TRUE
                     PASS_REGULAR_EXPRESSION "LEAK_DETECTED private")
# P9-B9: 932576 baytlik (~911 KiB) tampon havuzu sinifi. Normal kosuda sayi sinirli oynar (ust zarf + alt zarf siniri
# windows.ui_lifecycle icinde); her turda 1 blok kalsa (yavas tutma / sizinti) "LEAK_DETECTED pool" yazilmali.
add_test(NAME windows.ui_lifecycle_detects_pool_leak COMMAND ef_ui_lifecycle 100 --leak-heap=932576)
set_tests_properties(windows.ui_lifecycle_detects_pool_leak PROPERTIES TIMEOUT 240 LABELS "windows" RUN_SERIAL TRUE
                     PASS_REGULAR_EXPRESSION "LEAK_DETECTED pool")

# Gercek pencere + DC cizim hedefi: WM_PAINT maliyeti, bos kare, gorunmezken cizim yok, kaynak sizintisi yok
add_executable(ef_paint_probe windows/paint_probe.cpp)
target_link_libraries(ef_paint_probe PRIVATE ef_ui ef_core ef_options psapi)
target_link_options(ef_paint_probe PRIVATE /SUBSYSTEM:CONSOLE)
add_test(NAME windows.paint_probe COMMAND ef_paint_probe)
set_tests_properties(windows.paint_probe PROPERTIES TIMEOUT 120 LABELS "windows")

# UI-06: tepsi yasam dongusu (kapat -> tepside sur, tepsiden ac, tek ornek, cik ve geri al)
add_test(NAME windows.tray
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_tray.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.tray PROPERTIES TIMEOUT 90 LABELS "windows")
# UI-06 + spec 2026-11-12 E: oyun onayi uygulama ici kip penceresiyle; UIA ile Evet / Hayir / kapat
add_executable(ef_consent_probe windows/consent_probe.cpp ${EF_APP_MANIFEST})
target_link_libraries(ef_consent_probe PRIVATE ef_ui_dialogs ef_ui ef_core ef_options)
target_link_options(ef_consent_probe PRIVATE /SUBSYSTEM:CONSOLE)
add_test(NAME windows.consent_dialog
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_consent.ps1
            -Probe $<TARGET_FILE:ef_consent_probe>)
set_tests_properties(windows.consent_dialog PROPERTIES TIMEOUT 90 LABELS "windows")
# BRAND-01: EXE ikon kaynaklari, Shell ikon boyutlari, temiz klasor, tepsi ikonu ve TaskbarCreated
add_test(NAME windows.brand
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_brand.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.brand PROPERTIES TIMEOUT 90 LABELS "windows")

# Spec 2026-11-12 §4: oyun ikonu yukleyicisi (konsol; pencere acmaz, sureci baslatmaz, yalniz dosya okur).
# Uygulamanin kendi EXE'si ikon kaynagi ornegidir; JPEG'ler kodla uretilir (gercek kurulu oyun gerekmez).
add_executable(ef_game_icon_probe windows/game_icon_probe.cpp)
target_link_libraries(ef_game_icon_probe PRIVATE ef_platform ef_core ef_options)
target_link_options(ef_game_icon_probe PRIVATE /SUBSYSTEM:CONSOLE)
add_test(NAME windows.game_icons COMMAND ef_game_icon_probe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.game_icons PROPERTIES TIMEOUT 120 LABELS "windows")

# UI-09: canli kaynak halkalari icin PDH ornekleyicisi (gercek sayaclar, ornek maliyeti olculur)
# Task 12: gercek ag hiz testi probu — istege bagli (EXCLUDE_FROM_ALL, add_test yok); varsayilan ctest cevrimdisi kalir.
add_executable(ef_speedtest_probe EXCLUDE_FROM_ALL windows/speedtest_probe.cpp)
target_link_libraries(ef_speedtest_probe PRIVATE ef_platform ef_core ef_options)
target_link_options(ef_speedtest_probe PRIVATE /SUBSYSTEM:CONSOLE)
# Spec 2026-11-12 H: gercek Windows Update surucu aramasi (salt okuma) — istege bagli konsol araci; add_test yok
# (arama Windows Update hizmeti uzerinden aga cikar, varsayilan ctest cevrimdisi kalir).
add_executable(ef_wua_search_probe windows/wua_search_probe.cpp)
target_link_libraries(ef_wua_search_probe PRIVATE ef_platform ef_core ef_options)
target_link_options(ef_wua_search_probe PRIVATE /SUBSYSTEM:CONSOLE)
add_executable(ef_live_probe windows/live_probe.cpp)
target_link_libraries(ef_live_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.live_metrics COMMAND ef_live_probe 6)
set_tests_properties(windows.live_metrics PROPERTIES TIMEOUT 60 LABELS "windows")
# WIN-08: disk temizligi; yalniz KENDI gecici agacinda gercek silme (junction + kilitli dosya)
add_executable(ef_cleanup_probe windows/cleanup_probe.cpp)
target_link_libraries(ef_cleanup_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.cleanup COMMAND ef_cleanup_probe tree)
set_tests_properties(windows.cleanup PROPERTIES TIMEOUT 60 LABELS "windows")
# UI-10: ayar bazli Evet/Hayir gercek cihaz (opt-in: EF_ALLOW_MUTATION=1; aksi halde atlanir)
add_test(NAME windows.ui_selection
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_ui_selection.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.ui_selection PROPERTIES TIMEOUT 120 LABELS "windows" SKIP_RETURN_CODE 77)
# UIA turlari (pencere acar): varsayilan calismada atlanir (cikis 77 = Skipped). Calistirmak icin
# EF_UIA_TOUR=1 (+ duzeltme turu icin EF_ALLOW_MUTATION=1); EF_TEST_BACKGROUND=1 betiklerde varsayilan.
# Etiket "uia_tour": ctest -L uia_tour ile yalniz bunlar; -LE uia_tour ile disarida birakilir.
# CHK "Duzelt": geri alinabilir HKCU oyun ayari -> Evet -> geri okuma -> journal -> geri al (ef_fix_journal) -> ozgun deger.
add_executable(ef_fix_journal windows/fix_journal_tool.cpp)
target_link_libraries(ef_fix_journal PRIVATE ef_platform ef_core ef_options)
target_include_directories(ef_fix_journal PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME windows.readiness_fix_tour
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_readiness_fix_tour.ps1
            -Exe $<TARGET_FILE:EcuFps> -FixTool $<TARGET_FILE:ef_fix_journal>)
set_tests_properties(windows.readiness_fix_tour PROPERTIES TIMEOUT 180 LABELS "windows;uia_tour" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
# Spec 2026-11-12 A4: canli test ekrani (GERCEK GPU; pencere acar): denge testi baslar, icerik alani canli gorunume gecer,
# canli sayilar gelir, Esc testi durdurur (sonuc kaydedilmez). Opt-in: EF_UIA_TOUR=1; aksi halde atlanir (77).
add_test(NAME windows.live_view_tour
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_live_view.ps1
            -Exe $<TARGET_FILE:EcuFps>)
set_tests_properties(windows.live_view_tour PROPERTIES TIMEOUT 240 LABELS "windows;uia_tour" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
# MEAS-01: PresentData ETW kare oturumu (yukseltilmemis: AccessDenied beklenir; yonetici: 2 sn topla + yetim yok)
add_executable(ef_frame_probe windows/frame_probe.cpp)
target_link_libraries(ef_frame_probe PRIVATE ef_measurement ef_platform ef_core ef_options)
add_test(NAME windows.frame_session COMMAND ef_frame_probe)
add_executable(ef_process_gpu_probe windows/process_gpu_probe.cpp)
target_link_libraries(ef_process_gpu_probe PRIVATE ef_measurement ef_options)
add_test(NAME windows.process_gpu COMMAND ef_process_gpu_probe)
set_tests_properties(windows.process_gpu PROPERTIES TIMEOUT 30 LABELS "windows")
add_executable(ef_capture_lifecycle windows/capture_lifecycle.cpp)
target_link_libraries(ef_capture_lifecycle PRIVATE ef_measurement ef_options)
add_test(NAME windows.capture_lifecycle COMMAND ef_capture_lifecycle)
set_tests_properties(windows.capture_lifecycle PROPERTIES TIMEOUT 120 LABELS "windows")
add_executable(ef_bench_scene_probe windows/bench_scene_probe.cpp)
target_link_libraries(ef_bench_scene_probe PRIVATE ef_bench_scene ef_measurement ef_core ef_options pdh)
add_test(NAME windows.bench_scene COMMAND ef_bench_scene_probe)
set_tests_properties(windows.bench_scene PROPERTIES TIMEOUT 180 LABELS "windows")
# Denge testi "Arena v1" (spec 2026-11-12 A1 / D): penceresiz konsol denemesi. ctest YALNIZ yazilim bagdastiricisinda
# (WARP, 320x180, <= 5 sn; puanlanmaz) kosar ve son kareleri PNG olarak yazar. Gercek GPU: ef_arena_probe --real
# (ctest'e kayitli degil; urun sahibi oyundayken kosulmaz).
add_executable(ef_arena_probe windows/arena_probe.cpp)
target_link_libraries(ef_arena_probe PRIVATE ef_bench_balance ef_bench_scene ef_platform ef_measurement ef_core ef_options ole32 windowscodecs)
add_test(NAME windows.arena_warp COMMAND ef_arena_probe --out ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(windows.arena_warp PROPERTIES TIMEOUT 60 LABELS "windows" SKIP_RETURN_CODE 77)
# Denge testi kosucusu (spec 2026-11-12 A2 / A4): ayni deneme, --balance. ctest YALNIZ WARP'ta, kucuk cozunurluk ve
# cok kisa adimlarla (yalniz test icin gecersiz kilma; puanlanmaz, hukum beklenmez) dokuz adimi, butce hatasini,
# iptali, canli sayaclari ve beklemesiz onizleme okumasini kosar; adim basina bir onizleme PNG'si yazar.
# Gercek GPU: ef_arena_probe --balance --real [--quick] (ctest'e kayitli degil).
add_test(NAME windows.balance_warp COMMAND ef_arena_probe --balance --out ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(windows.balance_warp PROPERTIES TIMEOUT 120 LABELS "windows" SKIP_RETURN_CODE 77)
# Uyumluluk B9 (bosluk 24): opt-in gercek GPU denge testi, her donanim bagdastiricisinda --balance --real --quick.
# Varsayilan kosuda atlanir (77); ctest_evidence.ps1 "real_gpu" etiketini disarida birakir. Calistirma:
#   set EF_REAL_GPU=1 && ctest --preset x64-dist -L real_gpu   (oyun acikken kosulmaz)
add_test(NAME windows.balance_real_gpu
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_balance_real_gpu.ps1
            -Probe $<TARGET_FILE:ef_arena_probe> -Out ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(windows.balance_real_gpu PROPERTIES TIMEOUT 900 LABELS "real_gpu" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
# EF-X22: GPU compute/geri okuma dogrulamasi (ekran disi; donanim GPU yoksa 77 = atlandi, gecti degil)
add_executable(ef_gpu_verify_probe windows/gpu_verify_probe.cpp)
target_link_libraries(ef_gpu_verify_probe PRIVATE ef_bench_scene ef_options)
add_test(NAME windows.gpu_verify COMMAND ef_gpu_verify_probe)
set_tests_properties(windows.gpu_verify PROPERTIES TIMEOUT 60 LABELS "windows" SKIP_RETURN_CODE 77)
# EF-X22: islemci hesap dogrulamasi (degisken yuk + CPUID etiketli SIMD varyantlari); pencere acmaz, ayar yazmaz.
add_executable(ef_cpu_verify_probe windows/cpu_verify_probe.cpp)
target_link_libraries(ef_cpu_verify_probe PRIVATE ef_benchmark ef_options)
add_test(NAME windows.cpu_verify COMMAND ef_cpu_verify_probe)
set_tests_properties(windows.cpu_verify PROPERTIES TIMEOUT 60 LABELS "windows" RUN_SERIAL TRUE)
set_tests_properties(windows.frame_session PROPERTIES TIMEOUT 60 LABELS "windows")
# BENCH-04: RAM alt testi; OS payi, iptal/hata sonrasi commit ve working set tabana donusu
add_executable(ef_memory_budget windows/memory_budget.cpp)
target_link_libraries(ef_memory_budget PRIVATE ef_benchmark ef_options psapi)
add_test(NAME windows.memory_budget COMMAND ef_memory_budget)
set_tests_properties(windows.memory_budget PROPERTIES TIMEOUT 120 LABELS "windows" RUN_SERIAL TRUE)

# P9-B9: bosta ayak izi butcesi (UI acik + tepsi). scripts/footprint.ps1 kendi baslattigi kopyayi YALNIZ OKUR
# (EF_TEST_BACKGROUND=1, EF_UPDATE_CHECK=0, gecici EF_DATA_DIR; calisan uygulamaya/hizmete dokunmaz). Ayni oturumda
# baska bir EcuFps arayuzu varsa kopya ona devredilirdi: 77 = SKIP.
# Esikler 2026-11-18 olcumunden (artifacts/evidence/P9-footprint-0.9.x.md) ~1,5-2x payla: dist acik WS 88-91 MiB,
# ozel WS 49-51, commit 62-68, tepsi WS 4,5-5, ozel WS 2,7-3,1, handle 631-640, tepsi CPU %0,04-0,08, acik CPU %0,2-1,1 (periyodik patlama), handle 631-740.
# Tepsi CPU esigi design/tokens.json idle_budgets (%0,2 makine); tepsi ozel WS esigi o dosyadaki 50 MiB'in altindadir.
# Debug: acik WS 96, commit 62,5, tepsi WS 10 (olculen). RUN_SERIAL: diger agir testlerin CPU/RAM yuku olcumu bozmasin.
add_test(NAME windows.footprint_budget
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/footprint.ps1
            -Exe $<TARGET_FILE:EcuFps> -WarmupSeconds 25 -IdleSeconds 20 -TraySeconds 15 -Check
            -MaxOpenWorkingSetMiB $<IF:$<CONFIG:Debug>,150,130>
            -MaxOpenPrivateWsMiB $<IF:$<CONFIG:Debug>,100,80>
            -MaxOpenPrivateMiB 110
            -MaxTrayWorkingSetMiB $<IF:$<CONFIG:Debug>,40,20>
            -MaxTrayPrivateWsMiB $<IF:$<CONFIG:Debug>,30,15>
            -MaxTrayPrivateMiB 110
            -MaxIdleCpuPercent 0.2 -MaxOpenCpuPercent 2.0
            -MaxHandles 900)
set_tests_properties(windows.footprint_budget PROPERTIES TIMEOUT 240 LABELS "windows;footprint" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

# BENCH-08: CPU (cpu_v2, 5 cekirdek, ST + tum mantiksal islemcilerle MT) + RAM (mem_v2, MT bant genisligi).
# ctest kisa kipte calisir; gercek dogrulama: ef_cpu_mem_probe quick (MT CPU kullanimi >= %90 kontrolu).
add_executable(ef_cpu_mem_probe windows/cpu_mem_probe.cpp)
target_link_libraries(ef_cpu_mem_probe PRIVATE ef_benchmark ef_options)
add_test(NAME windows.cpu_mem COMMAND ef_cpu_mem_probe short)
set_tests_properties(windows.cpu_mem PROPERTIES TIMEOUT 180 LABELS "windows" RUN_SERIAL TRUE)

# BENCH-05: depolama alt testi (gercek dosya sistemi, %TEMP% volume'unda kucuk test)
add_executable(ef_storage_test windows/storage_test.cpp)
target_link_libraries(ef_storage_test PRIVATE ef_benchmark ef_options)
add_test(NAME windows.storage COMMAND ef_storage_test)
set_tests_properties(windows.storage PROPERTIES TIMEOUT 180 LABELS "windows" RUN_SERIAL TRUE)

# BENCH-06: tam Uygulama Testi akisi + iptal (gercek donanim; GPU yoksa sahne "unavailable")
add_executable(ef_benchmark_flow windows/benchmark_flow.cpp)
target_link_libraries(ef_benchmark_flow PRIVATE ef_bench_runner ef_options)
add_test(NAME windows.benchmark_flow COMMAND ef_benchmark_flow)
set_tests_properties(windows.benchmark_flow PROPERTIES TIMEOUT 300 LABELS "windows" RUN_SERIAL TRUE)

# Uyumluluk B14: uzun test kosucusu kisa butceyle (tek oturum, kesintisiz dilimler, tek saat, iptal); ekran disi cizim
add_executable(ef_endurance_probe windows/endurance_probe.cpp)
target_link_libraries(ef_endurance_probe PRIVATE ef_bench_runner ef_options)
add_test(NAME windows.endurance_session COMMAND ef_endurance_probe)
set_tests_properties(windows.endurance_session PROPERTIES TIMEOUT 120 LABELS "windows" RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# GAME-02: CS2 video config salt okuma (dosya yoksa atlanir)
add_executable(ef_cs2_probe windows/cs2_probe.cpp)
target_link_libraries(ef_cs2_probe PRIVATE ef_core ef_options)
add_test(NAME windows.cs2_config COMMAND ef_cs2_probe)
set_tests_properties(windows.cs2_config PROPERTIES TIMEOUT 30 LABELS "windows")

# F-59/F-60: Steam secenekleri, CS2 video ve koruma servisleri (salt okuma; ham metin yazdirilmaz)
add_executable(ef_game_risk_probe windows/game_risk_probe.cpp)
target_link_libraries(ef_game_risk_probe PRIVATE ef_platform ef_options)
add_test(NAME windows.game_risk COMMAND ef_game_risk_probe)
set_tests_properties(windows.game_risk PROPERTIES TIMEOUT 30 LABELS "windows")

# GAME-04: config ayristirma hatasi oyunun dosyasini bozmaz (gercek dosyanin gecici kopyasi)
add_executable(ef_game_config windows/game_config.cpp)
target_link_libraries(ef_game_config PRIVATE ef_core ef_options)
add_test(NAME windows.game_config COMMAND ef_game_config)
set_tests_properties(windows.game_config PROPERTIES TIMEOUT 30 LABELS "windows")

# OPT-03: gercek A/B deneyi (opt-in EF_ALLOW_MUTATION=1; guc plani geri yuklenir). ctest'e eklenmez.
add_executable(ef_experiment_probe windows/experiment_probe.cpp)
target_link_libraries(ef_experiment_probe PRIVATE ef_bench_scene ef_core ef_options)

# MEAS-02: gercek oyun yakalama araci (yonetici PowerShell'de elle; ctest'e eklenmez)
add_executable(ef_game_capture windows/game_capture_tool.cpp)
target_link_libraries(ef_game_capture PRIVATE ef_measurement ef_core ef_options)

# OPT-06: oyun izleyici gercek surec turu (pencere acmaz, ayar degistirmez)
add_executable(ef_watcher_probe windows/watcher_probe.cpp)
target_link_libraries(ef_watcher_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.game_watcher COMMAND ef_watcher_probe)
set_tests_properties(windows.game_watcher PROPERTIES TIMEOUT 60 LABELS "windows")

# OPT-06: salt okuma "su an calisan oyun" araci (ctest disi)
# EF-X26-X30: yerel servis sozlesme testleri (ag/hesap yok). Python + cryptography gerekir.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME services.contracts COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/services/ecufps-api/test_contracts.py)
  set_tests_properties(services.contracts PROPERTIES LABELS "services")
  # Task 16 / ADR-028: yayin imzasi araci (gecici anahtar; gercek anahtar, %APPDATA% veya ag yok)
  add_test(NAME services.release_sign COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/scripts/test_release_sign.py)
  set_tests_properties(services.release_sign PROPERTIES LABELS "services")
  # Siki yerlesim §6: gomulen ICO dosyalari (boyut, 32 bpp DIB, token renkleri, yesil/gri dolu tepsi durumu).
  # Yalniz dosya okur (Pillow gerekmez, pencere acmaz); uretim: python -I scripts/build_brand_icons.py
  add_test(NAME windows.brand_icons COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/scripts/build_brand_icons.py --check)
  set_tests_properties(windows.brand_icons PROPERTIES LABELS "windows")
endif()
add_executable(ef_incident_probe windows/incident_probe.cpp)
target_link_libraries(ef_incident_probe PRIVATE ef_platform ef_core ef_options)
add_test(NAME windows.incident_reader COMMAND ef_incident_probe)
set_tests_properties(windows.incident_reader PROPERTIES LABELS "windows")
add_executable(ef_watch_now windows/watch_now_tool.cpp)
target_link_libraries(ef_watch_now PRIVATE ef_platform ef_core ef_options)

# REC-01/02: salt okuma gercek journal ozeti (ctest disi)
add_executable(ef_history_now windows/history_now_tool.cpp)
target_link_libraries(ef_history_now PRIVATE ef_platform ef_core ef_options)
target_include_directories(ef_history_now PRIVATE ${PROJECT_SOURCE_DIR}/src)

# MEAS-03: salt okuma surec GPU araci (ctest disi)
add_executable(ef_gpu_now windows/gpu_now_tool.cpp)
target_link_libraries(ef_gpu_now PRIVATE ef_measurement ef_options)

# REL-02/REL-03: dagitim icerigi (yalniz 4 dosya, loose dosya yok) + import tablosu
# yalniz Windows System32 DLL'leri. cmake --install ile gercek paket yerlesimi denetlenir;
# ADR-033: tasinabilir ZIP teslim dosyasi degildir (ZIP denetimi kaldirildi).
add_executable(ef_package_probe windows/package_probe.cpp)
target_link_libraries(ef_package_probe PRIVATE ef_options)
add_test(NAME windows.package
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_package.ps1
            -Probe $<TARGET_FILE:ef_package_probe> -App $<TARGET_FILE:EcuFps>
            -BuildDir ${CMAKE_BINARY_DIR} -Config $<CONFIG>)
set_tests_properties(windows.package PROPERTIES TIMEOUT 120 LABELS "windows")

# CHK-01/02/05: ekran + Windows oyun ayarlari okuyucu probu (salt okuma; ortama bagli cikti, ctest'e eklenmez).
add_executable(ef_readiness_display_probe windows/readiness_display_probe.cpp)
target_link_libraries(ef_readiness_display_probe PRIVATE ef_platform ef_core ef_options)
# CHK-03/04: salt okuma grafik/yazilim/BIOS hazirlik probu (ortama bagli cikti; ctest'e eklenmez)
add_executable(ef_readiness_graphics_probe windows/readiness_graphics_probe.cpp)
target_link_libraries(ef_readiness_graphics_probe PRIVATE ef_platform ef_core ef_options)
# B5: salt okuma MSI/kesme + bekleme bellegi esik probu (ortama bagli cikti; ctest'e eklenmez; bakim isi calistirmaz)
add_executable(ef_readiness_interrupt_probe windows/readiness_interrupt_probe.cpp)
target_link_libraries(ef_readiness_interrupt_probe PRIVATE ef_platform ef_core ef_options)

# DIAG-06/BM-02: kalibrasyon veri seti degerlendiricisi (gelistirici araci; pakete girmez).
add_executable(ef_diag_calibrate ${PROJECT_SOURCE_DIR}/tools/diag_calibrate/main.cpp)
target_link_libraries(ef_diag_calibrate PRIVATE ef_core ef_options)
add_test(NAME windows.diagcal
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_diagcal.ps1
            -Tool $<TARGET_FILE:ef_diag_calibrate>
            -Dataset ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diagcal/synthetic_v1.json
            -DatasetV2 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diagcal/synthetic_v2.json
            -DatasetLeak ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diagcal/synthetic_v2_leak.json
            -DatasetV1Real ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diagcal/real_v1_ungrouped.json)
set_tests_properties(windows.diagcal PROPERTIES TIMEOUT 60 LABELS "windows")

# ctest -j siniflandirmasi (yayin kapisi suresi): ctest -j 4 ile yalniz (c) paralel kosar.
# Siniflandirma betik/prob kaynagi okunarak yapildi; emin olunmayan RUN_SERIAL. Yeni test eklerken sinifini secin.
# (a) Pencere / odak / UIA / gercek EcuFps.exe baslatan testler: ayni oturumda tek ornek mutex'i, FindWindow, on plan/odak,
#     gercek %LOCALAPPDATA% journal'i, Global makine kilidi ve HKCU\Software\EcuFpsTest anahtari paylasilir. Birbirleriyle
#     ayni anda KOSMAZ: asagida DEPENDS ile tek zincire dizilir (ctest 3.31'de RESOURCE_LOCKS -j altinda uygulanmadi:
#     dort pencere testi ayni anda basladi ve windows.brand dustu). Zincir, pencere/odak acmayan (c) testleriyle
#     (unit, ui_layout, ...) ust uste binebilir; (c) siniflari EcuFps.exe baslatmaz ve pencere acmaz.
#     DEPENDS yalniz sirayi belirler: onceki test dusse de sonraki kosar.
#     2026-11-20: crash_dump/crash_recovery/ipc/journal/scope/trust/vendor_runtime/package de gercek exe baslatir; zincire eklendi
#     (local_ci debug turunda tray/uia/brand bu testlerle ust uste binince dustu).
# (b) Sure / bellek / CPU / ayak izi / benchmark olcen veya GPU/ETW/PDH-GPU kullanan testler: RUN_SERIAL (hicbir testle
#     birlikte kosmaz; paralel yuk esikleri bozar). frame_session ve capture_lifecycle ayni sabit ETW oturum adini kullanir.
# (c) Geri kalan: guid/PID'li gecici klasorle calisan, sabit ad paylasmayan salt okuma / saf hesap testleri: paralel.
set_tests_properties(windows.first_run windows.single_instance windows.uninstall PROPERTIES RUN_SERIAL FALSE)
set(_ef_gui_chain
    smoke.app_open_close windows.offline windows.roles
    windows.crash_dump windows.crash_recovery windows.ipc windows.journal windows.scope windows.trust windows.vendor_runtime
    windows.package
    windows.tray windows.uia windows.brand windows.consent_dialog
    windows.single_instance windows.first_run windows.uninstall windows.ui_selection)
set(_ef_gui_prev "")
foreach(_t IN LISTS _ef_gui_chain)
    if(_ef_gui_prev)
        set_tests_properties(${_t} PROPERTIES DEPENDS ${_ef_gui_prev})
    endif()
    set(_ef_gui_prev ${_t})
endforeach()
set_tests_properties(
    windows.paint_probe
    windows.bench_scene windows.arena_warp windows.balance_warp windows.gpu_verify windows.capture_lifecycle
    windows.frame_session windows.process_gpu
    PROPERTIES RUN_SERIAL TRUE)

# Zorunlu guncelleme kontrolu (ADR-028) testlerde aga cikmasin: UI rolunu baslatan her test EF_UPDATE_CHECK=0 devralir.
get_property(_ef_all_tests DIRECTORY PROPERTY TESTS)
if(_ef_all_tests)
    set_property(TEST ${_ef_all_tests} APPEND PROPERTY ENVIRONMENT "EF_UPDATE_CHECK=0")
    # ADR-030: uygulamayi baslatan testler gercek HKCU Run kaydina varsayilan acik kaydi yazmasin (yalniz okur).
    set_property(TEST ${_ef_all_tests} APPEND PROPERTY ENVIRONMENT "EF_AUTOSTART_READONLY=1")
    # ADR-033: testler EXE'yi derleme klasorunden (kurulum yeri disi) bos acar; kurulum penceresi yerine Ui acilsin ve
    # Ui "Kurulum gerekli" seridini gostermesin (pencere testlerinin yerlesimi degismez).
    set_property(TEST ${_ef_all_tests} APPEND PROPERTY ENVIRONMENT "EF_SETUP_ROLE=0")
endif()
