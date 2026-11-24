#include "platform/windows/games/game_icons.h"

#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#include "platform/windows/win_env.h"

namespace ef::platform::games {

using core::games::IconCandidate;
using core::games::IconSourceKind;
using core::games::ImagePixels;
using core::games::ImageRef;
using core::games::kGameIconMaxFileBytes;
using core::games::kGameIconMaxSourcePx;
using core::games::kGameIconPx;
using Microsoft::WRL::ComPtr;

namespace {

struct IconHandle {
    HICON h = nullptr;
    ~IconHandle() {
        if (h) DestroyIcon(h);
    }
};

struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

// Kaynak -> 64x64 on-carpilmis BGRA. Kare olmayan kaynak oran korunarak sigdirilir ve ortalanir (kenarlar saydam).
// Hic gorunur piksel yoksa (tum alfa 0) ikon sayilmaz.
ImageRef to_icon_pixels(IWICImagingFactory* factory, IWICBitmapSource* src) {
    UINT w = 0, h = 0;
    if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0 || w > kGameIconMaxSourcePx || h > kGameIconMaxSourcePx) return nullptr;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return nullptr;
    UINT dw = kGameIconPx, dh = kGameIconPx;
    if (w > h) dh = std::max<UINT>(1, static_cast<UINT>(std::lround(static_cast<double>(kGameIconPx) * h / w)));
    else if (h > w) dw = std::max<UINT>(1, static_cast<UINT>(std::lround(static_cast<double>(kGameIconPx) * w / h)));
    ComPtr<IWICBitmapSource> scaled = conv;
    if (w != dw || h != dh) {
        ComPtr<IWICBitmapScaler> scaler;
        // Kucultmede Fant (alan ortalamasi), buyutmede kubik (32 px Steam ikonu bloklasmasin).
        const auto mode = (dw > w || dh > h) ? WICBitmapInterpolationModeCubic : WICBitmapInterpolationModeFant;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(conv.Get(), dw, dh, mode))) return nullptr;
        scaled = scaler;
    }
    std::vector<BYTE> part(static_cast<std::size_t>(dw) * dh * 4);
    if (FAILED(scaled->CopyPixels(nullptr, dw * 4, static_cast<UINT>(part.size()), part.data()))) return nullptr;
    auto out = std::make_shared<ImagePixels>();
    out->w = out->h = kGameIconPx;
    out->bgra.assign(static_cast<std::size_t>(kGameIconPx) * kGameIconPx * 4, 0);
    const std::size_t ox = (kGameIconPx - dw) / 2, oy = (kGameIconPx - dh) / 2;
    bool visible = false;
    for (std::size_t y = 0; y < dh; ++y) {
        const BYTE* row = part.data() + y * dw * 4;
        std::copy(row, row + static_cast<std::size_t>(dw) * 4, out->bgra.begin() + static_cast<std::ptrdiff_t>(((oy + y) * kGameIconPx + ox) * 4));
        for (std::size_t x = 0; x < dw && !visible; ++x) visible = row[x * 4 + 3] != 0;
    }
    if (!visible) return nullptr;
    return out;
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

struct GameIconLoader::Impl {
    bool com_owned = false;
    ComPtr<IWICImagingFactory> factory;
};

GameIconLoader::GameIconLoader() : impl_(std::make_unique<Impl>()) {
    // Izleme is parcaciginda COM yoktur; baska modda acilmis bir dairede (RPC_E_CHANGED_MODE) mevcut oturum kullanilir.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->com_owned = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->factory)))) impl_->factory.Reset();
}

GameIconLoader::~GameIconLoader() {
    impl_->factory.Reset();
    if (impl_->com_owned) CoUninitialize();
}

bool GameIconLoader::ready() const noexcept { return impl_->factory != nullptr; }

IconLoad GameIconLoader::load_exe_icon(const std::string& exe_path_utf8) {
    IconLoad out;
    if (!ready() || exe_path_utf8.empty()) return out;
    const std::wstring path = to_wide(exe_path_utf8);
    IconHandle icon;
    // Dosya veri olarak okunur (calistirilmaz). Yalniz buyuk ikon istenir; kucuk ikon tutamaci uretilmez.
    const HRESULT hr = SHDefExtractIconW(path.c_str(), 0, 0, &icon.h, nullptr, kGameIconPx);
    if (hr == S_FALSE) {
        out.definitive = true;  // EXE'de ikon kaynagi yok
        return out;
    }
    if (hr != S_OK || !icon.h) return out;  // erisilemedi: kesin sonuc degil
    out.definitive = true;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(impl_->factory->CreateBitmapFromHICON(icon.h, &bmp))) return out;
    out.image = to_icon_pixels(impl_->factory.Get(), bmp.Get());
    return out;
}

IconLoad GameIconLoader::load_image_file(const std::string& path_utf8) {
    IconLoad out;
    if (!ready() || path_utf8.empty()) return out;
    std::vector<BYTE> data;
    {
        FileHandle f;
        f.h = CreateFileW(to_wide(path_utf8).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (f.h == INVALID_HANDLE_VALUE) return out;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(f.h, &size)) return out;
        if (size.QuadPart <= 0 || static_cast<std::uint64_t>(size.QuadPart) > kGameIconMaxFileBytes) {
            out.definitive = true;  // bos veya sinirin ustunde: okunmaz
            return out;
        }
        data.resize(static_cast<std::size_t>(size.QuadPart));
        std::size_t got = 0;
        while (got < data.size()) {
            DWORD n = 0;
            if (!ReadFile(f.h, data.data() + got, static_cast<DWORD>(data.size() - got), &n, nullptr)) return out;
            if (n == 0) break;
            got += n;
        }
        if (got != data.size()) return out;  // okuma sirasinda dosya degisti: kesin degil
    }
    out.definitive = true;
    // Yalniz JPEG ve yalniz Windows'un kendi kod cozucusu. Akis EOI'ye kadar tam olmalidir: WIC kesik dosyayi kismen
    // cozebilir; yarim yazilmis onbellek dosyasi yarim resim olarak gosterilmez (harf kutusu kalir).
    if (!core::games::jpeg_stream_complete(data.data(), data.size())) return out;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(impl_->factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(data.data(), static_cast<DWORD>(data.size()))) ||
        FAILED(CoCreateInstance(CLSID_WICJpegDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))) ||
        FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) || FAILED(decoder->GetFrame(0, &frame)))
        return out;
    out.image = to_icon_pixels(impl_->factory.Get(), frame.Get());
    return out;
}

IconLoad GameIconLoader::load(const IconCandidate& candidate) {
    return candidate.kind == IconSourceKind::ImageFile ? load_image_file(candidate.path) : load_exe_icon(candidate.path);
}

IconFileStamp icon_file_stamp(const std::string& path_utf8) {
    IconFileStamp out;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(to_wide(path_utf8).c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return out;
    out.ok = true;
    out.mtime = (static_cast<std::uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    out.size = (static_cast<std::uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return out;
}

void GameIconCache::begin_pass() {
    for (auto& [k, e] : files_) e.used = false;
    for (auto& [k, e] : rows_) e.used = false;
}

ImageRef GameIconCache::resolve(const std::string& row_key, const std::vector<IconCandidate>& candidates, GameIconLoader& loader) {
    ImageRef result;
    for (const auto& c : candidates) {
        const IconFileStamp stamp = icon_file_stamp(c.path);
        if (!stamp.ok) continue;  // dosya yok / erisilemedi: siradaki aday
        const std::string key = (c.kind == IconSourceKind::ImageFile ? "i|" : "x|") + lower_ascii(c.path);
        if (const auto it = files_.find(key); it != files_.end() && it->second.mtime == stamp.mtime && it->second.size == stamp.size) {
            it->second.used = true;
            if (!it->second.image) continue;  // kesin "ikon yok": siradaki aday
            result = it->second.image;
            break;
        }
        const IconLoad load = loader.load(c);
        ++loads_;
        if (load.definitive) files_[key] = FileEntry{stamp.mtime, stamp.size, load.image, true};
        else files_.erase(key);
        if (load.image) {
            result = load.image;
            break;
        }
    }
    rows_[row_key] = RowEntry{result, true};
    return result;
}

ImageRef GameIconCache::remembered(const std::string& row_key) {
    const auto it = rows_.find(row_key);
    if (it == rows_.end()) return nullptr;
    it->second.used = true;
    return it->second.image;
}

void GameIconCache::end_pass(bool files_complete) {
    // Yalniz bastan sona derin calisan tur "bu satir/dosya artik kitaplikta yok" kanitidir. Oyun calisirken gelen tur
    // eksik bir kitaplik listesi tasiyabilir (launcher o sirada manifest yaziyor olabilir); o turda gorulmeyen satir
    // unutulursa liste duzeldiginde dosyaya da dokunulamadigi icin kart oyun kapanana kadar ikonsuz kalirdi.
    if (!files_complete) return;
    std::erase_if(rows_, [](const auto& kv) { return !kv.second.used; });
    std::erase_if(files_, [](const auto& kv) { return !kv.second.used; });
}

}  // namespace ef::platform::games
