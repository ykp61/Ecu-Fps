# BENCH-06: Uygulama Testi orkestrasyonu (sahne + CPU + RAM + depolama + birlesik).
add_library(ef_bench_runner STATIC runner.cpp ../endurance/endurance_runner.cpp)
target_include_directories(ef_bench_runner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(ef_bench_runner PUBLIC ef_bench_scene ef_benchmark ef_core ef_options)
