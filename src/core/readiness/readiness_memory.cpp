// F-62: RAM yuva/kanal yerlesimi hazirlik kurallari (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/memory_reader (SMBIOS tip 16/17; salt okuma).
// Ilkeler:
//  - Fiziksel kanal sayisi DIMM sayisindan kesinlestirilemez; sonuc SMBIOS yuva/bank
//    etiketlerinden turetilen OLASILIKTIR ve metin bunu acikca soyler.
//  - Etiket yok veya sayilar tutarsizsa kanal iddiasi uretilmez.
//  - Tek modul/tek yuva bilgisi kadar eslesmeyen modul (kapasite/hiz) bilgisi de raporlanir.
//  - Hicbir ayar yazilmaz; AutoFix yoktur (fiziksel mudahale kullaniciya aittir).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Hardware;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

bool is_sep(char c) { return c == ' ' || c == '_' || c == '-' || c == '.' || c == '/'; }

// SMBIOS device/bank etiketinden kanal izi: "CHANNEL A", "ChannelB-DIMM0", "DIMM_A1", "A1".
// "DIMM0"/"DIMM1" gibi yalniz sayi iceren etiketler yuva numarasi olabilir; kanal iddiasi yok.
std::string channel_token(std::string_view label) {
    std::string up;
    up.reserve(label.size());
    for (const char ch : label) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));

    auto token_after = [&](std::size_t pos) -> std::string {
        while (pos < up.size() && is_sep(up[pos])) ++pos;
        if (pos >= up.size()) return {};
        const char c = up[pos];
        if (c >= 'A' && c <= 'D') return std::string(1, c);
        if (c >= '0' && c <= '9') return std::string(1, c);
        return {};
    };

    if (const auto p = up.find("CHANNEL"); p != std::string::npos) {
        auto t = token_after(p + 7);
        if (!t.empty()) return t;
    }
    if (const auto p = up.find("DIMM"); p != std::string::npos) {
        auto t = token_after(p + 4);
        // DIMM0/DIMM1: rakam yuva numarasi olabilir; yalniz harf kanali kabul edilir.
        if (!t.empty() && t[0] >= 'A' && t[0] <= 'D') return t;
        return {};
    }
    if (up.size() >= 2 && up[0] >= 'A' && up[0] <= 'D' && std::isdigit(static_cast<unsigned char>(up[1])))
        return std::string(1, up[0]);
    return {};
}

std::string token_for(const MemoryModuleInfo& mod) {
    auto t = channel_token(mod.bank_locator);
    if (!t.empty()) return t;
    return channel_token(mod.locator);
}

std::string gib(std::uint64_t bytes) {  // olculen bayt -> GiB, bir basamak (ek hesap/esik yok)
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(bytes) / 1073741824.0);
    return buf;
}

bool all_known(const std::vector<MemoryModuleInfo>& modules, auto get) {
    return std::all_of(modules.begin(), modules.end(), [&](const MemoryModuleInfo& m) { return get(m).has_value(); });
}

bool same_values(const std::vector<MemoryModuleInfo>& modules, auto get) {
    const auto first = get(modules.front()).value();
    return std::all_of(modules.begin(), modules.end(), [&](const MemoryModuleInfo& m) { return get(m).value() == first; });
}

}  // namespace

std::vector<Check> memory_layout_checks(const MemoryLayoutInfo& m) {
    std::vector<Check> out;
    if (m.virtual_machine) return out;  // sanal makinede yuva bilgisi fiziksel yerlesimi yansitmaz
    if (m.modules.empty()) return out;
    if (!m.populated_slots.has_value()) return out;
    const auto n = m.modules.size();
    if (*m.populated_slots != n) return out;  // kismi/tutarsiz SMBIOS: iddia yok

    if (n == 1) {
        if (!m.total_slots.has_value()) return out;
        if (*m.total_slots > 1) {
            Check c = make("memory.single_module", Level::Advice, "chk.memory.single_module");
            c.args["populated"] = std::to_string(n);
            c.args["total"] = std::to_string(*m.total_slots);
            out.push_back(std::move(c));
        } else if (*m.total_slots == 1) {
            Check c = make("memory.single_slot", Level::Info, "chk.memory.single_slot");
            c.args["total"] = std::to_string(*m.total_slots);
            out.push_back(std::move(c));
        }
        return out;
    }

    const auto size_of = [](const MemoryModuleInfo& mod) { return mod.size_mib; };
    const auto speed_of = [](const MemoryModuleInfo& mod) { return mod.configured_mts; };
    const bool size_mismatch = all_known(m.modules, size_of) && !same_values(m.modules, size_of);
    const bool speed_mismatch = all_known(m.modules, speed_of) && !same_values(m.modules, speed_of);
    if (size_mismatch || speed_mismatch)
        out.push_back(make("memory.mismatch", Level::Advice, "chk.memory.mismatch"));

    std::vector<std::string> tokens;
    tokens.reserve(m.modules.size());
    for (const auto& mod : m.modules) {
        auto t = token_for(mod);
        if (t.empty()) return out;  // etiketsiz modul: kanal iddiasi yok
        tokens.push_back(std::move(t));
    }
    std::vector<std::string> distinct;
    for (const auto& t : tokens)
        if (std::find(distinct.begin(), distinct.end(), t) == distinct.end()) distinct.push_back(t);

    if (distinct.size() == 1) {
        Check c = make("memory.same_channel", Level::Advice, "chk.memory.same_channel");
        c.args["label"] = distinct.front();
        out.push_back(std::move(c));
    } else {
        Check c = make("memory.dual_channel", Level::Info, "chk.memory.dual_channel");
        std::string labels;
        for (const auto& d : distinct) {
            if (!labels.empty()) labels += ", ";
            labels += d;
        }
        c.args["labels"] = std::move(labels);
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Check> standby_pressure_checks(const MemoryPressureInfo& m) {
    std::vector<Check> out;
    if (!m.total_bytes || !m.standby_bytes || !m.free_bytes) return out;  // okunamayan veri: iddia yok
    const std::uint64_t total = *m.total_bytes;
    if (total == 0) return out;
    // ADR-040 m.6 sabitleri (tamsayi karsilastirma; tasma yok: bayt degerleri << 2^60).
    constexpr std::uint64_t kStandbyOverTotalDiv = 2;  // bekleme > toplam / 2
    constexpr std::uint64_t kFreeUnderTotalDiv = 10;   // bos < toplam / 10
    if (!(*m.standby_bytes * kStandbyOverTotalDiv > total)) return out;
    if (!(*m.free_bytes * kFreeUnderTotalDiv < total)) return out;
    Check c = make("memory.standby_pressure", Level::Advice, "chk.memory.standby_pressure");
    c.args["standby_gib"] = gib(*m.standby_bytes);
    c.args["free_gib"] = gib(*m.free_bytes);
    c.args["total_gib"] = gib(total);
    c.action = ActionKind::AutoFix;
    c.action_arg = kStandbyFixArg;
    out.push_back(std::move(c));
    return out;
}

void drop_superseded_standby_info(std::vector<Check>& checks) {
    const bool pressure = std::any_of(checks.begin(), checks.end(), [](const Check& c) { return c.id == "memory.standby_pressure"; });
    if (!pressure) return;
    checks.erase(std::remove_if(checks.begin(), checks.end(), [](const Check& c) { return c.id == "system.standby"; }), checks.end());
}

}  // namespace ef::core::readiness
