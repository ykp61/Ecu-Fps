#include "platform/windows/save_dialog.h"

#include <shobjidl.h>

namespace ef::platform {

SavePathResult ask_save_path(HWND owner, const std::wstring& title, const std::wstring& default_name,
                             const wchar_t* filter_name, const wchar_t* filter_pattern,
                             const wchar_t* default_extension) {
    SavePathResult out;
    IFileSaveDialog* dlg = nullptr;
    // CLSID/IID basliklardaki DECLSPEC_UUID'den gelir; uuid.lib gerekmez.
    if (FAILED(CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileSaveDialog),
                                reinterpret_cast<void**>(&dlg))) ||
        !dlg)
        return out;
    struct Release {
        IFileSaveDialog* p;
        ~Release() {
            if (p) p->Release();
        }
    } rel{dlg};

    FILEOPENDIALOGOPTIONS opts{};
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        (void)dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC spec{filter_name, filter_pattern};
    (void)dlg->SetFileTypes(1, &spec);
    (void)dlg->SetFileTypeIndex(1);
    (void)dlg->SetDefaultExtension(default_extension ? default_extension : L"zip");
    (void)dlg->SetFileName(default_name.c_str());
    (void)dlg->SetTitle(title.c_str());

    const HRESULT hr = dlg->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        out.status = SavePathResult::Status::Canceled;
        return out;
    }
    if (FAILED(hr)) return out;  // Failed: cagiran acik hata notu gosterir

    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) return out;
    struct ReleaseItem {
        IShellItem* p;
        ~ReleaseItem() {
            if (p) p->Release();
        }
    } rel_item{item};
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return out;
    out.path = path;
    CoTaskMemFree(path);
    out.status = SavePathResult::Status::Chosen;
    return out;
}

OpenPathResult ask_open_path(HWND owner, const std::wstring& title, const wchar_t* filter_name, const wchar_t* filter_pattern) {
    OpenPathResult out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileOpenDialog),
                                reinterpret_cast<void**>(&dlg))) ||
        !dlg)
        return out;
    struct Release {
        IFileOpenDialog* p;
        ~Release() {
            if (p) p->Release();
        }
    } rel{dlg};
    FILEOPENDIALOGOPTIONS opts{};
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        (void)dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT);
    const COMDLG_FILTERSPEC spec{filter_name, filter_pattern};
    (void)dlg->SetFileTypes(1, &spec);
    (void)dlg->SetFileTypeIndex(1);
    (void)dlg->SetTitle(title.c_str());
    const HRESULT hr = dlg->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        out.status = OpenPathResult::Status::Canceled;
        return out;
    }
    if (FAILED(hr)) return out;
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) return out;
    struct ReleaseItem {
        IShellItem* p;
        ~ReleaseItem() {
            if (p) p->Release();
        }
    } rel_item{item};
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return out;
    out.path = path;
    CoTaskMemFree(path);
    out.status = OpenPathResult::Status::Chosen;
    return out;
}

}  // namespace ef::platform
