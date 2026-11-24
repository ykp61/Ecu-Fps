// Guvenlik regresyonu (setup DLL ekme): urun EXE'si kullanici yazilabilir klasorden (Indirilenler, setup indirme
// klasoru) UAC ile yukseltilir. Statik importlar yalniz System32'den cozulmeli: PE load config DependentLoadFlags
// LOAD_LIBRARY_SEARCH_SYSTEM32 (0x800) icermeli. EXE calistirilmaz; yalniz dosya baytlari okunur.
#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "ef_test.h"

#ifdef EF_PRODUCT_EXE
// Onkosul basarisizsa testi burada bitir (sonraki okumalar anlamsiz olur).
#define EF_REQUIRE(expr) \
    do {                  \
        EF_CHECK(expr);  \
        if (!(expr)) return; \
    } while (0)

namespace {

std::string read_bytes(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

template <class T>
bool at(const std::string& b, std::size_t off, T& out) {
    if (off > b.size() || b.size() - off < sizeof(T)) return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}

// RVA -> dosya ofseti (bolum tablosu ile).
bool rva_to_off(const std::string& b, std::size_t sections, unsigned count, DWORD rva, std::size_t& off) {
    for (unsigned i = 0; i < count; ++i) {
        IMAGE_SECTION_HEADER s{};
        if (!at(b, sections + i * sizeof(IMAGE_SECTION_HEADER), s)) return false;
        const DWORD span = s.Misc.VirtualSize > s.SizeOfRawData ? s.Misc.VirtualSize : s.SizeOfRawData;
        if (rva >= s.VirtualAddress && rva < s.VirtualAddress + span) {
            off = static_cast<std::size_t>(s.PointerToRawData) + (rva - s.VirtualAddress);
            return true;
        }
    }
    return false;
}

}  // namespace

EF_TEST(product_exe_static_imports_resolve_from_system32_only) {
    const auto b = read_bytes(EF_PRODUCT_EXE);
    IMAGE_DOS_HEADER dos{};
    EF_REQUIRE(at(b, 0, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE);
    IMAGE_NT_HEADERS64 nt{};
    EF_REQUIRE(at(b, static_cast<std::size_t>(dos.e_lfanew), nt) && nt.Signature == IMAGE_NT_SIGNATURE);
    EF_REQUIRE(nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    EF_REQUIRE(dir.VirtualAddress != 0);
    const std::size_t sections = static_cast<std::size_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                                 nt.FileHeader.SizeOfOptionalHeader;
    std::size_t off = 0;
    EF_REQUIRE(rva_to_off(b, sections, nt.FileHeader.NumberOfSections, dir.VirtualAddress, off));
    IMAGE_LOAD_CONFIG_DIRECTORY64 lc{};
    DWORD lc_size = 0;
    EF_REQUIRE(at(b, off, lc_size));
    // Yapi SDK ile buyur; dosyadaki boyut kadar (en fazla sizeof) kopyalanir.
    const std::size_t n = lc_size < sizeof(lc) ? lc_size : sizeof(lc);
    EF_REQUIRE(off <= b.size() && b.size() - off >= n);
    std::memcpy(&lc, b.data() + off, n);
    EF_REQUIRE(lc.Size >= offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, DependentLoadFlags) + sizeof(lc.DependentLoadFlags));
    EF_CHECK((lc.DependentLoadFlags & LOAD_LIBRARY_SEARCH_SYSTEM32) == LOAD_LIBRARY_SEARCH_SYSTEM32);
    // Uygulama klasoru acikca istenmemeli.
    EF_CHECK((lc.DependentLoadFlags & (LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)) == 0);
}
#endif
