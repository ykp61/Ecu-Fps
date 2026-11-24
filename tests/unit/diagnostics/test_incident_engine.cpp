// EF-X01: kabul testleri T01-T04. Sentetik olaylar; gercek cihaz sonucu sayilmaz.
#include "core/diagnostics/incident_engine.h"

#include <algorithm>

#include "ef_test.h"

using namespace ef::core::diagnostics;
using ef::core::readiness::Level;

namespace {
std::string xml(std::string_view provider, int id, std::uint64_t rec, std::string_view when, std::string data = "", int level = 2) {
    return "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='" + std::string(provider) +
           "' Guid='{x}'/><EventID>" + std::to_string(id) + "</EventID><Version>8</Version><Level>" + std::to_string(level) + "</Level><TimeCreated SystemTime='" + std::string(when) +
           "'/><EventRecordID>" + std::to_string(rec) + "</EventRecordID><Channel>System</Channel></System><EventData>" + data + "</EventData></Event>";
}
EventReadResult ok_read(const std::vector<std::string>& xs) {
    EventReadResult r;
    r.status = ReadStatus::Ok;
    for (const auto& x : xs) {
        auto e = parse_event_xml(x);
        EF_CHECK(e.has_value());
        if (e) r.events.push_back(*e);
    }
    return r;
}
const IncidentFinding* find(const IncidentReport& r, IncidentKind k) {
    for (const auto& f : r.findings)
        if (f.kind == k) return &f;
    return nullptr;
}
const char* const kP41 = "Microsoft-Windows-Kernel-Power";
const char* const kPWer = "Microsoft-Windows-WER-SystemErrorReporting";
const char* const kPWhea = "Microsoft-Windows-WHEA-Logger";
}  // namespace

EF_TEST(x01_parse_real_shape_fields) {
    const auto e = parse_event_xml(xml(kP41, 41, 77, "2026-11-09T21:14:33.1234567Z", "<Data Name='BugcheckCode'>0</Data><Data Name='SleepInProgress'>0</Data>"));
    EF_CHECK(e && e->provider == kP41 && e->event_id == 41 && e->version == 8 && e->record_id == 77 && e->channel == "System");
    EF_CHECK(e && e->data.at("BugcheckCode") == "0" && e->raw_time == "2026-11-09T21:14:33.1234567Z");
    EF_CHECK(parse_system_time("1970-01-02T00:00:01.5Z") == std::optional<std::int64_t>(86401500));
    EF_CHECK(!parse_system_time("2026-11-09T21:14:33+03:00"));
    EF_CHECK(!parse_system_time("garbage"));
}

EF_TEST(x01_t01_single_event41_makes_no_psu_claim) {
    const auto r = analyze_incidents(ok_read({xml(kP41, 41, 1, "2026-11-09T10:00:00.0Z", "<Data Name='BugcheckCode'>0</Data>")}));
    const auto* f = find(r, IncidentKind::UnexpectedShutdown);
    EF_CHECK(f && f->count == 1 && !f->counter.empty());
    const auto v = incident_checks(r);
    EF_CHECK(v.size() == 1 && v[0].level == Level::Advice && v[0].text_key == "chk.incident.shutdown" && v[0].args.at("date") == "2026-11-09");
}

EF_TEST(x01_t02_access_denied_differs_from_no_events) {
    EventReadResult denied;
    denied.status = ReadStatus::AccessDenied;
    const auto d = incident_checks(analyze_incidents(denied));
    EF_CHECK(d.size() == 1 && d[0].level == Level::Unknown && d[0].text_key == "chk.incident.access_denied");
    EventReadResult err;
    err.status = ReadStatus::ReadError;
    EF_CHECK(incident_checks(analyze_incidents(err))[0].text_key == "chk.incident.read_error");
    const auto e = incident_checks(analyze_incidents(ok_read({})));
    EF_CHECK(e.size() == 1 && e[0].level == Level::Good && e[0].text_key == "chk.incident.none");
    EventReadResult cut;  // kesik okuma "temiz" denmez
    cut.status = ReadStatus::Ok;
    cut.truncated = true;
    EF_CHECK(incident_checks(analyze_incidents(cut))[0].level == Level::Unknown);
}

EF_TEST(x01_t03_malformed_or_huge_input_is_rejected_without_crash) {
    EF_CHECK(!parse_event_xml(""));
    EF_CHECK(!parse_event_xml("<Event><System>"));
    EF_CHECK(!parse_event_xml(std::string(kMaxEventXml + 1, 'a')));
    EF_CHECK(!parse_event_xml("<!DOCTYPE x [<!ENTITY e SYSTEM 'file:///c:/x'>]>" + xml(kP41, 41, 1, "2026-11-09T10:00:00Z")));
    EF_CHECK(!parse_event_xml(xml(kP41, 41, 1, "2026-11-09T10:00:00Z", "<Data Name='a'>unterminated")));
    EF_CHECK(!parse_event_xml(xml(kP41, 41, 0, "2026-11-09T10:00:00Z")));  // kayit kimligi yok: tekillesemez
    // WHEA: uzunluk tutmuyor veya hex degil -> sayilir ama gecersiz yuk isaretlenir
    auto r = analyze_incidents(ok_read({xml(kPWhea, 18, 5, "2026-11-09T10:00:00Z", "<Data Name='Length'>4</Data><Data Name='RawData'>AB</Data>")}));
    EF_CHECK(find(r, IncidentKind::WheaFatal) && find(r, IncidentKind::WheaFatal)->payload_invalid);
    r = analyze_incidents(ok_read({xml(kPWhea, 19, 6, "2026-11-09T10:00:00Z", "<Data Name='Length'>2</Data><Data Name='RawData'>ABCD</Data>", 3)}));
    EF_CHECK(find(r, IncidentKind::WheaCorrected) && !find(r, IncidentKind::WheaCorrected)->payload_invalid);
}

EF_TEST(x01_t04_duplicates_and_clock_change) {
    const auto a = xml(kP41, 41, 9, "2026-11-09T10:00:00Z", "<Data Name='BugcheckCode'>0</Data>");
    const auto r = analyze_incidents(ok_read({a, a}));
    EF_CHECK(find(r, IncidentKind::UnexpectedShutdown)->count == 1 && r.events_seen == 1);

    // Saat geri alindi: 1001 ile 41 eslestirilmez, belirsizlik yazilir.
    const auto later = xml(kPWer, 1001, 10, "2026-11-09T12:00:00Z", "<Data Name='param1'>0x9f (0x3)</Data>");
    const auto back = xml(kP41, 41, 11, "2026-11-09T11:59:00Z", "<Data Name='BugcheckCode'>0</Data>");
    const auto earlier = xml(kP41, 41, 12, "2026-11-09T09:00:00Z", "<Data Name='BugcheckCode'>0</Data>");
    const auto c = analyze_incidents(ok_read({later, back, earlier}));
    EF_CHECK(c.clock_uncertain && find(c, IncidentKind::UnexpectedShutdown)->count == 2);
    const auto v = incident_checks(c);
    EF_CHECK(std::any_of(v.begin(), v.end(), [](const auto& k) { return k.text_key == "chk.incident.clock"; }));
}

EF_TEST(x01_groups_bugcheck_with_its_event41_and_requires_provider_match) {
    const auto wer = xml(kPWer, 1001, 20, "2026-11-09T10:00:30Z", "<Data Name='param1'>0x0000009f (0x3, 0x0)</Data>");
    const auto kp = xml(kP41, 41, 19, "2026-11-09T10:00:00Z", "<Data Name='BugcheckCode'>159</Data>");
    const auto r = analyze_incidents(ok_read({wer, kp}));
    EF_CHECK(find(r, IncidentKind::BugCheck)->count == 1 && find(r, IncidentKind::BugCheck)->bugcheck == "0x0000009f");
    EF_CHECK(!find(r, IncidentKind::UnexpectedShutdown));
    // Ayni numara baska saglayicida kanit degil.
    EF_CHECK(analyze_incidents(ok_read({xml("Some-Other", 41, 3, "2026-11-09T10:00:00Z")})).findings.empty());
    EF_CHECK(analyze_incidents(ok_read({xml("Display", 41, 4, "2026-11-09T10:00:00Z")})).findings.empty());
}

EF_TEST(x01_disk_error_is_problem_with_protection_text) {
    const auto v = incident_checks(analyze_incidents(ok_read({xml("disk", 7, 30, "2026-11-08T10:00:00Z")})));
    EF_CHECK(v.size() == 1 && v[0].level == Level::Problem && v[0].text_key == "chk.incident.disk" && v[0].args.at("count") == "1");
}

EF_TEST(x01_whea_classified_by_level_not_short_id_list) {
    for (int id : {16, 46, 48, 22}) EF_CHECK(find(analyze_incidents(ok_read({xml(kPWhea, id, 40, "2026-11-09T10:00:00Z")})), IncidentKind::WheaFatal));
    for (int id : {49, 23, 28}) EF_CHECK(find(analyze_incidents(ok_read({xml(kPWhea, id, 41, "2026-11-09T10:00:00Z", "", 3)})), IncidentKind::WheaCorrected));
    EF_CHECK(analyze_incidents(ok_read({xml(kPWhea, 3, 42, "2026-11-09T10:00:00Z", "", 4)})).findings.empty());
    // Taninmayan seviye "temiz" denmez.
    const auto r = analyze_incidents(ok_read({xml(kPWhea, 99, 43, "2026-11-09T10:00:00Z", "", 9)}));
    EF_CHECK(r.unrecognized == 1 && incident_checks(r)[0].level == Level::Unknown);
}

EF_TEST(x01_links_whea_with_bugcheck_and_marks_0x124) {
    const auto wer = xml(kPWer, 1001, 50, "2026-11-09T10:00:30Z", "<Data Name='param1'>0x0000009f (0x3)</Data>");
    const auto wh = xml(kPWhea, 18, 49, "2026-11-09T10:00:00Z");
    {
        const auto v = incident_checks(analyze_incidents(ok_read({wer, wh})));
        EF_CHECK(std::any_of(v.begin(), v.end(), [](const auto& k) { return k.text_key == "chk.incident.bugcheck_whea"; }));
    }
    const auto c124 = xml(kPWer, 1001, 51, "2026-11-09T10:00:30Z", "<Data Name='param1'>0x00000124 (0x0)</Data>");
    EF_CHECK(incident_checks(analyze_incidents(ok_read({c124})))[0].text_key == "chk.incident.bugcheck_whea");
}

EF_TEST(x01_power_button_sleep_and_local_date) {
    const auto b = xml(kP41, 41, 60, "2026-11-09T22:30:00Z", "<Data Name='BugcheckCode'>0</Data><Data Name='PowerButtonTimestamp'>133</Data>");
    const auto v = incident_checks(analyze_incidents(ok_read({b})), 180);
    EF_CHECK(v[0].text_key == "chk.incident.shutdown_button" && v[0].args.at("date") == "2026-11-10");  // yerel gun
    const auto s = xml(kP41, 41, 61, "2026-11-09T10:00:00Z", "<Data Name='BugcheckCode'>0</Data><Data Name='SleepInProgress'>1</Data>");
    EF_CHECK(incident_checks(analyze_incidents(ok_read({s})))[0].text_key == "chk.incident.shutdown_sleep");
}

EF_TEST(x01_bugcheck_not_double_counted_on_slow_dump_or_clock_jump) {
    const auto kp = xml(kP41, 41, 70, "2026-11-09T10:00:00Z", "<Data Name='BugcheckCode'>159</Data>");
    const auto wer = xml(kPWer, 1001, 71, "2026-11-09T10:20:00Z", "<Data Name='param1'>0x0000009f (0x3)</Data>");
    EF_CHECK(find(analyze_incidents(ok_read({kp, wer})), IncidentKind::BugCheck)->count == 1);
}

EF_TEST(x01_short_log_coverage_is_not_clean) {
    EventReadResult r;
    r.status = ReadStatus::Ok;
    r.oldest_ms = 1000000 - 86400000;  // gunluk yalniz 1 gunluk
    const auto v = incident_checks(analyze_incidents(r, 7, 1000000));
    EF_CHECK(v[0].level == Level::Unknown && v[0].text_key == "chk.incident.partial");
}

EF_TEST(x01_disk_paging_warning_is_advice_and_reports_disk) {
    const auto f = analyze_incidents(ok_read({xml("disk", 51, 80, "2026-11-08T10:00:00Z", "<Data>\\Device\\Harddisk3\\DR3</Data>")}));
    const auto v = incident_checks(f);
    EF_CHECK(v[0].level == Level::Advice && v[0].args.at("disks") == "3");
}

EF_TEST(x01_invalid_whea_payload_is_flagged_to_user) {
    const auto v = incident_checks(analyze_incidents(ok_read({xml(kPWhea, 18, 90, "2026-11-09T10:00:00Z", "<Data Name='Length'>4</Data><Data Name='RawData'>AB</Data>")})));
    EF_CHECK(std::any_of(v.begin(), v.end(), [](const auto& k) { return k.text_key == "chk.incident.whea_payload"; }));
}

EF_TEST(x01_step2_window_is_7_or_30_and_row_offers_toggle) {
    EF_CHECK(normalize_incident_days(7) == 7 && normalize_incident_days(30) == 30);
    for (int bad : {0, -1, 8, 29, 31, 365}) EF_CHECK(normalize_incident_days(bad) == kDefaultDays);
    const auto r7 = analyze_incidents(ok_read({}), 7);
    auto v7 = incident_checks(r7);
    add_incident_window_action(v7, r7);
    EF_CHECK(v7.size() == 1 && v7[0].action == ef::core::readiness::ActionKind::IncidentWindow && v7[0].action_arg == "30" &&
              v7[0].args.at("days") == "7");
    const auto r30 = analyze_incidents(ok_read({xml(kP41, 41, 5, "2026-11-01T10:00:00Z", "<Data Name='BugcheckCode'>0</Data>")}), 30);
    auto v30 = incident_checks(r30);
    add_incident_window_action(v30, r30);
    EF_CHECK(!v30.empty() && v30[0].action_arg == "7" && v30[0].args.at("days") == "30");
    for (std::size_t i = 1; i < v30.size(); ++i) EF_CHECK(v30[i].action == ef::core::readiness::ActionKind::None);
    EventReadResult denied;  // T02: erisim reddi 30 gunde de "olay yok" olmaz ve pencere dugmesi verilmez
    denied.status = ReadStatus::AccessDenied;
    const auto rd = analyze_incidents(denied, 30);
    auto vd = incident_checks(rd);
    add_incident_window_action(vd, rd);
    EF_CHECK(vd.size() == 1 && vd[0].text_key == "chk.incident.access_denied" && vd[0].action == ef::core::readiness::ActionKind::None);
    std::vector<ef::core::readiness::Check> none;
    add_incident_window_action(none, r7);
    EF_CHECK(none.empty());
}

EF_TEST(x01_step2_30_day_window_on_short_log_is_partial_not_clean) {
    EventReadResult r;
    r.status = ReadStatus::Ok;
    const std::int64_t now = 100LL * 86400000;
    r.oldest_ms = now - 10LL * 86400000;  // gunluk yalniz 10 gun geriye gidiyor
    EF_CHECK(incident_checks(analyze_incidents(r, 7, now))[0].text_key == "chk.incident.none");
    const auto v = incident_checks(analyze_incidents(r, 30, now));
    EF_CHECK(v[0].level == Level::Unknown && v[0].text_key == "chk.incident.partial" && v[0].args.at("days") == "30");
}
