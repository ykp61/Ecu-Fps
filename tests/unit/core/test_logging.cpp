#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "contracts/support.h"
#include "core/logging/log.h"
#include "core/logging/redact.h"
#include "ef_test.h"
#include "platform/windows/win_env.h"

using namespace ef;
using core::logging::redact;
using core::logging::RedactionContext;

namespace {
RedactionContext ctx() {
    RedactionContext c;
    c.user_name = "testuser";
    c.machine_name = "TESTPC";
    c.profile_dir = "C:\\Users\\testuser";
    c.extra_secrets = {"hunter2secret"};
    return c;
}
bool contains(const std::string& s, std::string_view n) { return s.find(n) != std::string::npos; }
}  // namespace

EF_TEST(redact_identity_values) {
    const auto out = redact("user TESTUSER on testpc opened C:\\Users\\testuser\\AppData\\x.json", ctx());
    EF_CHECK(!contains(out, "testuser"));
    EF_CHECK(!contains(out, "TESTUSER"));
    EF_CHECK(!contains(out, "TESTPC") && !contains(out, "testpc"));
    EF_CHECK(contains(out, "<profile>\\AppData\\x.json"));
    EF_CHECK(contains(out, "<machine>"));
}

EF_TEST(redact_other_profiles_sids_emails_tokens) {
    const auto out = redact(
        "path D:\\Users\\Ahmet Yılmaz\\Desktop sid S-1-5-21-3623811015-3361044348-30300820-1013 "
        "mail someone@example.invalid Authorization: Bearer abc.def.ghi password=hunter2 token: 12345 "
        "k=hunter2secret",
        ctx());
    EF_CHECK(!contains(out, "Ahmet"));
    EF_CHECK(contains(out, "\\Users\\<user>\\Desktop"));
    EF_CHECK(!contains(out, "3623811015"));
    EF_CHECK(contains(out, "<sid>"));
    EF_CHECK(!contains(out, "gmail.com"));
    EF_CHECK(contains(out, "<email>"));
    EF_CHECK(!contains(out, "abc.def.ghi"));
    EF_CHECK(!contains(out, "hunter2"));
    EF_CHECK(!contains(out, "12345"));
}

EF_TEST(redact_keeps_ordinary_text) {
    const auto out = redact("GPU NVIDIA RTX 5080 driver 32.0.16.2101 fps 144 @ 1440p", ctx());
    EF_CHECK(out == "GPU NVIDIA RTX 5080 driver 32.0.16.2101 fps 144 @ 1440p");
    EF_CHECK(redact("S-1 not a sid", ctx()) == "S-1 not a sid");
}

EF_TEST(redact_blocks_log_injection) {
    const auto out = redact("ok\r\nFAKE 2026 ERROR admin: injected", ctx());
    EF_CHECK(!contains(out, "\n"));
    EF_CHECK(!contains(out, "\r"));
}

EF_TEST(logger_redacts_and_filters_level) {
    core::FakeClock clock;
    core::logging::MemorySink sink;
    core::logging::Logger log(sink, clock, ctx(), core::logging::Level::Info);
    log.log(core::logging::Level::Debug, "x", "hidden");
    log.info("journal", "wrote C:\\Users\\testuser\\AppData\\Local\\EcuFps\\j.json");
    EF_CHECK(sink.lines.size() == 1);
    EF_CHECK(contains(sink.lines[0], "INFO journal: wrote <profile>"));
    EF_CHECK(contains(sink.lines[0], "2023-11-14T22:13:20.000Z"));
}

EF_TEST(format_utc_known_dates) {
    using core::logging::format_utc;
    EF_CHECK(format_utc(core::UtcTime{0}) == "1970-01-01T00:00:00.000Z");
    EF_CHECK(format_utc(core::UtcTime{951782400000}) == "2000-02-29T00:00:00.000Z");
    EF_CHECK(format_utc(core::UtcTime{1793752041123}) == "2026-11-04T00:27:21.123Z");
}

EF_TEST(rotating_file_sink_enforces_quota) {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("ef-log-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        core::logging::RotatingFileSink sink(dir, "app", 1024, 3);
        const std::string line(100, 'x');
        for (int i = 0; i < 100; ++i) sink.write_line(line);
    }
    std::uintmax_t total = 0;
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        ++files;
        total += e.file_size();
        EF_CHECK(e.file_size() <= 1024);
    }
    EF_CHECK(files == 3);
    EF_CHECK(total <= 3 * 1024);
    std::filesystem::remove_all(dir);
}

EF_TEST(real_redaction_context_masks_this_machine) {
    const auto c = platform::current_redaction_context();
    EF_CHECK(!c.user_name.empty());
    EF_CHECK(!c.machine_name.empty());
    EF_CHECK(!c.profile_dir.empty());
    const auto out = redact("u=" + c.user_name + " m=" + c.machine_name + " p=" + c.profile_dir + "\\x", c);
    EF_CHECK(!contains(out, c.user_name));
    EF_CHECK(!contains(out, c.machine_name));
    auto dir = platform::app_data_dir();
    EF_CHECK(dir.has_value());
    EF_CHECK(dir->filename() == "EcuFps");
}

EF_TEST(release_gate_requires_real_device_evidence) {
    using namespace contracts;
    std::vector<SupportEvidence> ev{
        {"W01", "mock", SupportState::Supported, EvidenceLevel::Mock, "abc", "unit", "artifacts/evidence/W.md"},
        {"W01", "win", SupportState::Supported, EvidenceLevel::WindowsIntegration, "abc", "ctest", "artifacts/evidence/W.md"},
    };
    EF_CHECK(!may_enable_release("W01", ev));
    ev.push_back({"W01", "win11 rtx5080", SupportState::Supported, EvidenceLevel::Hardware, "abc", "manual", ""});
    EF_CHECK(!may_enable_release("W01", ev));  // kanit yolu yok
    ev.back().evidence_path = "artifacts/evidence/WIN-01.md";
    EF_CHECK(may_enable_release("W01", ev));
    EF_CHECK(!may_enable_release("W02", ev));
    ev.push_back({"W01", "win10", SupportState::Denied, EvidenceLevel::Hardware, "abc", "manual", "artifacts/evidence/x.md"});
    EF_CHECK(!may_enable_release("W01", ev));
}
