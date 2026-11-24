#include "benchmark/scene/d3d_renderer.h"

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <system_error>

#include "benchmark/scene/arena.h"
#include "benchmark/scene/sim_pool.h"

// Derleme zamani fxc ciktilari (g_<ad>)
#include "arena_ps_gbuffer.h"
#include "arena_ps_light.h"
#include "arena_vs_inst.h"
#include "arena_vs_object.h"
#include "arena_vs_tunnel.h"
#include "scene_ps_blur.h"
#include "scene_ps_gbuffer.h"
#include "scene_ps_light.h"
#include "scene_ps_preview.h"
#include "scene_ps_tonemap.h"
#include "scene_vs.h"
#include "scene_vs_fullscreen.h"

namespace ef::benchmark {

using Microsoft::WRL::ComPtr;

std::string_view to_string(RendererKind k) noexcept {
    switch (k) {
        case RendererKind::Hardware: return "hardware";
        case RendererKind::Software: return "software";
        case RendererKind::None: return "none";
    }
    return "none";
}

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double ms_between(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

constexpr std::uint32_t kRingSlots = 8;       // zaman damgasi + olay sorgusu halkasi (>= kuyruk + 2)
constexpr double kFrameTimeoutMs = 10000.0;   // tek kare bu kadar bitmezse GPU takildi/TDR sayilir
constexpr UINT kNoiseSize = 256;

struct Vtx {
    float p[3], n[3];
};
struct InstGpu {
    float pos_scale[4];
    float color[4];
    float spin;
};
struct FrameCb {
    float view_proj[16];
    float eye_frame[4];
    float sun_dir[4];
    std::uint32_t counts[4];
    float misc[4];
};
static_assert(sizeof(FrameCb) == 128, "HLSL cbuffer Frame ile ayni yerlesim");
struct PassCb {
    std::int32_t dir[2];
    std::int32_t flags[2];
};
static_assert(sizeof(PassCb) == 16, "HLSL cbuffer PassParams ile ayni yerlesim");

// ---- Arena v1 cizim sabitleri (frame_sim.h kamera / tunel sabitleriyle ayni geometri) ----
constexpr float kArenaTanHalfY = static_cast<float>(kCameraTanYQ8) / 256.f;  // ayiklama ile ayni dusey gorus
constexpr float kArenaNear = 0.5f;
constexpr float kArenaFar = static_cast<float>(kCameraFar) / 65536.f;  // sis ve uc kapak mesafesi (ayiklama uzak siniri)
constexpr float kArenaFarPlane = kArenaFar + 16.f;                     // uc kapak (uzak + 8) kirpilmasin
constexpr float kArenaTunnelHalf = static_cast<float>(kTunnelHardRadius + kCullRadius) / 65536.f + 2.f;
constexpr float kArenaPanel = 8.f;             // tunel panel adimi (birim)
constexpr float kArenaBloomThreshold = 0.8f;
constexpr float kArenaExposure = 1.0f;
constexpr float kArenaEmissiveGain = 1.0f;     // isima payi -> HDR
constexpr float kArenaObjectEmissive = 0.15f;  // nesne isima payi (G-buffer albedo alfa)
constexpr float kArenaLineEmissive = 0.9f;     // panel cizgisi isima payi (bloom esigini asar)
constexpr float kArenaFill[4] = {-0.35f, -0.75f, 0.55f, 0.28f};  // dolgu isigi yonu (normalize edilir) + siddeti
constexpr UINT kArenaTunnelVertices = 30;      // 4 duvar + uc kapak
struct ArenaFrameCb {
    std::uint32_t u[4];  // kare indeksi, akis maskesi, yakin ayiklama siniri (Q16.16)
    float f[4];          // kamera z (tunel), kamera z (isik periyodu), tunel yari kenari, sis mesafesi
    float g[4];          // isik periyodu, isik geri payi, isima kazanci, panel adimi
    float wall[4], line[4], fog[4];
};
static_assert(sizeof(ArenaFrameCb) == 96, "HLSL cbuffer ArenaFrame ile ayni yerlesim");

// Satir vektoru kurali: v * View * Proj. HLSL cbuffer varsayilan column_major oldugundan yuklerken transpoze edilir.
void look_at(const float eye[3], const float at[3], float m[16]) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float l = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& v : z) v /= l;
    const float up[3] = {0, 1, 0};
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& v : x) v /= l;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float r[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                         -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    std::memcpy(m, r, sizeof r);
}
void perspective(float fov_y, float aspect, float zn, float zf, float m[16]) {
    const float h = 1.f / std::tan(fov_y / 2), w = h / aspect;
    const float r[16] = {w, 0, 0, 0, 0, h, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    std::memcpy(m, r, sizeof r);
}
void mul(const float a[16], const float b[16], float o[16]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            o[i * 4 + j] = s;
        }
}
void transpose(const float a[16], float o[16]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) o[j * 4 + i] = a[i * 4 + j];
}

bool is_software(const DXGI_ADAPTER_DESC1& d) {
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || (d.VendorId == 0x1414 && d.DeviceId == 0x8c);  // Microsoft Basic Render
}

// Prosedurel gurultu dokusu (deterministik): rastgele texel + iki kez 3x3 kutu bulaniklik, CPU mip zinciri.
std::vector<std::vector<std::uint32_t>> make_noise_mips(std::uint64_t seed) {
    std::uint64_t s = (seed ? seed : 1) ^ 0xD1B54A32D192ED03ull;
    auto next = [&] {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    };
    const UINT n = kNoiseSize;
    std::vector<float> ch[3];
    for (auto& c : ch) {
        c.resize(static_cast<std::size_t>(n) * n);
        for (auto& v : c) v = static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24);
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<float> o(c.size());
            for (UINT y = 0; y < n; ++y)
                for (UINT x = 0; x < n; ++x) {
                    float acc = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) acc += c[((y + n + dy) % n) * n + (x + n + dx) % n];
                    o[static_cast<std::size_t>(y) * n + x] = acc / 9.f;
                }
            c.swap(o);
        }
    }
    std::vector<std::vector<std::uint32_t>> mips;
    for (UINT size = n; size >= 1; size /= 2) {
        std::vector<std::uint32_t> lv(static_cast<std::size_t>(size) * size);
        const UINT step = n / size;
        for (UINT y = 0; y < size; ++y)
            for (UINT x = 0; x < size; ++x) {
                std::uint32_t px = 0xFF000000u;
                for (int k = 0; k < 3; ++k) {
                    double acc = 0;
                    for (UINT yy = 0; yy < step; ++yy)
                        for (UINT xx = 0; xx < step; ++xx) acc += ch[k][static_cast<std::size_t>(y * step + yy) * n + x * step + xx];
                    // Kutu bulaniklik araligi daraltir; 0..1'e yayilir.
                    double v = (acc / (step * step) - 0.5) * 3.0 + 0.5;
                    v = std::clamp(v, 0.0, 1.0);
                    px |= static_cast<std::uint32_t>(v * 255.0 + 0.5) << (8 * k);
                }
                lv[static_cast<std::size_t>(y) * size + x] = px;
            }
        mips.push_back(std::move(lv));
        if (size == 1) break;
    }
    return mips;
}

// Win32 olay tutamaci (RAII). SceneGpu icinde cihaz/citten ONCE bildirilir: uyeler ters sirada yok
// edildiginden cit ve cihaz serbest kaldiktan sonra kapanir (bekleyen SetEventOnCompletion guvenli).
struct EventHandle {
    HANDLE h = nullptr;
    EventHandle() = default;
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    ~EventHandle() {
        if (h) CloseHandle(h);
    }
};

struct PreviewCb {
    float inv[4];  // xy: 1 / onizleme boyutu, z: eksen basina tap sayisi
};
static_assert(sizeof(PreviewCb) == 16, "HLSL cbuffer PreviewParams ile ayni yerlesim");

struct LightChunkCb {
    std::uint32_t chunk[4];  // ilk isik, parca sonu, taban parca mi, -
};
static_assert(sizeof(LightChunkCb) == 16, "HLSL cbuffer LightChunk ile ayni yerlesim");

// Tek cihaz + tum kaynaklar. Isinma, olcum, kalibrasyon ve dogrulama karesi ayni nesneyi kullanir.
class SceneGpu {
public:
    SceneGpu() = default;
    SceneGpu(const SceneGpu&) = delete;
    SceneGpu& operator=(const SceneGpu&) = delete;
    // Sira: baglam durumu temizlenir + bekleyen komutlar gonderilir; ComPtr uyeleri (cit, baglam, cihaz)
    // ters bildirim sirasiyla birakilir; olay tutamaci en son kapanir (bkz. EventHandle).
    ~SceneGpu() {
        if (ctx_) {
            ctx_->ClearState();
            ctx_->Flush();
        }
    }

    bool create_device(bool prefer_hardware, AdapterInfo& info, std::string& err, int hardware_index = -1) {
        ComPtr<IDXGIFactory6> fac;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) {
            err = "dxgi_factory";
            return false;
        }
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc{};
        if (prefer_hardware || hardware_index >= 0) {
            // Liste once toplanir, secim birim testli nth_hardware_adapter ile yapilir (tek secim yolu).
            std::vector<ComPtr<IDXGIAdapter1>> list;
            std::vector<DXGI_ADAPTER_DESC1> descs;
            std::vector<char> swc;  // vector<bool> span'e donusmez
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> a;
                if (fac->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
                if (!a) break;
                DXGI_ADAPTER_DESC1 d{};
                a->GetDesc1(&d);
                list.push_back(a);
                descs.push_back(d);
                swc.push_back(is_software(d) ? 1 : 0);
            }
            const auto flags = std::make_unique<bool[]>(swc.size() + 1);
            for (std::size_t k = 0; k < swc.size(); ++k) flags[k] = swc[k] != 0;
            const int pick = nth_hardware_adapter(std::span<const bool>(flags.get(), swc.size()),hardware_index >= 0 ? hardware_index : 0);
            if (pick >= 0) {
                adapter = list[static_cast<std::size_t>(pick)];
                desc = descs[static_cast<std::size_t>(pick)];
            }
            if (hardware_index >= 0 && !adapter) {
                err = "adapter_index";  // istenen bagdastirici yok: WARP'a dusulmez
                info.kind = RendererKind::None;
                return false;
            }
        }
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr;
        if (adapter) {
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, want, 2, D3D11_SDK_VERSION, &dev_, &got, &ctx_);
            info.kind = RendererKind::Hardware;
        } else {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 2, D3D11_SDK_VERSION, &dev_, &got, &ctx_);
            info.kind = RendererKind::Software;  // puanlanmaz; ayri sinif
            desc = {};
            wcscpy_s(desc.Description, L"WARP");
        }
        if (FAILED(hr)) {
            err = "d3d11_create_device";
            info.kind = RendererKind::None;
            return false;
        }
        info.name = desc.Description;
        info.vendor_id = desc.VendorId;
        info.device_id = desc.DeviceId;
        info.dedicated_vram = desc.DedicatedVideoMemory;
        if (adapter) {
            char luid[24];
            std::snprintf(luid, sizeof luid, "%08lx:%08lx", static_cast<unsigned long>(desc.AdapterLuid.HighPart),
                          static_cast<unsigned long>(desc.AdapterLuid.LowPart));
            info.luid = luid;
        }
        info.feature_level = static_cast<std::uint32_t>(got);
        // Surucu is parcacigi ozelligi (salt okuma; denge testi kosul kaydi). Okunamazsa bilinmiyor kalir.
        D3D11_FEATURE_DATA_THREADING threading{};
        if (SUCCEEDED(dev_->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof threading))) {
            info.driver_concurrent_creates = threading.DriverConcurrentCreates != FALSE;
            info.driver_command_lists = threading.DriverCommandLists != FALSE;
        }
        // Video bellegi butcesi sorgusu icin cihazin bagdastiricisi (yoksa butce "bilinmiyor" kalir).
        ComPtr<IDXGIDevice> dxgi_dev;
        ComPtr<IDXGIAdapter> dxgi_adapter;
        if (SUCCEEDED(dev_.As(&dxgi_dev)) && SUCCEEDED(dxgi_dev->GetAdapter(&dxgi_adapter))) dxgi_adapter.As(&adapter3_);
        return true;
    }

    // Kaynaklar `max_tier` boyutunda kurulur; daha dusuk tier ayni tamponlarin ilk N ogesini cizer.
    bool build(const SceneConfig& c, Tier max_tier, std::string& err) {
        SceneConfig mc = c;
        mc.tier = max_tier;
        const auto inst = generate_instances(mc);
        const auto lights = generate_lights(mc);

        // Kup geometrisi (24 kose, 36 indeks)
        std::vector<Vtx> vtx;
        std::vector<std::uint16_t> idx;
        const float N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int f = 0; f < 6; ++f) {
            const float* n = N[f];
            float u[3] = {n[1], n[2], n[0]}, v[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
            const auto base = static_cast<std::uint16_t>(vtx.size());
            for (int k = 0; k < 4; ++k) {
                const float su = (k & 1) ? 0.5f : -0.5f, sv = (k & 2) ? 0.5f : -0.5f;
                vtx.push_back({{n[0] * 0.5f + u[0] * su + v[0] * sv, n[1] * 0.5f + u[1] * su + v[1] * sv, n[2] * 0.5f + u[2] * su + v[2] * sv},
                               {n[0], n[1], n[2]}});
            }
            const std::uint16_t q[6] = {0, 1, 2, 2, 1, 3};
            for (auto i : q) idx.push_back(static_cast<std::uint16_t>(base + i));
        }
        // Ornek 0: zemin (ust yuzu y=-0.5); ardindan sehir ornekleri.
        std::vector<InstGpu> ig;
        ig.reserve(inst.size() + 1);
        ig.push_back({{0.f, -200.5f, 0.f, 400.f}, {0.42f, 0.40f, 0.37f, 1.f}, 0.f});
        for (const auto& s : inst) ig.push_back({{s.pos[0], s.pos[1], s.pos[2], s.scale}, {s.color[0], s.color[1], s.color[2], s.color[3]}, s.spin});

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"INST", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INST", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INST", 2, DXGI_FORMAT_R32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        D3D11_BUFFER_DESC lbd{static_cast<UINT>(lights.size() * sizeof(PointLight)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0,
                              D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(PointLight)};
        D3D11_SUBRESOURCE_DATA lsd{lights.data(), 0, 0};
        static_assert(sizeof(PointLight) == 32, "HLSL PointLight ile ayni yerlesim");
        if (FAILED(make_buf(vtx.data(), static_cast<UINT>(vtx.size() * sizeof(Vtx)), D3D11_BIND_VERTEX_BUFFER, &vb_)) ||
            FAILED(make_buf(idx.data(), static_cast<UINT>(idx.size() * 2), D3D11_BIND_INDEX_BUFFER, &ib_)) ||
            FAILED(make_buf(ig.data(), static_cast<UINT>(ig.size() * sizeof(InstGpu)), D3D11_BIND_VERTEX_BUFFER, &instb_)) ||
            FAILED(dev_->CreateBuffer(&lbd, &lsd, &lightb_)) || FAILED(dev_->CreateShaderResourceView(lightb_.Get(), nullptr, &light_srv_)) ||
            FAILED(dev_->CreateVertexShader(g_scene_vs, sizeof g_scene_vs, nullptr, &vs_)) ||
            FAILED(dev_->CreatePixelShader(g_scene_ps_gbuffer, sizeof g_scene_ps_gbuffer, nullptr, &ps_gbuf_)) ||
            FAILED(dev_->CreatePixelShader(g_scene_ps_light, sizeof g_scene_ps_light, nullptr, &ps_light_)) ||
            FAILED(dev_->CreateInputLayout(layout, 5, g_scene_vs, sizeof g_scene_vs, &il_))) {
            err = "resource_create";
            return false;
        }
        if (!build_shared(c.seed, err)) return false;
        // Aydinlatma parcalari: max_tier isik sayisina gore; daha dusuk tier ilk parcalari kullanir.
        if (!make_light_chunks(light_draws(static_cast<std::uint32_t>(lights.size())), kLightsPerDraw, err)) return false;
        if (!create_targets(c.width, c.height, c.preview != nullptr, err)) return false;
        perspective(1.0f, static_cast<float>(width_) / static_cast<float>(height_), 0.5f, 600.f, proj_);
        return true;
    }

    // ---- Arena v1: cozunurlukten bagimsiz kaynaklar (ag, golgelendiriciler, isik listesi). Hedefler
    // rebuild_targets ile, adim ayarlari prepare_arena ile kurulur. ----
    bool build_arena(std::uint64_t seed, std::string& err) {
        const ArenaMesh mesh = arena_chevron_mesh();
        const auto lights = arena_lights(seed, kArenaMaxLights);
        static_assert(sizeof(ArenaVertex) == sizeof(Vtx), "kose yerlesimi: konum + normal");
        if (mesh.indices.empty() || lights.empty()) {
            err = "resource_create";
            return false;
        }
        a_index_count_ = static_cast<UINT>(mesh.indices.size());
        const auto wall = srgb_to_linear(kArenaWallColor.rgb), line = srgb_to_linear(kArenaLineColor.rgb), fog = srgb_to_linear(kArenaFogColor.rgb);
        for (int k = 0; k < 3; ++k) a_wall_[k] = wall[static_cast<std::size_t>(k)], a_line_[k] = line[static_cast<std::size_t>(k)], a_fog_[k] = fog[static_cast<std::size_t>(k)];
        const D3D11_INPUT_ELEMENT_DESC il_inst[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"INST", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INST", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INST", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INST", 3, DXGI_FORMAT_R32G32B32A32_UINT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        D3D11_BUFFER_DESC fbd{sizeof(ArenaFrameCb), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        D3D11_BUFFER_DESC obd{sizeof(ArenaObjectGpu), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        D3D11_BUFFER_DESC lbd{static_cast<UINT>(lights.size() * sizeof(PointLight)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0,
                              D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(PointLight)};
        D3D11_SUBRESOURCE_DATA lsd{lights.data(), 0, 0};
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;  // tunel icten gorulur
        rd.DepthClipEnable = TRUE;
        if (FAILED(make_buf(mesh.vertices.data(), static_cast<UINT>(mesh.vertices.size() * sizeof(ArenaVertex)), D3D11_BIND_VERTEX_BUFFER, &a_vb_)) ||
            FAILED(make_buf(mesh.indices.data(), static_cast<UINT>(mesh.indices.size() * 2), D3D11_BIND_INDEX_BUFFER, &a_ib_)) ||
            FAILED(dev_->CreateBuffer(&fbd, nullptr, &a_frame_cb_)) || FAILED(dev_->CreateBuffer(&obd, nullptr, &a_obj_cb_)) ||
            FAILED(dev_->CreateBuffer(&lbd, &lsd, &a_lightb_)) || FAILED(dev_->CreateShaderResourceView(a_lightb_.Get(), nullptr, &a_light_srv_)) ||
            FAILED(dev_->CreateVertexShader(g_arena_vs_inst, sizeof g_arena_vs_inst, nullptr, &a_vs_inst_)) ||
            FAILED(dev_->CreateVertexShader(g_arena_vs_object, sizeof g_arena_vs_object, nullptr, &a_vs_obj_)) ||
            FAILED(dev_->CreateVertexShader(g_arena_vs_tunnel, sizeof g_arena_vs_tunnel, nullptr, &a_vs_tunnel_)) ||
            FAILED(dev_->CreatePixelShader(g_arena_ps_gbuffer, sizeof g_arena_ps_gbuffer, nullptr, &a_ps_gbuf_)) ||
            FAILED(dev_->CreatePixelShader(g_arena_ps_light, sizeof g_arena_ps_light, nullptr, &a_ps_light_)) ||
            FAILED(dev_->CreateInputLayout(il_inst, 6, g_arena_vs_inst, sizeof g_arena_vs_inst, &a_il_inst_)) ||
            FAILED(dev_->CreateInputLayout(il_inst, 2, g_arena_vs_object, sizeof g_arena_vs_object, &a_il_obj_)) ||
            FAILED(dev_->CreateRasterizerState(&rd, &a_rs_nocull_))) {
            err = "resource_create";
            return false;
        }
        return build_shared(seed, err);
    }

    // Adim ayarlari: aydinlatma parcalari (parca boyu piksel sayisiyla kuculur), ornek tamponu kapasitesi,
    // bloom hedeflerinin sifirlanmasi (bloom kapaliyken tonemap sifir okur) ve izdusum.
    bool prepare_arena(const SceneConfig& c, std::string& err) {
        const std::uint64_t pixels = std::uint64_t{width_} * height_;
        const std::uint32_t chunk = arena_light_chunk(pixels), draws = arena_light_draws(c.arena_lights, pixels);
        if (chunk != a_chunk_ || chunk_cb_.size() != draws) {
            a_chunk_ = 0;
            if (!make_light_chunks(draws, chunk, err)) return false;
            a_chunk_ = chunk;
        }
        if (c.object_draws > a_inst_capacity_) {
            a_instb_.Reset();
            a_inst_capacity_ = 0;
            D3D11_BUFFER_DESC ibd{static_cast<UINT>(c.object_draws * sizeof(ArenaObjectGpu)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER,
                                  D3D11_CPU_ACCESS_WRITE, 0, 0};
            if (FAILED(dev_->CreateBuffer(&ibd, nullptr, &a_instb_))) {
                err = "resource_create";
                return false;
            }
            a_inst_capacity_ = c.object_draws;
        }
        const float zero[4] = {0, 0, 0, 0};
        for (auto& r : bloom_rtv_) ctx_->ClearRenderTargetView(r.Get(), zero);
        perspective(2.f * std::atan(kArenaTanHalfY), static_cast<float>(width_) / static_cast<float>(height_), kArenaNear, kArenaFarPlane,
                    proj_);
        return true;
    }

    // Tek cihazda cozunurluk degisimi: eski hedefler birakilir, video bellegi butcesi denetlenir, yenileri kurulur.
    // Butce yetmezse "vram_budget", olusturulamazsa "target_create"; iki durumda da hedef kalmaz ama cihaz ve
    // diger kaynaklar sonraki deneme icin gecerlidir. Cihaz kaybedildiyse "device_removed" (oturum kullanilamaz).
    bool rebuild_targets(std::uint32_t w, std::uint32_t h, std::string& err) {
        // Kullanim, eski hedefler henuz serbest kalmadan okunur ve onlarin payi dusulur (serbest birakma
        // surucude gecikebilir; boylece sonuc zamanlamaya bagli olmaz).
        std::uint64_t budget = 0, usage = 0;
        video_memory(budget, usage);
        const std::uint64_t held = g_tex_[0] ? target_set_bytes(width_, height_) : 0;
        usage -= std::min(usage, held);
        release_targets();
        ctx_->ClearState();
        ctx_->Flush();
        if (budget_limit_ && (budget == 0 || budget_limit_ < budget)) budget = budget_limit_;
        if (targets_fit_budget(budget, usage, target_set_bytes(w, h)) == BudgetCheck::Exceeds) {
            err = "vram_budget";
            return false;
        }
        if (!create_targets(w, h, true, err)) {
            release_targets();
            // Cihaz kaybinda her olusturma basarisiz olur: bu "bu cozunurluk olculemedi" degil, kosuyu bitiren
            // hatadir (yoksa kalan butun cozunurlukler yanlislikla "hedef olusturulamadi" diye isaretlenirdi).
            if (device_lost()) err = "device_removed";
            return false;
        }
        return true;
    }
    void limit_budget(std::uint64_t bytes) noexcept { budget_limit_ = bytes; }
    // Bagdastiricinin yerel bellek butcesi ve bu surecin kullanimi; okunamazsa ikisi de 0 (bilinmiyor).
    void video_memory(std::uint64_t& budget, std::uint64_t& usage) const {
        budget = usage = 0;
        DXGI_QUERY_VIDEO_MEMORY_INFO m{};
        if (adapter3_ && SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m))) budget = m.Budget, usage = m.CurrentUsage;
    }

    bool has_ts() const noexcept { return have_ts_; }
    bool has_fence() const noexcept { return fence_ != nullptr; }

    // GPU bosta olana kadar bekler (yalniz hazirlik sonunda ve dogrulama oncesi).
    bool idle(std::string& err) {
        ComPtr<ID3D11Query> q;
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
        if (FAILED(dev_->CreateQuery(&qd, &q))) {
            err = "query_create";
            return false;
        }
        ctx_->End(q.Get());
        const auto t0 = Clock::now();
        HRESULT h;
        while ((h = ctx_->GetData(q.Get(), nullptr, 0, 0)) == S_FALSE) {
            if (ms_since(t0) > kFrameTimeoutMs) {
                err = device_lost() ? "device_removed" : "gpu_timeout";
                return false;
            }
            SwitchToThread();
        }
        if (FAILED(h)) {
            err = "device_removed";
            return false;
        }
        return true;
    }

    // Bir kareyi kaydeder. ts_slot < 0: zaman damgasi yok (dogrulama karesi).
    // preview_slot >= 0: kare sonunda onizleme kucultme gecisi + hazirlama yuvasina kopya (zaman damgasi parantezi icinde).
    void draw(const SceneConfig& c, Tier tier, std::uint32_t frame, int ts_slot, int preview_slot = -1) {
        const auto tp = tier_params(tier);
        const auto cam = camera_at(c, frame);
        float view[16], vpm[16];
        look_at(cam.eye.data(), cam.target.data(), view);
        mul(view, proj_, vpm);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            FrameCb fc{};
            transpose(vpm, fc.view_proj);
            fc.eye_frame[0] = cam.eye[0], fc.eye_frame[1] = cam.eye[1], fc.eye_frame[2] = cam.eye[2];
            fc.eye_frame[3] = static_cast<float>(frame);
            const float sd[3] = {-0.4f, -0.8f, -0.45f};
            const float sl = std::sqrt(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
            fc.sun_dir[0] = sd[0] / sl, fc.sun_dir[1] = sd[1] / sl, fc.sun_dir[2] = sd[2] / sl, fc.sun_dir[3] = 0.55f;
            fc.counts[0] = tp.lights;
            fc.counts[1] = tp.blur_radius;
            fc.misc[0] = 64.f / static_cast<float>(tp.lights);  // toplam isik enerjisi tier'dan bagimsiz kalsin
            fc.misc[1] = 0.8f;
            fc.misc[2] = 1.0f;
            fc.misc[3] = 1.f / static_cast<float>(height_);
            std::memcpy(m.pData, &fc, sizeof fc);
            ctx_->Unmap(cb_.Get(), 0);
        }
        if (ts_slot >= 0) {
            ctx_->Begin(disj_[ts_slot].Get());
            ctx_->End(ts0_[ts_slot].Get());
        }
        const D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ID3D11Buffer* cbs[2] = {cb_.Get(), pass_h_.Get()};
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 2, cbs);
        // 1) G-buffer
        const float zero[4] = {0, 0, 0, 0};
        for (auto& r : g_rtv_) ctx_->ClearRenderTargetView(r.Get(), zero);
        ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        ID3D11RenderTargetView* g[3] = {g_rtv_[0].Get(), g_rtv_[1].Get(), g_rtv_[2].Get()};
        ctx_->OMSetRenderTargets(3, g, dsv_.Get());
        ID3D11Buffer* vbs[2] = {vb_.Get(), instb_.Get()};
        const UINT strides[2] = {sizeof(Vtx), sizeof(InstGpu)}, offs[2] = {0, 0};
        ctx_->IASetInputLayout(il_.Get());
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 2, vbs, strides, offs);
        ctx_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx_->VSSetShader(vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_gbuf_.Get(), nullptr, 0);
        ctx_->PSSetShaderResources(0, 1, noise_srv_.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, smp_.GetAddressOf());
        ctx_->DrawIndexedInstanced(36, tp.instances + 1, 0, 0, 0);  // +1: zemin
        // 2) Aydinlatma (tam ekran)
        ctx_->OMSetRenderTargets(1, hdr_rtv_.GetAddressOf(), nullptr);
        ctx_->IASetInputLayout(nullptr);
        ctx_->VSSetShader(vs_fs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_light_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* ls[4] = {g_srv_[0].Get(), g_srv_[1].Get(), g_srv_[2].Get(), light_srv_.Get()};
        ctx_->PSSetShaderResources(0, 4, ls);
        // TDR guvenligi: en cok kLightsPerDraw isiklik cizimler; ilki yazar, sonrakiler toplar.
        const std::uint32_t draws = std::min<std::uint32_t>(light_draws(tp.lights), static_cast<std::uint32_t>(chunk_cb_.size()));
        for (std::uint32_t i = 0; i < draws; ++i) {
            ctx_->OMSetBlendState(i == 0 ? nullptr : add_blend_.Get(), nullptr, 0xFFFFFFFFu);
            ctx_->PSSetConstantBuffers(2, 1, chunk_cb_[i].GetAddressOf());
            ctx_->Draw(3, 0);
        }
        ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        unbind_srvs();
        bloom_and_tonemap(true);
        if (preview_slot >= 0) preview_copy(static_cast<std::uint32_t>(preview_slot));
        if (ts_slot >= 0) {
            ctx_->End(ts1_[ts_slot].Get());
            ctx_->End(disj_[ts_slot].Get());
        }
    }

    // ---- Arena v1: bir kare ----
    struct ArenaDraw {
        std::uint32_t frame = 0;                  // kare indeksi (golgelendirici: donus ve akis)
        std::int32_t camera_z = 0;                // Q16.16, [0, kTunnelLength)
        const ArenaObjectGpu* objects = nullptr;  // cizim listesi (onden arkaya)
        std::uint32_t count = 0;
        std::uint32_t flow_mask = kArenaNoFlowMask;
        bool upload = true;  // Instanced: ornek tamponu bu karede yenilensin mi (sim kapaliyken bir kez yuklenir)
    };

    // Kareyi kaydeder. PerObject: nesne basina sabit tampon (Map WRITE_DISCARD) + DrawIndexed. Instanced: tek
    // DrawIndexedInstanced (sim acikken ornek tamponu her kare yenilenir). ts_slot < 0: zaman damgasi yok.
    void draw_arena(const SceneConfig& c, const ArenaDraw& d, int ts_slot, int preview_slot = -1) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            FrameCb fc{};
            transpose(proj_, fc.view_proj);  // kamera orijinde, +z yonune bakar: gorus matrisi birim
            fc.eye_frame[3] = static_cast<float>(d.frame);
            const float fl = std::sqrt(kArenaFill[0] * kArenaFill[0] + kArenaFill[1] * kArenaFill[1] + kArenaFill[2] * kArenaFill[2]);
            fc.sun_dir[0] = kArenaFill[0] / fl, fc.sun_dir[1] = kArenaFill[1] / fl, fc.sun_dir[2] = kArenaFill[2] / fl;
            fc.sun_dir[3] = kArenaFill[3];
            fc.counts[0] = c.arena_lights;
            fc.counts[1] = c.arena_bloom ? kArenaBlurRadius : 0u;
            fc.misc[0] = c.arena_lights ? kArenaLightEnergy / static_cast<float>(c.arena_lights) : 0.f;
            fc.misc[1] = kArenaBloomThreshold;
            fc.misc[2] = kArenaExposure;
            fc.misc[3] = 1.f / static_cast<float>(height_);
            std::memcpy(m.pData, &fc, sizeof fc);
            ctx_->Unmap(cb_.Get(), 0);
        }
        if (SUCCEEDED(ctx_->Map(a_frame_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            ArenaFrameCb ac{};
            const auto cz = static_cast<std::uint32_t>(d.camera_z);
            const std::uint32_t period_q = static_cast<std::uint32_t>(kArenaLightPeriod) << kSimFixShift;
            ac.u[0] = d.frame;
            ac.u[1] = d.flow_mask;
            ac.u[2] = static_cast<std::uint32_t>(kCameraNear);  // ayiklama ile ayni yakin sinir
            ac.f[0] = static_cast<float>(cz) / 65536.f;
            ac.f[1] = static_cast<float>(cz % period_q) / 65536.f;
            ac.f[2] = kArenaTunnelHalf;
            ac.f[3] = kArenaFar;
            ac.g[0] = kArenaLightPeriod, ac.g[1] = kArenaLightBack, ac.g[2] = kArenaEmissiveGain, ac.g[3] = kArenaPanel;
            for (int k = 0; k < 3; ++k) ac.wall[k] = a_wall_[k], ac.line[k] = a_line_[k], ac.fog[k] = a_fog_[k];
            ac.line[3] = kArenaLineEmissive;
            ac.fog[3] = kArenaObjectEmissive;
            std::memcpy(m.pData, &ac, sizeof ac);
            ctx_->Unmap(a_frame_cb_.Get(), 0);
        }
        if (ts_slot >= 0) {
            ctx_->Begin(disj_[ts_slot].Get());
            ctx_->End(ts0_[ts_slot].Get());
        }
        const D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ID3D11Buffer* frame_cbs[2] = {cb_.Get(), pass_h_.Get()};
        ID3D11Buffer* arena_cbs[2] = {a_frame_cb_.Get(), a_obj_cb_.Get()};
        ctx_->VSSetConstantBuffers(0, 1, frame_cbs);
        ctx_->VSSetConstantBuffers(3, 2, arena_cbs);
        ctx_->PSSetConstantBuffers(0, 2, frame_cbs);
        ctx_->PSSetConstantBuffers(3, 1, arena_cbs);
        // 1) G-buffer: nesneler (onden arkaya), sonra tunel (her pikseli kaplar)
        const float zero[4] = {0, 0, 0, 0};
        for (auto& r : g_rtv_) ctx_->ClearRenderTargetView(r.Get(), zero);
        ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        ID3D11RenderTargetView* g[3] = {g_rtv_[0].Get(), g_rtv_[1].Get(), g_rtv_[2].Get()};
        ctx_->OMSetRenderTargets(3, g, dsv_.Get());
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->PSSetShader(a_ps_gbuf_.Get(), nullptr, 0);
        ctx_->PSSetShaderResources(0, 1, noise_srv_.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, smp_.GetAddressOf());
        if (d.count && d.objects) {
            ctx_->IASetIndexBuffer(a_ib_.Get(), DXGI_FORMAT_R16_UINT, 0);
            if (c.draw_mode == DrawMode::PerObject) {
                const UINT stride = sizeof(ArenaVertex), off = 0;
                ctx_->IASetInputLayout(a_il_obj_.Get());
                ctx_->IASetVertexBuffers(0, 1, a_vb_.GetAddressOf(), &stride, &off);
                ctx_->VSSetShader(a_vs_obj_.Get(), nullptr, 0);
                for (std::uint32_t i = 0; i < d.count; ++i) {
                    if (FAILED(ctx_->Map(a_obj_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) break;  // cihaz kaybi: bekleme yakalar
                    std::memcpy(m.pData, &d.objects[i], sizeof(ArenaObjectGpu));
                    ctx_->Unmap(a_obj_cb_.Get(), 0);
                    ctx_->DrawIndexed(a_index_count_, 0, 0);
                }
            } else if (a_instb_) {
                const std::uint32_t n = std::min(d.count, a_inst_capacity_);
                if (d.upload && SUCCEEDED(ctx_->Map(a_instb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    std::memcpy(m.pData, d.objects, std::size_t{n} * sizeof(ArenaObjectGpu));
                    ctx_->Unmap(a_instb_.Get(), 0);
                }
                ID3D11Buffer* vbs[2] = {a_vb_.Get(), a_instb_.Get()};
                const UINT strides[2] = {sizeof(ArenaVertex), sizeof(ArenaObjectGpu)}, offs[2] = {0, 0};
                ctx_->IASetInputLayout(a_il_inst_.Get());
                ctx_->IASetVertexBuffers(0, 2, vbs, strides, offs);
                ctx_->VSSetShader(a_vs_inst_.Get(), nullptr, 0);
                ctx_->DrawIndexedInstanced(a_index_count_, n, 0, 0, 0);
            }
        }
        ctx_->IASetInputLayout(nullptr);
        ctx_->VSSetShader(a_vs_tunnel_.Get(), nullptr, 0);
        ctx_->RSSetState(a_rs_nocull_.Get());
        ctx_->Draw(kArenaTunnelVertices, 0);
        ctx_->RSSetState(nullptr);
        // 2) Aydinlatma (tam ekran): parca boyu piksel sayisina gore (prepare_arena)
        ctx_->OMSetRenderTargets(1, hdr_rtv_.GetAddressOf(), nullptr);
        ctx_->VSSetShader(vs_fs_.Get(), nullptr, 0);
        ctx_->PSSetShader(a_ps_light_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* ls[4] = {g_srv_[0].Get(), g_srv_[1].Get(), g_srv_[2].Get(), a_light_srv_.Get()};
        ctx_->PSSetShaderResources(0, 4, ls);
        const std::uint32_t draws = std::min<std::uint32_t>(arena_light_draws(c.arena_lights, std::uint64_t{width_} * height_),
                                                            static_cast<std::uint32_t>(chunk_cb_.size()));
        for (std::uint32_t i = 0; i < draws; ++i) {
            ctx_->OMSetBlendState(i == 0 ? nullptr : add_blend_.Get(), nullptr, 0xFFFFFFFFu);
            ctx_->PSSetConstantBuffers(2, 1, chunk_cb_[i].GetAddressOf());
            ctx_->Draw(3, 0);
        }
        ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        unbind_srvs();
        bloom_and_tonemap(c.arena_bloom);
        if (preview_slot >= 0) preview_copy(static_cast<std::uint32_t>(preview_slot));
        if (ts_slot >= 0) {
            ctx_->End(ts1_[ts_slot].Get());
            ctx_->End(disj_[ts_slot].Get());
        }
    }

    // Son cizilen LDR karesinin tamami (RGBA8). GPU'yu bekler; yalniz dogrulama / deneme icin.
    bool read_frame(std::vector<std::uint8_t>& rgba, std::uint32_t& w, std::uint32_t& h) {
        if (!ldr_tex_ || width_ == 0 || height_ == 0) return false;
        if (!full_staging_) {
            D3D11_TEXTURE2D_DESC sd{width_, height_, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
            if (FAILED(dev_->CreateTexture2D(&sd, nullptr, &full_staging_))) return false;
        }
        ctx_->CopyResource(full_staging_.Get(), ldr_tex_.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx_->Map(full_staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        const std::size_t row = std::size_t{width_} * 4;
        rgba.resize(row * height_);
        for (std::uint32_t y = 0; y < height_; ++y)
            std::memcpy(rgba.data() + row * y, static_cast<const unsigned char*>(m.pData) + std::size_t{m.RowPitch} * y, row);
        ctx_->Unmap(full_staging_.Get(), 0);
        w = width_;
        h = height_;
        return true;
    }

    // ---- Canli onizleme (A4) ----
    // Onizleme hedefi + hazirlama halkasi guncel hedef boyutuna gore kurulur (boyut degismediyse hicbir sey yapmaz).
    // false: onizleme uretilemez (kosu etkilenmez; sonucta preview = false).
    bool ensure_preview() {
        if (!preview_capable_ || !ldr_srv_ || width_ == 0 || height_ == 0) return false;
        const PreviewSize want = preview_size(width_, height_);
        if (preview_tex_ && want.width == pw_ && want.height == ph_) return true;
        release_preview();
        const std::uint32_t sx = (width_ + want.width - 1) / want.width, sy = (height_ + want.height - 1) / want.height;
        const std::uint32_t taps = std::clamp<std::uint32_t>((std::max(sx, sy) + 1) / 2, 1, 4);  // her tap bilineer (2x2 texel)
        const PreviewCb pc{{1.f / static_cast<float>(want.width), 1.f / static_cast<float>(want.height), static_cast<float>(taps), 0.f}};
        D3D11_TEXTURE2D_DESC td{want.width, want.height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        D3D11_TEXTURE2D_DESC sd{want.width, want.height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
        bool ok = SUCCEEDED(make_buf(&pc, sizeof pc, D3D11_BIND_CONSTANT_BUFFER, &preview_cb_)) &&
                  SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &preview_tex_)) &&
                  SUCCEEDED(dev_->CreateRenderTargetView(preview_tex_.Get(), nullptr, &preview_rtv_));
        for (auto& st : preview_staging_) ok = ok && SUCCEEDED(dev_->CreateTexture2D(&sd, nullptr, &st));
        if (!ok) {
            release_preview();
            return false;
        }
        pw_ = want.width;
        ph_ = want.height;
        return true;
    }

    // LDR hedefinden onizleme boyutuna kucultme (BGRA sirasi) + `slot` hazirlama dokusuna kopya. Beklemez.
    void preview_copy(std::uint32_t slot) {
        if (!preview_tex_ || slot >= kPreviewSlots) return;
        const D3D11_VIEWPORT vp{0, 0, static_cast<float>(pw_), static_cast<float>(ph_), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ctx_->OMSetRenderTargets(1, preview_rtv_.GetAddressOf(), nullptr);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(vs_fs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_preview_.Get(), nullptr, 0);
        ctx_->PSSetConstantBuffers(5, 1, preview_cb_.GetAddressOf());
        ctx_->PSSetSamplers(1, 1, smp_linear_.GetAddressOf());
        ctx_->PSSetShaderResources(0, 1, ldr_srv_.GetAddressOf());
        ctx_->Draw(3, 0);
        unbind_srvs();
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        ctx_->CopyResource(preview_staging_[slot].Get(), preview_tex_.Get());
    }

    enum class PreviewRead { Delivered, Busy, Failed };
    // `slot` hazirlama dokusunu okur ve geri cagriya iletir. wait == false: D3D11_MAP_FLAG_DO_NOT_WAIT; GPU kopyayi
    // henuz bitirmediyse Busy doner (olcum dongusu beklemez). wait == true yalniz adim sonunda (olcum disi) kullanilir.
    PreviewRead preview_read(std::uint32_t slot, bool wait, PreviewFrame f, const PreviewCallback& cb) {
        if (!preview_tex_ || slot >= kPreviewSlots) return PreviewRead::Failed;
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = ctx_->Map(preview_staging_[slot].Get(), 0, D3D11_MAP_READ, wait ? 0u : static_cast<UINT>(D3D11_MAP_FLAG_DO_NOT_WAIT), &m);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return PreviewRead::Busy;
        if (FAILED(hr) || !m.pData) return PreviewRead::Failed;  // cihaz kaybi: geri basinc beklemesi yakalar
        f.pixels = static_cast<const std::uint8_t*>(m.pData);
        f.stride = m.RowPitch;
        f.width = pw_;
        f.height = ph_;
        f.source_width = width_;
        f.source_height = height_;
        bool delivered = true;
        try {
            cb(f);
        } catch (...) {
            delivered = false;  // tuketici hatasi olcumu durdurmaz; kare iletilmedi sayilir
        }
        ctx_->Unmap(preview_staging_[slot].Get(), 0);
        return delivered ? PreviewRead::Delivered : PreviewRead::Failed;
    }
    void flush() { ctx_->Flush(); }

    // Kare sonu: cit/olay isareti + Flush (Present karsiligi; komutlar GPU'ya gider, beklenmez).
    // Gonderim sirasi cihaz omru boyunca artar (kalibrasyonda tier'lar arasi cit degeri geri gitmez).
    std::uint64_t submitted() const noexcept { return seq_; }
    void submit() {
        const std::uint64_t id = seq_++;
        if (fence_) ctx4_->Signal(fence_.Get(), id + 1);
        else ctx_->End(ev_q_[id % kRingSlots].Get());
        ctx_->Flush();
    }

    // Gonderim `frame` (submitted() sirasi) GPU'da bitene kadar bekler (geri basinc; kare basi tam bekleme degil).
    bool wait_done(std::uint64_t frame, const std::atomic<bool>& cancel, bool& cancelled, std::string& err) {
        const auto t0 = Clock::now();
        if (fence_) {
            const UINT64 v = frame + 1;
            for (;;) {
                const UINT64 done = fence_->GetCompletedValue();
                if (done == UINT64_MAX) {  // cihaz kaybi
                    err = "device_removed";
                    return false;
                }
                if (done >= v) return true;
                if (cancel.load(std::memory_order_relaxed)) {
                    cancelled = true;
                    return false;
                }
                if (ms_since(t0) > kFrameTimeoutMs) {
                    err = device_lost() ? "device_removed" : "gpu_timeout";
                    return false;
                }
                if (SUCCEEDED(fence_->SetEventOnCompletion(v, ev_.h))) WaitForSingleObject(ev_.h, 50);
                else SwitchToThread();
            }
        }
        ID3D11Query* q = ev_q_[frame % kRingSlots].Get();
        std::uint32_t spins = 0;
        for (;;) {
            const HRESULT h = ctx_->GetData(q, nullptr, 0, 0);
            if (h == S_OK) return true;
            if (FAILED(h)) {
                err = "device_removed";
                return false;
            }
            if ((++spins & 63) == 0) {
                if (cancel.load(std::memory_order_relaxed)) {
                    cancelled = true;
                    return false;
                }
                if (ms_since(t0) > kFrameTimeoutMs) {
                    err = device_lost() ? "device_removed" : "gpu_timeout";
                    return false;
                }
            }
            SwitchToThread();
        }
    }

    // Halkadaki hazir zaman damgalarini okur. block=false: ilk hazir olmayanda doner (bloklamaz).
    void poll(GpuQueryRing& ring, bool block) {
        const UINT flags = block ? 0u : static_cast<UINT>(D3D11_ASYNC_GETDATA_DONOTFLUSH);
        while (ring.pending() > 0) {
            const auto s = ring.oldest_slot();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            HRESULT h = ctx_->GetData(disj_[s].Get(), &dj, sizeof dj, flags);
            if (h == S_FALSE) {
                if (!block) return;
                const auto t0 = Clock::now();
                while ((h = ctx_->GetData(disj_[s].Get(), &dj, sizeof dj, 0)) == S_FALSE && ms_since(t0) < 2000.0) SwitchToThread();
            }
            UINT64 a = 0, b = 0;
            const bool ok = h == S_OK && ctx_->GetData(ts0_[s].Get(), &a, sizeof a, flags) == S_OK &&
                            ctx_->GetData(ts1_[s].Get(), &b, sizeof b, flags) == S_OK;
            ring.resolve_oldest(dj.Disjoint != FALSE, dj.Frequency, ok, a, b);
        }
    }

    // Dogrulama karesinin merkezinden 16x16 geri okuma (sabit temizleme rengi degil, gercekten cizildi).
    bool checksum(std::uint64_t& out) {
        const D3D11_BOX box{width_ / 2 - 8, height_ / 2 - 8, 0, width_ / 2 + 8, height_ / 2 + 8, 1};
        ctx_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, ldr_tex_.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        std::uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < 16; ++y) {
            const auto* row = static_cast<const unsigned char*>(m.pData) + y * m.RowPitch;
            for (int x = 0; x < 64; ++x) h = (h ^ row[x]) * 1099511628211ull;
        }
        out = h;
        ctx_->Unmap(staging_.Get(), 0);
        return true;
    }

private:
    bool device_lost() const { return dev_ && dev_->GetDeviceRemovedReason() != S_OK; }
    void unbind_srvs() {
        ID3D11ShaderResourceView* none[4] = {};
        ctx_->PSSetShaderResources(0, 4, none);
    }
    HRESULT make_buf(const void* data, UINT bytes, UINT bind, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC bd{bytes, D3D11_USAGE_IMMUTABLE, bind, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
        return dev_->CreateBuffer(&bd, &sd, out);
    }

    // 3) Bloom: yatay (parlak kisim) + dikey; 4) Tonemap -> LDR. bloom == false: bulaniklik gecisleri atlanir
    // (bloom hedefi sifir; yalniz Arena, prepare_arena temizler).
    void bloom_and_tonemap(bool bloom) {
        if (bloom) {
            ctx_->OMSetRenderTargets(1, bloom_rtv_[0].GetAddressOf(), nullptr);
            ctx_->PSSetShader(ps_blur_.Get(), nullptr, 0);
            ctx_->PSSetConstantBuffers(1, 1, pass_h_.GetAddressOf());
            ctx_->PSSetShaderResources(0, 1, hdr_srv_.GetAddressOf());
            ctx_->Draw(3, 0);
            unbind_srvs();
            ctx_->OMSetRenderTargets(1, bloom_rtv_[1].GetAddressOf(), nullptr);
            ctx_->PSSetConstantBuffers(1, 1, pass_v_.GetAddressOf());
            ctx_->PSSetShaderResources(0, 1, bloom_srv_[0].GetAddressOf());
            ctx_->Draw(3, 0);
            unbind_srvs();
        }
        ctx_->OMSetRenderTargets(1, ldr_rtv_.GetAddressOf(), nullptr);
        ctx_->PSSetShader(ps_tone_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* ts[2] = {hdr_srv_.Get(), bloom_srv_[1].Get()};
        ctx_->PSSetShaderResources(0, 2, ts);
        ctx_->Draw(3, 0);
        unbind_srvs();
    }

    // Iki sahnenin ortak kaynaklari: kare sabit tamponu, bloom gecis tamponlari, tam ekran / bloom / tonemap
    // golgelendiricileri, gurultu dokusu + sarmal ornekleyici, toplamsal karisim, sorgular ve cit.
    bool build_shared(std::uint64_t seed, std::string& err) {
        D3D11_BUFFER_DESC cbd{sizeof(FrameCb), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        const PassCb ph{{1, 0}, {1, 0}}, pv{{0, 1}, {0, 0}};
        if (FAILED(make_buf(&ph, sizeof ph, D3D11_BIND_CONSTANT_BUFFER, &pass_h_)) ||
            FAILED(make_buf(&pv, sizeof pv, D3D11_BIND_CONSTANT_BUFFER, &pass_v_)) || FAILED(dev_->CreateBuffer(&cbd, nullptr, &cb_)) ||
            FAILED(dev_->CreateVertexShader(g_scene_vs_fullscreen, sizeof g_scene_vs_fullscreen, nullptr, &vs_fs_)) ||
            FAILED(dev_->CreatePixelShader(g_scene_ps_blur, sizeof g_scene_ps_blur, nullptr, &ps_blur_)) ||
            FAILED(dev_->CreatePixelShader(g_scene_ps_tonemap, sizeof g_scene_ps_tonemap, nullptr, &ps_tone_))) {
            err = "resource_create";
            return false;
        }
        // Onizleme (A4): kucultme golgelendiricisi + dogrusal ornekleyici. Kurulamazsa yalniz onizleme kapanir.
        {
            D3D11_SAMPLER_DESC ld{};
            ld.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            ld.AddressU = ld.AddressV = ld.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            ld.MaxLOD = D3D11_FLOAT32_MAX;
            preview_capable_ = SUCCEEDED(dev_->CreatePixelShader(g_scene_ps_preview, sizeof g_scene_ps_preview, nullptr, &ps_preview_)) &&
                               SUCCEEDED(dev_->CreateSamplerState(&ld, &smp_linear_));
        }
        // Gurultu dokusu + sarmal ornekleyici
        {
            const auto mips = make_noise_mips(seed);
            D3D11_TEXTURE2D_DESC nd{kNoiseSize, kNoiseSize, static_cast<UINT>(mips.size()), 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                                    D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            std::vector<D3D11_SUBRESOURCE_DATA> init(mips.size());
            UINT sz = kNoiseSize;
            for (std::size_t i = 0; i < mips.size(); ++i, sz = std::max(1u, sz / 2)) init[i] = {mips[i].data(), sz * 4, 0};
            ComPtr<ID3D11Texture2D> nt;
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_ANISOTROPIC;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.MaxAnisotropy = 8;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(dev_->CreateTexture2D(&nd, init.data(), &nt)) || FAILED(dev_->CreateShaderResourceView(nt.Get(), nullptr, &noise_srv_)) ||
                FAILED(dev_->CreateSamplerState(&sd, &smp_))) {
                err = "resource_create";
                return false;
            }
        }
        // Aydinlatma parcalari icin toplamsal karisim (parca > 0)
        {
            D3D11_BLEND_DESC bd{};
            auto& rt = bd.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = rt.DestBlend = D3D11_BLEND_ONE;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D11_BLEND_ONE;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(dev_->CreateBlendState(&bd, &add_blend_))) {
                err = "resource_create";
                return false;
            }
        }
        // Sorgular: kare basina disjoint + iki zaman damgasi (halka) ve kuyruk icin cit ya da olay.
        D3D11_QUERY_DESC qdj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, qts{D3D11_QUERY_TIMESTAMP, 0}, qev{D3D11_QUERY_EVENT, 0};
        have_ts_ = true;
        for (std::uint32_t i = 0; i < kRingSlots; ++i) {
            if (FAILED(dev_->CreateQuery(&qev, &ev_q_[i]))) {
                err = "query_create";
                return false;
            }
            have_ts_ = have_ts_ && SUCCEEDED(dev_->CreateQuery(&qdj, &disj_[i])) && SUCCEEDED(dev_->CreateQuery(&qts, &ts0_[i])) &&
                       SUCCEEDED(dev_->CreateQuery(&qts, &ts1_[i]));
        }
        ComPtr<ID3D11Device5> dev5;
        if (SUCCEEDED(dev_.As(&dev5)) && SUCCEEDED(ctx_.As(&ctx4_)) &&
            SUCCEEDED(dev5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
            if (!ev_.h) ev_.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // yeniden kurulumda (open tekrar denenirse) sizinti yok
            if (!ev_.h) fence_.Reset();
        } else {
            fence_.Reset();
        }
        return true;
    }

    // Aydinlatma parcasi sabit tamponlari: `draws` parca, parca basina en cok `per_draw` isik; ilki taban parca.
    bool make_light_chunks(std::uint32_t draws, std::uint32_t per_draw, std::string& err) {
        chunk_cb_.clear();
        chunk_cb_.resize(draws);
        for (std::uint32_t i = 0; i < draws; ++i) {
            const LightChunkCb lc{{i * per_draw, (i + 1) * per_draw, i == 0 ? 1u : 0u, 0u}};
            if (FAILED(make_buf(&lc, sizeof lc, D3D11_BIND_CONSTANT_BUFFER, &chunk_cb_[i]))) {
                chunk_cb_.clear();
                err = "resource_create";
                return false;
            }
        }
        return true;
    }

    // Hedefler: G-buffer (albedo RGBA8, normal RGBA16F, konum RGBA32F), derinlik, HDR, bloom x2, LDR (+16x16 geri okuma).
    // ldr_srv: LDR hedefi golgelendirici kaynagi olarak da baglanabilir (onizleme kucultme gecisi okur). scene_v3,
    // onizleme istenmedikce eski bayraklarla kurulur.
    bool create_targets(std::uint32_t w, std::uint32_t h, bool ldr_srv, std::string& err) {
        width_ = w;
        height_ = h;
        auto make_rt = [&](DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& t, ComPtr<ID3D11RenderTargetView>& rtv,
                           ComPtr<ID3D11ShaderResourceView>* srv) {
            D3D11_TEXTURE2D_DESC td{width_, height_, 1, 1, fmt, {1, 0}, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_RENDER_TARGET | (srv ? D3D11_BIND_SHADER_RESOURCE : 0u), 0, 0};
            return SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &t)) && SUCCEEDED(dev_->CreateRenderTargetView(t.Get(), nullptr, &rtv)) &&
                   (!srv || SUCCEEDED(dev_->CreateShaderResourceView(t.Get(), nullptr, &*srv)));
        };
        D3D11_TEXTURE2D_DESC dd{width_, height_, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
        D3D11_TEXTURE2D_DESC sdsc{16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
        ComPtr<ID3D11Texture2D> ds;
        // Not: `kosul ? &comptr : nullptr` YAZILMAZ; ComPtrRef bos isaretciyle kurulur ve donusumde onu cozer.
        ComPtr<ID3D11ShaderResourceView>* const ldr_view = ldr_srv ? std::addressof(ldr_srv_) : nullptr;
        if (!make_rt(DXGI_FORMAT_R8G8B8A8_UNORM, g_tex_[0], g_rtv_[0], &g_srv_[0]) ||
            !make_rt(DXGI_FORMAT_R16G16B16A16_FLOAT, g_tex_[1], g_rtv_[1], &g_srv_[1]) ||
            !make_rt(DXGI_FORMAT_R32G32B32A32_FLOAT, g_tex_[2], g_rtv_[2], &g_srv_[2]) ||
            !make_rt(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr_tex_, hdr_rtv_, &hdr_srv_) ||
            !make_rt(DXGI_FORMAT_R16G16B16A16_FLOAT, bloom_tex_[0], bloom_rtv_[0], &bloom_srv_[0]) ||
            !make_rt(DXGI_FORMAT_R16G16B16A16_FLOAT, bloom_tex_[1], bloom_rtv_[1], &bloom_srv_[1]) ||
            !make_rt(DXGI_FORMAT_R8G8B8A8_UNORM, ldr_tex_, ldr_rtv_, ldr_view) || FAILED(dev_->CreateTexture2D(&dd, nullptr, &ds)) ||
            FAILED(dev_->CreateDepthStencilView(ds.Get(), nullptr, &dsv_)) || FAILED(dev_->CreateTexture2D(&sdsc, nullptr, &staging_))) {
            err = "target_create";
            return false;
        }
        return true;
    }
    void release_targets() {
        for (int i = 0; i < 3; ++i) g_srv_[i].Reset(), g_rtv_[i].Reset(), g_tex_[i].Reset();
        for (int i = 0; i < 2; ++i) bloom_srv_[i].Reset(), bloom_rtv_[i].Reset(), bloom_tex_[i].Reset();
        hdr_srv_.Reset(), hdr_rtv_.Reset(), hdr_tex_.Reset();
        ldr_srv_.Reset(), ldr_rtv_.Reset(), ldr_tex_.Reset();
        release_preview();
        dsv_.Reset();
        staging_.Reset();
        full_staging_.Reset();
        width_ = height_ = 0;
    }

    void release_preview() {
        preview_cb_.Reset();
        preview_rtv_.Reset();
        preview_tex_.Reset();
        for (auto& st : preview_staging_) st.Reset();
        pw_ = ph_ = 0;
    }

    std::uint32_t width_ = 0, height_ = 0;
    std::uint64_t seq_ = 0;
    float proj_[16]{};
    EventHandle ev_;  // cihaz/citten once bildirilir -> en son kapanir
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11DeviceContext4> ctx4_;
    ComPtr<ID3D11Fence> fence_;
    ComPtr<ID3D11Buffer> vb_, ib_, instb_, cb_, pass_h_, pass_v_, lightb_;
    std::vector<ComPtr<ID3D11Buffer>> chunk_cb_;  // aydinlatma parcasi basina sabit tampon
    ComPtr<ID3D11BlendState> add_blend_;          // parca > 0: toplamsal
    ComPtr<ID3D11ShaderResourceView> light_srv_, noise_srv_;
    ComPtr<ID3D11SamplerState> smp_;
    ComPtr<ID3D11VertexShader> vs_, vs_fs_;
    ComPtr<ID3D11PixelShader> ps_gbuf_, ps_light_, ps_blur_, ps_tone_;
    ComPtr<ID3D11InputLayout> il_;
    ComPtr<ID3D11Texture2D> g_tex_[3], hdr_tex_, bloom_tex_[2], ldr_tex_, staging_;
    ComPtr<ID3D11RenderTargetView> g_rtv_[3], hdr_rtv_, bloom_rtv_[2], ldr_rtv_;
    ComPtr<ID3D11ShaderResourceView> g_srv_[3], hdr_srv_, bloom_srv_[2];
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11Query> ev_q_[kRingSlots], disj_[kRingSlots], ts0_[kRingSlots], ts1_[kRingSlots];
    bool have_ts_ = false;
    // Video bellegi butcesi (rebuild_targets)
    ComPtr<IDXGIAdapter3> adapter3_;
    std::uint64_t budget_limit_ = 0;
    // Arena v1
    ComPtr<ID3D11Buffer> a_vb_, a_ib_, a_instb_, a_frame_cb_, a_obj_cb_, a_lightb_;
    ComPtr<ID3D11ShaderResourceView> a_light_srv_;
    ComPtr<ID3D11VertexShader> a_vs_inst_, a_vs_obj_, a_vs_tunnel_;
    ComPtr<ID3D11PixelShader> a_ps_gbuf_, a_ps_light_;
    ComPtr<ID3D11InputLayout> a_il_inst_, a_il_obj_;
    ComPtr<ID3D11RasterizerState> a_rs_nocull_;
    ComPtr<ID3D11Texture2D> full_staging_;  // read_frame icin tam boy geri okuma (istenince kurulur)
    UINT a_index_count_ = 0;
    std::uint32_t a_inst_capacity_ = 0;  // ornek tamponu kapasitesi (nesne)
    std::uint32_t a_chunk_ = 0;          // chunk_cb_ hangi parca boyuyla kuruldu (0: Arena icin kurulmadi)
    float a_wall_[3]{}, a_line_[3]{}, a_fog_[3]{};  // palet (dogrusal)
    // Canli onizleme (A4)
    bool preview_capable_ = false;
    ComPtr<ID3D11PixelShader> ps_preview_;
    ComPtr<ID3D11SamplerState> smp_linear_;
    ComPtr<ID3D11ShaderResourceView> ldr_srv_;
    ComPtr<ID3D11Buffer> preview_cb_;
    ComPtr<ID3D11Texture2D> preview_tex_, preview_staging_[kPreviewSlots];
    ComPtr<ID3D11RenderTargetView> preview_rtv_;
    std::uint32_t pw_ = 0, ph_ = 0;  // onizleme boyutu (0: kurulmadi)
};

struct LoopOut {
    std::vector<double> frame_ms, cpu_submit_ms, gpu_elapsed_ms, gpu_pacing_ms;
    std::vector<double> wait_ms, sim_ms, cull_ms;  // kare basi: GPU'yu bekleme; Arena simulasyon; Arena ayiklama + siralama
    std::uint32_t draws_min = 0, draws_max = 0;    // Arena: olculen karelerde cizilen nesne
    bool preview = false;                          // onizleme bu dongude uretildi mi
    std::uint32_t preview_frames = 0;              // iletilen onizleme karesi (adim sonu karesi dahil)
    double preview_ms = 0;                         // olcum penceresinde onizleme okuma + geri cagri toplam suresi
    std::uint32_t gpu_excluded = 0, warmup_frames = 0, measured_frames = 0;
    double warmup_s = 0, measure_s = 0;
    Clock::time_point measure_begin{}, measure_end{};
    bool cancelled = false;
    std::string error;
};

// Arena v1: bir adimin CPU tarafi. Her adim yeni bir simulasyonla kare 0'dan baslar.
struct ArenaStep {
    explicit ArenaStep(const SceneConfig& c) : sim(arena_sim_config(c)) {}
    FrameSim sim;
    SimPool* pool = nullptr;                       // sim aciksa: kalici isci havuzu (oturuma ait)
    const std::vector<ArenaObjectStyle>* styles = nullptr;
    VisibleSet visible;
    std::vector<ArenaObjectGpu> objects;           // cizim listesi (sim kapaliyken adim boyunca sabit)
    std::uint32_t flow_mask = kArenaNoFlowMask;
};

// Isinma + olcum dongusu. Kare basina GPU beklemesi yok: yalniz `frames_in_flight` geri basinci.
// `arena` verilirse Arena v1 karesi cizilir: simulasyon (isci havuzu) -> ayiklama + siralama -> kayit + gonderim;
// sureler ayri yazilir. Verilmezse scene_v3 (davranis degismedi; yalniz bekleme suresi de kaydedilir).
bool run_loop(SceneGpu& g, const SceneConfig& c, Tier tier, const std::atomic<bool>& cancel, LoopOut& o, ArenaStep* arena = nullptr) {
    const std::uint32_t in_flight = std::clamp<std::uint32_t>(c.frames_in_flight, 1, kRingSlots - 2);
    GpuQueryRing ring(kRingSlots);
    const std::uint64_t base = g.submitted();  // bu dongunun ilk gonderim sirasi
    // A4: canli sayilar (isinma dahil) ve beklemesiz onizleme. Ikisi de kapaliyken dongu eskisiyle aynidir.
    // Onizleme hedefleri (ilk kez / cozunurluk degisince) dongu saati baslamadan kurulur.
    FrameTap* const tap = c.frame_tap;
    TapFeeder feeder(tap);
    TapPhase tap_phase = TapPhase::Idle;
    if (tap) {
        tap->reset_live();
        tap->set_resolution(c.width, c.height);
    }
    const PreviewCallback* const preview = c.preview && *c.preview && g.ensure_preview() ? c.preview : nullptr;
    PreviewRing pring;
    o.preview = preview != nullptr;
    const auto t0 = Clock::now();
    Clock::time_point prev{};
    bool have_prev = false;
    std::uint64_t f = 0;
    if (!duration_mode(c)) {
        o.frame_ms.reserve(c.frames);
        o.cpu_submit_ms.reserve(c.frames);
        o.wait_ms.reserve(c.frames);
    }
    const bool sim_on = arena && c.sim && arena->pool;
    for (;; ++f) {
        if (cancel.load(std::memory_order_relaxed)) {
            o.cancelled = true;
            break;
        }
        const auto phase = scene_phase(c, f, std::chrono::duration<double>(Clock::now() - t0).count());
        if (phase == ScenePhase::Done) break;
        const bool measuring = phase == ScenePhase::Measure;
        const auto tw = Clock::now();  // GPU'yu bekleme baslangici (geri basinc + zaman damgasi okuma)
        if (f >= in_flight && !g.wait_done(base + f - in_flight, cancel, o.cancelled, o.error)) break;
        if (g.has_ts()) {
            g.poll(ring, false);
            if (ring.full()) g.poll(ring, true);
        }
        if (tap) {
            const TapPhase ph = measuring ? TapPhase::Measure : TapPhase::Warmup;
            if (ph != tap_phase) tap->set_phase(tap_phase = ph);
            double live_ms = 0;
            std::uint32_t live_n = 0;
            ring.take_live(live_ms, live_n);
            feeder.gpu(live_ms, live_n);
        }
        const auto ts = Clock::now();
        // Onizleme kopyasi en fazla 20 Hz ve yalniz bos yuva varken istenir; zaman damgasi yuvasini ETKILEMEZ.
        int pslot = -1;
        if (preview) {
            const double now_ms = ms_between(t0, ts);
            if (pring.should_issue(now_ms))
                if (const auto ps = pring.issue(now_ms, f, measuring)) pslot = static_cast<int>(*ps);
        }
        int slot = -1;
        std::uint32_t s = 0;
        if (g.has_ts()) {
            if (ring.issue(measuring, s)) slot = static_cast<int>(s);
            else ring.note_unissued(measuring);  // zaman damgasiz kare: dislanir, tamamlama zinciri kopar
        }
        auto t_sim = ts, t_cull = ts;  // Arena: simulasyon sonu, ayiklama + siralama sonu
        if (arena) {
            SceneGpu::ArenaDraw d;
            if (sim_on) {
                arena->pool->step(arena->sim);  // isciler: sabit araliklar; bitince commit
                t_sim = Clock::now();
                arena->sim.cull_and_sort(arena->visible);  // cizim is parcacigi
                t_cull = Clock::now();
                arena_fill_objects(arena->sim, arena->visible, *arena->styles, false, arena->objects);
                d.frame = static_cast<std::uint32_t>(arena->sim.frame());
                d.camera_z = sim_camera_z(arena->sim.frame());
            } else {
                d.frame = static_cast<std::uint32_t>(f);
                d.camera_z = sim_camera_z(f);
                d.upload = false;  // liste adim basinda yuklendi; golgelendirici kare indeksiyle ilerletir
            }
            d.objects = arena->objects.data();
            d.count = static_cast<std::uint32_t>(arena->objects.size());
            d.flow_mask = arena->flow_mask;
            g.draw_arena(c, d, slot, pslot);
        } else {
            g.draw(c, tier, static_cast<std::uint32_t>(f), slot, pslot);
        }
        g.submit();
        const auto t_end = Clock::now();  // kayit + gonderim sonu
        auto tp = t_end;
        // Onizleme okuma: en eski kopya, istendiginden >= kPreviewDelayFrames kare sonra BEKLEMEDEN denenir. GPU
        // bitirmediyse yuva bekler. Suresi kare araligina girer ama cizim (submit) suresine yazilmaz; ayri toplanir.
        if (preview) {
            if (const auto pend = pring.ready(f)) {
                PreviewFrame pf;
                pf.frame = pend->frame;
                pf.measuring = pend->measuring;
                const auto r = g.preview_read(pend->slot, false, pf, *preview);
                if (r != SceneGpu::PreviewRead::Busy) pring.pop();
                if (r == SceneGpu::PreviewRead::Delivered) ++o.preview_frames;
                tp = Clock::now();
                if (measuring) o.preview_ms += ms_between(t_end, tp);
            }
        }
        if (tap) feeder.frame(have_prev ? ms_between(prev, tp) : 0.0, ms_between(ts, t_end), ms_between(tw, ts));
        if (measuring) {
            if (o.measured_frames == 0) o.measure_begin = have_prev ? prev : ts;
            ++o.measured_frames;
            o.cpu_submit_ms.push_back(ms_between(t_cull, t_end));  // scene_v3: t_cull == ts
            o.wait_ms.push_back(ms_between(tw, ts));
            if (arena) {
                const auto n = static_cast<std::uint32_t>(arena->objects.size());
                o.draws_min = o.measured_frames == 1 ? n : std::min(o.draws_min, n);
                o.draws_max = std::max(o.draws_max, n);
                if (sim_on) {
                    o.sim_ms.push_back(ms_between(ts, t_sim));
                    o.cull_ms.push_back(ms_between(t_sim, t_cull));
                }
            }
            if (have_prev) {
                o.frame_ms.push_back(ms_between(prev, tp));
                if (c.frame_tap) c.frame_tap->add(o.frame_ms.back());
            }
            o.measure_end = tp;
        } else {
            ++o.warmup_frames;
        }
        prev = tp;
        have_prev = true;
    }
    if (tap) tap->set_phase(TapPhase::Idle);
    if (o.cancelled || !o.error.empty()) return false;
    // Son bosaltma: yalniz olcum bittikten sonra.
    if (f > 0 && !g.wait_done(base + f - 1, cancel, o.cancelled, o.error)) return false;
    if (g.has_ts()) g.poll(ring, true);
    // Adim sonu karesi: olcum bitti, GPU bosta; son cizilen kare kucultulur ve (bu kez bekleyerek) okunur.
    if (preview && f > 0) {
        g.preview_copy(0);
        g.flush();
        PreviewFrame pf;
        pf.frame = f - 1;
        pf.final_frame = true;
        if (g.preview_read(0, true, pf, *preview) == SceneGpu::PreviewRead::Delivered) ++o.preview_frames;
    }
    o.gpu_elapsed_ms = ring.elapsed_ms();
    o.gpu_pacing_ms = ring.pacing_ms();
    o.gpu_excluded = ring.excluded();
    o.warmup_s = o.measured_frames ? std::chrono::duration<double>(o.measure_begin - t0).count() : 0.0;
    o.measure_s = o.measured_frames ? std::chrono::duration<double>(o.measure_end - o.measure_begin).count() : 0.0;
    return true;
}

bool valid_resolution(const SceneConfig& c) { return c.width >= 16 && c.height >= 16 && c.width <= 16384 && c.height <= 16384; }

// Dongu ciktisini sonuca tasir ve ozetler. false: sonuc kesinlesti (iptal, hata ya da olcum karesi yok).
bool finish_loop(SceneRunResult& res, LoopOut& o, bool ok) {
    res.warmup_frames = o.warmup_frames;
    res.measured_frames = o.measured_frames;
    if (o.cancelled) {
        res.cancelled = true;
        return false;  // kismi sonuc puanlanmaz
    }
    if (!ok) {
        res.error = o.error.empty() ? "loop_failed" : o.error;
        return false;
    }
    // Olcum penceresinde kare yoksa (kare kipi frames=0, tek kare, ya da pencere hic acilmadi) sonuc yok.
    if (o.measured_frames == 0 || o.frame_ms.empty()) {
        res.error = "no_measured_frames";
        return false;
    }
    res.frame_ms = std::move(o.frame_ms);
    res.cpu_submit_ms = std::move(o.cpu_submit_ms);
    res.gpu_elapsed_ms = std::move(o.gpu_elapsed_ms);
    res.gpu_pacing_ms = std::move(o.gpu_pacing_ms);
    res.wait_ms = std::move(o.wait_ms);
    res.sim_ms = std::move(o.sim_ms);
    res.cull_ms = std::move(o.cull_ms);
    res.object_draws_min = o.draws_min;
    res.object_draws_max = o.draws_max;
    res.preview = o.preview;
    res.preview_frames = o.preview_frames;
    res.preview_ms = o.preview_ms;
    res.gpu_excluded = o.gpu_excluded;
    res.warmup_s = o.warmup_s;
    res.measure_s = o.measure_s;
    res.measure_begin = o.measure_begin;
    res.measure_end = o.measure_end;
    res.gpu_elapsed = summarize_ms(res.gpu_elapsed_ms);
    res.gpu_pacing = summarize_ms(res.gpu_pacing_ms);
    res.cpu_submit = summarize_ms(res.cpu_submit_ms);
    res.wait = summarize_ms(res.wait_ms);
    res.sim_step = summarize_ms(res.sim_ms);
    res.cull = summarize_ms(res.cull_ms);
    res.queue_saturation = queue_saturation(res.gpu_elapsed, res.gpu_pacing);
    res.frame_source = choose_frame_source(res.gpu_timestamps, res.gpu_pacing_ms.size(), res.frame_ms.size(), res.gpu_excluded);
    if (res.gpu_timestamps) res.gpu_pacing_coverage = pacing_coverage(res.gpu_pacing_ms.size(), res.frame_ms.size());
    return true;
}

}  // namespace

const std::vector<double>& scored_frame_ms(const SceneRunResult& r) noexcept {
    return r.frame_source == FrameSource::GpuCompletion ? r.gpu_pacing_ms : r.frame_ms;
}

void ScoredFrames::add(const SceneRunResult& r) {
    ++runs_;
    excluded_ += r.gpu_excluded;
    if (r.frame_source != FrameSource::GpuCompletion) {
        all_gpu_ = false;
        pacing_.clear();
        pacing_.shrink_to_fit();
    }
    if (all_gpu_) pacing_.insert(pacing_.end(), r.gpu_pacing_ms.begin(), r.gpu_pacing_ms.end());
    submit_.insert(submit_.end(), r.frame_ms.begin(), r.frame_ms.end());
}

bool gpu_timestamp_valid(bool disjoint, std::uint64_t freq, bool read_ok, std::uint64_t a, std::uint64_t b) noexcept {
    return !disjoint && freq != 0 && read_ok && b >= a;
}

SceneRunResult run_scene(const SceneConfig& c, const std::atomic<bool>& cancel, bool prefer_hardware) {
    if (c.arena) {  // Arena v1: tek adimlik oturum (scene_v3 yolu asagida, degismedi)
        ArenaSession session;
        if (!session.open(c, prefer_hardware)) {
            SceneRunResult bad;
            bad.arena = true;
            bad.error = session.error();
            bad.adapter = session.adapter();
            bad.width = c.width;
            bad.height = c.height;
            bad.workload_hash = workload_hash(c, {});
            return bad;
        }
        return session.run_step(c, cancel);
    }
    SceneRunResult res;
    const auto t_prep = Clock::now();
    const auto tp = tier_params(c.tier);
    res.tier = c.tier;
    res.width = c.width;
    res.height = c.height;
    res.lights = tp.lights;
    res.instances = tp.instances;
    res.light_draws = light_draws(tp.lights);
    res.frames_in_flight = std::clamp<std::uint32_t>(c.frames_in_flight, 1, kRingSlots - 2);
    res.workload_hash = workload_hash(c, generate_instances(c));
    if (!valid_resolution(c)) {
        res.error = "bad_resolution";
        return res;
    }
    SceneGpu g;
    if (!g.create_device(prefer_hardware, res.adapter, res.error)) return res;
    if (!g.build(c, c.tier, res.error)) return res;
    res.gpu_timestamps = g.has_ts();
    res.fence_wait = g.has_fence();
    // Hazirlik bitti: yuklemenin GPU'da tamamlanmasi da beklenir (skor penceresine sizmaz).
    if (!g.idle(res.error)) return res;
    res.prepare_ms = ms_since(t_prep);

    LoopOut o;
    const bool ok = run_loop(g, c, c.tier, cancel, o);
    if (!finish_loop(res, o, ok)) return res;
    // Sabit dogrulama karesi (kare 0): sure kipinde de piksel ozeti deterministik.
    g.draw(c, c.tier, 0, -1);
    if (!g.checksum(res.pixel_checksum)) {
        res.error = "readback";
        return res;
    }
    res.ok = true;
    return res;
}

// ---- Uyumluluk B14: uzun test oturumu (scene_v3, tek cihaz) ----
struct SceneSession::Impl {
    SceneGpu gpu;
    AdapterInfo adapter;
    std::string error;
    bool opened = false;
    Tier tier = Tier::Medium;
    std::uint32_t width = 0, height = 0;
    bool lost = false;  // cihaz kaybi: oturum kullanilamaz
};

SceneSession::SceneSession() : impl_(std::make_unique<Impl>()) {}
SceneSession::~SceneSession() = default;
const std::string& SceneSession::error() const noexcept { return impl_->error; }
const AdapterInfo& SceneSession::adapter() const noexcept { return impl_->adapter; }

bool SceneSession::open(const SceneConfig& c, bool prefer_hardware) {
    Impl& s = *impl_;
    if (s.opened) return true;
    s.error.clear();
    if (c.arena || !valid_resolution(c)) {
        s.error = c.arena ? "bad_config" : "bad_resolution";
        return false;
    }
    if (!s.gpu.create_device(prefer_hardware, s.adapter, s.error)) return false;
    // Hazirlik (yukleme dahil) burada biter: dilimlerin olcum penceresine sizmaz.
    if (!s.gpu.build(c, c.tier, s.error) || !s.gpu.idle(s.error)) return false;
    s.tier = c.tier;
    s.width = c.width;
    s.height = c.height;
    s.opened = true;
    return true;
}

SceneRunResult SceneSession::run_slice(const SceneConfig& c, const std::atomic<bool>& cancel) {
    Impl& s = *impl_;
    SceneRunResult res;
    const auto tp = tier_params(c.tier);
    res.tier = c.tier;
    res.width = c.width;
    res.height = c.height;
    res.lights = tp.lights;
    res.instances = tp.instances;
    res.light_draws = light_draws(tp.lights);
    res.frames_in_flight = std::clamp<std::uint32_t>(c.frames_in_flight, 1, kRingSlots - 2);
    res.adapter = s.adapter;
    if (!s.opened || s.lost) {
        res.error = s.lost ? "device_removed" : "not_open";
        return res;
    }
    if (c.arena || c.tier != s.tier || c.width != s.width || c.height != s.height || !duration_mode(c)) {
        res.error = "bad_config";
        return res;
    }
    res.gpu_timestamps = s.gpu.has_ts();
    res.fence_wait = s.gpu.has_fence();
    LoopOut o;
    const bool ok = run_loop(s.gpu, c, c.tier, cancel, o);
    if (!finish_loop(res, o, ok)) {
        if (!res.cancelled && res.error == "device_removed") s.lost = true;  // bekleme yolu cihaz kaybini boyle bildirir
        return res;
    }
    res.ok = true;  // dogrulama karesi geri okunmaz: dilimler arasinda GPU bekletilmez
    return res;
}

// ---- Arena v1 oturumu ----
struct ArenaSession::Impl {
    SceneGpu gpu;
    AdapterInfo adapter;
    std::string error;
    std::uint64_t seed = 0;
    bool opened = false;
    std::uint32_t width = 0, height = 0;    // kurulu hedef kumesi (0: yok)
    std::unique_ptr<SimPool> pool;          // kalici isci havuzu (isci sayisi degisirse yeniden kurulur)
    std::vector<ArenaObjectStyle> styles;   // nesne stilleri (tohum + indeks; adimlar arasinda yeniden kullanilir)
};

ArenaSession::ArenaSession() : impl_(std::make_unique<Impl>()) {}
ArenaSession::~ArenaSession() = default;

const std::string& ArenaSession::error() const noexcept { return impl_->error; }
const AdapterInfo& ArenaSession::adapter() const noexcept { return impl_->adapter; }
void ArenaSession::limit_vram_budget(std::uint64_t bytes) noexcept { impl_->gpu.limit_budget(bytes); }

bool ArenaSession::open(const SceneConfig& base, bool prefer_hardware, int hardware_index) {
    Impl& s = *impl_;
    if (s.opened) return true;
    s.error.clear();
    s.seed = base.seed;
    if (!s.gpu.create_device(prefer_hardware, s.adapter, s.error, hardware_index)) return false;
    if (!s.gpu.build_arena(base.seed, s.error) || !s.gpu.idle(s.error)) return false;
    s.opened = true;
    return true;
}

bool ArenaSession::read_frame(std::vector<std::uint8_t>& rgba, std::uint32_t& width, std::uint32_t& height) {
    Impl& s = *impl_;
    return s.opened && s.width != 0 && s.gpu.read_frame(rgba, width, height);
}

SceneRunResult ArenaSession::run_step(const SceneConfig& c, const std::atomic<bool>& cancel) {
    Impl& s = *impl_;
    SceneRunResult res;
    const auto t_prep = Clock::now();
    const std::uint32_t workers = c.sim ? (c.sim_workers ? c.sim_workers : 1u) : 0u;
    res.arena = true;
    res.adapter = s.adapter;
    res.width = c.width;
    res.height = c.height;
    res.lights = c.arena_lights;
    res.instances = c.sim_objects;
    res.light_draws = arena_light_draws(c.arena_lights, std::uint64_t{c.width} * c.height);
    res.frames_in_flight = std::clamp<std::uint32_t>(c.frames_in_flight, 1, kRingSlots - 2);
    res.sim = c.sim;
    res.sim_workers = workers;
    res.draw_mode = c.draw_mode;
    res.workload_hash = workload_hash(c, {});
    if (!s.opened) {
        res.error = "not_open";
        return res;
    }
    if (!arena_config_valid(c) || c.seed != s.seed) {
        res.error = "bad_config";
        return res;
    }
    if (!valid_resolution(c)) {
        res.error = "bad_resolution";
        return res;
    }
    if (cancel.load(std::memory_order_relaxed)) {
        res.cancelled = true;
        return res;
    }
    // Hedefler: cozunurluk degistiyse ayni cihazda yeniden kurulur (butce denetimi icinde).
    if (c.width != s.width || c.height != s.height) {
        s.width = s.height = 0;
        if (!s.gpu.rebuild_targets(c.width, c.height, res.error)) return res;  // "vram_budget" | "target_create" | "device_removed"
        s.width = c.width;
        s.height = c.height;
    }
    if (!s.gpu.prepare_arena(c, res.error)) return res;
    res.gpu_timestamps = s.gpu.has_ts();
    res.fence_wait = s.gpu.has_fence();

    // CPU tarafi: stiller, isci havuzu ve simulasyon dogrulamasi (hepsi hazirlik; skor penceresine girmez).
    const std::uint32_t have = static_cast<std::uint32_t>(s.styles.size());
    if (have < c.sim_objects) {
        s.styles.reserve(c.sim_objects);
        for (std::uint32_t i = have; i < c.sim_objects; ++i) s.styles.push_back(arena_object_style(s.seed, i));
    }
    ArenaStep step(c);
    step.styles = &s.styles;
    if (c.sim) {
        if (!s.pool || s.pool->workers() != workers) {
            s.pool.reset();
            try {
                s.pool = std::make_unique<SimPool>(workers);
            } catch (const std::system_error&) {
                res.error = "sim_pool";
                return res;
            }
        }
        // Ilk karelerin saglamasi olcumde kullanilacak havuzla hesaplanir: C profilinde altin deger, baska
        // ayarda seri hesap beklenir. Tutmuyorsa hesap guvenilmez; adim kosmaz.
        const SimConfig sc = arena_sim_config(c);
        FrameSim probe(sc);
        VisibleSet seen;
        std::uint64_t acc = 0;
        for (std::uint32_t f = 0; f < kSimGoldenFrames; ++f) {
            s.pool->step(probe);
            probe.cull_and_sort(seen);
            acc = sim_checksum_fold(acc, probe.frame_checksum(seen));
        }
        const std::uint64_t expected = sim_golden_applies(sc) ? sim_golden_checksum() : sim_prefix_checksum(sc, kSimGoldenFrames, 1);
        res.sim_checksum = acc;
        res.sim_verified = acc == expected;
        if (!*res.sim_verified) {
            res.error = "sim_checksum";
            return res;
        }
        step.pool = s.pool.get();
    }
    // Kare 0 cizim listesi. Sim kapaliyken bu liste adim boyunca sabittir ve golgelendiricide ilerler.
    step.sim.cull_and_sort(step.visible);
    arena_fill_objects(step.sim, step.visible, s.styles, !c.sim, step.objects);
    if (!c.sim) step.flow_mask = arena_flow_mask(step.visible.draws ? visible_depth(step.visible.keys[step.visible.draws - 1]) : 0u);
    // Adimin tum gecisleri olcumden once bir kez cizilir (kare 0) ve ortasi geri okunur: gercekten cizildi mi.
    SceneGpu::ArenaDraw first;
    first.objects = step.objects.data();
    first.count = static_cast<std::uint32_t>(step.objects.size());
    first.flow_mask = step.flow_mask;
    s.gpu.draw_arena(c, first, -1);
    if (!s.gpu.checksum(res.pixel_checksum)) {
        res.error = "readback";
        return res;
    }
    if (!s.gpu.idle(res.error)) return res;
    res.prepare_ms = ms_since(t_prep);

    LoopOut o;
    const bool ok = run_loop(s.gpu, c, Tier::Low, cancel, o, &step);
    if (!finish_loop(res, o, ok)) return res;
    res.ok = true;
    return res;
}

SceneCalibration calibrate_scene(const SceneConfig& base, const CalibrationOptions& o, const std::atomic<bool>& cancel, bool prefer_hardware) {
    SceneCalibration cal;
    const auto t0 = Clock::now();
    if (!valid_resolution(base)) {
        cal.error = "bad_resolution";
        return cal;
    }
    SceneGpu g;
    if (!g.create_device(prefer_hardware, cal.adapter, cal.error)) return cal;
    if (!g.build(base, Tier::High, cal.error) || !g.idle(cal.error)) return cal;
    const Tier order[3] = {Tier::Low, Tier::Medium, Tier::High};
    std::array<double, 3> cost{};  // tier secim maliyeti: GPU tamamlama araligi, yoksa gonderim araligi
    for (const Tier t : order) {
        SceneConfig cc = base;
        cc.tier = t;
        cc.warmup_frames = 0;
        cc.frame_tap = nullptr;  // EF-X15: kalibrasyon onizleme sayacina girmez
        cc.preview = nullptr;    // A4: kalibrasyon onizleme karesi uretmez
        cc.warmup_s = o.warmup_s > 0 ? o.warmup_s : 0.0;
        cc.measure_s = o.measure_s > 0 ? o.measure_s : 0.5;
        LoopOut lo;
        const bool ok = run_loop(g, cc, t, cancel, lo);
        if (lo.cancelled) {
            cal.cancelled = true;
            cal.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            return cal;
        }
        if (!ok) {
            cal.error = lo.error.empty() ? "loop_failed" : lo.error;
            cal.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            return cal;
        }
        const auto fs = summarize_ms(lo.frame_ms);
        const auto ps = summarize_ms(lo.gpu_pacing_ms);
        const auto es = summarize_ms(lo.gpu_elapsed_ms);
        const auto i = static_cast<std::size_t>(t);
        cal.avg_frame_ms[i] = fs.n ? fs.avg_ms : 0.0;
        const bool gpu_src = choose_frame_source(g.has_ts(), lo.gpu_pacing_ms.size(), lo.frame_ms.size(), lo.gpu_excluded) ==
                             FrameSource::GpuCompletion;
        cal.avg_gpu_pacing_ms[i] = gpu_src && ps.n ? ps.avg_ms : 0.0;
        cal.avg_gpu_elapsed_ms[i] = es.n ? es.avg_ms : 0.0;
        cost[i] = cal.avg_gpu_pacing_ms[i] > 0 ? cal.avg_gpu_pacing_ms[i] : cal.avg_frame_ms[i];
        // Bu tier butceyi astiysa daha agirlari denemeye gerek yok (maliyet tier ile artar).
        if (!(cost[i] > 0) || cost[i] > o.max_frame_ms) break;
    }
    cal.tier = pick_scene_tier(cost, o.max_frame_ms);
    cal.ok = true;
    cal.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return cal;
}

std::uint32_t vram_steps_allowed(std::uint64_t budget, std::uint64_t usage, std::uint64_t step, double fraction, std::uint64_t cap) noexcept {
    if (budget == 0 || step == 0 || fraction <= 0) return 0;
    const auto limit = static_cast<std::uint64_t>(static_cast<double>(budget) * fraction);
    if (usage >= limit) return 0;
    std::uint64_t room = limit - usage;
    if (room > cap) room = cap;
    return static_cast<std::uint32_t>(room / step);
}

int nth_hardware_adapter(std::span<const bool> software, int n) noexcept {
    if (n < 0) return -1;
    int seen = 0;
    for (std::size_t i = 0; i < software.size(); ++i) {
        if (software[i]) continue;
        if (seen++ == n) return static_cast<int>(i);
    }
    return -1;
}

std::vector<std::wstring> hardware_adapter_names() {
    std::vector<std::wstring> out;
    ComPtr<IDXGIFactory6> fac;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) return out;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (fac->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (!is_software(d)) out.emplace_back(d.Description);
    }
    return out;
}

VramResult run_vram(std::uint64_t step_bytes, double fraction, std::uint64_t cap, const std::atomic<bool>& cancel) {
    VramResult r;
    ComPtr<IDXGIFactory6> fac;
    ComPtr<IDXGIAdapter1> a1;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) {
        r.error = "dxgi_factory";
        return r;
    }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (fac->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (!is_software(d)) {
            a1 = a;
            break;
        }
    }
    ComPtr<IDXGIAdapter3> a3;
    if (!a1 || FAILED(a1.As(&a3))) {
        r.error = "no_hardware_adapter";
        return r;
    }
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDevice(a1.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        r.error = "d3d11_create_device";
        return r;
    }
    auto query = [&](DXGI_QUERY_VIDEO_MEMORY_INFO& m) { return SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m)); };
    DXGI_QUERY_VIDEO_MEMORY_INFO m{};
    if (!query(m) || m.Budget == 0) {
        r.error = "budget_unavailable";  // sahte 0 yok: desteklenmiyor
        return r;
    }
    r.supported = true;
    r.budget_start = m.Budget;
    const auto n = vram_steps_allowed(m.Budget, m.CurrentUsage, step_bytes, fraction, cap);
    const UINT w = 4096, h = static_cast<UINT>(step_bytes / (4096ull * 4));  // step baytlik tek RGBA8 doku
    std::vector<ComPtr<ID3D11Texture2D>> held;
    std::vector<std::uint32_t> pattern(static_cast<std::size_t>(w) * 64);
    for (std::size_t i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<std::uint32_t>(i * 2654435761u);  // sikistirilamaz desen
    ComPtr<ID3D11Query> done;
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    dev->CreateQuery(&qd, &done);
    for (std::uint32_t i = 0; i < n; ++i) {
        if (cancel.load()) {
            r.cancelled = true;
            break;
        }
        const auto t0 = Clock::now();
        D3D11_TEXTURE2D_DESC td{w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        ComPtr<ID3D11Texture2D> t;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &t))) {
            r.error = "allocation_failed";
            break;
        }
        for (UINT y = 0; y < h; y += 64) {  // gercekten yerlesmesi icin icerik yuklenir
            const D3D11_BOX box{0, y, 0, w, std::min(h, y + 64), 1};
            ctx->UpdateSubresource(t.Get(), 0, &box, pattern.data(), w * 4, 0);
        }
        ctx->End(done.Get());
        while (ctx->GetData(done.Get(), nullptr, 0, 0) == S_FALSE) YieldProcessor();
        held.push_back(t);
        VramStep st;
        st.allocated = static_cast<std::uint64_t>(held.size()) * step_bytes;
        st.upload_ms = ms_since(t0);
        if (query(m)) st.usage = m.CurrentUsage, st.budget = m.Budget;
        r.steps.push_back(st);
        if (st.budget && st.usage > static_cast<std::uint64_t>(static_cast<double>(st.budget) * fraction)) {
            r.stopped_at_budget = true;  // butce dustu veya kullanim siniri asti: kontrollu dur
            break;
        }
    }
    held.clear();  // tum tahsisler geri verilir
    ctx->Flush();
    return r;
}

}  // namespace ef::benchmark
