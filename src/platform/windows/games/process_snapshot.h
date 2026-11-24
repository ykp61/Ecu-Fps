#pragma once
// OPT-06 platform: surec anlik goruntusu (PID + olusturma zamani + exe yolu). Yalniz
// PROCESS_QUERY_LIMITED_INFORMATION ile sorgu; surece yazma/enjeksiyon yok. Erisilemeyen surecin yolu bos.

#include "core/games/watcher.h"

namespace ef::platform::games {

// Tam surec listesi okunamazsa state=Failed ve processes bos/eksik olabilir. Tek tek
// OpenProcess/QueryFullProcessImageName reddi normal sistem sureclerinde beklenir; bu
// surecler exe_name ile listelenir ve inaccessible_count'a eklenir, taramanin tamamini
// basarisiz yapmaz. Oyuna ait olup olmadigi GameWatcher'da bilinen EXE adlariyla denetlenir.
core::games::ProcessSnapshot snapshot_processes();

}  // namespace ef::platform::games
