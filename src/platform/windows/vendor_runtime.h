#pragma once
// GPU-01 platform: uretici runtime'larini guvenli yukleme. Yalniz System32, gercek yol dogrulamasi,
// imza (gomulu veya surucu katalogu). Calisma/EXE klasorundeki taklit DLL asla yuklenmez.

#include <filesystem>
#include <string>

#include "core/security/trust.h"
#include "core/vendors/dispatch.h"

namespace ef::platform {

// Surec geneli DLL arama sertlestirmesi: varsayilan arama yalniz System32, gecerli dizin cikarilir.
// Uygulama girisinde (her rolde) bir kez cagrilir.
void harden_dll_search();

// Gomulu Authenticode yoksa Windows katalog imzasini dener (surucu DLL'leri katalog imzalidir).
core::security::SignatureStatus verify_file_or_catalog_signature(const std::filesystem::path& file);

struct LoadedRuntime {
    core::vendors::RuntimeInfo info;
    void* module = nullptr;  // HMODULE; Loaded degilse nullptr
};

LoadedRuntime load_vendor_runtime(core::vendors::Vendor v);
void unload_vendor_runtime(LoadedRuntime& r);

}  // namespace ef::platform
