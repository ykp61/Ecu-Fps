#pragma once
// Zorunlu guncelleme (ADR-028) ve kurulum penceresi (ADR-033) icin ortak yardimcilar: imzali manifestin indirilip
// karara baglanmasi, indirilen dosyanin yazilip geri okunmasi, ortam anahtari ve OS derleme numarasi. Ui (App) ve Setup
// rolu ayni kodu kullanir (kopya yok).

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "core/update/update_check.h"

namespace ef::app::upd {

// Test/otomasyon anahtari: EF_UPDATE_CHECK=0 iken ag kullanilmaz.
bool update_check_disabled_by_env();

// "26100.1234" -> 26100; okunamazsa 0 (OsTooOld: guncelleme sunulmaz, uydurma yok).
std::uint32_t leading_build(const std::string& s);
// environment.cpp ile ayni bicim: <build>.<ubr>; okunamazsa "?".
std::string os_build_string();

struct CheckResult {
    core::update::UpdateDecision decision;
    std::string envelope;  // yalniz Mandatory / NotWritable: imzali zarf (hizmet yeniden dogrular)
};
// kManifestUrl indirilir, gomulu yayin anahtariyla dogrulanir ve decide_update ile karara baglanir. highest_seen: kalici
// tekrar oynatma tabani. cancel ayarlandiysa nullopt (sonuc yazilmaz).
std::optional<CheckResult> check_for_update(const std::string& current_version, std::uint64_t highest_seen, bool exe_dir_writable,
                                            const std::atomic<bool>& cancel);

// Paylasimsiz yazar, FlushFileBuffers; hata olursa yarim dosya silinir.
bool write_file(const std::string& path, const std::string& data);
// max_bytes'tan buyuk dosya okunmaz.
std::optional<std::string> read_file(const std::string& path, std::uint64_t max_bytes);

}  // namespace ef::app::upd
