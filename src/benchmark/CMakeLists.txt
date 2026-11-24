# Yerlesik benchmark (BENCH-*): alt testler tek statik kutuphanede toplanir. Uygulamaya henuz baglanmaz.
add_library(ef_benchmark STATIC
    cpu/cpu_workload.cpp
    cpu/cpu_groups.cpp
    cpu/cpu_verify.cpp
    memory/memory_budget.cpp
    memory/memory_bench.cpp
    storage/storage_rules.cpp
    storage/storage_test.cpp
    endurance/endurance.cpp)
target_include_directories(ef_benchmark PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ef_benchmark PUBLIC ef_options)
# CPU FP cekirdekleri bit-ayni checksum ister: kesin FP modeli acikca secilir. MSVC'de /fp:contract-
# bayragi yoktur; FMA birlestirmesi cpu_workload.cpp icinde "#pragma fp_contract(off)" ile kapatilir.
target_compile_options(ef_benchmark PRIVATE /fp:precise)
