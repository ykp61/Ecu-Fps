// Ecu Fps shim: Intel PresentMon CommonUtilities/win/WinAPI.h yerine.
// Upstream dosya _WIN32_WINNT degerini 0x0603'e zorlar; burada proje degerleri
// (CMake hedef tanimlari, Windows 10) korunur ve yalniz windows.h dahil edilir.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef STRICT
#define STRICT
#endif
#include <windows.h>
