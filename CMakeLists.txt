cmake_minimum_required(VERSION 3.25)

project(EcuFps
    VERSION 0.9.6
    DESCRIPTION "Ecu Fps - Windows oyun performansi"
    LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "Ecu Fps yalniz MSVC (x64) ile derlenir. scripts/dev.ps1 kullanin.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Yalniz x64 hedeflenir.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statik CRT (tek EXE, VC++ redistributable gerekmez)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Derleme kimligi: ayni 0.1.0 surumundeki farkli derlemeler ayirt edilir (git yoksa "unknown"; degisiklik varsa -dirty).
# EF-X18 dogrulama bulgusu 1: kimlik configure aninda sabitlenmez; ef_build_id hedefi HER derlemede git'i
# yeniden okur (cmake/gen_build_id.cmake) ve baslik yalniz kimlik degisince yeniden yazilir. Boylece
# configure'suz `build` da gercek HEAD/-dirty durumunu tasir; gate kapsami eski commit'i "temiz" sanmaz.
find_package(Git QUIET)
set(EF_GIT_FOR_BUILD_ID "")
if(GIT_FOUND)
    set(EF_GIT_FOR_BUILD_ID "${GIT_EXECUTABLE}")
endif()
set(EF_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(EF_BUILD_ID_HEADER "${EF_GENERATED_DIR}/ef_build_id.h")
file(MAKE_DIRECTORY "${EF_GENERATED_DIR}")
set(EF_BUILD_ID_ARGS -DEF_GIT=${EF_GIT_FOR_BUILD_ID} -DEF_SRC=${PROJECT_SOURCE_DIR} -DEF_OUT=${EF_BUILD_ID_HEADER})
# configure aninda da bir kez: baslik ilk derlemeden once vardir ve kimlik konsolda gorulur.
execute_process(COMMAND ${CMAKE_COMMAND} ${EF_BUILD_ID_ARGS} -DEF_PRINT=ON -P "${PROJECT_SOURCE_DIR}/cmake/gen_build_id.cmake")
add_custom_target(ef_build_id ALL
    COMMAND ${CMAKE_COMMAND} ${EF_BUILD_ID_ARGS} -P "${PROJECT_SOURCE_DIR}/cmake/gen_build_id.cmake"
    BYPRODUCTS "${EF_BUILD_ID_HEADER}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Ecu Fps derleme kimligi (git HEAD + dirty)"
    VERBATIM)

option(EF_BUILD_TESTS "Birim testlerini derle" ON)
# Yayin guven ilkesi: ON iken dev muafiyeti (imzasiz broker) ikiliye hic derlenmez (SEC-04).
option(EF_RELEASE_TRUST "Release trust policy (signed broker only)" OFF)
# ADR-034 imzasiz alpha istisnasi: ON iken release ilkesi HIC imzasi olmayan EXE'yi kabul eder (Invalid/Untrusted/
# Unknown yine reddedilir). Kod imzalama geldigi gun x64-dist preset'inde OFF yapilmalidir.
option(EF_UNSIGNED_ALPHA "Release trust: accept unsigned EXE (unsigned alpha exception, ADR-034)" OFF)

# Tum hedeflerin ortak derleme kurallari
add_library(ef_options INTERFACE)
target_compile_definitions(ef_options INTERFACE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
    EF_RELEASE_TRUST=$<BOOL:${EF_RELEASE_TRUST}>
    EF_UNSIGNED_ALPHA=$<BOOL:${EF_UNSIGNED_ALPHA}>)
target_compile_options(ef_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/utf-8;/EHsc;/Zc:__cplusplus;/Zc:preprocessor;/Zc:inline;/sdl;/guard:cf;/diagnostics:caret>")
target_link_options(ef_options INTERFACE
    /guard:cf /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT
    "$<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>")

# Ucuncu taraf statik kutuphaneler (kendi uyari ayarlariyla)
add_subdirectory(third_party/presentmon)

add_subdirectory(src)

if(EF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Portable paket (REL-02/REL-03): ZIP kokunde YALNIZ
#     EcuFps.exe + LICENSE.txt + NOTICE.txt + README.txt. Loose DLL/config/asset yok.
install(TARGETS EcuFps RUNTIME DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE.txt" DESTINATION . RENAME LICENSE.txt)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/NOTICES" DESTINATION . RENAME NOTICE.txt)
configure_file("${PROJECT_SOURCE_DIR}/packaging/README.txt.in"
               "${CMAKE_CURRENT_BINARY_DIR}/package_README.txt" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/package_README.txt" DESTINATION . RENAME README.txt)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "EcuFps")
set(CPACK_PACKAGE_VENDOR "Ecu Fps")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "EcuFps-${PROJECT_VERSION}-win64")
# ADR-033: CPack ZIP yalniz yerel ara cikti; teslim dosyasi degildir (yayin: EcuFps-Setup-<v>.exe).
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
# ZIP icinde ust klasor yok: dosyalar kokte.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
