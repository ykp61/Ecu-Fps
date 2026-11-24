#pragma once
// R21: kullaniciya yerel kayit yolu sectirir (IFileSaveDialog). Ag/yukleme yoktur; yalniz dosya sistemi.
// COM STA gerektirir: cagiran is parcaciginda CoInitializeEx(COINIT_APARTMENTTHREADED) yapilmis olmali.

#include <windows.h>

#include <string>

namespace ef::platform {

struct SavePathResult {
    enum class Status { Chosen, Canceled, Failed };
    Status status = Status::Failed;
    std::wstring path;  // yalniz Chosen
};

// title: iletişim kutusu basligi; default_name: onerilen dosya adi; filter_name/pattern: ornek "Zip"/"*.zip";
// default_extension: uzanti yazilmazsa eklenir (ornek L"zip", L"json").
SavePathResult ask_save_path(HWND owner, const std::wstring& title, const std::wstring& default_name,
                             const wchar_t* filter_name, const wchar_t* filter_pattern,
                             const wchar_t* default_extension = L"zip");

struct OpenPathResult {
    enum class Status { Chosen, Canceled, Failed };
    Status status = Status::Failed;
    std::wstring path;  // yalniz Chosen
};
// EF-X11: var olan tek dosya sectirir (IFileOpenDialog; yalniz dosya sistemi, son kullanilanlara eklenmez). COM STA.
OpenPathResult ask_open_path(HWND owner, const std::wstring& title, const wchar_t* filter_name, const wchar_t* filter_pattern);

}  // namespace ef::platform
