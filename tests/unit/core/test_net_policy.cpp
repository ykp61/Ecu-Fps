// Ag politikasi (spec 2026-11-11 §7, ADR-027): urun kaynaginda ag kodu dar bir kapida durur.
//  - WinHTTP cagrilari yalniz kNetFolder altindaki dosyalarda (speedtest_http.cpp, update_http.cpp).
//  - Ag hostlari tek bir sabit kumesindedir (kNetHosts, tam liste 4 host); her host yalniz kendi tanim dosyasinda
//    yazilir. Zorunlu guncelleme hostlari ve adresleri yalniz src/core/update/update_check.h'de (ADR-028).
//  - Hiz testi yollari yalniz /__down ve /__up; kurtarma/broker kaynaklari ag koduna girmez.
//  - ADR-031: Windows Update surucu aramasi yalniz src/platform/windows/wua/ (baglantiyi WU hizmeti kurar; host listesi
//    degismez); WUA indirme / kurulum arabirimleri yalniz yardimcinin klasorunde (src/platform/windows/wua_install/).
// Kaynak taramasidir (Win32 cagrisi yok); tests/windows/check_no_install.ps1 ayni klasor kuralini uygular.
#include <algorithm>
#include <cstdio>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/net/speed_test.h"
#include "core/update/update_check.h"
#include "ef_test.h"

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kNetFolder = "src/platform/windows/net/";

struct NetHost {
    std::string_view host;
    std::string_view defined_in;  // hostun yazilabildigi tek dosya (depo kokune gore)
};
// Tek host kumesi: hiz testi + zorunlu guncelleme (spec 2026-11-11-zorunlu-guncelleme-design.md §5).
constexpr std::string_view kUpdateDefs = "src/core/update/update_check.h";
constexpr std::array<NetHost, 4> kNetHosts{{{"speed.cloudflare.com", "src/core/net/speed_test.h"},
                                             {"github.com", kUpdateDefs},
                                             {"objects.githubusercontent.com", kUpdateDefs},
                                             {"release-assets.githubusercontent.com", kUpdateDefs}}};

struct SrcFile {
    std::string rel;  // "src/..." (ileri egik cizgi)
    std::string text;
};

std::vector<SrcFile> source_files() {
    std::vector<SrcFile> out;
    const fs::path root = fs::path(EF_SOURCE_ROOT);
    for (const auto& e : fs::recursive_directory_iterator(root / "src")) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension().string();
        if (ext != ".h" && ext != ".cpp" && ext != ".rc" && ext != ".manifest" && ext != ".in") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        auto rel = fs::relative(e.path(), root).generic_string();
        out.push_back({std::move(rel), ss.str()});
    }
    return out;
}

bool in_net_folder(const std::string& rel) { return rel.rfind(std::string(kNetFolder), 0) == 0; }

bool contains(const std::string& s, std::string_view needle) { return s.find(needle) != std::string::npos; }

}  // namespace

EF_TEST(net_policy_single_host_single_file) {
    const auto files = source_files();
    EF_CHECK(files.size() > 50);  // tarama gercekten kaynak agacini gordu

    // Hiz testi istemcisi mevcut ve ag klasorunde.
    const auto speed = std::find_if(files.begin(), files.end(),
                                    [](const SrcFile& f) { return f.rel == "src/platform/windows/net/speedtest_http.cpp"; });
    EF_CHECK(speed != files.end());
    if (speed != files.end()) {
        EF_CHECK(contains(speed->text, "WinHttpOpen"));
        EF_CHECK(contains(speed->text, "WINHTTP_FLAG_SECURE"));
        EF_CHECK(contains(speed->text, "WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY"));
        EF_CHECK(contains(speed->text, "kSpeedHost"));
        EF_CHECK(contains(speed->text, "kDownPath"));
        EF_CHECK(contains(speed->text, "kUpPath"));
    }

    // Guncelleme istemcisi mevcut ve ag klasorunde; her yonlendirme adimi host izin listesinden gecer.
    const auto upd = std::find_if(files.begin(), files.end(),
                                  [](const SrcFile& f) { return f.rel == "src/platform/windows/net/update_http.cpp"; });
    EF_CHECK(upd != files.end());
    if (upd != files.end()) {
        EF_CHECK(contains(upd->text, "WinHttpOpen"));
        EF_CHECK(contains(upd->text, "WINHTTP_FLAG_SECURE"));
        EF_CHECK(contains(upd->text, "WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY"));
        EF_CHECK(contains(upd->text, "WINHTTP_OPTION_REDIRECT_POLICY_NEVER"));
        EF_CHECK(contains(upd->text, "update_host_allowed"));
        EF_CHECK(!contains(upd->text, "kSpeedHost"));
    }

    for (const auto& f : files) {
        // 1) WinHTTP yalniz ag klasorunde.
        // check_no_install.ps1 ile ayni: buyuk/kucuk harf duyarsiz "winhttp" (yorumlar dahil).
        std::string lower = f.text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool winhttp = contains(lower, "winhttp");
        if (winhttp && !in_net_folder(f.rel)) {
            ef::test::report_failure("WinHTTP ag klasoru disinda", __FILE__, __LINE__);
            std::fprintf(stderr, "  winhttp outside net folder: %s\n", f.rel.c_str());
        }
        // 2) Her host yalniz tanim dosyasinda; *.cloudflare.com adlari da yalniz orada.
        for (const auto& h : kNetHosts) {
            // legacy_names.h: eski yayin adresi yalniz sabit metin (goc gorevi icin); baglanti kodu degil.
            if (f.rel == "src/core/legacy/legacy_names.h") continue;
            if (contains(f.text, h.host) && f.rel != h.defined_in) {
                ef::test::report_failure("ag hostu tanim dosyasi disinda", __FILE__, __LINE__);
                std::fprintf(stderr, "  host %.*s in %s\n", static_cast<int>(h.host.size()), h.host.data(), f.rel.c_str());
            }
        }
        if (contains(f.text, "githubusercontent.com") && f.rel != kUpdateDefs) {
            ef::test::report_failure("githubusercontent.com update_check.h disinda", __FILE__, __LINE__);
            std::fprintf(stderr, "  githubusercontent.com in %s\n", f.rel.c_str());
        }
        if (contains(f.text, ".cloudflare.com") && f.rel != "src/core/net/speed_test.h") {
            ef::test::report_failure(".cloudflare.com speed_test.h disinda", __FILE__, __LINE__);
            std::fprintf(stderr, "  .cloudflare.com in %s\n", f.rel.c_str());
        }
        // 3) Hiz testi yollari yalniz sabitlerde.
        if ((contains(f.text, "\"/__down") || contains(f.text, "\"/__up")) && f.rel != "src/core/net/speed_test.h") {
            ef::test::report_failure("hiz testi yolu speed_test.h disinda", __FILE__, __LINE__);
            std::fprintf(stderr, "  speed path literal in %s\n", f.rel.c_str());
        }
        // 4) Ag klasoru adres/host metni yazmaz; hostu ve yolu core sabitlerinden alir.
        if (in_net_folder(f.rel)) {
            EF_CHECK(!contains(f.text, "http://"));
            EF_CHECK(!contains(f.text, "https://"));
            EF_CHECK(!contains(f.text, ".com\""));
            EF_CHECK(!contains(f.text, ".com\")"));
        }
        // 5) Kurtarma ve broker rolleri ag koduna hic girmez.
        const bool privileged = f.rel.rfind("src/core/recovery/", 0) == 0 || f.rel.rfind("src/core/broker/", 0) == 0 ||
                                contains(f.rel, "recovery_host.") || contains(f.rel, "broker_channel.") || f.rel == "src/app/main.cpp";
        if (privileged) {
            for (const char* bad : {"speedtest", "SpeedTestRunner", "WinHttp", "core/net/", "platform/windows/net/"}) {
                if (contains(f.text, bad)) {
                    ef::test::report_failure("kurtarma/broker kaynagi ag koduna dokunuyor", __FILE__, __LINE__);
                    std::fprintf(stderr, "  %s uses %s\n", f.rel.c_str(), bad);
                }
            }
        }
    }
}

// ADR-031 / spec 2026-11-12 H: Guncellemeler surucu aramasi. Baglantiyi Windows Update hizmeti kurar; uygulamanin kendi
// ag listesi (kNetHosts) degismez. Arama cagrilari yalniz arama klasorunde; WUA indirme / kurulum arabirimleri yalniz
// yukseltilmis yardimcinin tek klasorunde (check_no_install.ps1 ayni kurali uygular). Arama klasoru ag kodu, adres ve
// kurulum cagrisi icermez; kurtarma / broker rolleri aramaya dokunmaz.
EF_TEST(net_policy_windows_update_search_folder) {
    constexpr std::string_view kWuaSearch = "src/platform/windows/wua/";
    constexpr std::string_view kWuaInstall = "src/platform/windows/wua_install/";
    const auto files = source_files();
    const auto search = std::find_if(files.begin(), files.end(), [](const SrcFile& f) { return f.rel == "src/platform/windows/wua/wua_search.cpp"; });
    EF_CHECK(search != files.end());
    if (search != files.end()) {
        EF_CHECK(contains(search->text, "BeginSearch"));
        EF_CHECK(contains(search->text, "kWuaDriverCriteria"));  // olcut core sabitinden (IsInstalled=0 and Type='Driver' ...)
        EF_CHECK(contains(search->text, "RequestAbort"));        // iptal / zaman asimi
    }
    for (const auto& f : files) {
        const bool in_search = f.rel.rfind(std::string(kWuaSearch), 0) == 0;
        const bool in_install = f.rel.rfind(std::string(kWuaInstall), 0) == 0;
        for (const char* api : {"IUpdateSearcher", "BeginSearch", "CreateUpdateSearcher"}) {
            if (contains(f.text, api) && !in_search && !in_install) {
                ef::test::report_failure("WUA aramasi arama klasoru disinda", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s in %s\n", api, f.rel.c_str());
            }
        }
        for (const char* api : {"IUpdateInstaller", "IUpdateDownloader", "CreateUpdateInstaller", "CreateUpdateDownloader"}) {
            if (contains(f.text, api) && !in_install) {
                ef::test::report_failure("WUA indirme/kurulum yardimci klasoru disinda", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s in %s\n", api, f.rel.c_str());
            }
        }
        // Spec H2: yardimcinin kurulum klasoru hizmet secimini Windows Update'e sabitler, ogeyi cekirdek olcutuyle yeniden
        // arar, lisansi kullanici adina kabul etmez ve kendi ag kodu / adresi yoktur.
        if (in_install && f.rel.size() > 4 && f.rel.substr(f.rel.size() - 4) == ".cpp") {
            EF_CHECK(contains(f.text, "ssWindowsUpdate"));
            EF_CHECK(contains(f.text, "install_criteria"));
            EF_CHECK(!contains(f.text, "AcceptEula"));
            std::string lower = f.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            EF_CHECK(!contains(lower, "winhttp"));
            EF_CHECK(!contains(f.text, "http://") && !contains(f.text, "https://"));
        }
        if (in_search) {
            std::string lower = f.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            EF_CHECK(!contains(lower, "winhttp"));
            EF_CHECK(!contains(f.text, "http://"));
            EF_CHECK(!contains(f.text, "https://"));
        }
        const bool privileged = f.rel.rfind("src/core/recovery/", 0) == 0 || f.rel.rfind("src/core/broker/", 0) == 0 ||
                                contains(f.rel, "recovery_host.") || contains(f.rel, "broker_channel.");
        if (privileged && (contains(f.text, "platform/windows/wua/") || contains(f.text, "DriverScanner"))) {
            ef::test::report_failure("kurtarma/broker kaynagi surucu aramasina dokunuyor", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s\n", f.rel.c_str());
        }
    }
}

// Host sabiti tek ve yol sabitleri yalniz iki yol.
EF_TEST(net_policy_constants) {
    EF_CHECK(kNetHosts[0].host == ef::core::net::kSpeedHost);
    // Guncelleme host listesi politika kumesiyle birebir ayni (eksik/fazla host yok).
    EF_CHECK(ef::core::update::kUpdateHosts.size() + 1 == kNetHosts.size());
    for (std::size_t i = 0; i < ef::core::update::kUpdateHosts.size(); ++i) EF_CHECK(ef::core::update::kUpdateHosts[i] == kNetHosts[i + 1].host);
    EF_CHECK(ef::core::net::kDownPath == "/__down");
    EF_CHECK(ef::core::net::kUpPath == "/__up");
}

// ---- B7: ag uc noktasi izin listesi ------------------------------------------------------------------------------------
// Kaynak taramasi: uygulamanin bagli oldugu / actigi her adres bu listede olmali. Listede olmayan yeni bir host, IP, ag API'si
// veya http:// adresi testi kirar; yeni uc nokta eklemek icin once bu liste, gizlilik sayfasi (site/gizlilik-kvkk.html) ve
// kurulum gizlilik metni birlikte guncellenir (test_site_b11.cpp sayfa ile kodu ayrica karsilastirir).
namespace {

// Kullanici bir dugmeye tiklayinca VARSAYILAN TARAYICIDA acilan sayfalar: uygulamanin kendisi bunlara baglanmaz (WinHTTP yok),
// yalniz adresi tarayiciya verir. Ag klasorunde (src/platform/windows/net/) hic yazilamaz.
constexpr std::array<std::string_view, 12> kBrowserLinkHosts{
    "ecufast.asilpolat.com",                                                                 // guvenlik/gizlilik/EULA sayfalari (site_links.h)
    "www.nvidia.com",       "www.amd.com",         "www.intel.com",                         // uretici surucu sayfalari
    "www.msi.com",          "www.gigabyte.com",    "www.asus.com",         "www.asrock.com",  // anakart destek sayfalari
    "www.microsoft.com",    "learn.microsoft.com",                                          // DirectX 9 / VC++ yeniden dagitilabilir sayfalari
    "www.waveform.com",                                                                     // bufferbloat testi (tarayicida)
    "prosettings.net"};                                                                     // oyun ayar rehberi (tarayicida)

// Yalniz XML ad alani tanimlayicilari (baglanti degil): app.manifest.in icinde.
constexpr std::string_view kXmlNamespaceHost = "schemas.microsoft.com";
constexpr std::string_view kManifestFile = "src/app/app.manifest.in";

// DNS karsilastirmasi ve ag testinin hedef IPv4 adresleri (yalniz kullanici baslatinca; kullanici verisi tasimaz) + DNS ayar
// secenekleri (core/adapters/windows/dns.h: Evet/Hayir ile sistem DNS'i yazilir; uygulama bu adreslere kendisi baglanmaz).
constexpr std::array<std::string_view, 8> kResolverIps{"1.1.1.1",  "1.0.0.1",         "8.8.8.8",      "8.8.4.4",
                                                      "9.9.9.9",  "149.112.112.112", "94.140.14.14", "94.140.15.15"};
// IPv4 benzeri ama adres olmayanlar: yorumlardaki ornekler (varsayilan gecit, WUA baslik surum numaralari) ve surum numaralari
// (manifest ortak denetim bileseni 6.0.0.0, MSIX aile 1.0.0.0, oyun kart basligi ornegi 1.40.4.8).
constexpr std::array<std::string_view, 6> kNonEndpointDotted{"192.168.0.1", "10.1.1.44", "552.0.0.0", "6.0.0.0", "1.0.0.0", "1.40.4.8"};

bool is_digit_c(char c) { return c >= '0' && c <= '9'; }

// Kimligin basinda eslesir ("WinHttpSendRequest" icindeki "HttpSendRequest" sayilmaz).
bool contains_word_start(const std::string& s, std::string_view token) {
    for (std::size_t p = s.find(token); p != std::string::npos; p = s.find(token, p + 1))
        if (p == 0 || !(std::isalnum(static_cast<unsigned char>(s[p - 1])) || s[p - 1] == '_')) return true;
    return false;
}

bool url_host_end(char c) {
    switch (c) {
        case '/': case ':': case '?': case '#': case '"': case '\'': case ' ': case ')': case '\\': case '<': case '>': case '\n': case '\r':
        case ',': case ';':
            return true;
        default:
            return false;
    }
}

// "http(s)://host" adreslerinin {sema, host} ciftleri. Bos host (yalniz "https://" on eki) atlanir.
std::vector<std::pair<std::string, std::string>> url_hosts(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string_view scheme : {std::string_view("https://"), std::string_view("http://")}) {
        for (std::size_t p = text.find(scheme); p != std::string::npos; p = text.find(scheme, p + 1)) {
            if (scheme == "http://" && p > 0 && text[p - 1] == 's') continue;  // "https://" icindeki alt dize degil
            std::size_t e = p + scheme.size();
            while (e < text.size() && !url_host_end(text[e])) ++e;
            std::string host = text.substr(p + scheme.size(), e - p - scheme.size());
            if (host.empty()) continue;
            out.emplace_back(std::string(scheme.substr(0, scheme.size() - 3)), std::move(host));
        }
    }
    return out;
}

// d.d.d.d (her bayt 1-3 hane). Daha uzun noktali dizinin (surum numarasi) parcasi sayilmaz.
std::vector<std::string> dotted_quads(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (!is_digit_c(s[i]) || (i > 0 && (is_digit_c(s[i - 1]) || s[i - 1] == '.'))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        bool ok = true;
        for (int part = 0; part < 4 && ok; ++part) {
            const std::size_t d = j;
            while (j < s.size() && is_digit_c(s[j])) ++j;
            if (j == d || j - d > 3) ok = false;
            else if (part < 3) {
                if (j < s.size() && s[j] == '.') ++j;
                else ok = false;
            }
        }
        // Dorduncu bayttan sonra '.'+rakam geliyorsa daha uzun bir surum dizisidir.
        if (ok && j + 1 < s.size() && s[j] == '.' && is_digit_c(s[j + 1])) ok = false;
        if (ok) out.push_back(s.substr(i, j - i));
        while (j < s.size() && (is_digit_c(s[j]) || s[j] == '.')) ++j;
        i = std::max(j, i + 1);
    }
    return out;
}

bool host_in_net_set(const std::string& host) {
    for (const auto& h : kNetHosts)
        if (host == h.host) return true;
    return false;
}

bool host_in_browser_set(const std::string& host) {
    for (const auto h : kBrowserLinkHosts)
        if (host == h) return true;
    return false;
}

}  // namespace

EF_TEST(net_policy_every_url_literal_host_is_on_the_allowlist) {
    const auto files = source_files();
    std::size_t https_seen = 0;
    for (const auto& f : files) {
        for (const auto& [scheme, host] : url_hosts(f.text)) {
            if (scheme == "http") {
                // Sifresiz adres yalniz XML ad alanlarinda (manifest); baska hicbir yerde.
                if (host != kXmlNamespaceHost || f.rel != kManifestFile) {
                    ef::test::report_failure("http:// (sifresiz) adres izin listesinde degil", __FILE__, __LINE__);
                    std::fprintf(stderr, "  http://%s in %s\n", host.c_str(), f.rel.c_str());
                }
                continue;
            }
            ++https_seen;
            std::string h = host;
            std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (host_in_net_set(h)) continue;  // uygulamanin baglandigi hostlar: tanim dosyasi kurali ilk testte
            if (host_in_browser_set(h)) {
                // Tarayicida acilan sayfa adresi ag klasorune girmez (orada adres yazilamaz).
                if (in_net_folder(f.rel)) {
                    ef::test::report_failure("tarayici baglantisi ag klasorunde", __FILE__, __LINE__);
                    std::fprintf(stderr, "  %s in %s\n", h.c_str(), f.rel.c_str());
                }
                continue;
            }
            ef::test::report_failure("https adresi izin listesinde degil", __FILE__, __LINE__);
            std::fprintf(stderr, "  https://%s in %s\n", h.c_str(), f.rel.c_str());
        }
    }
    EF_CHECK(https_seen >= 10);  // tarama gercekten adresleri gordu
    // Kendi kendini denetle: izin listesi disi bir host bu tarayicida yakalanir.
    const auto probe = url_hosts("auto u = \"https://evil.example.org/x\"; auto v = \"http://plain.example.org\";");
    EF_CHECK(probe.size() == 2 && probe[0].second == "evil.example.org" && !host_in_net_set(probe[0].second) && !host_in_browser_set(probe[0].second));
    EF_CHECK(probe.size() == 2 && probe[1].first == "http");
    // Iki liste ayri kumelerdir.
    for (const auto h : kBrowserLinkHosts) EF_CHECK(!host_in_net_set(std::string(h)));
}

// Ag API'leri: her biri yalniz kendi dosyasinda. Listede olmayan yeni bir ag API'si (soket, WinInet, URLMon, BITS indirme...)
// testi kirar; once buraya, gizlilik sayfasina ve ADR'ye eklenir.
EF_TEST(net_policy_network_apis_only_in_their_single_file) {
    struct Api {
        std::string_view token;
        std::vector<std::string_view> allowed;  // izinli dosyalar (tam yol); bos = hicbir yerde
    };
    const std::vector<Api> apis{
        {"DnsQueryEx", {"src/platform/windows/net/dns_query.cpp", "src/platform/windows/net/dns_query.h"}},
        {"DnsQuery_", {}},
        {"DnsQuery(", {}},
        {"IcmpCreateFile", {"src/platform/windows/readiness/network_reader.cpp", "src/platform/windows/readiness/network_reader.h"}},
        {"IcmpSendEcho", {"src/platform/windows/readiness/network_reader.cpp", "src/platform/windows/readiness/network_reader.h"}},
        {"Icmp6", {}},
        // BITS yalniz is listesini OKUR (guncelleme durumu); indirme isi olusturulmaz.
        {"IBackgroundCopyManager", {"src/platform/windows/inventory/update_status.cpp"}},
        {"WSAStartup", {}},
        {"getaddrinfo", {}},
        {"GetAddrInfo", {}},
        {"gethostbyname", {}},
        {"InternetOpen", {}},
        {"InternetConnect", {}},
        {"HttpSendRequest", {}},
        {"URLDownloadTo", {}},
        {"URLOpenStream", {}},
        {"WinHttpWebSocket", {}},
        {"curl_easy", {}},
        {"libcurl", {}},
        {"CreateUrlCacheEntry", {}},
        {"WebSocket", {}},
    };
    const auto files = source_files();
    for (const auto& f : files) {
        for (const auto& api : apis) {
            if (!contains_word_start(f.text, api.token)) continue;
            const bool allowed = std::find(api.allowed.begin(), api.allowed.end(), std::string_view(f.rel)) != api.allowed.end();
            if (!allowed) {
                ef::test::report_failure("ag API'si izin listesi disinda", __FILE__, __LINE__);
                std::fprintf(stderr, "  %.*s in %s\n", static_cast<int>(api.token.size()), api.token.data(), f.rel.c_str());
            }
        }
        // Ham soket: "socket(" cagrisi (kimlik siniri ile); hicbir yerde izinli degil.
        for (std::size_t p = f.text.find("socket("); p != std::string::npos; p = f.text.find("socket(", p + 1)) {
            const bool word_start = p == 0 || !(std::isalnum(static_cast<unsigned char>(f.text[p - 1])) || f.text[p - 1] == '_');
            if (word_start) {
                ef::test::report_failure("ham soket cagrisi", __FILE__, __LINE__);
                std::fprintf(stderr, "  socket( in %s\n", f.rel.c_str());
            }
        }
    }
    // BITS dosyasi gercekten okuma kipinde (is olusturan / baslatan cagrilar yok).
    const auto bits = std::find_if(files.begin(), files.end(), [](const SrcFile& f) { return f.rel == "src/platform/windows/inventory/update_status.cpp"; });
    EF_CHECK(bits != files.end());
    if (bits != files.end()) {
        EF_CHECK(contains(bits->text, "EnumJobs"));
        for (const char* write : {"CreateJob", "AddFile", "->Resume(", "->Complete("}) EF_CHECK(!contains(bits->text, write));
    }
}

// IPv4 adresleri: yalniz bilinen DNS cozucu / ping hedefleri; yeni bir sabit adres testi kirar.
EF_TEST(net_policy_ipv4_literals_are_known_resolvers_only) {
    const auto files = source_files();
    std::size_t seen = 0;
    for (const auto& f : files) {
        for (const auto& ip : dotted_quads(f.text)) {
            ++seen;
            const bool known = std::find(kResolverIps.begin(), kResolverIps.end(), ip) != kResolverIps.end() ||
                               std::find(kNonEndpointDotted.begin(), kNonEndpointDotted.end(), ip) != kNonEndpointDotted.end();
            if (!known) {
                ef::test::report_failure("IPv4 adresi izin listesinde degil", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s in %s\n", ip.c_str(), f.rel.c_str());
            }
        }
    }
    EF_CHECK(seen >= 10);
    const auto probe = dotted_quads("a 203.0.113.9 b 1.2.3.4.5 c 7.7.7.7;");
    EF_CHECK(probe.size() == 2 && probe[0] == "203.0.113.9" && probe[1] == "7.7.7.7");  // 1.2.3.4.5 surum dizisidir
    // DNS karsilastirmasi alan adlari kumesi sabit (5 alan adi); uygulama baska alan adi sormaz.
    const auto dns_h = std::find_if(files.begin(), files.end(), [](const SrcFile& f) { return f.rel == "src/core/net/dns_bench.h"; });
    EF_CHECK(dns_h != files.end());
    if (dns_h != files.end()) {
        const auto pos = dns_h->text.find("kDnsBenchHosts[]");
        EF_CHECK(pos != std::string::npos);
        const auto end = dns_h->text.find("};", pos);
        const std::string list = dns_h->text.substr(pos, end - pos);
        std::size_t quotes = 0;
        for (char c : list) quotes += c == '"';
        EF_CHECK(quotes == 10);  // tam 5 tirnakli alan adi
        for (const char* host : {"www.microsoft.com", "www.wikipedia.org", "store.steampowered.com", "www.youtube.com", "www.amazon.com"})
            EF_CHECK(contains(list, host));
    }
}
