# BENCH-01/BENCH-07: deterministik sahne + D3D11 ekran disi renderer. Shader'lar derleme zamani fxc ile
# basliga derlenir ve gomulur (calisma aninda d3dcompiler veya dosya yok).
find_program(EF_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64")
if(NOT EF_FXC)
    file(GLOB _fxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT _fxc_candidates)
    list(REVERSE _fxc_candidates)
    list(GET _fxc_candidates 0 EF_FXC)
endif()
if(NOT EF_FXC)
    message(FATAL_ERROR "fxc.exe bulunamadi (Windows SDK)")
endif()

set(_gen ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${_gen})
# ad|profil|giris|kaynak -> gen/<ad>.h icinde g_<ad>
# arena.hlsl: denge testi "Arena v1" (spec 2026-11-12 A1); bloom + tonemap scene.hlsl'den ortak kullanilir.
set(_shaders
    "scene_vs|vs_5_0|vs_main|scene.hlsl"
    "scene_ps_gbuffer|ps_5_0|ps_gbuffer|scene.hlsl"
    "scene_vs_fullscreen|vs_5_0|vs_fullscreen|scene.hlsl"
    "scene_ps_light|ps_5_0|ps_light|scene.hlsl"
    "scene_ps_blur|ps_5_0|ps_blur|scene.hlsl"
    "scene_ps_tonemap|ps_5_0|ps_tonemap|scene.hlsl"
    "scene_ps_preview|ps_5_0|ps_preview|scene.hlsl"
    "arena_vs_inst|vs_5_0|vs_arena_inst|arena.hlsl"
    "arena_vs_object|vs_5_0|vs_arena_object|arena.hlsl"
    "arena_vs_tunnel|vs_5_0|vs_arena_tunnel|arena.hlsl"
    "arena_ps_gbuffer|ps_5_0|ps_arena_gbuffer|arena.hlsl"
    "arena_ps_light|ps_5_0|ps_arena_light|arena.hlsl")
set(_shader_headers)
foreach(_s IN LISTS _shaders)
    string(REPLACE "|" ";" _parts "${_s}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _profile)
    list(GET _parts 2 _entry)
    list(GET _parts 3 _file)
    set(_hlsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file})
    add_custom_command(OUTPUT ${_gen}/${_name}.h
        COMMAND "${EF_FXC}" /nologo /T ${_profile} /E ${_entry} /O3 /WX /Vn g_${_name} /Fh ${_gen}/${_name}.h ${_hlsl}
        DEPENDS ${_hlsl} VERBATIM)
    list(APPEND _shader_headers ${_gen}/${_name}.h)
endforeach()
# EF-X22: GPU hesap dogrulamasi compute shader'i (ayri kaynak dosyasi).
set(_verify_hlsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/verify.hlsl)
add_custom_command(OUTPUT ${_gen}/verify_cs.h
    COMMAND "${EF_FXC}" /nologo /T cs_5_0 /E cs_verify /O3 /WX /Vn g_verify_cs /Fh ${_gen}/verify_cs.h ${_verify_hlsl}
    DEPENDS ${_verify_hlsl} VERBATIM)
list(APPEND _shader_headers ${_gen}/verify_cs.h)

add_library(ef_bench_scene STATIC
    scene.cpp
    frame_sim.cpp
    arena.cpp
    sim_pool.cpp
    d3d_renderer.cpp
    gpu_verify_rules.cpp
    gpu_verify.cpp
    ${_shader_headers})
target_include_directories(ef_bench_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../.. PRIVATE ${_gen})
target_link_libraries(ef_bench_scene PUBLIC ef_options PRIVATE d3d11 dxgi)
