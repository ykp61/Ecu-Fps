// T19: site/ yasal, guvenlik ve rehber sayfalari (statik HTML). Dagitim bu testin disindadir; yalniz kaynak denetimi.
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "ef_test.h"

namespace {

std::string read_site(const std::string& rel) {
    std::ifstream f(std::string(EF_SOURCE_ROOT) + "/site/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::size_t count_of(const std::string& s, const std::string& needle) {
    std::size_t n = 0, pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// Sayisal iddia denetimi yalniz okunan metne bakar: <style>/<script> bloklari (CSS olculeri vb.) atlanir.
std::string strip_blocks(std::string s) {
    for (const char* tag : {"style", "script"}) {
        const std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
        std::size_t a;
        while ((a = s.find(open)) != std::string::npos) {
            const auto b = s.find(close, a);
            s.erase(a, b == std::string::npos ? std::string::npos : b + close.size() - a);
        }
    }
    return s;
}

const std::vector<std::string> kPages = {"eula.html",
                                         "gizlilik-kvkk.html",
                                         "guvenlik-seffaflik.html",
                                         "ucuncu-taraf-lisanslari.html",
                                         "rehber/kurulum.html",
                                         "rehber/cs2-akicilik-rehberi.html",
                                         "rehber/valorant-akicilik-rehberi.html",
                                         "rehber/fps-dusmesi.html"};
const std::vector<std::string> kLegal = {"eula.html", "gizlilik-kvkk.html"};

}  // namespace

EF_TEST(t19_site_pages_have_h1_title_description_canonical) {
    for (const auto& p : kPages) {
        const auto s = read_site(p);
        EF_CHECK(!s.empty());
        EF_CHECK(count_of(s, "<h1>") == 1);
        std::smatch m;
        EF_CHECK(std::regex_search(s, m, std::regex("<title>([^<]+)</title>")) && !m[1].str().empty());
        EF_CHECK(std::regex_search(s, m, std::regex("<meta name=\"description\" content=\"([^\"]+)\"")) && m[1].str().size() > 40);
        EF_CHECK(s.find("<link rel=\"canonical\" href=\"https://ecufast.asilpolat.com/" + p + "\"") != std::string::npos);
        EF_CHECK(s.find("<script") == std::string::npos);        // dis/yerel betik yok
        EF_CHECK(s.find("http-equiv=\"refresh\"") == std::string::npos);
    }
}

EF_TEST(t19_old_fps_guide_urls_are_redirect_stubs) {
    for (const char* k : {"cs2", "valorant"}) {
        const auto s = read_site(std::string("rehber/") + k + "-fps-arttirma.html");
        const std::string target = std::string("/rehber/") + k + "-akicilik-rehberi.html";
        EF_CHECK(s.find("http-equiv=\"refresh\"") != std::string::npos);
        EF_CHECK(s.find("url=" + target) != std::string::npos);
        EF_CHECK(s.find("<link rel=\"canonical\" href=\"https://ecufast.asilpolat.com" + target + "\"") != std::string::npos);
    }
}

EF_TEST(t19_site_titles_unique) {
    std::vector<std::string> titles;
    for (const auto& p : kPages) {
        std::smatch m;
        const auto s = read_site(p);
        EF_CHECK(std::regex_search(s, m, std::regex("<title>([^<]+)</title>")));
        for (const auto& t : titles) EF_CHECK(t != m[1].str());
        titles.push_back(m[1].str());
    }
}

EF_TEST(t19_site_guides_have_faq_and_no_unmeasured_claims) {
    const std::regex claim("[0-9]+\\s*(FPS|%)", std::regex::icase);
    for (const auto& p : kPages) {
        if (p.rfind("rehber/", 0) != 0) continue;
        const auto s = read_site(p);
        EF_CHECK(s.find("<details>") != std::string::npos);  // SSS bolumu
        EF_CHECK(count_of(s, "href=\"/") >= 3);              // ic baglantilar
        EF_CHECK(!std::regex_search(strip_blocks(s), claim));
    }
}

EF_TEST(t19_site_legal_pages_are_marked_draft_with_placeholder) {
    for (const auto& p : kLegal) {
        const auto s = read_site(p);
        EF_CHECK(s.find("Taslak - hukuki inceleme bekliyor") != std::string::npos);
        EF_CHECK(s.find("class=\"ph\"") != std::string::npos);  // iletisim yer tutucusu belirgin
    }
}

EF_TEST(t19_site_vanguard_guide_keeps_security_features_on) {
    const auto s = read_site("rehber/valorant-akicilik-rehberi.html");
    EF_CHECK(s.find("Secure Boot ve TPM açık kalmalı") != std::string::npos);
}

EF_TEST(t19_site_transparency_lists_same_network_destinations_as_setup_privacy) {
    const auto page = read_site("guvenlik-seffaflik.html");
    const auto kvkk = read_site("gizlilik-kvkk.html");
    for (const char* h : {"GitHub", "speed.cloudflare.com", "1.1.1.1", "8.8.8.8", "9.9.9.9", "94.140.14.14", "Windows Update"}) {
        EF_CHECK(page.find(h) != std::string::npos);
        EF_CHECK(kvkk.find(h) != std::string::npos);
    }
    EF_CHECK(page.find("resmon") != std::string::npos);
    for (const char* lang : {"tr-TR", "en-US"}) {
        std::ifstream f(std::string(EF_SOURCE_ROOT) + "/design/strings/" + lang + ".json", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const auto strings = ss.str();
        const auto at = strings.find("\"setup.privacy\"");
        EF_CHECK(at != std::string::npos);
        if (at == std::string::npos) continue;
        const auto line = strings.substr(at, strings.find('\n', at) - at);
        for (const char* h : {"GitHub", "speed.cloudflare.com", "1.1.1.1", "8.8.8.8", "9.9.9.9", "94.140.14.14"})
            EF_CHECK(line.find(h) != std::string::npos);
    }
}

EF_TEST(t19_index_links_every_page_and_sitemap_lists_them) {
    const auto idx = read_site("index.html");
    const auto map = read_site("sitemap.xml");
    EF_CHECK(!map.empty());
    for (const auto& p : kPages) {
        EF_CHECK(idx.find("href=\"" + p + "\"") != std::string::npos);
        EF_CHECK(map.find("<loc>https://ecufast.asilpolat.com/" + p + "</loc>") != std::string::npos);
    }
    // Surum betigi (update_version.py) index.html'e dokunmaz: yalniz version.json yazar.
    const auto py = read_site("server/update_version.py");
    EF_CHECK(py.find("index.html") == std::string::npos);
}
