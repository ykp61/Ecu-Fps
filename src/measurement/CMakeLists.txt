# Olcum katmani (MEAS-01): ETW kare oturumu. Uygulamaya henuz baglanmaz.
add_library(ef_measurement STATIC
    frame/frame_session.cpp
    telemetry/process_gpu.cpp
    telemetry/adapter_gpu.cpp
    frame/game_capture.cpp
    frame/capture_result.cpp)
target_include_directories(ef_measurement PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ef_measurement PUBLIC ef_options ef_core PRIVATE ef_presentdata tdh advapi32 pdh)
