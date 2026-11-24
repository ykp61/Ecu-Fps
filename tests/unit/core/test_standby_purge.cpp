// T15: bekleme bellegi bosaltma (sahte API; gercek NtSetSystemInformation calistirilmaz).
#include "core/broker/admin.h"
#include "core/cleanup/session.h"
#include "core/maintenance/standby_purge.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::maintenance;
using ef::contracts::ErrorCode;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
struct FakeMem final : MemoryListApi {
    std::vector<std::optional<std::uint64_t>> reads;  // sirayla okuma sonuclari (nullopt = okunamaz)
    bool purge_ok = true;
    int purges = 0, read_calls = 0;
    ef::contracts::Result<std::uint64_t> standby_bytes() override {
        const auto v = read_calls < static_cast<int>(reads.size()) ? reads[static_cast<std::size_t>(read_calls)] : std::nullopt;
        ++read_calls;
        if (!v) return ef::contracts::Result<std::uint64_t>::fail(ErrorCode::IoError, "unreadable");
        return *v;
    }
    ef::contracts::Status purge_standby() override {
        ++purges;
        return purge_ok ? ef::contracts::ok_status() : ef::contracts::Status::fail(ErrorCode::Denied, "privilege");
    }
};
}  // namespace

EF_TEST(t15_purge_reports_measured_before_after) {
    FakeMem m;
    m.reads = {2048ull * 1024 * 1024, 100ull * 1024 * 1024};
    const auto r = run_standby_purge(m);
    EF_CHECK(r.ok && r.measured() && *r.before_bytes == 2048ull * 1024 * 1024 && *r.after_bytes == 100ull * 1024 * 1024 && m.purges == 1);
}

EF_TEST(t15_purge_failure_shows_no_numbers) {
    FakeMem m;
    m.reads = {5000, 4000};
    m.purge_ok = false;
    const auto r = run_standby_purge(m);
    EF_CHECK(!r.ok && !r.before_bytes && !r.after_bytes && !r.measured() && r.error_key == "purge");
}

EF_TEST(t15_unreadable_sizes_give_no_numbers) {
    FakeMem a;  // once okunamaz
    a.reads = {std::nullopt, 10};
    auto r = run_standby_purge(a);
    EF_CHECK(r.ok && !r.measured() && !r.before_bytes && !r.after_bytes);  // yarim sayi gosterilmez
    FakeMem b;  // sonra okunamaz
    b.reads = {10, std::nullopt};
    r = run_standby_purge(b);
    EF_CHECK(r.ok && !r.measured() && !r.before_bytes && !r.after_bytes);
}

EF_TEST(t15_detail_roundtrip_and_garbage) {
    PurgeRun p;
    p.ok = true;
    p.before_bytes = 123;
    p.after_bytes = 45;
    const auto d = encode_purge_detail(p);
    EF_CHECK(d == "mj:purged:b=123:a=45");
    const auto back = parse_purge_detail(d);
    EF_CHECK(back.ok && back.measured() && *back.before_bytes == 123 && *back.after_bytes == 45);
    p.after_bytes.reset();
    const auto half = parse_purge_detail(encode_purge_detail(p));
    EF_CHECK(half.ok && !half.measured() && !half.before_bytes);
    EF_CHECK(!parse_purge_detail("mj:failed:purge").ok);
    EF_CHECK(!parse_purge_detail("mj:purged:b=x:a=1").measured());
    EF_CHECK(!parse_purge_detail("").ok);
}

EF_TEST(t15_broker_allows_only_the_fixed_target) {
    ef::contracts::SettingOperation op{OperationId::MaintenanceJob, OperationScope::Machine, kStandbyPurgeTarget, kMaintenanceValue, false, false};
    EF_CHECK(broker::admin_operation_allowed(op));
    op.target = "maint:standby_purge2";
    EF_CHECK(!broker::admin_operation_allowed(op));
    op.target = "maint:other";
    EF_CHECK(!broker::admin_operation_allowed(op));
    op.target = kStandbyPurgeTarget;
    op.new_value = "list=4";
    EF_CHECK(!broker::admin_operation_allowed(op));
    op.new_value = kMaintenanceValue;
    op.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(op));  // yalniz makine kapsami
}

EF_TEST(t15_cleanup_category_present_default_unselected) {
    using cleanup::CleanupCategory;
    bool in_product = false;
    for (auto c : cleanup::kProductCategories) in_product |= c == CleanupCategory::StandbyMemory;
    EF_CHECK(in_product);
    EF_CHECK(cleanup::kProductCategories.back() == CleanupCategory::StandbyMemory);  // listenin sonunda
    const auto& i = cleanup::info(CleanupCategory::StandbyMemory);
    EF_CHECK(!i.default_selected && i.title_key == "cleanup.standby" && i.desc_key == "cleanup.standby.desc");
}

EF_TEST(t15_cleanup_session_standby_measured_selectable_not_disk_bytes) {
    using cleanup::CleanupCategory;
    cleanup::FakeCleanupFs fs;
    fs.standby_value = 3000;
    cleanup::CleanupEnv env;
    env.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    env.temp = L"C:\\Users\\u\\AppData\\Local\\Temp";
    env.windir = L"C:\\Windows";
    env.program_data = L"C:\\ProgramData";
    std::vector<std::function<void()>> q;
    cleanup::SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const cleanup::PreviewOptions& o) {
        return cleanup::preview(c, env, fs, v, o);
    };
    d.run = [&](const cleanup::CleanupPlan& p, const std::vector<CleanupCategory>& s, const cleanup::RunOptions& o) { return cleanup::run(p, s, fs, o); };
    d.run_background = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    auto drain = [&] {
        while (!q.empty()) {
            auto f = std::move(q.front());
            q.erase(q.begin());
            f();
        }
    };
    cleanup::CleanupSession s(d);
    s.scan();
    drain();
    const auto* cp = s.plan().find(CleanupCategory::StandbyMemory);
    EF_CHECK(cp && cp->preview.status == cleanup::PreviewStatus::Ready && cp->preview.bytes == 3000);
    EF_CHECK(s.selectable(CleanupCategory::StandbyMemory) && !s.selected(CleanupCategory::StandbyMemory));  // varsayilan secili degil
    EF_CHECK(!s.any_selected());
    s.request_run();
    EF_CHECK(s.phase() == cleanup::SessionPhase::Ready);  // secim yok: onay istenmez
    s.set_selected(CleanupCategory::StandbyMemory, true);
    EF_CHECK(s.any_selected() && s.selected_bytes() == 0);  // bellek disk alani sayilmaz
    s.request_run();
    EF_CHECK(s.phase() == cleanup::SessionPhase::Confirming);
}

EF_TEST(t15_cleanup_preview_error_when_standby_unreadable) {
    cleanup::FakeCleanupFs fs;  // standby_value yok: okunamaz
    cleanup::CleanupEnv env;
    env.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    env.temp = L"C:\\Users\\u\\AppData\\Local\\Temp";
    env.windir = L"C:\\Windows";
    env.program_data = L"C:\\ProgramData";
    const auto plan = cleanup::preview({cleanup::CleanupCategory::StandbyMemory}, env, fs);
    const auto* cp = plan.find(cleanup::CleanupCategory::StandbyMemory);
    EF_CHECK(cp && cp->preview.status == cleanup::PreviewStatus::Error && cp->preview.bytes == 0);
}

EF_TEST(t15_core_run_never_treats_standby_as_file_work) {
    cleanup::FakeCleanupFs fs;
    fs.standby_value = 1;
    cleanup::CleanupEnv env;
    env.temp = L"C:\\Users\\u\\AppData\\Local\\Temp";
    const auto plan = cleanup::preview({cleanup::CleanupCategory::StandbyMemory}, env, fs);
    const auto r = cleanup::run(plan, {cleanup::CleanupCategory::StandbyMemory}, fs);
    EF_CHECK(r.ok() && r.value().categories.empty() && r.value().bytes_deleted() == 0 && !r.value().standby.ran);
}
