# Intel PresentMon PresentData (MIT, v2.6.0) - ETW present olay tuketicisi.
# Kaynak ve degisiklik kaydi: VERSION.md. Servis, ek EXE, vcpkg veya ag erisimi yoktur.
# Ucuncu taraf kodu proje uyari kurallarindan (/W4 /WX) muaf tutulur; yalniz bu hedef icin.

set(EF_PM_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(ef_presentdata STATIC
    ${EF_PM_DIR}/PresentData/Debug.cpp
    ${EF_PM_DIR}/PresentData/GpuTrace.cpp
    ${EF_PM_DIR}/PresentData/InterPresentActivity.cpp
    ${EF_PM_DIR}/PresentData/NvidiaTraceConsumer.cpp
    ${EF_PM_DIR}/PresentData/PresentMonTraceConsumer.cpp
    ${EF_PM_DIR}/PresentData/PresentMonTraceSession.cpp
    ${EF_PM_DIR}/PresentData/TraceConsumer.cpp
    ${EF_PM_DIR}/PresentData/TraceLogging.cpp
    # Upstream CommonUtilities'ten degistirilmeden alinanlar
    ${EF_PM_DIR}/IntelPresentMon/CommonUtilities/Hash.cpp
    ${EF_PM_DIR}/IntelPresentMon/CommonUtilities/Qpc.cpp
    # Ecu Fps shim gerceklemeleri
    ${EF_PM_DIR}/IntelPresentMon/CommonUtilities/Exception.cpp
    ${EF_PM_DIR}/IntelPresentMon/CommonUtilities/PrecisionWaiter.cpp
    ${EF_PM_DIR}/IntelPresentMon/CommonUtilities/str/String.cpp)

# Kullanicilar <PresentData/...> ile dahil eder; upstream kendi icinde "../IntelPresentMon/..." kullanir.
target_include_directories(ef_presentdata SYSTEM PUBLIC ${EF_PM_DIR})

# Upstream PresentData.vcxproj tanimlari; Windows surum degerleri projeyle ayni (Windows 10).
# PRESENTMON_ENABLE_DEBUG_TRACE=0: Debug derlemede de ayrintili wprintf izi ve assert() yok.
target_compile_definitions(ef_presentdata PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
    PRESENTMON_ENABLE_DEBUG_TRACE=0
    _LIB)
target_compile_options(ef_presentdata PRIVATE
    /W3 /WX- /EHsc /utf-8 /Zc:__cplusplus /guard:cf
    /wd4996)
target_link_libraries(ef_presentdata PUBLIC tdh advapi32)
