// T3b (ADR-043 ek): eski yoldan "--service" giris karari, yol kanoniklestirme, ImagePath ayristirma, eski durum
// dosyasinin geri getirme sonrasi bicimi ve temizlik yeniden deneme plani. Saf; Win32 / gercek hizmet yok.
#include "core/legacy/legacy_entry.h"
#include "core/legacy/legacy_install.h"
#include "core/update/staged_update.h"
#include "ef_test.h"

using namespace ef::core::legacy;
namespace setup = ef::core::setup;
namespace upd = ef::core::update;

namespace {

setup::KnownRoots entry_roots() {
    setup::KnownRoots r;
    r.program_files = L"C:\\Program Files";
    r.program_data = L"C:\\ProgramData";
    r.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    r.local_app_data = L"C:\\Users\\oyuncu\\AppData\\Local";
    r.common_desktop = L"C:\\Users\\Public\\Desktop";
    return r;
}

// Gecerli eski hizmet sureci: her denetimden gecer.
LegacyServiceFacts good_facts(const setup::InstallLayout& legacy) {
    LegacyServiceFacts f;
    f.self_raw = legacy.exe.native();
    f.self_final = L"\\\\?\\" + legacy.exe.native();
    f.token_system = true;
    f.scm_connected = true;
    f.self_pid = 4242;
    f.scm_pid = 4242;
    f.scm_image_path = setup::service_image_path(legacy);
    f.image_trusted = true;
    return f;
}

}  // namespace

EF_TEST(legacy_entry_canonical_path_rules) {
    EF_CHECK(canonical_local_path(L"C:\\Program Files\\X\\a.exe") == std::wstring(L"C:\\PROGRAM FILES\\X\\A.EXE"));
    EF_CHECK(canonical_local_path(L"c:/program files/x/A.exe\\") == canonical_local_path(L"C:\\Program Files\\X\\a.exe"));
    EF_CHECK(canonical_local_path(L"\\\\?\\C:\\Program Files\\X\\a.exe") == canonical_local_path(L"C:\\Program Files\\X\\a.exe"));
    EF_CHECK(!canonical_local_path(L""));
    EF_CHECK(!canonical_local_path(L"Program Files\\X\\a.exe"));            // goreli
    EF_CHECK(!canonical_local_path(L"C:Program Files\\a.exe"));              // surucu-goreli
    EF_CHECK(!canonical_local_path(L"\\Program Files\\a.exe"));              // kok-goreli
    EF_CHECK(!canonical_local_path(L"\\\\server\\share\\X\\a.exe"));         // UNC
    EF_CHECK(!canonical_local_path(L"\\\\?\\UNC\\server\\share\\a.exe"));    // genisletilmis UNC
    EF_CHECK(!canonical_local_path(L"\\\\.\\C:\\Program Files\\a.exe"));     // aygit ad alani
    EF_CHECK(!canonical_local_path(L"C:\\Program Files\\X\\..\\X\\a.exe"));  // ".." bileseni
    EF_CHECK(!canonical_local_path(L"C:\\Program Files\\.\\X\\a.exe"));      // "." bileseni
    EF_CHECK(!canonical_local_path(L"C:\\Program Files\\X\\a.exe:stream"));  // ADS
    EF_CHECK(!canonical_local_path(L"C:\\Program Files\\\\X\\a.exe"));       // bos bilesen
    EF_CHECK(same_canonical_path(L"C:\\A\\B", L"c:\\a\\b\\"));
    EF_CHECK(!same_canonical_path(L"C:\\A\\B", L"C:\\A\\BC"));
    EF_CHECK(!same_canonical_path(L"", L""));
}

EF_TEST(legacy_entry_decision_accepts_only_verified_old_service) {
    const auto legacy = legacy_layout(entry_roots());
    const auto cur = setup::make_layout(entry_roots());
    {
        const auto d = decide_legacy_service_entry(good_facts(legacy), legacy);
        EF_CHECK(d.action == LegacyEntry::RunUpgrade);
    }
    {  // buyuk/kucuk harf ve sondaki ayirac farki: ayni yol
        auto f = good_facts(legacy);
        std::wstring lower;
        for (wchar_t c : f.self_raw) lower += (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        f.self_raw = lower;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::RunUpgrade);
        EF_CHECK(legacy_service_candidate(lower, legacy));
    }
    {  // yeni kurulum yolu: eski giris DEGIL (normal yeni hizmet yolu degismez)
        auto f = good_facts(legacy);
        f.self_raw = cur.exe.native();
        f.self_final = cur.exe.native();
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::NotLegacy);
        EF_CHECK(!legacy_service_candidate(cur.exe.native(), legacy));
    }
    {  // goreli ham yol: aday degil
        auto f = good_facts(legacy);
        f.self_raw = L"iTunex Pro\\iTunexPro.exe";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::NotLegacy);
    }
    {  // UNC ham yol: aday degil
        auto f = good_facts(legacy);
        f.self_raw = L"\\\\server\\c$\\Program Files\\iTunex Pro\\iTunexPro.exe";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::NotLegacy);
    }
    {  // baglanti (junction): ham yol eski EXE ama son yol baska yer -> red, hicbir sey yazilmaz
        auto f = good_facts(legacy);
        f.self_final = L"\\\\?\\D:\\elsewhere\\iTunexPro.exe";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // son yol okunamadi
        auto f = good_facts(legacy);
        f.self_final.clear();
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // zincirde yeniden ayristirma noktasi (ayni hedefe giden sembolik bag dahil)
        auto f = good_facts(legacy);
        f.reparse_in_path = true;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
        f.reparse_in_path = false;
        f.path_probe_failed = true;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // SYSTEM degil (kullanici "--service" ile calistirdi)
        auto f = good_facts(legacy);
        f.token_system = false;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // SCM dagiticisina baglanmadi
        auto f = good_facts(legacy);
        f.scm_connected = false;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // SCM'nin eski hizmet PID'i baska surec
        auto f = good_facts(legacy);
        f.scm_pid = 1;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
        f.scm_pid = 0;
        f.self_pid = 0;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // eski hizmet ImagePath'i beklenen degil (ek arguman / baska EXE / tirnaksiz)
        auto f = good_facts(legacy);
        f.scm_image_path += L" --x";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
        f.scm_image_path = L"\"" + cur.exe.native() + L"\" --service";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
        f.scm_image_path = legacy.exe.native() + L" --service";
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::Refuse);
    }
    {  // goruntu guvenilmez (imza): yukseltme YOK, eski EXE geri
        auto f = good_facts(legacy);
        f.image_trusted = false;
        EF_CHECK(decide_legacy_service_entry(f, legacy).action == LegacyEntry::RestoreOldExe);
    }
    {  // bos yerlesim (bilinen klasor okunamadi): asla aday degil
        setup::InstallLayout empty;
        auto f = good_facts(legacy);
        EF_CHECK(decide_legacy_service_entry(f, empty).action == LegacyEntry::NotLegacy);
    }
}

EF_TEST(legacy_entry_image_path_exe) {
    EF_CHECK(image_path_exe(L"\"C:\\Program Files\\X\\a.exe\" --service") == std::wstring(L"C:\\Program Files\\X\\a.exe"));
    EF_CHECK(image_path_exe(L"\"C:\\a.exe\"") == std::wstring(L"C:\\a.exe"));
    EF_CHECK(!image_path_exe(L"C:\\a.exe --service"));
    EF_CHECK(!image_path_exe(L"\"C:\\a.exe"));
    EF_CHECK(!image_path_exe(L"\"\" --service"));
    EF_CHECK(!image_path_exe(L""));
}

EF_TEST(legacy_entry_state_after_restore_keeps_old_schema_and_floor) {
    upd::StagedUpdateState s;
    s.phase = upd::StagePhase::UiHealthy;
    s.version = "0.9.6";
    s.from_version = "0.9.5";
    s.highest_manifest_version = 77;
    s.swapped_at_ms = 123456;
    const auto out = legacy_state_after_restore(upd::encode_state(s));
    EF_CHECK(out.has_value());
    if (out) {
        const auto back = upd::decode_state(*out);  // 0.9.5 ile ayni kodek: eski hizmet okuyabilir
        EF_CHECK(back.ok());
        if (back.ok()) {
            EF_CHECK(back.value().phase == upd::StagePhase::Idle);
            EF_CHECK(back.value().highest_manifest_version == 77);  // tekrar oynatma tabani asla dusmez
            EF_CHECK(back.value().version.empty() && back.value().swapped_at_ms == 0);
        }
        // Alan kumesi 0.9.5 ile ayni (yeni alan yok).
        for (const char* k : {"\"phase\"", "\"version\"", "\"from_version\"", "\"highest_manifest_version\"", "\"swapped_at_ms\""})
            EF_CHECK(out->find(k) != std::string::npos);
    }
    EF_CHECK(!legacy_state_after_restore("not json"));
    EF_CHECK(!legacy_state_after_restore("{\"phase\":\"idle\"}"));
}

EF_TEST(legacy_entry_cleanup_retry_plan) {
    EF_CHECK(legacy_cleanup_retry_ms(UpgradeOutcome::WaitingForHost, 0) == 5000u);
    EF_CHECK(legacy_cleanup_retry_ms(UpgradeOutcome::WaitingForHost, 119) == 5000u);
    EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::WaitingForHost, 120));
    EF_CHECK(legacy_cleanup_retry_ms(UpgradeOutcome::CleanupPending, 0) == 60000u);
    EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::CleanupPending, 30));
    EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::Completed, 0));
    EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::NotApplicable, 0));
    EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::RolledBack, 0));
}
