// B11 (P9 site/hukuk): site butunlugu, durust iddia, gercek ag davranisiyla gizlilik uyumu.
// Yalniz kaynak denetimi; sunucuya yukleme ve ag yok. Ag uc noktalari kod tabanindan OKUNUR, burada uydurulmaz.
#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "core/site_test_util.h"
#include "ef_test.h"

namespace {

using namespace ef::test::b11;
namespace fs = std::filesystem;

constexpr const char* kHost = "ecufast.asilpolat.com";
constexpr const char* kReleaseRepo = "https://github.com/ykp61/Ecu-Fps-releases/";

bool is_external(const std::string& h) {
    return h.rfind("http://", 0) == 0 || h.rfind("https://", 0) == 0 || h.rfind("mailto:", 0) == 0 || h.rfind("//", 0) == 0;
}

// href'i site/ icindeki dosya yoluna cevirir ("" = dis/yalniz parca). `page` site/ gorecedir.
std::string resolve_internal(const std::string& page, std::string href) {
    if (href.empty() || href[0] == '#' || is_external(href)) return {};
    const auto cut = href.find_first_of("#?");
    if (cut != std::string::npos) href.erase(cut);
    fs::path p;
    if (!href.empty() && href[0] == '/') {
        p = fs::path(href.substr(1));
    } else {
        p = (fs::path(page).parent_path() / href).lexically_normal();
    }
    std::string s = p.generic_string();
    if (s.empty() || s.back() == '/') s += "index.html";
    return s;
}

bool site_has(const std::string& rel) { return fs::exists(fs::path(EF_SOURCE_ROOT) / "site" / rel); }

bool has_noindex(const std::string& html) { return html.find("name=\"robots\" content=\"noindex\"") != std::string::npos; }

std::vector<std::string> sitemap_locs() {
    std::vector<std::string> out;
    const auto map = read_repo("site/sitemap.xml");
    std::size_t pos = 0;
    while ((pos = map.find("<loc>", pos)) != std::string::npos) {
        const auto end = map.find("</loc>", pos);
        if (end == std::string::npos) break;
        out.push_back(map.substr(pos + 5, end - pos - 5));
        pos = end;
    }
    return out;
}

// --- Kod tabanindan okunan gercek ag uc noktalari ---------------------------------------------------------

struct Endpoints {
    std::string speed_host;
    std::vector<std::string> update_hosts, dns_hosts, dns_resolvers, icmp_targets;
    std::set<std::string> https_hosts_in_net_sources;
    int speed_max_mib = 0, speed_max_seconds = 0;
};

Endpoints read_endpoints() {
    Endpoints e;
    const auto speed = read_repo("src/core/net/speed_test.h");
    auto a = speed.find("string_view kSpeedHost");
    if (a != std::string::npos) {
        const auto q = quoted_items(speed.substr(a, 120));
        if (!q.empty()) e.speed_host = q[0];
    }
    a = speed.find("uint64_t kSpeedMaxBytes");
    if (a != std::string::npos) {
        // "256ull << 20" -> 256 MiB
        const std::string tail = speed.substr(a, 90);
        const auto eq = tail.find('=');
        if (eq != std::string::npos) {
            const long base = std::strtol(tail.c_str() + eq + 1, nullptr, 10);
            const auto sh = tail.find("<<");
            const long shift = sh == std::string::npos ? 0 : std::strtol(tail.c_str() + sh + 2, nullptr, 10);
            if (shift == 20) e.speed_max_mib = static_cast<int>(base);
        }
    }
    a = speed.find("double kSpeedMaxSeconds");
    if (a != std::string::npos) {
        const auto eq = speed.find('=', a);
        e.speed_max_seconds = static_cast<int>(std::strtol(speed.c_str() + eq + 1, nullptr, 10));
    }
    e.update_hosts = quoted_items(braced_after(read_repo("src/core/update/update_check.h"), "kUpdateHosts"));
    e.dns_hosts = quoted_items(braced_after(read_repo("src/core/net/dns_bench.h"), "kDnsBenchHosts"));
    // Hazir DNS listelerinin birincil (ilk) IPv4 adresi: dns karsilastirmasi yalniz bunlara sorgu atar (readiness_collect.cpp).
    const auto dns = read_repo("src/core/adapters/windows/dns.h");
    std::size_t pos = dns.find("kDnsPresets[]");
    if (pos != std::string::npos) {
        const auto end = dns.find("};", pos);
        for (const auto& item : quoted_items(dns.substr(pos, end - pos))) {
            const auto bar = item.find('|');
            if (bar == std::string::npos) continue;  // "id"/"ad" ogeleri; "v4|v6" ogesi degil
            const auto v4 = item.substr(0, bar);
            e.dns_resolvers.push_back(v4.substr(0, v4.find(',')));
        }
    }
    // ICMP hedefleri: network_reader.cpp default_ping_targets (modem + sabit iki adres).
    const auto nr = read_repo("src/platform/windows/readiness/network_reader.cpp");
    pos = nr.find("default_ping_targets(");
    if (pos != std::string::npos) {
        const auto end = nr.find("\n}", pos);
        const auto body = nr.substr(pos, end - pos);
        std::size_t p = 0;
        while ((p = body.find("push_back({\"", p)) != std::string::npos) {
            p += 12;
            const auto q = body.find('"', p);
            const auto v = body.substr(p, q - p);
            if (!v.empty() && std::isdigit(static_cast<unsigned char>(v[0]))) e.icmp_targets.push_back(v);
            p = q;
        }
    }
    // Ag kodundaki tum https:// ana bilgisayarlari (yalniz ag klasorleri ve sabit basliklari).
    for (const char* dir : {"src/core/net", "src/platform/windows/net", "src/core/update"}) {
        for (const auto& ent : fs::directory_iterator(fs::path(EF_SOURCE_ROOT) / dir)) {
            if (!ent.is_regular_file()) continue;
            const auto rel = fs::relative(ent.path(), EF_SOURCE_ROOT).generic_string();
            const auto src = read_repo(rel);
            std::size_t h = 0;
            while ((h = src.find("https://", h)) != std::string::npos) {
                h += 8;
                const auto end = src.find_first_of("/\" )", h);
                const auto host = src.substr(h, end - h);
                if (host.find('.') != std::string::npos && host.find('{') == std::string::npos) e.https_hosts_in_net_sources.insert(host);
            }
        }
    }
    return e;
}

// Caddyfile'daki site adresi bloklari (girintisiz, '{' ile biten satirlar).
int count_hosts_like(const std::string& caddy) {
    int n = 0;
    std::size_t pos = 0;
    while (pos < caddy.size()) {
        auto end = caddy.find('\n', pos);
        if (end == std::string::npos) end = caddy.size();
        const auto line = caddy.substr(pos, end - pos);
        if (!line.empty() && line[0] != '\t' && line[0] != ' ' && line[0] != '#' && line[0] != '}' && line.find('{') != std::string::npos) ++n;
        pos = end + 1;
    }
    return n;
}

// <code>...</code> icindeki ana bilgisayar / IP benzeri ogeler.
std::set<std::string> code_hosts(const std::string& html) {
    std::set<std::string> out;
    std::size_t pos = 0;
    while ((pos = html.find("<code>", pos)) != std::string::npos) {
        pos += 6;
        const auto end = html.find("</code>", pos);
        if (end == std::string::npos) break;
        const auto v = html.substr(pos, end - pos);
        const bool ok = v.find('.') != std::string::npos && v.find_first_of("%\\ /:") == std::string::npos &&
                        v.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") == std::string::npos;
        if (ok) out.insert(v);
        pos = end;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Baglantilar, sitemap, alan adi
// ---------------------------------------------------------------------------------------------------------

EF_TEST(b11_site_every_internal_link_resolves_to_a_file_and_anchor) {
    const auto files = site_html_files();
    EF_CHECK(files.size() >= 10);
    for (const auto& page : files) {
        const auto html = read_repo("site/" + page);
        for (const char* attr : {"href", "src"}) {
            for (const auto& v : attr_values(html, attr)) {
                if (is_external(v)) continue;
                const auto target = resolve_internal(page, v);
                if (target.empty()) {
                    // yalniz "#parca": ayni sayfada id olmali ("#" tek basina serbest)
                    if (v.size() > 1 && v[0] == '#') EF_CHECK(html.find("id=\"" + v.substr(1) + "\"") != std::string::npos);
                    continue;
                }
                EF_CHECK(site_has(target));
                const auto hash = v.find('#');
                if (hash != std::string::npos && hash + 1 < v.size() && site_has(target) && target.size() > 5 &&
                    target.substr(target.size() - 5) == ".html")
                    EF_CHECK(read_repo("site/" + target).find("id=\"" + v.substr(hash + 1) + "\"") != std::string::npos);
            }
        }
    }
}

EF_TEST(b11_site_external_links_only_to_own_host_or_official_release_repo) {
    for (const auto& page : site_html_files()) {
        const auto html = read_repo("site/" + page);
        for (const auto& v : attr_values(html, "href")) {
            if (!is_external(v)) continue;
            const bool own = v.rfind(std::string("https://") + kHost + "/", 0) == 0;
            const bool repo = v.rfind(kReleaseRepo, 0) == 0;
            EF_CHECK(own || repo);
        }
    }
}

EF_TEST(b11_site_sitemap_lists_exactly_the_indexable_pages) {
    const auto locs = sitemap_locs();
    EF_CHECK(!locs.empty());
    std::set<std::string> uniq(locs.begin(), locs.end());
    EF_CHECK(uniq.size() == locs.size());  // tekrar yok
    const std::string prefix = std::string("https://") + kHost + "/";
    for (const auto& loc : locs) {
        EF_CHECK(loc.rfind(prefix, 0) == 0);  // tek alan adi
        EF_CHECK(site_has(resolve_internal("index.html", "/" + loc.substr(prefix.size()))));
    }
    for (const auto& page : site_html_files()) {
        const auto html = read_repo("site/" + page);
        const std::string url = page == "index.html" ? prefix : prefix + page;
        const bool listed = uniq.count(url) == 1;
        if (has_noindex(html)) {
            EF_CHECK(!listed);  // yonlendirme kalintilari sitemap'te olmaz
        } else {
            EF_CHECK(listed);
            // kanonik adres sayfanin kendi adresi
            EF_CHECK(html.find("<link rel=\"canonical\" href=\"" + url + "\">") != std::string::npos);
        }
    }
}

EF_TEST(b11_site_has_robots_txt_pointing_at_sitemap_and_single_domain) {
    EF_CHECK(site_has("robots.txt"));
    const auto robots = read_repo("site/robots.txt");
    EF_CHECK(robots.find(std::string("Sitemap: https://") + kHost + "/sitemap.xml") != std::string::npos);
    EF_CHECK(robots.find("Disallow: /\n") == std::string::npos);  // tum siteyi kapatma
    // Sunucu yapilandirmasi yalniz kanonik alan adini tanimlar; ikinci alan adi bu depoda hicbir yerde yok.
    const auto caddy = read_repo("site/server/Caddyfile");
    EF_CHECK(caddy.rfind(kHost, 0) == 0);
    EF_CHECK(count_hosts_like(caddy) == 1);
}

// ---------------------------------------------------------------------------------------------------------
// Durust iddia: imzasiz alpha, FPS vaadi yok, rakip adi yok, surum sabit degil
// ---------------------------------------------------------------------------------------------------------

EF_TEST(b11_site_unsigned_alpha_notice_on_entry_and_install_pages) {
    for (const char* p : {"index.html", "rehber/kurulum.html", "guvenlik-seffaflik.html", "eula.html"}) {
        const auto t = claim_text(read_repo(std::string("site/") + p));
        EF_CHECK(t.find("imzalı değil") != std::string::npos);  // "dijital imzalı değil(dir)"
        EF_CHECK(t.find("alpha") != std::string::npos);
    }
    EF_CHECK(claim_text(read_repo("site/index.html")).find("SmartScreen") != std::string::npos || claim_text(read_repo("site/index.html")).find("bilgisayarınız korundu") != std::string::npos);
    // Tum sayfalarin altbilgisi alpha der.
    for (const auto& page : site_html_files()) {
        if (has_noindex(read_repo("site/" + page))) continue;
        EF_CHECK(read_repo("site/" + page).find("alpha") != std::string::npos);
    }
}

EF_TEST(b11_site_no_fps_or_percent_promises_anywhere) {
    const std::regex number_claim("[0-9]+\\s*(FPS|%|kat\\b)", std::regex::icase);
    const std::regex promise(
        "(FPS['’]?(i|yi|ini)?\\s*(artırır|arttırır|yükseltir|kazandırır)(?!\\s*m[ıi]))|"
        "(hızlandırır|hızlanır|hızlandırıyor|hızlandırarak)|"
        "(garantili\\s+(FPS|performans|hız))|(kesin\\s+(FPS|hız|performans)\\s+(artış|kazanç))|"
        "(FPS\\s+(kazan|artar|yükselir))|(FPS\\s+(artışı|kazancı)\\s+(sağlar|sağlanır|garanti\\s+edilir))",
        std::regex::icase);
    for (const auto& page : site_html_files()) {
        const auto t = claim_text(read_repo("site/" + page));
        EF_CHECK(!std::regex_search(t, number_claim));
        EF_CHECK(!std::regex_search(t, promise));
    }
    // Giris sayfasi bunu acikca soyler.
    const auto idx = claim_text(read_repo("site/index.html"));
    EF_CHECK(idx.find("FPS artışı vaat etmiyoruz") != std::string::npos);
    EF_CHECK(idx.find("Fark yok") != std::string::npos);  // ilk gercek olcumun sonucu saklanmaz
}

EF_TEST(b11_site_and_owned_docs_never_name_the_competitor) {
    std::vector<std::string> sources;
    for (const auto& page : site_html_files()) sources.push_back("site/" + page);
    for (const char* extra : {"site/sitemap.xml", "site/robots.txt", "site/server/Caddyfile", "site/server/update_version.py", "README.md",
                              "docs/THIRD_PARTY_NOTICES.md", "docs/13_DEPENDENCIES_LICENSES.md", "docs/18_CALIBRATION_PROTOCOL.md",
                              "scripts/game_round.ps1", "state/branch-notes/p9-site-legal.md", "artifacts/evidence/P9-site.md",
                              "tests/unit/core/test_site_pages.cpp", "tests/unit/core/test_site_b11.cpp",
                              "tests/unit/core/test_third_party_inventory.cpp", "tests/unit/core/test_game_round_protocol.cpp",
                              "tests/unit/core/site_test_util.h"})
        sources.push_back(extra);
    int read = 0;
    for (const auto& rel : sources) {
        if (!repo_exists(rel)) continue;
        ++read;
        const auto text = lower_ascii(read_repo(rel));
        for (const auto& name : forbidden_competitor_names()) EF_CHECK(text.find(name) == std::string::npos);
    }
    EF_CHECK(read >= 15);
}

EF_TEST(b11_site_has_no_hardcoded_release_version) {
    const std::regex ipv4("[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+");
    const std::regex version("(^|[^0-9.])v?[0-9]+\\.[0-9]+\\.[0-9]+([^0-9.]|$)|(^|[^A-Za-z0-9])v[0-9]+\\.[0-9]+([^0-9.]|$)");
    for (const auto& page : site_html_files()) {
        if (page == "ucuncu-taraf-lisanslari.html") continue;  // bilesen surumleri burada bilerek yazili (kilitli bagimliliklar)
        auto t = claim_text(read_repo("site/" + page));
        t = std::regex_replace(t, ipv4, "IP");  // DNS adresleri surum degildir
        std::smatch m;
        EF_CHECK(!std::regex_search(t, m, version));
    }
    const auto idx = read_repo("site/index.html");
    EF_CHECK(idx.find("fetch('v/version.json'") != std::string::npos);  // surum sunucudan gelir
    EF_CHECK(idx.find("Sürüm bilgisi GitHub yayınlarından otomatik alınır") != std::string::npos);
}

EF_TEST(b11_site_entry_page_has_canonical_title_h1_description) {
    const auto idx = read_repo("site/index.html");
    EF_CHECK(idx.find(std::string("<link rel=\"canonical\" href=\"https://") + kHost + "/\">") != std::string::npos);
    EF_CHECK(idx.find("<title>") != std::string::npos);
    EF_CHECK(idx.find("<meta name=\"description\"") != std::string::npos);
    std::size_t n = 0, pos = 0;
    while ((pos = idx.find("<h1", pos)) != std::string::npos) {
        ++n;
        pos += 3;
    }
    EF_CHECK(n == 1);
}

// ---------------------------------------------------------------------------------------------------------
// Hukuk sayfalari: taslak etiketi sahip onayina kadar kalir; gercek davranisi yazar
// ---------------------------------------------------------------------------------------------------------

EF_TEST(b11_legal_pages_stay_labelled_draft_without_lawyer_approval_claims) {
    for (const char* p : {"eula.html", "gizlilik-kvkk.html", "ucuncu-taraf-lisanslari.html"}) {
        const auto html = read_repo(std::string("site/") + p);
        EF_CHECK(html.find("Taslak - hukuki inceleme bekliyor") != std::string::npos);
        EF_CHECK(html.find("class=\"draft\"") != std::string::npos);
        const auto t = lower_ascii(claim_text(html));
        EF_CHECK(t.find("hukukçu onaylı") == std::string::npos);
        EF_CHECK(t.find("hukuk danışmanı onayladı") == std::string::npos);
        EF_CHECK(t.find("hukuken onaylan") == std::string::npos);
    }
}

EF_TEST(b11_privacy_page_lists_exactly_the_real_network_endpoints) {
    const auto e = read_endpoints();
    EF_CHECK(e.speed_host == "speed.cloudflare.com");
    EF_CHECK(e.update_hosts.size() == 3);
    EF_CHECK(e.dns_hosts.size() == 5);
    EF_CHECK(e.dns_resolvers.size() == 4);
    EF_CHECK(e.icmp_targets.size() >= 2);
    EF_CHECK(e.speed_max_mib == 256 && e.speed_max_seconds == 60);

    std::set<std::string> real;
    real.insert(e.speed_host);
    real.insert(e.update_hosts.begin(), e.update_hosts.end());
    real.insert(e.dns_hosts.begin(), e.dns_hosts.end());
    real.insert(e.dns_resolvers.begin(), e.dns_resolvers.end());
    real.insert(e.icmp_targets.begin(), e.icmp_targets.end());
    // Ag klasorlerinde gecen her https ana bilgisayari ya yukaridaki listede ya da kasitli olarak sayfada olmali.
    for (const auto& h : e.https_hosts_in_net_sources) real.insert(h);

    for (const char* page : {"gizlilik-kvkk.html", "guvenlik-seffaflik.html"}) {
        const auto html = read_repo(std::string("site/") + page);
        for (const auto& h : real) {
            // Sorgulanan alan adlari yalniz ayrintili gizlilik metninde tek tek yazilir; ozet sayfa yalniz ag uc noktalarini listeler.
            const bool dns_name = std::find(e.dns_hosts.begin(), e.dns_hosts.end(), h) != e.dns_hosts.end();
            if (dns_name && std::string(page) != "gizlilik-kvkk.html") continue;
            if (html.find(h) == std::string::npos) std::fprintf(stderr, "[b11] %s sayfasinda ag uc noktasi yok: %s\n", page, h.c_str());
            EF_CHECK(html.find(h) != std::string::npos);
        }
        // Sayfadaki her <code> ana bilgisayari/IP'si kodda gercekten var (uydurma uc nokta yok).
        for (const auto& h : code_hosts(html)) EF_CHECK(real.count(h) == 1);
    }
}

EF_TEST(b11_privacy_page_states_real_behaviour_not_marketing) {
    const auto e = read_endpoints();
    const auto kvkk = claim_text(read_repo("site/gizlilik-kvkk.html"));
    // Hiz testi: kullanici onayi, veri tavani, sure tavani, sifir bayt yukleme, IP gorunurlugu.
    EF_CHECK(kvkk.find("Evet / Hayır") != std::string::npos);
    EF_CHECK(kvkk.find(std::to_string(e.speed_max_mib) + " MB") != std::string::npos);
    EF_CHECK(kvkk.find(std::to_string(e.speed_max_seconds) + " saniye") != std::string::npos);
    EF_CHECK(kvkk.find("IP adresi") != std::string::npos);
    EF_CHECK(kvkk.find("sıfır") != std::string::npos || kvkk.find("anlamsız") != std::string::npos);  // yukleme verisi icerik tasimaz
    // Guncelleme: acilista bir kez, elle denetle, zorunlu, imzali bildirim, kullanici verisi yok.
    EF_CHECK(kvkk.find("bir kez") != std::string::npos);
    EF_CHECK(kvkk.find("Denetle") != std::string::npos);
    EF_CHECK(kvkk.find("zorunlu") != std::string::npos);
    EF_CHECK(kvkk.find("imza") != std::string::npos);
    EF_CHECK(kvkk.find("Kullanıcı adı") != std::string::npos || kvkk.find("kimlik") != std::string::npos);
    // DNS karsilastirmasi: sorgulanan alan adlari tek tek yazili.
    for (const auto& h : e.dns_hosts) EF_CHECK(kvkk.find(h) != std::string::npos);
    // Site sunucusu: erisim gunlugu tanimli degil; yapilandirma bunu dogrular.
    EF_CHECK(read_repo("site/server/Caddyfile").find("log ") == std::string::npos && read_repo("site/server/Caddyfile").find("log{") == std::string::npos);
    EF_CHECK(kvkk.find("erişim günlüğü") != std::string::npos);
    // Cerez/izleme yok iddiasi: sitede <script src> ve cerez yazan kod yok.
    for (const auto& page : site_html_files()) {
        const auto html = read_repo("site/" + page);
        EF_CHECK(html.find("<script src") == std::string::npos);
        EF_CHECK(html.find("document.cookie") == std::string::npos);
        EF_CHECK(html.find("googletagmanager") == std::string::npos && html.find("analytics") == std::string::npos);
    }
}

EF_TEST(b11_eula_describes_real_install_update_and_autostart_behaviour) {
    const auto t = claim_text(read_repo("site/eula.html"));
    for (const char* needle : {"yardımcı hizmet", "zorunlu", "Windows ile başlat", "Evet / Hayır", "UAC", "geri yükle", "imzalı değil",
                               "alpha", "tepsi"})
        EF_CHECK(t.find(needle) != std::string::npos);
    // "Hicbir degisiklik sorulmadan yapilmaz" mutlak iddiasi, bilinen tek istisnayi (ADR-030) anmadan yazilmamali.
    if (t.find("sorulmadan yapılmaz") != std::string::npos) EF_CHECK(t.find("istisna") != std::string::npos);
}

EF_TEST(b11_third_party_page_lists_every_embedded_component) {
    const auto t = claim_text(read_repo("site/ucuncu-taraf-lisanslari.html"));
    for (const char* needle : {"NVAPI", "PresentMon", "JetBrains Mono", "SIL Open Font License", "MSVC", "MIT"})
        EF_CHECK(t.find(needle) != std::string::npos);
}
