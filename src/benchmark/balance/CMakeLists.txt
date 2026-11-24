# Denge testi kosucusu (spec 2026-11-12 A2): Arena adimlari -> core::diagnosis::balance girdileri.
add_library(ef_bench_balance STATIC balance_runner.cpp)
target_include_directories(ef_bench_balance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(ef_bench_balance PUBLIC ef_bench_scene ef_core ef_options)
