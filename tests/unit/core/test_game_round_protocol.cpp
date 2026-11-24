// B11: gercek oyun ABBA turu protokolu (docs/18 bolum 7) ve scripts/game_round.ps1 sinirlari.
// Betik AYAR YAZMAZ: Performans Acik/Kapali gecisini sahip urunun Evet/Hayir akisiyla yapar. Bu test kaynagi tarar;
// betigi calistirmaz (calistirma kaniti artifacts/evidence/P9-site.md icinde).
#include <regex>
#include <string>
#include <vector>

#include "core/site_test_util.h"
#include "ef_test.h"

namespace {

using namespace ef::test::b11;

std::string abba_section() {
    const auto doc = read_repo("docs/18_CALIBRATION_PROTOCOL.md");
    const auto a = doc.find("## 7. Gerçek oyun ABBA turu");
    return a == std::string::npos ? std::string() : doc.substr(a);
}

}  // namespace

EF_TEST(b11_abba_protocol_section_defines_design_gates_and_stop_rules) {
    const auto s = abba_section();
    EF_CHECK(!s.empty());
    for (const char* needle : {
             "ABBA", "BAAB",                                  // karsilikli dengeli sira
             "en az 3 blok",                                  // >= 6 + 6 kosu
             "Performans Kapalı", "Performans Açık",          // A ve B kollari
             "Evet / Hayır",                                  // urunun kendi onay akisi
             "çevrimdışı", "korumasız", "tek oyunculu",       // yalniz guvenli oyun
             "EasyAntiCheat", "BEService", "vgc",             // koruma servisi varsa tur durur
             "UAC'ye yalnız sen basarsın",                    // ajan/betik UAC tiklamaz
             "yeniden başlatma",                              // reboot isteyen gruplar ABBA'ya girmez
             "ısınma",                                        // ilk kosu atilir
             "güven aralığı",                                 // %95 GA
             "Fark yok",                                      // sifiri iceren GA = fark yok
             "tek bilgisayar",                                // genelleme sinirlari
             "oyun belleği",                                  // yasak: bellek okuma/enjeksiyon
             "enjeksiyon",
             "FPS vaadi"})
        EF_CHECK(s.find(needle) != std::string::npos);
}

EF_TEST(b11_abba_result_template_has_every_field_needed_to_reproduce_the_round) {
    const auto s = abba_section();
    EF_CHECK(s.find("### 7.5 Sonuç şablonu") != std::string::npos);
    for (const char* field : {"commit", "build_id", "Ecu Fps sürümü", "İşletim sistemi", "Ekran kartı ve sürücü", "Güç kaynağı",
                              "Oyun adı", "Oyun sürümü", "Oyun korumasız mı", "Sahne", "Çözünürlük", "FPS sınırı", "VSync",
                              "Performans grupları", "Blok", "Kol", "Ortalama FPS", "%1 düşük", "Geçerli mi", "Blok farkı",
                              "%95 güven aralığı", "Hüküm", "Atlanan", "Kapsam sınırı"})
        EF_CHECK(s.find(field) != std::string::npos);
    // Sablon bos birakilir: doldurulmus sayi iceren ornek sonuc yok.
    EF_CHECK(s.find("ÖRNEK SONUÇ") == std::string::npos);
}

EF_TEST(b11_game_round_script_never_writes_settings_or_touches_game) {
    const auto raw = read_repo("scripts/game_round.ps1");
    EF_CHECK(!raw.empty());
    // Yorum satirlari ve tirnakli aciklamalar hariç komut taramasi: satir bazinda, "#" ile baslayanlar atlanir.
    std::string code;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        auto end = raw.find('\n', pos);
        if (end == std::string::npos) end = raw.size();
        auto line = raw.substr(pos, end - pos);
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#') code += lower_ascii(line) + "\n";
        pos = end + 1;
    }
    for (const char* verb : {"set-itemproperty", "new-itemproperty", "remove-itemproperty", "set-item ", "new-item -path hk", "reg add", "reg delete",
                             "reg import", "powercfg /set", "powercfg -set", "set-service", "stop-service", "start-service", "restart-service",
                             "sc.exe config", "sc config", "netsh ", "bcdedit", "set-mppreference", "add-mppreference", "set-netfirewall",
                             "set-dnsclient", "disable-", "enable-", "-verb runas", "start-process", "stop-process", "taskkill", "readprocessmemory",
                             "writeprocessmemory", "openprocess", "virtualallocex", "createremotethread", "add-type", "invoke-webrequest",
                             "invoke-restmethod", "downloadstring", "downloadfile", "set-executionpolicy"})
        EF_CHECK(code.find(verb) == std::string::npos);
    // Yalniz kanit klasorune yazar.
    EF_CHECK(raw.find("artifacts\\evidence\\game-round-") != std::string::npos);
    // Koruma geri cekilmeleri korunur.
    EF_CHECK(raw.find("$protectedExe") != std::string::npos);
    EF_CHECK(raw.find("EasyAntiCheat") != std::string::npos);
    EF_CHECK(raw.find("Read-Host") != std::string::npos);
    EF_CHECK(raw.find("--emit-header") != std::string::npos);  // yalniz "asla kullanilmaz" notu olarak
}

EF_TEST(b11_game_round_script_supports_plan_round_and_analyze_modes) {
    const auto raw = read_repo("scripts/game_round.ps1");
    for (const char* needle : {"[switch]$Plan", "$Round", "$Analyze", "'ABBA'", "'BAAB'", "Get-TQuantile95", "Evet / Hayir", "Fark yok", "veri yetersiz"})
        EF_CHECK(raw.find(needle) != std::string::npos);
    // Betik ayar yazmadigini kendisi de soyler.
    EF_CHECK(raw.find("AYAR YAZMAZ") != std::string::npos);
}

EF_TEST(b11_game_round_docs_stay_consistent_with_acceptance_round_rules) {
    const auto d19 = read_repo("docs/19_GAME_ACCEPTANCE_ROUND.md");
    const auto d18 = abba_section();
    // docs/19'daki degismez kurallar ABBA bolumunde de var: ayni koruma listesi ve UAC kurali.
    EF_CHECK(d19.find("EasyAntiCheat") != std::string::npos && d18.find("EasyAntiCheat") != std::string::npos);
    EF_CHECK(d18.find("docs/19_GAME_ACCEPTANCE_ROUND.md") != std::string::npos);
    EF_CHECK(d18.find("scripts\\game_round.ps1") != std::string::npos || d18.find("scripts/game_round.ps1") != std::string::npos);
}
