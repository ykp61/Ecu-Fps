#pragma once
// UI-05: GPU entegre mi? DXCore IsIntegrated ozelligi cihaz OLUSTURMADAN cekirdek surucuden okunur;
// boylece UI sureci GPU kullanici modu suruculerini (yuzlerce MiB adres alani) yuklemez.
// dxcore.dll yalniz System32'den calisma aninda yuklenir (Windows 10 2004+).

#include <windows.h>

#include <optional>

namespace ef::platform::inventory {

// LUID'e gore entegre/harici. DXCore yoksa veya adaptor bulunamazsa nullopt.
std::optional<bool> dxcore_is_integrated(LUID luid);

}  // namespace ef::platform::inventory
