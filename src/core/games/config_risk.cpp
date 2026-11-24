#include "core/games/config_risk.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ef::core::games {

std::string_view to_string(RiskSeverity s) noexcept {
    switch (s) {
        case RiskSeverity::Info: return "info";
        case RiskSeverity::Caution: return "caution";
        case RiskSeverity::Warning: return "warning";
    }
    return "info";
}

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Boslukla ayrilmis belirtecler; cift tirnak icindeki bosluk belirteci bolmez.
std::vector<std::string> tokenize(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;
        std::string tok;
        if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') tok.push_back(s[i++]);
            if (i < s.size()) ++i;  // kapanis tirnagi
        } else {
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) tok.push_back(s[i++]);
        }
        if (!tok.empty()) out.push_back(std::move(tok));
    }
    return out;
}

bool has_code(const std::vector<ConfigRisk>& v, std::string_view code) {
    return std::any_of(v.begin(), v.end(), [&](const ConfigRisk& r) { return r.code == code; });
}

}  // namespace

std::vector<ConfigRisk> scan_launch_options(std::string_view options) {
    std::vector<ConfigRisk> out;
    for (const auto& raw : tokenize(options)) {
        const std::string tok = lower(raw);
        if (tok == "-insecure") {
            out.push_back({"risk.launch.insecure", RiskSeverity::Warning, raw});
        } else if (tok == "-allow_third_party_software") {
            out.push_back({"risk.launch.third_party", RiskSeverity::Warning, raw});
        } else if (tok == "-high") {
            out.push_back({"risk.launch.high_priority", RiskSeverity::Caution, raw});
        } else if (tok == "-threads") {
            out.push_back({"risk.launch.threads", RiskSeverity::Caution, raw});
        } else if (tok == "-w" || tok == "-h") {
            if (!has_code(out, "risk.launch.resolution_override"))
                out.push_back({"risk.launch.resolution_override", RiskSeverity::Info, raw});
        }
    }
    return out;
}

std::vector<ConfigRisk> scan_cs2_video(const cs2::VideoSettings& v) {
    std::vector<ConfigRisk> out;
    if (v.vsync && *v.vsync) out.push_back({"risk.video.vsync", RiskSeverity::Caution, "setting.mat_vsync"});
    if (v.fullscreen && !*v.fullscreen) out.push_back({"risk.video.borderless", RiskSeverity::Caution, "setting.fullscreen"});
    if (v.low_latency && *v.low_latency == 0) out.push_back({"risk.video.reflex_off", RiskSeverity::Info, "setting.r_low_latency"});
    if (v.msaa_samples && *v.msaa_samples > 4) out.push_back({"risk.video.msaa", RiskSeverity::Caution, "setting.msaa_samples"});
    return out;
}

std::vector<ConfigRisk> merge_risks(std::vector<ConfigRisk> a, const std::vector<ConfigRisk>& b) {
    for (const auto& r : b)
        if (!has_code(a, r.code)) a.push_back(r);
    return a;
}

}  // namespace ef::core::games
