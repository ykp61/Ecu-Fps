#include "platform/windows/signature.h"

#include <windows.h>
#include <softpub.h>
#include <wintrust.h>

#include <cstddef>
#include <fstream>

namespace ef::platform {

using core::security::SignatureStatus;

bool pe_certificate_table_present(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    auto read_at = [&in](std::streamoff off, void* dst, std::streamsize n) {
        in.clear();
        in.seekg(off);
        return static_cast<bool>(in.read(static_cast<char*>(dst), n));
    };
    IMAGE_DOS_HEADER dos{};
    if (!read_at(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) return false;
    DWORD sig = 0;
    WORD magic = 0;
    if (!read_at(dos.e_lfanew, &sig, sizeof(sig)) || sig != IMAGE_NT_SIGNATURE) return false;
    const std::streamoff opt = static_cast<std::streamoff>(dos.e_lfanew) + 4 + sizeof(IMAGE_FILE_HEADER);
    if (!read_at(opt, &magic, sizeof(magic))) return false;
    std::streamoff count_off = 0, dir_off = 0;
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        count_off = opt + offsetof(IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes);
        dir_off = opt + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        count_off = opt + offsetof(IMAGE_OPTIONAL_HEADER32, NumberOfRvaAndSizes);
        dir_off = opt + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    } else {
        return false;
    }
    DWORD count = 0;
    if (!read_at(count_off, &count, sizeof(count)) || count <= IMAGE_DIRECTORY_ENTRY_SECURITY) return false;
    IMAGE_DATA_DIRECTORY sec{};
    if (!read_at(dir_off + IMAGE_DIRECTORY_ENTRY_SECURITY * static_cast<std::streamoff>(sizeof(IMAGE_DATA_DIRECTORY)), &sec,
                 sizeof(sec)))
        return false;
    return sec.VirtualAddress != 0 || sec.Size != 0;
}

SignatureStatus verify_file_signature(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;  // ag yok
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    switch (r) {
        case ERROR_SUCCESS: return SignatureStatus::Valid;
        case TRUST_E_NOSIGNATURE:
        case TRUST_E_SUBJECT_FORM_UNKNOWN:
        case TRUST_E_PROVIDER_UNKNOWN:
            // ADR-034: "imza yok" yalniz sertifika tablosu da yoksa. WinVerifyTrust bozulmus/sifirlanmis bir
            // sertifika tablosunu da NOSIGNATURE der; bu kurcalanmis imzadir (release alpha istisnasi gecmemeli).
            return pe_certificate_table_present(file) ? SignatureStatus::Invalid : SignatureStatus::Unsigned;
        case TRUST_E_BAD_DIGEST:
        case CRYPT_E_SECURITY_SETTINGS:
        case TRUST_E_SYSTEM_ERROR: return SignatureStatus::Invalid;
        case CERT_E_UNTRUSTEDROOT:
        case CERT_E_CHAINING:
        case CERT_E_EXPIRED:
        case CERT_E_REVOKED:
        case TRUST_E_EXPLICIT_DISTRUST:
        case TRUST_E_SUBJECT_NOT_TRUSTED:
        case CERT_E_UNTRUSTEDTESTROOT: return SignatureStatus::Untrusted;
        default: return SignatureStatus::Unknown;
    }
}

}  // namespace ef::platform
