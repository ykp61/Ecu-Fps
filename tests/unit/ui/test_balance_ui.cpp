// Denge testi arayuzu (spec 2026-11-12 A5): kapsam secimi "Genel test | Denge testi" sabit kimlikleri, UIA
// AutomationId'leri ve odak sirasi. Mevcut kimlikler ve sira degismez.
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
namespace cm = ef::core::measure;

namespace {
std::ptrdiff_t pos(const std::vector<Hit>& v, Hit h) {
    const auto it = std::find(v.begin(), v.end(), h);
    return it == v.end() ? -1 : it - v.begin();
}
bool has(const std::vector<Hit>& v, Hit h) { return pos(v, h) >= 0; }
}  // namespace

EF_TEST(balance_scope_hits_are_fixed_and_named) {
    for (Hit h : {Hit::ScopeGeneral, Hit::ScopeBalance}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    EF_CHECK(hit_automation_id(Hit::ScopeGeneral) == L"measure.scope.general");
    EF_CHECK(hit_automation_id(Hit::ScopeBalance) == L"measure.scope.balance");
    // Mevcut kimlikler yerinde.
    static_assert(static_cast<int>(Hit::SourceBuiltin) == 110 && static_cast<int>(Hit::SourceGame) == 111 && static_cast<int>(Hit::TestStart) == 112 &&
                  static_cast<int>(Hit::TestStop) == 113 && static_cast<int>(Hit::ModeQuick) == 114 && static_cast<int>(Hit::ModeFull) == 115);
    EF_CHECK(hit_automation_id(Hit::SourceBuiltin) == L"measure.source.builtin" && hit_automation_id(Hit::TestStart) == L"measure.start" &&
              hit_automation_id(Hit::ModeQuick) == L"measure.mode.quick");
}

EF_TEST(balance_scope_focus_order_follows_visual_order) {
    UiState s;
    s.page = Page::Measurements;
    s.measure.builtin_available = true;
    s.measure.ingame_available = true;
    s.measure.storage_roots = {"C:\\"};
    s.speed_can_start = true;
    // Spec 2026-11-12 C + B10 (Test karti): kaynak haplari -> kapsam haplari -> sure -> birim -> baslat (serit sag ucu).
    auto ord = focus_order(s);
    EF_CHECK(pos(ord, Hit::ModeQuick) < pos(ord, Hit::TestStart));  // C: baslat dugmesi seridin sag ucunda
    EF_CHECK(pos(ord, Hit::SourceBuiltin) < pos(ord, Hit::SourceGame));
    EF_CHECK(pos(ord, Hit::SourceGame) < pos(ord, Hit::ScopeGeneral));
    EF_CHECK(pos(ord, Hit::ScopeGeneral) < pos(ord, Hit::ScopeBalance));
    EF_CHECK(pos(ord, Hit::ScopeBalance) < pos(ord, Hit::ModeQuick));
    EF_CHECK(has(ord, hit_of(HitKind::StorageChoice, 0)));
    // Denge testi kapsaminda sure secimi kalir; depolama birimi secimi cizilmez (denge testi diske yazmaz).
    s.measure.scope = cm::BuiltinScope::Balance;
    ord = focus_order(s);
    EF_CHECK(has(ord, Hit::ScopeGeneral) && has(ord, Hit::ScopeBalance) && has(ord, Hit::TestStart) && has(ord, Hit::ModeQuick) && has(ord, Hit::ModeFull));
    EF_CHECK(!has(ord, hit_of(HitKind::StorageChoice, 0)) && !has(ord, hit_of(HitKind::StorageChoice, 1)));
    // Oyunda Test seciliyken kapsam secimi yok.
    s.measure.source = cm::TestSource::InGame;
    ord = focus_order(s);
    EF_CHECK(has(ord, Hit::SourceBuiltin) && !has(ord, Hit::ScopeGeneral) && !has(ord, Hit::ScopeBalance));
    // Test islerken / uzun test surerken kapsam Tab almaz (devre disi cizilir).
    s.measure.source = cm::TestSource::Builtin;
    s.measure.phase = cm::TestPhase::Running;
    ord = focus_order(s);
    EF_CHECK(has(ord, Hit::TestStop) && !has(ord, Hit::ScopeGeneral) && !has(ord, Hit::ScopeBalance));
    s.measure.phase = cm::TestPhase::Idle;
    s.measure.endurance.running = true;
    ord = focus_order(s);
    EF_CHECK(!has(ord, Hit::ScopeGeneral) && !has(ord, Hit::ScopeBalance));
}

// P3: Darbogaz "Raporu kaydet": sabit kimlik, UIA AutomationId, yalniz "Bu PC'de olculemedi" iken odak sirasinda.
EF_TEST(balance_report_hit_is_fixed_named_and_gated) {
    EF_CHECK(classify_hit(Hit::BalanceReport).kind == HitKind::Fixed);
    EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), Hit::BalanceReport) != std::end(kFixedHits));
    EF_CHECK(hit_automation_id(Hit::BalanceReport) == L"measure.balance.report");
    static_assert(static_cast<int>(Hit::ProofStart) == 218 && static_cast<int>(Hit::ProofStop) == 219 && static_cast<int>(Hit::BalanceReport) == 220);
    UiState s;
    s.page = Page::Measurements;
    s.measure.builtin_available = true;
    // Kart yok: dugme yok.
    EF_CHECK(!has(focus_order(s), Hit::BalanceReport));
    // Her satir hukumlu: dugme yok.
    cm::BalanceView v;
    for (auto& r : v.rows) r.verdict = ef::core::diagnosis::BalanceVerdict::CpuLimited;
    s.measure.balance_latest = v;
    EF_CHECK(!cm::balance_report_offered(s.measure) && !has(focus_order(s), Hit::BalanceReport));
    // Bir satir Belirlenemedi: dugme var.
    s.measure.balance_latest->rows[1].verdict = ef::core::diagnosis::BalanceVerdict::Undetermined;
    EF_CHECK(cm::balance_report_offered(s.measure) && has(focus_order(s), Hit::BalanceReport));
    // Rapor hazirlanirken (kart yazar) Tab almaz.
    s.balance_report_busy = true;
    EF_CHECK(!has(focus_order(s), Hit::BalanceReport));
}
