// EF-X25: gecikme plani alanlari (T03), DX12 kuyruk kapsam secici (T01), olculmemis gecikme sayisi yok (T02). Sentetik.
#include <string>
#include <string_view>

#include "core/adapters/nvidia/drs.h"
#include "core/games/game_guide.h"
#include "core/i18n/catalog.h"
#include "core/perf/admin_plan.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core;
namespace nv = ef::core::adapters::nvidia;
namespace i18n = ef::core::i18n;

namespace {
// A3: tek NVIDIA donanim GPU'su (MOCK) -> melez kapisi acik.
const perf::NvidiaGate kNvOk = [] {
    ef::core::inventory::GpuAdapter g;
    g.index = 0;
    g.vendor_id = ef::contracts::Field<std::uint32_t>::known(ef::core::inventory::kVendorNvidia, ef::contracts::Provenance::Api);
    g.kind = ef::core::inventory::GpuKind::Discrete;
    return perf::nvidia_adapter_gate({g}, std::nullopt, true);
}();
}  // namespace

namespace {
// "<rakam>[bosluk]ms" var mi: uctan uca gecikme sayisi iddiasi yakalayici.
bool has_ms_number(std::string_view s) {
    for (std::size_t p = s.find("ms"); p != std::string_view::npos; p = s.find("ms", p + 1)) {
        std::size_t q = p;
        while (q > 0 && s[q - 1] == ' ') --q;
        if (q > 0 && s[q - 1] >= '0' && s[q - 1] <= '9') return true;
    }
    return false;
}
}  // namespace

EF_TEST(x25_t03_every_nvidia_item_names_its_field) {
    nv::FakeDrs d;
    d.defaults[0x1057EB71] = 5;
    d.defaults[0x007BA09E] = 0;
    d.defaults[0x00AC8497] = 0x4000;
    perf::Plan p;
    p.items = perf::nvidia_plan_items(&d, kNvOk);
    EF_CHECK(p.items.size() == 2);
    for (const auto& i : p.items) EF_CHECK(!i.field_key.empty() && i18n::id_of(i.field_key).has_value());
    const auto f = perf::group_field_keys(p, "plan.nvidia");
    EF_CHECK(f.size() == 2);
    if (f.size() == 2) EF_CHECK(f[0] == "plan.field.nv_power" && f[1] == "plan.field.nv_queue");
    EF_CHECK(i18n::id_of("plan.field.nv_shader").has_value());
}

EF_TEST(x25_t03_only_changing_fields_listed) {
    nv::FakeDrs d;
    d.defaults[0x00AC8497] = 0x4000;
    d.global[0x1057EB71] = 1;  // guc zaten hedefte: onayda listelenmez
    d.global[0x007BA09E] = 0;
    perf::Plan p;
    p.items = perf::nvidia_plan_items(&d, kNvOk);
    const auto f = perf::group_field_keys(p, "plan.nvidia");
    EF_CHECK(f.size() == 1);
    if (f.size() == 1) EF_CHECK(f[0] == "plan.field.nv_queue");
    EF_CHECK(perf::group_field_keys(p, "plan.hags").empty());
}

EF_TEST(x25_t03_group_field_keys_dedup_and_skip_empty) {
    perf::Plan p;
    p.items.resize(3);
    for (auto& i : p.items) i.title_key = i.group_key = "plan.nvidia";
    p.items[0].field_key = "plan.field.nv_queue";
    p.items[1].field_key = "plan.field.nv_queue";
    const auto f = perf::group_field_keys(p, "plan.nvidia");
    EF_CHECK(f.size() == 1);
    if (f.size() == 1) EF_CHECK(f[0] == "plan.field.nv_queue");
}

EF_TEST(x25_t01_driver_queue_note_only_for_dx12_vulkan) {
    using G = games::GuideRenderer;
    EF_CHECK(games::driver_queue_note_key(G::D3D12) == "guide.driver_queue_game_managed");
    EF_CHECK(games::driver_queue_note_key(G::Vulkan) == "guide.driver_queue_game_managed");
    EF_CHECK(games::driver_queue_note_key(G::D3D11).empty() && games::driver_queue_note_key(G::D3D9).empty());
    EF_CHECK(games::driver_queue_note_key(G::Unknown).empty());  // gozlenmemis API icin kapsam iddiasi yok
    EF_CHECK(i18n::id_of("guide.driver_queue_game_managed").has_value());
    EF_CHECK(i18n::id_of("games.nv.detail_game_managed").has_value());
}

EF_TEST(x25_t02_latency_texts_carry_no_measured_ms_number) {
    EF_CHECK(has_ms_number("12 ms") && has_ms_number("3ms") && !has_ms_number("kadar ms azaldi"));
    for (auto l : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto c = i18n::Catalog::embedded(l);
        EF_CHECK(c.ok());
        if (!c.ok()) continue;
        for (auto id : {i18n::StringId::ChkLatencyGuide, i18n::StringId::GuideLatency, i18n::StringId::GuideDriverQueueGameManaged,
                        i18n::StringId::GamesNvDetailGameManaged, i18n::StringId::PlanFieldNvPower, i18n::StringId::PlanFieldNvQueue,
                        i18n::StringId::PlanFieldNvShader, i18n::StringId::PlanNvidia, i18n::StringId::PlanNvidiaDesc}) {
            const auto s = c.value().get(id);
            EF_CHECK(!s.empty() && !has_ms_number(s));
        }
    }
}

// Inceleme: Reflex varligi tespit edilmez; oyun basina not Reflex'i var saymamali (yalniz ornek, "varsa").
EF_TEST(x25_review_game_note_does_not_assert_reflex_present) {
    struct L {
        i18n::Locale loc;
        std::string_view hedge, cond, bad;
    };
    for (const L& l : {L{i18n::Locale::TrTR, "(Reflex gibi)", "varsa", "Reflex belirleyicidir"},
                       L{i18n::Locale::EnUS, "(such as Reflex)", "if present", "in-game Reflex decides"}}) {
        auto c = i18n::Catalog::embedded(l.loc);
        EF_CHECK(c.ok());
        if (!c.ok()) continue;
        for (auto id : {i18n::StringId::GamesNvDetailGameManaged, i18n::StringId::GuideDriverQueueGameManaged}) {
            const std::string s(c.value().get(id));
            EF_CHECK(s.find(l.hedge) != std::string::npos);
            EF_CHECK(s.find(l.bad) == std::string::npos);
        }
        const std::string g(c.value().get(i18n::StringId::GamesNvDetailGameManaged));
        EF_CHECK(g.find(l.cond) != std::string::npos);
    }
}
