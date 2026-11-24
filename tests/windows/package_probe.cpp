// REL-02/REL-03: portable paket denetimi icin PE import tablosu okuyucu.
// Salt okuma: dosyayi degistirmez, yalniz x64 PE basligini ve import adlarini yazar.
// Cikti: "machine=x64" ve her DLL icin "import=<ad>" satiri.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

bool InFile(std::uint64_t offset, std::uint64_t need, std::uint64_t size) {
    return offset <= size && need <= size - offset;
}

// RVA -> dosya ofseti (bolum tablosu uzerinden; RVA == dosya ofseti varsayilmaz).
const unsigned char* RvaToPtr(const unsigned char* base, std::uint64_t file_size,
                              const IMAGE_NT_HEADERS64* nt, DWORD rva) {
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD start = section->VirtualAddress;
        const DWORD virt = section->Misc.VirtualSize;
        const DWORD raw = section->SizeOfRawData;
        const DWORD span = virt > raw ? virt : raw;
        if (rva < start) continue;
        const DWORD delta = rva - start;
        if (delta >= span) continue;
        const std::uint64_t offset = static_cast<std::uint64_t>(section->PointerToRawData) + delta;
        if (!InFile(offset, 1, file_size)) return nullptr;
        return base + offset;
    }
    return nullptr;
}

bool ReadImportName(const unsigned char* base, std::uint64_t file_size,
                    const IMAGE_NT_HEADERS64* nt, DWORD rva, std::string& out) {
    const unsigned char* p = RvaToPtr(base, file_size, nt, rva);
    if (!p) return false;
    const std::uint64_t offset = static_cast<std::uint64_t>(p - base);
    for (std::uint64_t i = 0; i < 256; ++i) {
        if (!InFile(offset + i, 1, file_size)) return false;
        const char c = static_cast<char>(base[offset + i]);
        if (c == '\0') return !out.empty();
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7E) return false;
        out.push_back(c);
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: ef_package_probe <exe>\n");
        return 2;
    }

    HANDLE file = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "open failed (error %lu)\n", GetLastError());
        return 2;
    }

    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || length.QuadPart > (1LL << 31)) {
        std::fprintf(stderr, "invalid file size\n");
        CloseHandle(file);
        return 2;
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(length.QuadPart);

    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        std::fprintf(stderr, "map failed (error %lu)\n", GetLastError());
        CloseHandle(file);
        return 2;
    }
    const auto* base = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (base == nullptr) {
        std::fprintf(stderr, "map view failed (error %lu)\n", GetLastError());
        CloseHandle(mapping);
        CloseHandle(file);
        return 2;
    }

    bool ok = false;
    std::vector<std::string> imports;
    do {
        if (!InFile(0, sizeof(IMAGE_DOS_HEADER), file_size)) break;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) break;
        const std::uint64_t nt_offset = static_cast<std::uint64_t>(dos->e_lfanew);
        if (!InFile(nt_offset, sizeof(IMAGE_NT_HEADERS64), file_size)) break;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE) break;
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) break;
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) break;

        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (dir.VirtualAddress == 0) {
            ok = true;
            break;
        }
        if (dir.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) break;

        bool terminated = false;
        for (DWORD i = 0; i < 512; ++i) {
            const std::uint64_t entry_rva = static_cast<std::uint64_t>(dir.VirtualAddress) +
                                            static_cast<std::uint64_t>(i) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
            if (entry_rva > 0xFFFFFFFFull) break;
            const unsigned char* desc = RvaToPtr(base, file_size, nt, static_cast<DWORD>(entry_rva));
            if (desc == nullptr) break;
            const auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(desc);
            if (imp->Name == 0) {
                terminated = true;
                break;
            }
            std::string name;
            if (!ReadImportName(base, file_size, nt, imp->Name, name)) break;
            imports.push_back(std::move(name));
        }
        ok = terminated;
    } while (false);

    if (ok) {
        std::printf("machine=x64\n");
        for (const auto& name : imports) std::printf("import=%s\n", name.c_str());
    } else {
        std::fprintf(stderr, "not a valid x64 PE or malformed import table\n");
    }

    UnmapViewOfFile(base);
    CloseHandle(mapping);
    CloseHandle(file);
    return ok ? 0 : 3;
}
