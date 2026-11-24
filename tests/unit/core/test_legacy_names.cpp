#include "core/legacy/legacy_names.h"
#include "core/product_info.h"
#include "core/setup/layout.h"
#include "core/settings/autostart.h"
#include "core/update/update_check.h"
#include "ef_test.h"

namespace leg = ef::core::legacy;

EF_TEST(legacy_names_are_set_and_differ_from_new_names) {
    using namespace ef::core::setup;
    EF_CHECK(!leg::kOldDataDirName.empty());
    EF_CHECK(!leg::kOldServiceName.empty());
    EF_CHECK(!leg::kOldInstallDirName.empty());
    EF_CHECK(!leg::kOldUninstallKeyName.empty());
    EF_CHECK(!leg::kOldShortcutName.empty());
    EF_CHECK(!leg::kOldAutostartValueName.empty());
    EF_CHECK(!leg::kOldPipePrefix.empty());
    EF_CHECK(!leg::kOldSingleInstanceMutexPrefix.empty());
    EF_CHECK(!leg::kOldReleaseRepoPrefix.empty());
    EF_CHECK(leg::kOldServiceName != kServiceName);
    EF_CHECK(leg::kOldInstallDirName != kInstallDirName);
    EF_CHECK(leg::kOldShortcutName != kShortcutName);
    EF_CHECK(leg::kOldExeName != kExeName);
    EF_CHECK(leg::kOldAutostartValueName != ef::core::settings::kAutostartValueName);
    EF_CHECK(leg::kOldReleaseRepoPrefix != ef::core::update::kReleaseRepoPrefix);
    EF_CHECK(leg::kOldProductName != ef::core::product_name());
}
