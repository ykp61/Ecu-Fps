# Gomulu manifest kimligi CMake project(VERSION) ile uretilir (elle surum yazilmaz; FILEVERSION ile ayni kaynak).
configure_file(app.manifest.in "${EF_GENERATED_DIR}/EcuFps.manifest" @ONLY)
set(EF_APP_MANIFEST "${EF_GENERATED_DIR}/EcuFps.manifest" CACHE INTERNAL "uretilen uygulama manifesti")
# P9/B5: olcum denetleyicisi (durum + is parcaciklari) App'ten ayri statik kutuphane: App yalniz MeasureHost'u uygular,
# birim testleri (tests/unit/test_measure_controller.cpp) sahte host ile baglanir.
add_library(ef_app_measure STATIC
    measure_controller.cpp
    measure_builtin.cpp
    measure_balance.cpp
    measure_ingame.cpp
    measure_proof.cpp
    measure_sampling.cpp)
target_link_libraries(ef_app_measure PUBLIC ef_bench_runner ef_bench_balance ef_ui ef_core ef_platform ef_options)
add_executable(EcuFps WIN32
    main.cpp
    model_loader.cpp
    readiness_collect.cpp
    app_health.cpp
    app.cpp
    app_games.cpp
    app_readiness.cpp
    app_measure.cpp
    app_drivers.cpp
    app_system.cpp
    app_update.cpp
    update_support.cpp
    setup_app.cpp
    app.rc
    "${EF_APP_MANIFEST}")
target_link_libraries(EcuFps PRIVATE ef_app_measure ef_bench_runner ef_bench_balance ef_ui_dialogs ef_ui ef_core ef_platform ef_options user32 dwmapi)
# Manifest app.manifest ile verilir (CMake gomer); linker'in UAC parcasini kapat.
target_link_options(EcuFps PRIVATE /MANIFESTUAC:NO)
# Guvenlik (setup DLL ekme): ayni EXE Indirilenler / %LOCALAPPDATA%\EcuFps\setup gibi kullanici yazilabilir bir
# klasorden UAC ile yukseltilir. Statik importlar main'den (harden_dll_search) ONCE cozulur; uygulama klasorune
# birakilan version.dll / dwmapi.dll / winhttp.dll vb. yonetici olarak yuklenirdi. 0x800 = LOAD_LIBRARY_SEARCH_SYSTEM32:
# yukleyici statik bagimliliklari yalniz System32'den cozer (SxS manifest yonlendirmesi etkilenmez).
target_link_options(EcuFps PRIVATE /DEPENDENTLOADFLAG:0x800)
# Derleme kimligi her derlemede uretilen basliktan (EF-X18 dogrulama bulgusu 1).
# ICO dosyalari da bagimlilik: ikon yeniden uretilince (scripts/build_brand_icons.py) kaynak yeniden derlenir.
file(GLOB EF_BRAND_ICOS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/branding/ecu-fps/app/*.ico"
     "${PROJECT_SOURCE_DIR}/assets/branding/ecu-fps/tray/*.ico")
set_source_files_properties(app.rc PROPERTIES OBJECT_DEPENDS "${EF_BUILD_ID_HEADER};${EF_BRAND_ICOS}")
target_include_directories(EcuFps PRIVATE ${EF_GENERATED_DIR})
add_dependencies(EcuFps ef_build_id)
