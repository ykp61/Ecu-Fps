#include "core/diagnostics/incident_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <tuple>

namespace ef::core::diagnostics {

namespace {

constexpr std::int64_t kEpisodeGapMs = 5 * 60 * 1000;      // ayni olay zinciri sayilan pencere
constexpr std::int64_t kClockJumpMs = 10 * 60 * 1000;      // kayit sirasina ters geri gidis
constexpr std::int64_t kBugcheckPairMs = 30 * 60 * 1000;   // yavas dokumde 1001 acilistan sonra gelebilir
constexpr std::uint64_t kWheaBugcheck = 0x124;             // WHEA_UNCORRECTABLE_ERROR

std::string decode_entities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        const auto e = s.find(';', i);
        if (e == std::string_view::npos || e - i > 8) {
            out.push_back('&');
            continue;
        }
        const auto name = s.substr(i + 1, e - i - 1);
        if (name == "amp") out.push_back('&');
        else if (name == "lt") out.push_back('<');
        else if (name == "gt") out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else out.append(s.substr(i, e - i + 1));  // sayisal/bilinmeyen varlik acilmaz
        i = e;
    }
    return out;
}

std::optional<std::string> attribute(std::string_view tag, std::string_view name) {
    std::size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string_view::npos) {
        const bool boundary = pos > 0 && std::isspace(static_cast<unsigned char>(tag[pos - 1]));
        std::size_t p = pos + name.size();
        if (boundary && p < tag.size() && tag[p] == '=' && p + 1 < tag.size() && (tag[p + 1] == '\'' || tag[p + 1] == '"')) {
            const char q = tag[p + 1];
            const auto end = tag.find(q, p + 2);
            if (end == std::string_view::npos) return std::nullopt;
            return decode_entities(tag.substr(p + 2, end - p - 2));
        }
        pos = p;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> to_u64(std::string_view s) {
    if (s.empty() || s.size() > 20) return std::nullopt;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        const auto d = static_cast<std::uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10) return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}

// <name ...>metin</name>: yalniz System blogundaki basit alanlar icin.
std::optional<std::string> element_text(std::string_view xml, std::string_view name) {
    const std::string open = "<" + std::string(name);
    std::size_t p = 0;
    while ((p = xml.find(open, p)) != std::string_view::npos) {
        const auto after = p + open.size();
        if (after < xml.size() && (xml[after] == '>' || xml[after] == ' ')) {
            const auto gt = xml.find('>', after);
            if (gt == std::string_view::npos || xml[gt - 1] == '/') return std::nullopt;
            const auto end = xml.find("</" + std::string(name) + ">", gt);
            if (end == std::string_view::npos) return std::nullopt;
            return decode_entities(xml.substr(gt + 1, end - gt - 1));
        }
        p = after;
    }
    return std::nullopt;
}

std::optional<std::string_view> tag_of(std::string_view xml, std::string_view name) {
    const std::string open = "<" + std::string(name);
    std::size_t p = 0;
    while ((p = xml.find(open, p)) != std::string_view::npos) {
        const auto after = p + open.size();
        if (after < xml.size() && (xml[after] == ' ' || xml[after] == '/' || xml[after] == '>')) {
            const auto gt = xml.find('>', after);
            if (gt == std::string_view::npos) return std::nullopt;
            return xml.substr(p, gt - p + 1);
        }
        p = after;
    }
    return std::nullopt;
}

std::optional<int> digits(std::string_view s, std::size_t at, std::size_t n) {
    if (at + n > s.size()) return std::nullopt;
    int v = 0;
    for (std::size_t i = at; i < at + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return std::nullopt;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool hex_only(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string iso_date(std::int64_t ms) {
    std::int64_t z = ms / 86400000 + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));
    y += m <= 2;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(y), m, d);
    return buf;
}

std::string hex_code(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%08llx", static_cast<unsigned long long>(v));
    return buf;
}

std::optional<std::uint64_t> parse_code(std::string s) {
    if (s.empty()) return std::nullopt;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s = s.substr(2);
        if (s.empty() || s.size() > 16 || !hex_only(s)) return std::nullopt;
        std::uint64_t v = 0;
        for (char c : s) v = v * 16 + static_cast<std::uint64_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
        return v;
    }
    return to_u64(s);
}

std::optional<std::uint32_t> disk_number_of(const RawEvent& e) {
    for (const auto& kv : e.data) {
        const auto p = kv.second.find("\\Device\\Harddisk");
        if (p == std::string::npos) continue;
        std::uint32_t v = 0;
        std::size_t i = p + 16, n = 0;
        for (; i < kv.second.size() && kv.second[i] >= '0' && kv.second[i] <= '9' && n < 6; ++i, ++n) v = v * 10 + static_cast<std::uint32_t>(kv.second[i] - '0');
        if (n) return v;
    }
    return std::nullopt;
}

bool nonzero_field(const RawEvent& e, const char* key) {
    const auto it = e.data.find(key);
    return it != e.data.end() && parse_code(it->second).value_or(0) != 0;
}

enum class Tag { None, Shutdown41, Bugcheck1001, PrevShutdown6008, WheaFatal, WheaCorrected, WheaUnknown, Display4101, Disk };

// Saglayici + olay no birlikte: baska saglayicidaki ayni numara sayilmaz.
Tag classify(const RawEvent& e) {
    if (e.provider == "Microsoft-Windows-Kernel-Power" && e.event_id == 41) return Tag::Shutdown41;
    if (e.provider == "Microsoft-Windows-WER-SystemErrorReporting" && e.event_id == 1001) return Tag::Bugcheck1001;
    if (e.provider == "EventLog" && e.event_id == 6008) return Tag::PrevShutdown6008;
    if (e.provider == "Display" && e.event_id == 4101) return Tag::Display4101;
    if (e.provider == "disk" && (e.event_id == 7 || e.event_id == 51)) return Tag::Disk;
    if (e.provider == "Microsoft-Windows-WHEA-Logger") {
        // Olay no listesi eksik kalir (manifestte 30'a yakin no var): olayin kendi seviyesi esastir.
        if (e.level && (*e.level == 1 || *e.level == 2)) return Tag::WheaFatal;
        if (e.level && *e.level == 3) return Tag::WheaCorrected;
        if (!e.level || *e.level != 4) return Tag::WheaUnknown;
    }
    return Tag::None;
}

IncidentFinding& slot(std::vector<IncidentFinding>& v, IncidentKind k) {
    for (auto& f : v)
        if (f.kind == k) return f;
    v.push_back({});
    v.back().kind = k;
    return v.back();
}

void bump(IncidentFinding& f, const RawEvent& e, std::string evidence) {
    ++f.count;
    if (e.time_unix_ms && (!f.last_ms || *e.time_unix_ms > *f.last_ms)) f.last_ms = e.time_unix_ms;
    if (f.evidence.size() < 8) f.evidence.push_back(std::move(evidence));
}

std::string ev_id(const RawEvent& e) { return e.provider + ":" + std::to_string(e.event_id); }

}  // namespace

std::optional<std::int64_t> parse_system_time(std::string_view s) {
    if (s.size() < 20 || s.back() != 'Z' || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') return std::nullopt;
    const auto y = digits(s, 0, 4), mo = digits(s, 5, 2), d = digits(s, 8, 2), h = digits(s, 11, 2), mi = digits(s, 14, 2), sec = digits(s, 17, 2);
    if (!y || !mo || !d || !h || !mi || !sec || *mo < 1 || *mo > 12 || *d < 1 || *d > 31 || *h > 23 || *mi > 59 || *sec > 60) return std::nullopt;
    int ms = 0;
    if (s[19] == '.') {
        int scale = 100;
        for (std::size_t i = 20; i + 1 < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return std::nullopt;
            ms += (s[i] - '0') * scale;
            scale /= 10;
        }
    } else if (s[19] != 'Z') {
        return std::nullopt;
    }
    return days_from_civil(*y, static_cast<unsigned>(*mo), static_cast<unsigned>(*d)) * 86400000 +
           (*h * 3600 + *mi * 60 + *sec) * std::int64_t{1000} + ms;
}

std::optional<RawEvent> parse_event_xml(std::string_view xml) {
    if (xml.empty() || xml.size() > kMaxEventXml) return std::nullopt;
    if (xml.find("<!") != std::string_view::npos) return std::nullopt;  // DOCTYPE/ENTITY/CDATA kabul edilmez
    const auto sys_end = xml.find("</System>");
    if (sys_end == std::string_view::npos) return std::nullopt;
    const auto sys = xml.substr(0, sys_end);

    RawEvent e;
    const auto ptag = tag_of(sys, "Provider");
    if (!ptag) return std::nullopt;
    auto pname = attribute(*ptag, "Name");
    if (!pname) pname = attribute(*ptag, "EventSourceName");
    if (!pname || pname->empty()) return std::nullopt;
    e.provider = std::move(*pname);

    const auto id = element_text(sys, "EventID");
    if (!id) return std::nullopt;
    const auto idv = to_u64(*id);
    if (!idv || *idv > 0xFFFFFFFFu) return std::nullopt;
    e.event_id = static_cast<std::uint32_t>(*idv);
    if (const auto l = element_text(sys, "Level"))
        if (const auto n = to_u64(*l); n && *n <= 255) e.level = static_cast<std::uint32_t>(*n);
    if (const auto v = element_text(sys, "Version"))
        if (const auto n = to_u64(*v); n && *n <= 0xFFFFFFFFu) e.version = static_cast<std::uint32_t>(*n);
    if (const auto r = element_text(sys, "EventRecordID"))
        if (const auto n = to_u64(*r)) e.record_id = *n;
    if (e.record_id == 0) return std::nullopt;  // tekillestirme kimligi olmadan sayilmaz
    if (const auto c = element_text(sys, "Channel")) e.channel = *c;
    if (const auto t = tag_of(sys, "TimeCreated")) {
        if (const auto st = attribute(*t, "SystemTime")) {
            e.raw_time = *st;
            e.time_unix_ms = parse_system_time(*st);
        }
    }

    const auto body = xml.substr(sys_end);
    std::size_t p = 0;
    std::size_t unnamed = 0;
    while ((p = body.find("<Data", p)) != std::string_view::npos) {
        const auto after = p + 5;
        if (after >= body.size() || (body[after] != ' ' && body[after] != '>' && body[after] != '/')) {
            p = after;
            continue;  // <DataItem> gibi baska etiket
        }
        const auto gt = body.find('>', after);
        if (gt == std::string_view::npos) return std::nullopt;
        if (e.data.size() >= kMaxDataFields) break;
        const auto tag = body.substr(p, gt - p + 1);
        auto name = attribute(tag, "Name");
        if (!name) name = "param" + std::to_string(++unnamed);
        if (body[gt - 1] == '/') {
            e.data[*name] = "";
            p = gt + 1;
            continue;
        }
        const auto end = body.find("</Data>", gt);
        if (end == std::string_view::npos) return std::nullopt;
        e.data[*name] = decode_entities(body.substr(gt + 1, end - gt - 1));
        p = end + 7;
    }
    return e;
}

IncidentReport analyze_incidents(const EventReadResult& read, int days, std::optional<std::int64_t> now_ms) {
    IncidentReport r;
    r.status = read.status;
    r.rejected = read.rejected;
    r.truncated = read.truncated;
    r.days = days;
    if (now_ms && read.oldest_ms) r.coverage_short = *read.oldest_ms > *now_ms - std::int64_t{days} * 86400000;
    if (read.status != ReadStatus::Ok) return r;  // okunamadi: "olay yok" uretilmez

    // Tekillestirme ve tanima.
    std::set<std::tuple<std::string, std::string, std::uint64_t>> seen;
    std::vector<const RawEvent*> events;
    for (const auto& e : read.events) {
        const auto tag = classify(e);
        if (tag == Tag::None) continue;
        if (tag == Tag::WheaUnknown) {
            ++r.unrecognized;
            continue;
        }
        if (!seen.insert({e.channel, e.provider, e.record_id}).second) continue;
        events.push_back(&e);
    }
    r.events_seen = events.size();
    std::sort(events.begin(), events.end(), [](const RawEvent* a, const RawEvent* b) { return a->record_id < b->record_id; });

    // Saat geri alindiysa kayit sirasi ile zaman celisir: olaylar birbirine baglanmaz.
    std::optional<std::int64_t> prev;
    for (const auto* e : events) {
        if (!e->time_unix_ms) continue;
        if (prev && *prev - *e->time_unix_ms > kClockJumpMs) r.clock_uncertain = true;
        prev = e->time_unix_ms;
    }

    const auto near_bugcheck = [&](const RawEvent& s) {
        if (r.clock_uncertain || !s.time_unix_ms) return false;
        return std::any_of(events.begin(), events.end(), [&](const RawEvent* o) {
            return classify(*o) == Tag::Bugcheck1001 && o->time_unix_ms && std::llabs(*o->time_unix_ms - *s.time_unix_ms) <= kEpisodeGapMs;
        });
    };

    for (const auto* e : events) {
        switch (classify(*e)) {
            case Tag::Shutdown41: {
                std::uint64_t code = 0;
                if (const auto it = e->data.find("BugcheckCode"); it != e->data.end())
                    code = parse_code(it->second).value_or(0);
                if (code != 0) {
                    auto& f = slot(r.findings, IncidentKind::BugCheck);
                    // Tekil saat sicramasi tum eslesmeleri bozmaz: ayni kodlu 1001 kaydi cift sayilmaz.
                    const bool paired = e->time_unix_ms && std::any_of(events.begin(), events.end(), [&](const RawEvent* o) {
                        if (classify(*o) != Tag::Bugcheck1001 || !o->time_unix_ms || std::llabs(*o->time_unix_ms - *e->time_unix_ms) > kBugcheckPairMs) return false;
                        const auto it = o->data.find("param1");
                        return it != o->data.end() && parse_code(it->second.substr(0, it->second.find_first_of(" ("))) == std::optional<std::uint64_t>(code);
                    });
                    if (paired) break;  // ayni durdurma 1001 kaydinda sayilir
                    bump(f, *e, ev_id(*e) + " BugcheckCode=" + hex_code(code));
                    f.bugcheck = hex_code(code);
                    break;
                }
                if (near_bugcheck(*e)) break;
                auto& f = slot(r.findings, IncidentKind::UnexpectedShutdown);
                bump(f, *e, ev_id(*e));
                f.power_button += nonzero_field(*e, "PowerButtonTimestamp");
                f.sleeping += nonzero_field(*e, "SleepInProgress");
                if (f.counter.empty()) f.counter.push_back("no_bugcheck_recorded");  // neden kayittan belirlenemez
                break;
            }
            case Tag::Bugcheck1001: {
                auto& f = slot(r.findings, IncidentKind::BugCheck);
                bump(f, *e, ev_id(*e));
                for (const char* k : {"param1", "BugcheckCode"}) {
                    const auto it = e->data.find(k);
                    if (it == e->data.end()) continue;
                    const auto sp = it->second.find_first_of(" (");
                    if (const auto c = parse_code(it->second.substr(0, sp))) f.bugcheck = hex_code(*c);
                    break;
                }
                break;
            }
            case Tag::WheaFatal:
            case Tag::WheaCorrected: {
                auto& f = slot(r.findings, classify(*e) == Tag::WheaFatal ? IncidentKind::WheaFatal : IncidentKind::WheaCorrected);
                bump(f, *e, ev_id(*e));
                const auto len = e->data.find("Length");
                const auto raw = e->data.find("RawData");
                if (raw != e->data.end() && (!hex_only(raw->second) || raw->second.size() % 2 != 0 ||
                                             (len != e->data.end() && to_u64(len->second).value_or(UINT64_MAX) * 2 != raw->second.size()))) {
                    f.payload_invalid = true;
                    if (f.counter.empty()) f.counter.push_back("whea_payload_invalid");
                }
                break;
            }
            case Tag::Display4101: bump(slot(r.findings, IncidentKind::DisplayReset), *e, ev_id(*e)); break;
            case Tag::Disk: {
                auto& f = slot(r.findings, IncidentKind::DiskError);
                bump(f, *e, ev_id(*e));
                f.bad_block = f.bad_block || e->event_id == 7;
                if (const auto n = disk_number_of(*e); n && std::find(f.disks.begin(), f.disks.end(), *n) == f.disks.end()) f.disks.push_back(*n);
                break;
            }
            case Tag::PrevShutdown6008:
            case Tag::WheaUnknown:
            case Tag::None: break;  // 6008 yalniz yan kanit: 41 ile cift sayilmaz
        }
    }
    std::erase_if(r.findings, [](const IncidentFinding& f) { return f.count == 0; });
    for (auto& f : r.findings) {
        if (f.kind == IncidentKind::WheaCorrected) f.at_least = read.corrected_truncated;
        if (f.kind != IncidentKind::BugCheck) continue;
        f.related_whea = parse_code(f.bugcheck) == std::optional<std::uint64_t>(kWheaBugcheck);
        if (r.clock_uncertain) continue;
        for (const auto* w : events) {
            if (classify(*w) != Tag::WheaFatal || !w->time_unix_ms) continue;
            f.related_whea = f.related_whea || std::any_of(events.begin(), events.end(), [&](const RawEvent* b) {
                return classify(*b) == Tag::Bugcheck1001 && b->time_unix_ms && std::llabs(*b->time_unix_ms - *w->time_unix_ms) <= kEpisodeGapMs;
            });
        }
    }
    if (r.clock_uncertain)
        for (auto& f : r.findings) f.counter.push_back("clock_changed");
    return r;
}

std::vector<readiness::Check> incident_checks(const IncidentReport& rep, int utc_offset_min) {
    using readiness::Check;
    using readiness::Level;
    std::vector<Check> v;
    const auto make = [](std::string id, Level l, std::string key) {
        Check c;
        c.id = std::move(id);
        c.section = readiness::Section::Hardware;
        c.level = l;
        c.text_key = std::move(key);
        return c;
    };
    if (rep.status == ReadStatus::AccessDenied) {
        v.push_back(make("hardware.incidents", Level::Unknown, "chk.incident.access_denied"));
        return v;
    }
    if (rep.status != ReadStatus::Ok) {
        v.push_back(make("hardware.incidents", Level::Unknown, "chk.incident.read_error"));
        return v;
    }
    const auto days = std::to_string(rep.days);
    if (rep.findings.empty()) {
        // Kesik/bozuk okuma "temiz" denmez.
        const bool partial = rep.truncated || rep.rejected || rep.coverage_short || rep.unrecognized;  // eksik okuma "temiz" denmez
        auto c = make("hardware.incidents", partial ? Level::Unknown : Level::Good, partial ? "chk.incident.partial" : "chk.incident.none");
        c.args["days"] = days;
        v.push_back(std::move(c));
        return v;
    }
    for (const auto& f : rep.findings) {
        const char* id = "";
        const char* key = "";
        Level level = Level::Advice;
        switch (f.kind) {
            case IncidentKind::UnexpectedShutdown:
                id = "shutdown";
                key = f.power_button == f.count ? "chk.incident.shutdown_button" : f.sleeping == f.count ? "chk.incident.shutdown_sleep" : "chk.incident.shutdown";
                break;
            case IncidentKind::BugCheck:
                id = "bugcheck";
                key = f.related_whea ? "chk.incident.bugcheck_whea" : f.bugcheck.empty() ? "chk.incident.bugcheck_nocode" : "chk.incident.bugcheck";
                level = Level::Problem;
                break;
            case IncidentKind::WheaFatal: id = "whea_fatal"; key = "chk.incident.whea_fatal"; level = Level::Problem; break;
            case IncidentKind::WheaCorrected: id = "whea_corrected"; key = "chk.incident.whea_corrected"; break;
            case IncidentKind::DisplayReset: id = "display_reset"; key = "chk.incident.display_reset"; break;
            case IncidentKind::DiskError: id = "disk"; key = "chk.incident.disk"; level = f.bad_block ? Level::Problem : Level::Advice; break;
        }
        auto c = make(std::string("hardware.incidents.") + id, level, key);
        c.args["days"] = days;
        c.args["count"] = std::to_string(f.count) + (f.at_least ? "+" : "");
        c.args["date"] = f.last_ms ? iso_date(*f.last_ms + std::int64_t{utc_offset_min} * 60000) : std::string("-");
        if (f.kind == IncidentKind::DiskError) {
            std::string list;
            for (auto n : f.disks) list += (list.empty() ? "" : ",") + std::to_string(n);
            c.args["disks"] = list;
        }
        if (!f.bugcheck.empty()) c.args["code"] = f.bugcheck;
        v.push_back(std::move(c));
    }
    for (const auto& f : rep.findings)
        if (f.payload_invalid) {
            v.push_back(make("hardware.incidents.payload", Level::Info, "chk.incident.whea_payload"));
            break;
        }
    if (rep.clock_uncertain) v.push_back(make("hardware.incidents.clock", Level::Info, "chk.incident.clock"));
    if (rep.truncated || rep.rejected || rep.coverage_short || rep.unrecognized) v.push_back(make("hardware.incidents.partial", Level::Info, "chk.incident.partial_note"));
    return v;
}

void add_incident_window_action(std::vector<readiness::Check>& rows, const IncidentReport& rep) {
    if (rep.status != ReadStatus::Ok || rows.empty()) return;  // erisim reddi/okuma hatasi: pencere degistirmek cozum degil
    rows.front().action = readiness::ActionKind::IncidentWindow;
    rows.front().action_arg = normalize_incident_days(rep.days) == kLongDays ? "7" : "30";
}

}  // namespace ef::core::diagnostics
