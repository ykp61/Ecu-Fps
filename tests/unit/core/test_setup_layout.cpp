#include "core/setup/layout.h"
#include "ef_test.h"

using namespace ef::core::setup;
namespace fs = std::filesystem;

namespace {
KnownRoots roots() {
    return {L"C:\\Program Files", L"C:\\ProgramData", L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs",
            L"C:\\Users\\u\\AppData\\Local", L"C:\\Users\\Public\\Desktop"};
}
}  // namespace

EF_TEST(setup_layout_paths) {
    const auto l = make_layout(roots());
    EF_CHECK(l.dir == fs::path(L"C:\\Program Files\\Ecu Fps"));
    EF_CHECK(l.exe == fs::path(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe"));
    EF_CHECK(l.shortcut == fs::path(L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Ecu Fps.lnk"));
    EF_CHECK(l.desktop_shortcut == fs::path(L"C:\\Users\\Public\\Desktop\\Ecu Fps.lnk"));  // tum kullanicilar
    EF_CHECK(l.machine_dir == fs::path(L"C:\\ProgramData\\EcuFps"));
    EF_CHECK(l.updates_dir == fs::path(L"C:\\ProgramData\\EcuFps\\updates"));
    EF_CHECK(l.update_state == fs::path(L"C:\\ProgramData\\EcuFps\\updates\\state.json"));
    EF_CHECK(l.setup_dir == fs::path(L"C:\\ProgramData\\EcuFps\\setup"));
    EF_CHECK(l.setup_journal == fs::path(L"C:\\ProgramData\\EcuFps\\setup\\setup.journal"));
    EF_CHECK(l.user_dir == fs::path(L"C:\\Users\\u\\AppData\\Local\\EcuFps"));
    EF_CHECK(l.user_setup_dir == fs::path(L"C:\\Users\\u\\AppData\\Local\\EcuFps\\setup"));
    const std::string exe = "C:\\Program Files\\Ecu Fps\\EcuFps.exe";
    EF_CHECK(l.swap.exe == exe);
    EF_CHECK(l.swap.staged == exe + ".new");
    EF_CHECK(l.swap.backup == exe + ".old");
    EF_CHECK(l.swap.marker == exe + ".updated");
    EF_CHECK(kServiceName == L"EcuFpsHelper");
    EF_CHECK(kPipeName == L"\\\\.\\pipe\\EcuFps.Helper.v1");
    EF_CHECK(kSetupRoleEnabled);  // gorev 6: kurulum yeri disindan bos acilis kurulum penceresi
}

EF_TEST(setup_layout_empty_roots) {
    const auto none = make_layout(KnownRoots{});
    EF_CHECK(none.dir.empty() && none.exe.empty() && none.shortcut.empty() && none.desktop_shortcut.empty() && none.machine_dir.empty());
    EF_CHECK(none.updates_dir.empty() && none.update_state.empty() && none.setup_dir.empty() && none.setup_journal.empty());
    EF_CHECK(none.user_dir.empty() && none.user_setup_dir.empty() && none.swap.exe.empty() && none.swap.staged.empty());
    EF_CHECK(!is_installed_location(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe", none));
    EF_CHECK(!is_installed_location(L"Ecu Fps\\EcuFps.exe", none));
    EF_CHECK(service_image_path(none).empty() && uninstall_command(none).empty());

    // Yalniz ProgramData eksik: kurulum yolu var, makine yollari bos
    auto r = roots();
    r.program_data.clear();
    const auto partial = make_layout(r);
    EF_CHECK(!partial.exe.empty() && partial.machine_dir.empty() && partial.updates_dir.empty() && partial.setup_journal.empty());
    r = roots();
    r.program_files.clear();
    const auto no_pf = make_layout(r);
    EF_CHECK(no_pf.exe.empty() && !no_pf.machine_dir.empty());
    EF_CHECK(!is_installed_location(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe", no_pf));
}

EF_TEST(setup_layout_same_path) {
    const fs::path a = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe";
    EF_CHECK(same_path(a, a));
    EF_CHECK(same_path(a, L"c:\\program files\\ECU FPS\\ecufps.EXE"));
    EF_CHECK(same_path(a, L"\\\\?\\C:\\Program Files\\Ecu Fps\\EcuFps.exe"));
    EF_CHECK(same_path(a, L"C:/Program Files/Ecu Fps/EcuFps.exe"));
    EF_CHECK(same_path(a, L"C:\\Program Files\\x\\..\\Ecu Fps\\.\\EcuFps.exe"));
    EF_CHECK(same_path(L"C:\\Program Files\\Ecu Fps", L"C:\\Program Files\\Ecu Fps\\"));
    EF_CHECK(same_path(L"\\\\?\\C:\\Program Files\\Ecu Fps\\", L"c:\\program files\\ecu fps"));
    EF_CHECK(!same_path(a, L"D:\\Program Files\\Ecu Fps\\EcuFps.exe"));
    EF_CHECK(!same_path(a, L"C:\\Program Files\\Ecu Fps\\EcuFps.exe.new"));
    EF_CHECK(!same_path(a, L"C:\\Program Files (x86)\\Ecu Fps\\EcuFps.exe"));
    EF_CHECK(!same_path(a, L""));
    EF_CHECK(!same_path(L"", L""));
}

EF_TEST(setup_layout_installed_location) {
    const auto l = make_layout(roots());
    EF_CHECK(is_installed_location(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe", l));
    EF_CHECK(is_installed_location(L"\\\\?\\c:\\PROGRAM FILES\\ecu fps\\ECUFPS.exe", l));
    EF_CHECK(!is_installed_location(L"C:\\Users\\u\\Downloads\\EcuFps-Setup-0.3.0.exe", l));
    EF_CHECK(!is_installed_location(L"C:\\Users\\u\\Desktop\\Ecu Fps\\EcuFps.exe", l));
    EF_CHECK(!is_installed_location(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe.old", l));
    EF_CHECK(!is_installed_location(L"C:\\Program Files\\Ecu Fps\\other.exe", l));
    EF_CHECK(!is_installed_location(L"", l));
}

EF_TEST(setup_layout_command_quoting) {
    const auto l = make_layout(roots());
    EF_CHECK(service_image_path(l) == L"\"C:\\Program Files\\Ecu Fps\\EcuFps.exe\" --service");
    EF_CHECK(uninstall_command(l) == L"\"C:\\Program Files\\Ecu Fps\\EcuFps.exe\" --uninstall");
}
