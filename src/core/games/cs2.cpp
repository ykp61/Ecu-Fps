#include "core/games/cs2.h"

#include <charconv>
#include <cstdlib>

namespace ef::core::games::cs2 {

std::optional<std::map<std::string, std::string>> parse_video_cfg(std::string_view t) {
    std::map<std::string, std::string> kv;
    std::size_t i = 0;
    int depth = 0;
    std::optional<std::string> pending;  // anahtar bekliyor
    auto skip_ws = [&] {
        while (i < t.size()) {
            if (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n') ++i;
            else if (t.substr(i, 2) == "//") {
                while (i < t.size() && t[i] != '\n') ++i;
            } else break;
        }
    };
    if (t.size() > 1024 * 1024) return std::nullopt;  // sinir
    while (true) {
        skip_ws();
        if (i >= t.size()) break;
        const char c = t[i];
        if (c == '{') {
            if (!pending) return std::nullopt;
            pending.reset();
            if (++depth > 1) return std::nullopt;  // yalniz tek bolum beklenir
            ++i;
        } else if (c == '}') {
            if (depth == 0 || pending) return std::nullopt;
            --depth;
            ++i;
        } else if (c == '"') {
            const auto e = t.find('"', i + 1);
            if (e == std::string_view::npos) return std::nullopt;
            std::string s(t.substr(i + 1, e - i - 1));
            i = e + 1;
            if (!pending) pending = std::move(s);
            else {
                if (depth != 1) return std::nullopt;
                kv[*pending] = std::move(s);
                pending.reset();
            }
        } else {
            return std::nullopt;
        }
    }
    if (depth != 0 || pending || kv.empty()) return std::nullopt;
    return kv;
}

namespace {
std::optional<int> as_int(const std::map<std::string, std::string>& kv, const char* k) {
    const auto it = kv.find(k);
    if (it == kv.end()) return std::nullopt;
    int v = 0;
    const auto& s = it->second;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}
}  // namespace

VideoSettings typed(const std::map<std::string, std::string>& kv) {
    VideoSettings v;
    v.version = as_int(kv, "Version");
    v.width = as_int(kv, "setting.defaultres");
    v.height = as_int(kv, "setting.defaultresheight");
    const auto num = as_int(kv, "setting.refreshrate_numerator"), den = as_int(kv, "setting.refreshrate_denominator");
    if (num && den && *den > 0) v.refresh_hz = static_cast<double>(*num) / *den;
    if (auto f = as_int(kv, "setting.fullscreen")) v.fullscreen = *f != 0;
    if (auto s = as_int(kv, "setting.mat_vsync")) v.vsync = *s != 0;
    v.msaa_samples = as_int(kv, "setting.msaa_samples");
    v.fsr_detail = as_int(kv, "setting.videocfg_fsr_detail");
    v.low_latency = as_int(kv, "setting.r_low_latency");
    v.shader_quality = as_int(kv, "setting.shaderquality");
    v.texture_detail = as_int(kv, "setting.videocfg_texture_detail");
    v.shadow_quality = as_int(kv, "setting.videocfg_shadow_quality");
    return v;
}

namespace {
// "key"<bosluk>"value" deger tirnaklarinin konumu (ilk eslesme; anahtar tam eslesmeli).
std::optional<std::pair<std::size_t, std::size_t>> value_span(std::string_view t, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t p = 0;
    while ((p = t.find(needle, p)) != std::string_view::npos) {
        std::size_t q = p + needle.size();
        while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) ++q;
        if (q < t.size() && t[q] == '"') {
            const auto e = t.find('"', q + 1);
            if (e == std::string_view::npos) return std::nullopt;
            return std::make_pair(q + 1, e);
        }
        p += needle.size();
    }
    return std::nullopt;
}
}  // namespace

std::optional<std::string> set_field(std::string_view text, std::string_view key, std::string_view value) {
    if (!parse_video_cfg(text)) return std::nullopt;  // bozuk dosyaya yazilmaz
    if (value.find('"') != std::string_view::npos || value.find('\n') != std::string_view::npos) return std::nullopt;
    const auto sp = value_span(text, key);
    if (!sp) return std::nullopt;
    std::string out(text.substr(0, sp->first));
    out += value;
    out += text.substr(sp->second);
    if (!parse_video_cfg(out)) return std::nullopt;
    return out;
}

RestoreResult restore_field(std::string_view text, std::string_view key, std::string_view old_value, std::string_view written_value) {
    RestoreResult r;
    const auto kv = parse_video_cfg(text);
    if (!kv) return r;
    const auto it = kv->find(std::string(key));
    if (it == kv->end() || it->second != written_value) {
        r.conflict = true;  // kullanici/oyun sonradan degistirdi: ustune yazilmaz
        return r;
    }
    r.text = set_field(text, key, old_value);
    return r;
}

AdapterRecord adapter_record() {
    AdapterRecord r;
    r.store = "steam";
    r.app_id = std::string(kAppId);
    r.verified_versions = {};  // hicbir surumde yazim/benchmark dogrulanmadi
    r.capabilities = Capability::Observation | Capability::ConfigRead;
    r.evidence = "artifacts/game-first-support.md";
    return r;
}

}  // namespace ef::core::games::cs2
