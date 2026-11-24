#pragma once
// Onceki urun adi "iTunex Pro" icin KALAN tek yer (marka degisikligi: Ecu Fps). Bu sabitler yalniz ileride yazilacak
// goc/migrasyon gorevinin eski kurulumu bulup tasimasi, eski hizmeti/kaydi/kisayolu temizlemesi icindir. Yeni kod bu
// adlari urun adi olarak KULLANMAZ. Baska hicbir dosyada eski ad bulunmaz.

#include <string_view>

namespace ef::core::legacy {

inline constexpr std::wstring_view kOldProductName = L"iTunex Pro";
inline constexpr std::wstring_view kOldDataDirName = L"iTunexPro";        // LocalAppData ve ProgramData altindaki klasor
inline constexpr std::wstring_view kOldServiceName = L"iTunexProHelper";  // yardimci hizmet adi
inline constexpr std::wstring_view kOldServiceDisplayName = L"iTunex Pro Helper";
inline constexpr std::wstring_view kOldInstallDirName = L"iTunex Pro";    // Program Files altinda
inline constexpr std::wstring_view kOldExeName = L"iTunexPro.exe";
inline constexpr std::wstring_view kOldUninstallKeyName = L"iTunexPro";   // ARP Uninstall anahtari
inline constexpr std::wstring_view kOldShortcutName = L"iTunex Pro.lnk";
inline constexpr std::wstring_view kOldAutostartValueName = L"iTunexPro";  // HKCU Run degeri
inline constexpr std::wstring_view kOldAutostartLegacyValueName = L"iTunex Pro";

// Adlandirilmis nesne oneklerinin eski halleri.
inline constexpr std::wstring_view kOldPipePrefix = L"\\\\.\\pipe\\iTunexPro.";
inline constexpr std::wstring_view kOldHelperPipeName = L"\\\\.\\pipe\\iTunexPro.Helper.v1";
inline constexpr std::wstring_view kOldSingleInstanceMutexPrefix = L"Local\\iTunexPro.SingleInstance.";
inline constexpr std::wstring_view kOldSetupMutexName = L"Local\\iTunexPro.Setup";
inline constexpr std::wstring_view kOldOperationMutexName = L"Global\\iTunexPro.Operation";
inline constexpr std::wstring_view kOldMachineOpsMutexName = L"Global\\iTunexPro.MachineOps";
inline constexpr std::wstring_view kOldMainWindowClass = L"iTunexPro.MainWindow";
inline constexpr std::wstring_view kOldFrameSessionPrefix = L"iTunexPro-";

// Eski yayin deposundan indirme adresi oneki.
inline constexpr std::string_view kOldReleaseRepoPrefix = "https://github.com/ykp61/iTunex-Pro-releases/releases/download/";

}  // namespace ef::core::legacy
