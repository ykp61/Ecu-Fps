#include "core/guided/guided.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::guided;

EF_TEST(guided_uris_are_allowlisted_ms_settings) {
    for (auto a : {Action::GameModeSettings, Action::GraphicsSettings, Action::CaptureSettings, Action::WindowsUpdate,
                   Action::StartupApps, Action::PowerSettings, Action::DisplaySettings, Action::StorageSettings}) {
        EF_CHECK(uri(a).rfind("ms-settings:", 0) == 0);
        EF_CHECK(is_allowed_uri(uri(a)));
        bool found = false;
        for (std::size_t i = 0; i < ef::core::i18n::kStringCount; ++i)
            if (ef::core::i18n::key_of(static_cast<ef::core::i18n::StringId>(i)) == text_key(a)) found = true;
        EF_CHECK(found);
    }
    EF_CHECK(!is_allowed_uri("file:///C:/Windows/System32/cmd.exe"));
    EF_CHECK(!is_allowed_uri("ms-settings:"));
    EF_CHECK(!is_allowed_uri("https://example.com"));
}
