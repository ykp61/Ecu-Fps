#include "core/adapters/windows/update_guard.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using ef::contracts::Field;
using ef::contracts::Provenance;

EF_TEST(update_guard_never_writes_and_advises_on_risk) {
    UpdateStatus s;
    s.reboot_pending = Field<bool>::known(false, Provenance::Api);
    s.active_downloads = Field<int>::known(0, Provenance::Api);
    s.managed_by_policy = Field<bool>::known(false, Provenance::Registry);
    auto a = advise_for_game_session(s);
    EF_CHECK(a.risks.empty() && !a.action && !a.writes_anything);
    s.reboot_pending = Field<bool>::known(true, Provenance::Api);
    s.active_downloads = Field<int>::known(2, Provenance::Api);
    a = advise_for_game_session(s);
    EF_CHECK(a.risks.size() == 2 && a.action == ef::core::guided::Action::WindowsUpdate && !a.writes_anything);
}

EF_TEST(update_guard_unknown_is_not_risk) {
    UpdateStatus s;
    s.reboot_pending = Field<bool>::unknown("WUA unavailable");
    s.active_downloads = Field<int>::unknown("BITS unavailable");
    EF_CHECK(advise_for_game_session(s).risks.empty());
}

EF_TEST(update_risk_texts_exist) {
    for (auto r : {UpdateRisk::RebootPending, UpdateRisk::DownloadActive, UpdateRisk::ManagedByPolicy}) {
        bool found = false;
        for (std::size_t i = 0; i < ef::core::i18n::kStringCount; ++i)
            if (ef::core::i18n::key_of(static_cast<ef::core::i18n::StringId>(i)) == text_key(r)) found = true;
        EF_CHECK(found);
    }
}
