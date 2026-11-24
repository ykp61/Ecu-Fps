// ADR-034 review: bozulmus/sifirlanmis Authenticode sertifika tablosu "imzasiz" sayilmamali (alpha istisnasi
// yalniz HIC imza yoksa). Urun EXE'si okunur ve gecici kopyasi yamanir; calistirilmaz.
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/security/trust.h"
#include "ef_test.h"
#include "platform/windows/signature.h"

namespace fs = std::filesystem;
using ef::core::security::SignatureStatus;

namespace {
std::vector<char> read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_all(const fs::path& p, const std::vector<char>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}
// Guvenlik veri dizininin dosya ofseti (PE32+ / PE32).
std::size_t security_dir_offset(const std::vector<char>& b) {
    LONG lfanew = 0;
    std::memcpy(&lfanew, b.data() + offsetof(IMAGE_DOS_HEADER, e_lfanew), sizeof(lfanew));
    const std::size_t opt = static_cast<std::size_t>(lfanew) + 4 + sizeof(IMAGE_FILE_HEADER);
    WORD magic = 0;
    std::memcpy(&magic, b.data() + opt, sizeof(magic));
    const std::size_t dirs = opt + (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory)
                                                                            : offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory));
    return dirs + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
}
}  // namespace

EF_TEST(signature_corrupt_certificate_table_is_invalid_not_unsigned) {
    const fs::path exe = EF_PRODUCT_EXE;
    // Gelistirme/dist ikilisi imzasizdir: tablo yok, durum Unsigned.
    EF_CHECK(!ef::platform::pe_certificate_table_present(exe));
    EF_CHECK(ef::platform::verify_file_signature(exe) == SignatureStatus::Unsigned);

    const fs::path dir = fs::temp_directory_path() / ("ef-sigtable-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto bytes = read_all(exe);
    EF_CHECK(bytes.size() > 4096);
    if (bytes.size() <= 4096) return;

    // Sertifika tablosu var ama icerigi sifir (imza silinmis/bozulmus): WinVerifyTrust NOSIGNATURE der.
    const auto off = security_dir_offset(bytes);
    IMAGE_DATA_DIRECTORY sec{static_cast<DWORD>(bytes.size()), 16};
    bytes.resize(bytes.size() + 16, '\0');
    std::memcpy(bytes.data() + off, &sec, sizeof(sec));
    const fs::path patched = dir / "corrupt_table.exe";
    write_all(patched, bytes);
    EF_CHECK(ef::platform::pe_certificate_table_present(patched));
    EF_CHECK(ef::platform::verify_file_signature(patched) == SignatureStatus::Invalid);
    EF_CHECK(!ef::core::security::broker_trust(ef::core::security::TrustPolicy::Release,
                                                 ef::platform::verify_file_signature(patched))
                   .allowed);

    // PE olmayan dosya: tablo yok sayilir (sinif degismez).
    const fs::path text = dir / "plain.txt";
    write_all(text, std::vector<char>{'h', 'i'});
    EF_CHECK(!ef::platform::pe_certificate_table_present(text));
    EF_CHECK(!ef::platform::pe_certificate_table_present(dir / "missing.exe"));
    fs::remove_all(dir, ec);
}
