#pragma once
// T3b (ADR-043 ek): eski UI "--updated-from=<surum>" ile ESKI yolda baslattiginda eski hizmete saglik sinyali (Win32
// tasima). Sira ve denetimler core/legacy/legacy_health.h'dadir (saf, birim testli). Yalniz istemci: SCM salt okunur,
// eski pipe'a SECURITY_IDENTIFICATION ile baglanilir, sunucu PID'i SCM PID'iyle karsilastirilir. Eski hizmet yoksa
// hicbir sey yapilmaz; hata asla UI'yi durdurmaz (cagiran sonucu yalniz loglar).

#include <string_view>

#include "core/legacy/legacy_health.h"
#include "core/legacy/legacy_install.h"

namespace ef::platform {

// Calisan EXE eski kurulu EXE mi (ham ve son yol ikisi de; baglanti/sembolik bag yok). Yalniz okuma.
bool launched_from_legacy_install();

// T3c (ADR-044): wWinMain icin eski yoldan baslatma yonlendirmesi. Eski yol degilse Normal. Yukseltme kaydi
// (yeni ProgramData\...\setup\legacy-upgrade.json) yalniz gerektiginde okunur (GetFileAttributesW; yok/yol yok disindaki
// her hata "var" sayilir). Yalniz okuma.
core::legacy::LegacyRoute legacy_launch_route(std::wstring_view command_line);

// launched_from_legacy_install() degilse hicbir sey yapmaz (NoOldService doner).
core::legacy::LegacyHealthOutcome send_legacy_health_signal(std::string_view version);

}  // namespace ef::platform
