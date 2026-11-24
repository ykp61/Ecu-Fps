// B11: ucuncu taraf bildirimleri ve bagimlilik kaydi, GERCEK bagimliliklarla uyumlu mu?
// Gercek = third_party/ dizinleri + CMake'in dis kaynak cekmesi + urun EXE'sinin PE import tablosu.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/site_test_util.h"
#include "ef_test.h"

namespace {

using namespace ef::test::b11;
namespace fs = std::filesystem;

std::string squash(std::string s) {
    s = lower_ascii(s);
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o += c;
    return o;
}

// third_party/<dizin> ve third_party/fonts/<dizin> adlari.
std::vector<std::string> vendored_components() {
    std::vector<std::string> out;
    const fs::path tp = fs::path(EF_SOURCE_ROOT) / "third_party";
    for (const auto& e : fs::directory_iterator(tp)) {
        if (!e.is_directory()) continue;
        const auto name = e.path().filename().string();
        if (name == "fonts") {
            for (const auto& f : fs::directory_iterator(e.path()))
                if (f.is_directory()) out.push_back(f.path().filename().string());
        } else {
            out.push_back(name);
        }
    }
    return out;
}

std::string first_match_after(const std::string& s, const std::string& key, std::size_t len) {
    const auto a = s.find(key);
    if (a == std::string::npos) return {};
    std::size_t b = a + key.size();
    while (b < s.size() && (s[b] == ' ' || s[b] == ':' || s[b] == '`' || s[b] == '\t')) ++b;
    return s.substr(b, len);
}

// --- Minimal PE import okuyucu (urun EXE'si; calistirilmaz) ---------------------------------------------

std::vector<std::string> pe_imports(const std::string& path) {
    std::vector<std::string> out;
    std::string data;
    {
        std::FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return out;
        char buf[1 << 16];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
        std::fclose(f);
    }
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) return out;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    const auto nt_off = static_cast<std::size_t>(dos->e_lfanew);
    if (nt_off + sizeof(IMAGE_NT_HEADERS64) > data.size()) return out;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data.data() + nt_off);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return out;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    const unsigned nsec = nt->FileHeader.NumberOfSections;
    auto rva_to_off = [&](DWORD rva) -> std::size_t {
        for (unsigned i = 0; i < nsec; ++i) {
            const DWORD va = sec[i].VirtualAddress;
            const DWORD span = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            if (rva >= va && rva < va + span) return static_cast<std::size_t>(rva - va) + sec[i].PointerToRawData;
        }
        return 0;
    };
    auto cstr_at = [&](DWORD rva) -> std::string {
        const auto off = rva_to_off(rva);
        if (!off || off >= data.size()) return {};
        const auto end = data.find('\0', off);
        return data.substr(off, end == std::string::npos ? std::string::npos : end - off);
    };
    // Normal import
    const auto& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp.VirtualAddress) {
        auto off = rva_to_off(imp.VirtualAddress);
        while (off && off + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= data.size()) {
            const auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(data.data() + off);
            if (!d->Name) break;
            out.push_back(cstr_at(d->Name));
            off += sizeof(IMAGE_IMPORT_DESCRIPTOR);
        }
    }
    // Gecikmeli yukleme
    const auto& dly = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (dly.VirtualAddress) {
        auto off = rva_to_off(dly.VirtualAddress);
        while (off && off + sizeof(IMAGE_DELAYLOAD_DESCRIPTOR) <= data.size()) {
            const auto* d = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(data.data() + off);
            if (!d->DllNameRVA) break;
            out.push_back(cstr_at(d->DllNameRVA));
            off += sizeof(IMAGE_DELAYLOAD_DESCRIPTOR);
        }
    }
    return out;
}

}  // namespace

EF_TEST(b11_every_vendored_component_is_in_all_notice_documents) {
    const auto comps = vendored_components();
    EF_CHECK(comps.size() >= 3);
    const std::vector<std::pair<const char*, std::string>> docs = {
        {"docs/THIRD_PARTY_NOTICES.md", squash(read_repo("docs/THIRD_PARTY_NOTICES.md"))},
        {"third_party/NOTICES", squash(read_repo("third_party/NOTICES"))},
        {"site/ucuncu-taraf-lisanslari.html", squash(claim_text(read_repo("site/ucuncu-taraf-lisanslari.html")))},
        {"docs/13_DEPENDENCIES_LICENSES.md", squash(read_repo("docs/13_DEPENDENCIES_LICENSES.md"))}};
    for (const auto& c : comps) {
        for (const auto& [name, text] : docs) {
            const bool ok = text.find(squash(c)) != std::string::npos;
            if (!ok) std::fprintf(stderr, "[b11] '%s' bilesen '%s' icin kayit yok\n", name, c.c_str());
            EF_CHECK(ok);
        }
    }
}

EF_TEST(b11_pinned_versions_in_notices_match_vendored_sources) {
    const auto nvapi_src = read_repo("third_party/nvapi/SOURCE.txt");
    const auto nvapi_commit = first_match_after(nvapi_src, "Commit:", 40);
    EF_CHECK(nvapi_commit.size() == 40);
    const auto pm_commit = first_match_after(read_repo("third_party/presentmon/VERSION.md"), "| Commit |", 40);
    EF_CHECK(pm_commit.size() == 40);
    const auto pm_tag = first_match_after(read_repo("third_party/presentmon/VERSION.md"), "| Etiket |", 6);  // `v2.6.0`
    EF_CHECK(pm_tag.size() == 6);
    const auto font_src = read_repo("third_party/fonts/jetbrains-mono/SOURCE.txt");
    EF_CHECK(font_src.rfind("JetBrains Mono 2.304", 0) == 0);

    for (const char* doc : {"docs/THIRD_PARTY_NOTICES.md", "third_party/NOTICES", "docs/13_DEPENDENCIES_LICENSES.md"}) {
        const auto t = read_repo(doc);
        EF_CHECK(t.find(nvapi_commit) != std::string::npos);
        EF_CHECK(t.find(pm_commit) != std::string::npos);
        EF_CHECK(t.find("2.304") != std::string::npos);
    }
    const auto page = read_repo("site/ucuncu-taraf-lisanslari.html");
    EF_CHECK(page.find(nvapi_commit) != std::string::npos);
    EF_CHECK(page.find(pm_commit) != std::string::npos);
    EF_CHECK(page.find("2.304") != std::string::npos);
}

EF_TEST(b11_license_texts_are_vendored_and_reproduced_in_notice_file) {
    for (const char* lic : {"third_party/nvapi/License.txt", "third_party/presentmon/LICENSE.txt", "third_party/fonts/jetbrains-mono/OFL.txt"})
        EF_CHECK(read_repo(lic).size() > 500);
    const auto notices = collapse_ws(read_repo("third_party/NOTICES"));
    EF_CHECK(notices.find("Permission is hereby granted, free of charge") != std::string::npos);  // MIT (NVAPI, PresentMon)
    EF_CHECK(notices.find("SIL OPEN FONT LICENSE Version 1.1") != std::string::npos);              // OFL (JetBrains Mono)
    // Telif satirlari lisans dosyalarindan birebir.
    const auto ofl = collapse_ws(read_repo("third_party/fonts/jetbrains-mono/OFL.txt"));
    const auto copy = ofl.find("Copyright");
    EF_CHECK(copy != std::string::npos);
    EF_CHECK(notices.find(ofl.substr(copy, 60)) != std::string::npos);
    const auto pm = collapse_ws(read_repo("third_party/presentmon/LICENSE.txt"));
    const auto pmc = pm.find("Copyright");
    EF_CHECK(pmc != std::string::npos);
    EF_CHECK(notices.find(pm.substr(pmc, 40)) != std::string::npos);
}

EF_TEST(b11_build_pulls_no_undeclared_external_dependency) {
    namespace fs2 = std::filesystem;
    int scanned = 0;
    for (fs2::recursive_directory_iterator it(EF_SOURCE_ROOT), end; it != end; ++it) {
        const auto& e = *it;
        const auto rel = fs2::relative(e.path(), EF_SOURCE_ROOT).generic_string();
        if (e.is_directory()) {
            // Uretilmis/ucuncu taraf agaclari ve gecici calisma alanlari taranmaz (kendi CMake'leri ayri denetlenir).
            if (rel.rfind("build", 0) == 0 || rel == ".git" || rel == ".claude" || rel == "artifacts" || rel == "dist" || rel == "node_modules")
                it.disable_recursion_pending();
            continue;
        }
        if (!e.is_regular_file()) continue;
        const auto name = e.path().filename().string();
        if (name != "CMakeLists.txt" && e.path().extension() != ".cmake") continue;
        ++scanned;
        // Yorum satirlari (ornegin "vcpkg yoktur" notu) bagimlilik sayilmaz.
        std::string t;
        {
            std::istringstream in(read_repo(rel));
            std::string line;
            while (std::getline(in, line)) {
                const auto first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line[first] != '#') t += line + "\n";
            }
        }
        EF_CHECK(t.find("FetchContent") == std::string::npos);
        EF_CHECK(t.find("ExternalProject") == std::string::npos);
        EF_CHECK(t.find("vcpkg") == std::string::npos);
        EF_CHECK(t.find("CPMAddPackage") == std::string::npos);
        std::size_t p = 0;
        while ((p = t.find("find_package(", p)) != std::string::npos) {
            p += 13;
            const auto pkg = t.substr(p, t.find_first_of(" )", p) - p);
            EF_CHECK(pkg == "Git" || pkg == "Python3");  // yalniz derleme araci; calisma zamani bagimliligi degil
        }
    }
    EF_CHECK(scanned >= 5);
    EF_CHECK(!repo_exists("vcpkg.json"));
    EF_CHECK(!repo_exists("conanfile.txt"));
}

EF_TEST(b11_product_exe_imports_only_system_dlls_and_all_are_recorded) {
    const auto imports = pe_imports(EF_PRODUCT_EXE);
    EF_CHECK(imports.size() >= 10);
    const auto docs13 = lower_ascii(read_repo("docs/13_DEPENDENCIES_LICENSES.md"));
    char sysdir[MAX_PATH]{};
    const UINT n = GetSystemDirectoryA(sysdir, MAX_PATH);
    EF_CHECK(n > 0 && n < MAX_PATH);
    std::set<std::string> seen;
    for (const auto& dll : imports) {
        const auto l = lower_ascii(dll);
        if (!seen.insert(l).second) continue;
        EF_CHECK(l.rfind("vcruntime", 0) != 0 && l.rfind("msvcp", 0) != 0 && l.rfind("ucrtbase", 0) != 0 && l.rfind("api-ms-win-crt", 0) != 0);
        EF_CHECK(fs::exists(fs::path(sysdir) / dll));  // yalniz System32'de bulunan DLL (windows.package ile ayni kural)
        const bool recorded = docs13.find(l) != std::string::npos;
        if (!recorded) std::fprintf(stderr, "[b11] import '%s' docs/13 icinde kayitli degil\n", dll.c_str());
        EF_CHECK(recorded);
    }
}

EF_TEST(b11_dependency_records_state_unsigned_alpha_and_unused_vendor_sdks) {
    const auto d13 = read_repo("docs/13_DEPENDENCIES_LICENSES.md");
    EF_CHECK(d13.find("imzasız alpha") != std::string::npos);
    const auto notices = read_repo("docs/THIRD_PARTY_NOTICES.md");
    EF_CHECK(notices.find("AMD ADLX") != std::string::npos && notices.find("kabul edilmedi") != std::string::npos);
    EF_CHECK(notices.find("Intel IGCL") != std::string::npos);
    // Kullanilmayan SDK basliklari gercekten depoda yok.
    EF_CHECK(!repo_exists("third_party/adlx") && !repo_exists("third_party/igcl"));
    // Ag kutuphanesi yok: yalniz WinHTTP (docs/13 bunu soyler).
    EF_CHECK(d13.find("Üçüncü taraf ağ kütüphanesi yoktur") != std::string::npos);
}

// Koordinator bulgusu (B8): AMD ADLX lisansi hicbir kayitta "MIT" diye yazilamaz; ADR-039 ve docs/13 ile ayni seyi soyler.
namespace {
bool has_word(const std::string& l, const std::string& w) {
    for (std::size_t p = l.find(w); p != std::string::npos; p = l.find(w, p + 1)) {
        const bool lo = p == 0 || !std::isalpha(static_cast<unsigned char>(l[p - 1]));
        const bool hi = p + w.size() >= l.size() || !std::isalpha(static_cast<unsigned char>(l[p + w.size()]));
        if (lo && hi) return true;
    }
    return false;
}
}  // namespace

EF_TEST(b11_adlx_license_is_never_claimed_as_mit_and_records_agree) {
    for (const char* doc : {"third_party/DEPENDENCIES.md", "docs/13_DEPENDENCIES_LICENSES.md", "docs/THIRD_PARTY_NOTICES.md", "third_party/NOTICES"}) {
        std::istringstream in(read_repo(doc));
        std::string line;
        int adlx_lines = 0;
        while (std::getline(in, line)) {
            const auto l = lower_ascii(line);
            if (l.find("adlx") == std::string::npos) continue;
            ++adlx_lines;
            const bool negated = l.find("mit degil") != std::string::npos || l.find("mit değil") != std::string::npos || l.find("not mit") != std::string::npos;
            const bool says_mit = has_word(l, "mit") && !negated;
            if (says_mit) std::fprintf(stderr, "[b11] %s ADLX satiri MIT iddia ediyor: %s\n", doc, line.c_str());
            EF_CHECK(!says_mit);
        }
        EF_CHECK(adlx_lines > 0);
    }
    const auto adr = read_repo("docs/12_DECISIONS.md");
    EF_CHECK(adr.find("ADR-039") != std::string::npos && adr.find("ADLX SDK License Agreement") != std::string::npos);
    const auto deps = lower_ascii(read_repo("third_party/DEPENDENCIES.md"));
    // Tam metin 2026-11-24 okundu (gizlilik, tazminat, "yalniz AMD" amaci); kayit bunu belirtmeli ve B-006 acik kalmali.
    EF_CHECK(deps.find("b-006") != std::string::npos && deps.find("tam metin 2026-11-24 okundu") != std::string::npos);
    EF_CHECK(!repo_exists("third_party/adlx"));
}
