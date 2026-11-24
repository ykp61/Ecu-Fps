#pragma once
// SEC-04 platform: WinVerifyTrust ile Authenticode (gomulu imza) dogrulamasi. Ag erisimi istenmez
// (iptal kontrolu onbellekten); cevrimdisi calisir.

#include <filesystem>

#include "core/security/trust.h"

namespace ef::platform {

// Unsigned yalniz PE sertifika tablosu (guvenlik veri dizini) bos iken; tablo var ama WinVerifyTrust imza
// bulamadiysa (bozulmus/sifirlanmis imza) Invalid doner (ADR-034 alpha istisnasi kurcalanmis imzayi gecirmez).
core::security::SignatureStatus verify_file_signature(const std::filesystem::path& file);

// PE dosyasinin guvenlik veri dizini (Authenticode sertifika tablosu) bos degil mi? PE degilse false.
bool pe_certificate_table_present(const std::filesystem::path& file);

}  // namespace ef::platform
