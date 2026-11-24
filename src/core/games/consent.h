#pragma once
// UI-06 (ADR-013): oyun basina tek seferlik onay. Oyun ilk algilandiginda kullaniciya bir kez
// Evet/Hayir sorulur; karar kalici yazilir ve Ayarlar'dan degistirilebilir/kaldirilabilir.
// Onaysiz oyunda hicbir ayar otomatik uygulanmaz. Saf mantik + JSON (birim testlenir).

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::core::games {

enum class Consent { Ask, Allow, Deny };
std::string_view to_string(Consent c) noexcept;

struct ConsentEntry {
    std::string id;            // normallestirilmis oyun kimligi (kucuk harf tam yol)
    std::string display_name;  // kullaniciya gosterilen ad
    Consent decision = Consent::Ask;
    std::int64_t decided_unix_ms = 0;
};

// Yol -> kimlik: ters bolu, kucuk harf (ASCII), bas/son bosluk yok. Bos yol: bos kimlik.
std::string normalize_game_id(std::string_view exe_path);

class ConsentStore {
public:
    Consent get(const std::string& id) const;
    // Karar yalniz Ask ise soru gosterilir (tek seferlik).
    bool needs_prompt(const std::string& id) const { return get(id) == Consent::Ask; }
    void set(const std::string& id, std::string display_name, Consent c, std::int64_t when_unix_ms);
    bool clear(const std::string& id);  // karari kaldir: bir sonraki algilamada yeniden sorulur
    std::vector<ConsentEntry> list() const;  // ada gore sirali
    std::size_t size() const { return entries_.size(); }

    std::string serialize() const;
    // Bozuk/yeni sema: hata (cagiran bos depoyla devam eder, dosyayi silmez).
    static contracts::Result<ConsentStore> parse(std::string_view json);

    static constexpr std::size_t kMaxEntries = 500;

private:
    std::map<std::string, ConsentEntry> entries_;
};

}  // namespace ef::core::games
