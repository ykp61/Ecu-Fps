// Oyun ikonu yukleyici probu (spec 2026-11-12 §4/§5). Konsol; pencere acmaz, hicbir sureci baslatmaz, yalniz dosya okur.
//   ef_game_icon_probe <EcuFps.exe yolu>
//   ef_game_icon_probe --image <jpg yolu> | --exe <exe yolu>   (elle tani: tek dosyayi yukler, sonucu yazar; ctest'te yok)
//  - Uygulamanin kendi EXE'si (ikon kaynagi var) -> 64x64, w*h*4 bayt, gorunur alfa.
//  - Ikonsuz ikili (bu prob) -> bos ve kesin; olmayan yol -> bos ve kesin DEGIL (onbellege "ikon yok" yazilmaz).
//  - Kodla uretilmis JPEG -> 64x64 opak; kare olmayan -> ortalanmis; kesik / sinirdan buyuk dosya / sinirdan buyuk
//    kenar / JPEG olmayan icerik -> bos.
//  - 1000 yuklemede GDI ve USER tutamac sayisi sabit (DestroyIcon kacagi yok).
//  - Onbellek: ayni damgada dosya yeniden okunmaz, degisince okunur, kitaplikta olmayan duser.
// Gercek kurulu oyunlara bagli degildir.
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/windows/games/game_icons.h"
#include "platform/windows/win_env.h"

using namespace ef;
using Microsoft::WRL::ComPtr;
namespace pg = platform::games;
using core::games::IconCandidate;
using core::games::IconSourceKind;
using core::games::ImageRef;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

// Kodla uretilmis pikseller (24bpp BGR): kosegen renk gecisi; dosya/oyun/telifli gorsel yok.
bool write_jpeg(IWICImagingFactory* f, const std::wstring& path, UINT w, UINT h) {
    std::vector<BYTE> px(static_cast<std::size_t>(w) * h * 3);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            BYTE* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
            p[0] = static_cast<BYTE>(40 + (x * 180) / (w ? w : 1));
            p[1] = static_cast<BYTE>(60 + (y * 160) / (h ? h : 1));
            p[2] = static_cast<BYTE>(200 - ((x + y) * 120) / (w + h));
        }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    return SUCCEEDED(f->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
           SUCCEEDED(frame->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat24bppBGR) &&
           SUCCEEDED(frame->WritePixels(h, w * 3, static_cast<UINT>(px.size()), px.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}

std::vector<BYTE> read_all(const std::wstring& path) {
    std::vector<BYTE> out;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (64ll << 20)) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD n = 0;
        if (!ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &n, nullptr) || n != out.size()) out.clear();
    }
    CloseHandle(h);
    return out;
}

bool write_all(const std::wstring& path, const BYTE* data, std::size_t size, std::size_t pad = 0) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    bool ok = WriteFile(h, data, static_cast<DWORD>(size), &n, nullptr) && n == size;
    if (ok && pad) {
        const std::vector<BYTE> zeros(pad, 0);
        ok = WriteFile(h, zeros.data(), static_cast<DWORD>(zeros.size()), &n, nullptr) && n == pad;
    }
    CloseHandle(h);
    return ok;
}

std::size_t visible_pixels(const ImageRef& img) {
    std::size_t n = 0;
    for (std::size_t i = 3; i < img->bgra.size(); i += 4) n += img->bgra[i] != 0;
    return n;
}

bool icon_shape(const ImageRef& img) {
    return img && img->w == core::games::kGameIconPx && img->h == core::games::kGameIconPx &&
           img->bgra.size() == static_cast<std::size_t>(img->w) * img->h * 4 && core::games::image_pixels_valid(*img);
}

struct Counts {
    DWORD gdi = 0, user = 0;
};
Counts gui_counts() { return {GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)}; }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: ef_game_icon_probe <app exe>\n");
        return 2;
    }
    if (argc == 3 && (std::wstring(argv[1]) == L"--image" || std::wstring(argv[1]) == L"--exe")) {
        // Elle tani (cihaz kaniti): tek dosya salt okunur yuklenir. Sonuc: boyut, gorunur piksel sayisi, kesinlik.
        pg::GameIconLoader loader;
        const std::string path = platform::to_utf8(argv[2]);
        const auto r = std::wstring(argv[1]) == L"--image" ? loader.load_image_file(path) : loader.load_exe_icon(path);
        if (r.image) std::printf("icon %ux%u visible=%zu definitive=%d\n", r.image->w, r.image->h, visible_pixels(r.image), r.definitive ? 1 : 0);
        else std::printf("no icon definitive=%d\n", r.definitive ? 1 : 0);
        return r.image ? 0 : 3;
    }
    const std::string app = platform::to_utf8(argv[1]);
    wchar_t self_w[MAX_PATH * 4] = {};
    GetModuleFileNameW(nullptr, self_w, static_cast<DWORD>(std::size(self_w)));
    const std::string self = platform::to_utf8(self_w);

    wchar_t tmp_w[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp_w);
    const std::wstring dir = std::wstring(tmp_w) + L"ef_icon_probe_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const auto in_dir = [&](const wchar_t* name) { return dir + L"\\" + name; };
    const std::wstring files[] = {in_dir(L"icon.jpg"), in_dir(L"wide.jpg"), in_dir(L"cut_header.jpg"), in_dir(L"cut_half.jpg"), in_dir(L"big.jpg"),
                                  in_dir(L"huge_side.jpg"), in_dir(L"not_jpeg.jpg"), in_dir(L"empty.jpg"), in_dir(L"swap.jpg")};

    {
        pg::GameIconLoader loader;
        check(loader.ready(), "loader ready (COM + WIC factory)");
        if (!loader.ready()) return 1;

        // 1) EXE ikon kaynagi
        const auto a = loader.load_exe_icon(app);
        check(a.definitive && icon_shape(a.image), "app EXE icon -> 64x64 premultiplied BGRA");
        if (a.image) {
            const std::size_t vis = visible_pixels(a.image);
            std::printf("     app icon visible pixels=%zu of %u\n", vis, a.image->w * a.image->h);
            check(vis > 64, "app EXE icon has visible (non-zero alpha) pixels");
            bool premul = true;
            for (std::size_t i = 0; i + 3 < a.image->bgra.size(); i += 4)
                premul = premul && a.image->bgra[i] <= a.image->bgra[i + 3] && a.image->bgra[i + 1] <= a.image->bgra[i + 3] &&
                         a.image->bgra[i + 2] <= a.image->bgra[i + 3];
            check(premul, "app EXE icon is premultiplied (channel <= alpha)");
        }
        const auto none = loader.load_exe_icon(self);
        check(none.definitive && !none.image, "icon-less binary -> null (definitive; no generic/shell icon)");
        const auto missing = loader.load_exe_icon(platform::to_utf8(in_dir(L"no_such.exe")));
        check(!missing.image && !missing.definitive, "missing EXE -> null, not cached as 'no icon'");

        // 2) Resim dosyasi (kodla uretilmis JPEG)
        ComPtr<IWICImagingFactory> wic;
        check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))), "test WIC factory");
        if (!wic) return 1;
        check(write_jpeg(wic.Get(), files[0], 32, 32), "write 32x32 jpeg");
        const auto j = loader.load_image_file(platform::to_utf8(files[0]));
        check(j.definitive && icon_shape(j.image), "32x32 jpeg -> 64x64");
        if (j.image) {
            check(visible_pixels(j.image) == 64u * 64u, "jpeg icon is fully opaque");
            const BYTE* c = &j.image->bgra[(32u * 64u + 32u) * 4];  // merkez: uretilen gecisin ortasi (B~130, G~140, R~140)
            std::printf("     jpeg centre bgra=%u,%u,%u,%u\n", c[0], c[1], c[2], c[3]);
            check(std::abs(c[0] - 130) < 24 && std::abs(c[1] - 140) < 24 && std::abs(c[2] - 140) < 24 && c[3] == 255, "jpeg centre colour matches generated pixels");
        }
        check(!loader.load_exe_icon(platform::to_utf8(files[0])).image, "non-PE file as EXE -> null");
        check(write_jpeg(wic.Get(), files[1], 64, 32), "write 64x32 jpeg");
        const auto wide = loader.load_image_file(platform::to_utf8(files[1]));
        check(icon_shape(wide.image), "64x32 jpeg -> 64x64");
        if (wide.image) {
            const auto alpha = [&](UINT x, UINT y) { return wide.image->bgra[(static_cast<std::size_t>(y) * 64 + x) * 4 + 3]; };
            check(alpha(32, 2) == 0 && alpha(32, 61) == 0 && alpha(32, 32) == 255 && alpha(1, 32) == 255, "non-square jpeg is centred, not stretched");
        }

        const auto good = read_all(files[0]);
        check(good.size() > 200, "generated jpeg readable");
        check(write_jpeg(wic.Get(), files[8], 256, 256), "write 256x256 jpeg");
        const auto large = read_all(files[8]);
        check(large.size() > 1000, "256x256 jpeg readable");
        check(write_all(files[2], good.data(), 60), "write header-truncated jpeg");
        const auto cut1 = loader.load_image_file(platform::to_utf8(files[2]));
        check(cut1.definitive && !cut1.image, "truncated jpeg (header) -> null");
        check(write_all(files[3], large.data(), large.size() / 2), "write half-truncated jpeg");
        const auto cut2 = loader.load_image_file(platform::to_utf8(files[3]));
        check(cut2.definitive && !cut2.image, "truncated jpeg (half of the scan data) -> null");
        check(write_all(files[4], good.data(), good.size(), core::games::kGameIconMaxFileBytes + 1 - good.size()), "write oversize file");
        const auto big = loader.load_image_file(platform::to_utf8(files[4]));
        check(big.definitive && !big.image, "file over 4 MiB -> null (not read)");
        check(write_all(files[4], good.data(), good.size(), core::games::kGameIconMaxFileBytes - good.size()), "write file at the size limit");
        check(icon_shape(loader.load_image_file(platform::to_utf8(files[4])).image), "valid jpeg exactly at 4 MiB -> loaded");
        check(write_jpeg(wic.Get(), files[5], core::games::kGameIconMaxSourcePx + 1, 4), "write 4097x4 jpeg");
        const auto side = loader.load_image_file(platform::to_utf8(files[5]));
        check(side.definitive && !side.image, "source side over 4096 px -> null");
        const BYTE png_magic[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0};
        check(write_all(files[6], png_magic, sizeof png_magic), "write non-jpeg content");
        const auto notj = loader.load_image_file(platform::to_utf8(files[6]));
        check(notj.definitive && !notj.image, "non-JPEG content -> null");
        check(write_all(files[7], png_magic, 0), "write empty file");
        check(!loader.load_image_file(platform::to_utf8(files[7])).image, "empty file -> null");
        const auto gone = loader.load_image_file(platform::to_utf8(in_dir(L"no_such.jpg")));
        check(!gone.image && !gone.definitive, "missing image -> null, not cached as 'no icon'");
        check(!loader.load_image_file(platform::to_utf8(dir)).image, "directory as image -> null");

        // 3) Tutamac kacagi: 1000 EXE ikonu + 1000 ikonsuz + 200 JPEG yuklemesi
        (void)loader.load_exe_icon(app);  // isinma (kabuk/WIC tembel ilklendirmeleri)
        (void)loader.load_exe_icon(self);
        const Counts before = gui_counts();
        const ULONGLONG t0 = GetTickCount64();
        std::size_t loaded = 0;
        for (int i = 0; i < 1000; ++i) {
            loaded += loader.load_exe_icon(app).image != nullptr;
            loaded += loader.load_exe_icon(self).image != nullptr;
            if (i % 5 == 0) loaded += loader.load_image_file(platform::to_utf8(files[0])).image != nullptr;
        }
        const ULONGLONG ms = GetTickCount64() - t0;
        const Counts after = gui_counts();
        std::printf("     2200 loads in %llu ms; gdi %lu -> %lu, user %lu -> %lu\n", ms, before.gdi, after.gdi, before.user, after.user);
        check(loaded == 1200, "1000 EXE + 200 jpeg loads succeeded, 1000 icon-less loads stayed null");
        check(after.gdi == before.gdi && after.user == before.user, "GDI/USER handle counts stable over 1000 loads");

        // 4) Onbellek
        pg::GameIconCache cache;
        const std::vector<IconCandidate> cands{{IconSourceKind::ImageFile, platform::to_utf8(in_dir(L"swap.jpg"))},
                                               {IconSourceKind::ExeResource, app}};
        cache.begin_pass();
        const ImageRef c1 = cache.resolve("steam|a", cands, loader);
        check(icon_shape(c1) && cache.loads() == 1, "cache: first candidate (image file) wins, one load");
        const ImageRef c2 = cache.resolve("steam|a", cands, loader);
        check(c2 == c1 && cache.loads() == 1, "cache: same stamp -> same pixels, file not read again");
        const ImageRef c3 = cache.resolve("epic|b", {{IconSourceKind::ExeResource, platform::to_utf8(in_dir(L"no_such.exe"))}, {IconSourceKind::ExeResource, app}}, loader);
        check(icon_shape(c3) && c3 != c1, "cache: unreachable candidate skipped, next candidate used");
        const ImageRef c4 = cache.resolve("manual|c", {{IconSourceKind::ExeResource, self}}, loader);
        const std::size_t loads_before = cache.loads();
        const ImageRef c5 = cache.resolve("manual|c", {{IconSourceKind::ExeResource, self}}, loader);
        check(!c4 && !c5 && cache.loads() == loads_before, "cache: definitive 'no icon' is remembered (no reload, initials stay)");
        cache.end_pass(true);
        check(cache.rows() == 3, "cache: three rows remembered");
        check(cache.remembered("steam|a") == c1 && cache.remembered("epic|b") == c3 && !cache.remembered("manual|c") && !cache.remembered("x|none"),
              "cache: remembered() returns the last icon without touching files");
        // Dosya degisti (boyut farkli): yeniden okunur.
        check(write_jpeg(wic.Get(), files[8], 48, 48), "rewrite swap.jpg");
        cache.begin_pass();
        const std::size_t loads_mid = cache.loads();
        const ImageRef c6 = cache.resolve("steam|a", cands, loader);
        check(icon_shape(c6) && c6 != c1 && cache.loads() == loads_mid + 1, "cache: changed file is reloaded");
        cache.end_pass(true);
        check(cache.rows() == 1 && cache.files() == 1, "cache: rows/files no longer in the library are dropped");
        // Oyun calisirken (derin kapi kapali) tur: dosyaya dokunulmaz, girdiler korunur.
        cache.begin_pass();
        check(cache.remembered("steam|a") == c6, "cache: non-deep pass keeps the known icon");
        cache.end_pass(false);
        check(cache.rows() == 1 && cache.files() == 1, "cache: non-deep pass does not drop file entries");
        // Oyun calisirken kitaplik listesi eksik gelebilir: o turda gorulmeyen satir unutulmaz (liste duzeldiginde dosyaya
        // dokunulamayacagi icin kart ikonsuz kalirdi). Satir ancak derin turda da gorulmezse duser.
        cache.begin_pass();
        cache.end_pass(false);
        check(cache.rows() == 1 && cache.files() == 1, "cache: non-deep pass with an incomplete library keeps unseen rows");
        cache.begin_pass();
        check(cache.remembered("steam|a") == c6, "cache: row icon survives an incomplete non-deep pass");
        cache.end_pass(false);
        cache.begin_pass();
        cache.end_pass(true);
        check(cache.rows() == 0 && cache.files() == 0, "cache: empty library drops everything");
    }

    for (const auto& f : files) DeleteFileW(f.c_str());
    RemoveDirectoryW(dir.c_str());
    std::printf("game_icon_probe: %s (%d failed)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
