# Tum birim test dosyalari tek EXE'de toplanir; yeni test dosyasi otomatik eklenir.
file(GLOB_RECURSE EF_UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(ef_unit_tests ${EF_UNIT_SOURCES})
target_include_directories(ef_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ef_unit_tests PRIVATE ef_app_measure ef_bench_runner ef_bench_balance ef_bench_scene ef_benchmark ef_ui_dialogs ef_ui ef_core ef_contracts ef_platform ef_options)
add_test(NAME unit COMMAND ef_unit_tests)
set_tests_properties(unit PROPERTIES LABELS "unit")
# UI denetimi B: neden kodu katalog kapsami testi kaynak dosyalarini tarar (salt okuma).
target_compile_definitions(ef_unit_tests PRIVATE EF_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
# Guvenlik regresyonu: urun EXE'sinin PE load config'i okunur (calistirilmaz); DLL ekmeye karsi DependentLoadFlags.
target_compile_definitions(ef_unit_tests PRIVATE EF_PRODUCT_EXE="$<TARGET_FILE:EcuFps>")
add_dependencies(ef_unit_tests EcuFps)
