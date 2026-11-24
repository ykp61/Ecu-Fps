// Panel saf bicim yardimcilari (spec 2026-11-12 B / D). Bkz. panel_text.h.
#include "ui/shell/panel_text.h"

#include <algorithm>
#include <cmath>

namespace ef::ui {

namespace {
constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "
constexpr std::string_view kTimes = "\xC3\x97";  // "×"

std::vector<std::string_view> split(std::string_view s, std::string_view sep) {
    std::vector<std::string_view> out;
    while (true) {
        const auto at = s.find(sep);
        out.push_back(s.substr(0, at));
        if (at == std::string_view::npos) break;
        s.remove_prefix(at + sep.size());
    }
    return out;
}
bool digit(char c) { return c >= '0' && c <= '9'; }
}  // namespace

std::optional<int> cpu_core_count(std::string_view d) {
    std::size_t i = 0;
    long n = 0;
    while (i < d.size() && digit(d[i]) && n < 100000) n = n * 10 + (d[i++] - '0');
    if (i == 0 || i >= d.size() || d[i] != 'C' || n <= 0 || n >= 100000) return std::nullopt;
    return static_cast<int>(n);
}

std::string ram_tile_name(std::string_view ram) {
    const auto at = ram.find(kSep);
    std::string out(ram.substr(0, at));
    if (out.empty() || at == std::string_view::npos) return out;
    auto tok = split(ram.substr(at + kSep.size()), " ");
    std::erase_if(tok, [](std::string_view t) { return t.empty(); });
    // Modul parcasi ("2×32 GB" / "2×?") ad satirina girmez.
    if (!tok.empty() && tok.front().find(kTimes) != std::string_view::npos) {
        tok.erase(tok.begin());
        if (!tok.empty() && tok.front() == "GB") tok.erase(tok.begin());
    }
    std::string speed;
    if (tok.size() >= 2 && tok.back() == "MT/s" && digit(tok[tok.size() - 2].front())) {
        speed = std::string(tok[tok.size() - 2]) + " MT/s";
        tok.resize(tok.size() - 2);
    }
    for (const auto t : tok) {
        out += ' ';
        out += t;
    }
    if (!speed.empty()) {
        out += kSep;
        out += speed;
    }
    return out;
}

std::string display_mode_text(std::string_view display) {
    std::string out;
    for (const auto part : split(display, kSep)) {
        bool keep = part.size() > 3 && part.ends_with(" Hz") && digit(part.front());
        if (const auto x = part.find(kTimes); !keep && x != std::string_view::npos && x > 0 && x + kTimes.size() < part.size())
            keep = digit(part[x - 1]) && digit(part[x + kTimes.size()]);
        if (!keep) continue;
        if (!out.empty()) out += kSep;
        out += part;
    }
    return out;
}

std::wstring percent_text(int n, bool turkish) { return turkish ? L"%" + std::to_wstring(n) : std::to_wstring(n) + L"%"; }

std::wstring scale_percents_text(const std::vector<double>& scales, bool turkish) {
    std::wstring out;
    for (std::size_t i = 0; i < scales.size(); ++i) {
        if (i) out += L" · ";
        out += percent_text(static_cast<int>(scales[i] * 100.0 + 0.5), turkish);
    }
    return out;
}

std::vector<VolumeFill> volume_fills(const std::vector<core::metrics::VolumeUse>& vols) {
    std::vector<VolumeFill> out;
    for (const auto& v : vols) {
        if (!v.total_gib || !v.free_gib || *v.total_gib == 0 || *v.free_gib > *v.total_gib) continue;
        std::string_view root = v.root;
        while (!root.empty() && (root.back() == '\\' || root.back() == '/')) root.remove_suffix(1);
        if (root.empty()) continue;
        VolumeFill f;
        for (const char c : root) f.label += static_cast<wchar_t>(static_cast<unsigned char>(c));  // surucu harfi: ASCII
        const double used = 100.0 * static_cast<double>(*v.total_gib - *v.free_gib) / static_cast<double>(*v.total_gib);
        f.used_pct = static_cast<int>(std::floor(used + 0.5));
        out.push_back(std::move(f));
    }
    return out;
}

std::wstring volume_fill_list(const std::vector<VolumeFill>& v, std::size_t shown, bool turkish) {
    std::wstring out;
    const std::size_t n = shown < v.size() ? shown : v.size();
    if (n == 0) return out;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += L" · ";
        out += v[i].label + L" " + percent_text(v[i].used_pct, turkish);
    }
    if (n < v.size()) out += L" +" + std::to_wstring(v.size() - n);
    return out;
}

std::optional<BalanceShare> balance_share(const core::measure::BalanceView& v, std::size_t row) {
    using core::diagnosis::BalanceVerdict;
    if (row >= v.rows.size()) return std::nullopt;
    const auto& r = v.rows[row];
    std::optional<double> cap;  // sinirlamayan tarafin kapasitesi
    bool at_least = false;
    if (r.verdict == BalanceVerdict::CpuLimited) cap = r.gpu_fps, at_least = r.gpu_lower_bound;
    else if (r.verdict == BalanceVerdict::GpuLimited) cap = v.cpu_fps, at_least = v.cpu_lower_bound;
    else return std::nullopt;
    if (!cap || !r.coupled_fps || !std::isfinite(*cap) || !std::isfinite(*r.coupled_fps) || !(*cap > 0) || *r.coupled_fps < 0)
        return std::nullopt;
    const double n = std::floor(100.0 * (1.0 - *r.coupled_fps / *cap) + 0.5);
    if (!(n >= 0) || n > 100) return std::nullopt;  // negatif: kirpilmaz, pay yazilmaz
    return BalanceShare{static_cast<int>(n), at_least};
}

std::wstring_view tr_possessive_suffix(int n) {
    if (n < 0) return {};
    static constexpr std::wstring_view ones[] = {L"", L"'i", L"'si", L"'ü", L"'ü", L"'i", L"'sı", L"'si", L"'i", L"'u"};
    static constexpr std::wstring_view tens[] = {L"", L"'u", L"'si", L"'u", L"'ı", L"'si", L"'ı", L"'i", L"'i", L"'ı"};
    if (n == 0) return L"'ı";
    if (n % 10) return ones[n % 10];
    if ((n / 10) % 10) return tens[(n / 10) % 10];
    return L"'ü";  // yuz
}

std::wstring share_text(const BalanceShare& s, bool turkish, bool possessive, std::wstring_view at_least_tpl) {
    std::wstring v = percent_text(s.pct, turkish);
    if (turkish && possessive) v += tr_possessive_suffix(s.pct);
    if (!s.at_least) return v;
    std::wstring out(at_least_tpl);
    constexpr std::wstring_view key = L"{v}";
    if (const auto pos = out.find(key); pos != std::wstring::npos) out.replace(pos, key.size(), v);
    else out += L" " + v;
    return out;
}

BottleneckWord bottleneck_word(core::diagnosis::BalanceVerdict v) {
    using core::diagnosis::BalanceVerdict;
    switch (v) {
        case BalanceVerdict::CpuLimited: return BottleneckWord::Cpu;
        case BalanceVerdict::GpuLimited:
        case BalanceVerdict::Balanced: return BottleneckWord::None;
        case BalanceVerdict::Undetermined:
        case BalanceVerdict::Unmeasurable: break;
    }
    return BottleneckWord::Other;
}

std::optional<std::size_t> balance_display_row(std::string_view d) {
    const auto digit = [&](std::size_t i) { return i < d.size() && d[i] >= '0' && d[i] <= '9'; };
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (!digit(i) || (i > 0 && digit(i - 1))) continue;  // sayinin basi
        std::size_t j = i;
        long w = 0, h = 0;
        while (digit(j) && w < 100000) w = w * 10 + (d[j++] - '0');
        std::size_t k = j;
        if (k < d.size() && (d[k] == 'x' || d[k] == 'X')) ++k;
        else if (d.substr(k, 2) == "\xC3\x97") k += 2;  // "×"
        else continue;
        if (!digit(k)) continue;
        while (digit(k) && h < 100000) h = h * 10 + (d[k++] - '0');
        if (w == 1920 && h == 1080) return 0;
        if (w == 2560 && h == 1440) return 1;
        if (w == 3840 && h == 2160) return 2;
        return std::nullopt;  // ilk cozunurluk birincil ekrandir; baska kip -> vurgu yok
    }
    return std::nullopt;
}

std::optional<int> test_remaining_s(double total_s, double elapsed_s) {
    if (!(std::isfinite(total_s) && total_s > 0) || !std::isfinite(elapsed_s)) return std::nullopt;
    const double left = std::ceil(total_s - std::max(elapsed_s, 0.0) - 1e-9);
    return left > 0 ? static_cast<int>(left) : 0;
}

ResultCardMode result_card_mode(bool busy, bool has_result, bool balance) {
    if (busy) return ResultCardMode::Numbers;
    if (!has_result) return ResultCardMode::Empty;
    return balance ? ResultCardMode::Balance : ResultCardMode::Numbers;
}

BalanceResultSummary balance_result_summary(const core::measure::BalanceView& v, std::string_view display) {
    BalanceResultSummary out;
    const auto finite = [](const std::optional<double>& x) -> std::optional<double> {
        if (x && std::isfinite(*x) && *x >= 0) return x;
        return std::nullopt;
    };
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        auto& l = out.lines[i];
        const auto& r = v.rows[i];
        l.row = i;
        l.verdict = r.verdict;
        l.word = bottleneck_word(r.verdict);
        l.cpu_fps = finite(v.cpu_fps);
        l.cpu_at_least = l.cpu_fps && v.cpu_lower_bound;
        l.gpu_fps = finite(r.gpu_fps);
        l.gpu_at_least = l.gpu_fps && r.gpu_lower_bound;
    }
    if (const auto mine = balance_display_row(display); mine && *mine < out.lines.size()) {
        out.headline = *mine;
        out.headline_is_display = true;
    } else {
        for (const auto& l : out.lines)
            if (l.word != BottleneckWord::Other) {
                out.headline = l.row;
                break;
            }
    }
    return out;
}

PretestWarning pretest_warning(bool builtin_source, bool busy, bool available, bool endurance_running, bool storage_blocked, bool mixed_load) {
    if (!builtin_source || busy || !available || endurance_running || storage_blocked) return PretestWarning::None;
    return mixed_load ? PretestWarning::StressDisk : PretestWarning::Stress;
}

int follow_open_row(int open, const std::vector<std::int64_t>& before, const std::vector<std::int64_t>& after) {
    if (open < 0 || static_cast<std::size_t>(open) >= before.size()) return -1;
    if (before == after) return open;
    const std::int64_t key = before[static_cast<std::size_t>(open)];
    if (std::count(before.begin(), before.end(), key) != 1 || std::count(after.begin(), after.end(), key) != 1) return -1;
    return static_cast<int>(std::find(after.begin(), after.end(), key) - after.begin());
}

}  // namespace ef::ui
