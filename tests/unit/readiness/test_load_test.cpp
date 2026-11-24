// EF-X13: yuk testi karari T01-T04 (sentetik olcum; gercek ag yuku yok).
#include "core/readiness/load_test.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {
std::vector<std::optional<double>> rtts(double base, int n = 20) {
    std::vector<std::optional<double>> v;
    for (int i = 0; i < n; ++i) v.push_back(base + (i % 5));
    return v;
}
LoadTestInput ready() {
    LoadTestInput in;
    in.endpoint_configured = true;
    in.gate_open = true;
    in.link_mbps = 1000;
    in.idle_rtt_ms = rtts(10);
    in.download = {rtts(14), 900, 10, true};
    in.upload = {rtts(15), 850, 10, true};
    return in;
}
}  // namespace

EF_TEST(x13_without_operated_server_test_is_blocked) {
    auto in = ready();
    in.endpoint_configured = false;
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::BlockedBackend);
}

EF_TEST(x13_t01_capped_test_does_not_call_link_good) {
    auto in = ready();
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::Ok);
    in.download = {rtts(14), 100, 10, false};  // 1 Gbps hatta 100 Mbps: doymadi
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::InsufficientLoad);
    in.download = {rtts(80), 100, 10, false};  // doymasa da gozlenen artis raporlanir
    const auto r = evaluate_load_test(in);
    EF_CHECK(r.verdict == LoadVerdict::Bloat && r.bloat_down_ms && *r.bloat_down_ms > 60);
}

EF_TEST(x13_t02_blocked_icmp_is_not_internet_failure) {
    auto in = ready();
    in.idle_rtt_ms.assign(20, std::nullopt);  // %100 kayip
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::NoBaseline);
}

EF_TEST(x13_t03_cancel_and_budget_enforced) {
    EF_CHECK(load_budget_stop(10, 1 << 20, true) == std::optional<LoadVerdict>(LoadVerdict::Cancelled));
    EF_CHECK(load_budget_stop(kLoadMaxSeconds, 0, false) == std::optional<LoadVerdict>(LoadVerdict::BudgetReached));
    EF_CHECK(load_budget_stop(1, kLoadMaxBytes, false) == std::optional<LoadVerdict>(LoadVerdict::BudgetReached));
    EF_CHECK(!load_budget_stop(59, kLoadMaxBytes - 1, false));
    auto in = ready();
    in.cancelled = true;
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::Cancelled);
}

EF_TEST(x13_t04_gate_blocks_load_test) {
    auto in = ready();
    in.gate_open = false;
    EF_CHECK(evaluate_load_test(in).verdict == LoadVerdict::Gated);
}

EF_TEST(x13_loss_under_load_is_not_ok) {
    auto in = ready();
    for (int i = 0; i < 9; ++i) in.download.rtt_ms.push_back(std::nullopt);  // 29 ornekte 9 kayip
    const auto r = evaluate_load_test(in);
    EF_CHECK(r.verdict == LoadVerdict::LossUnderLoad && r.down_loss_pct > 30);
}
