// EF-X22: D3D11 compute hesap/geri okuma dogrulamasi (bkz. gpu_verify.h). Ayar yazmaz; yalniz kendi
// gecici GPU tamponlarini kullanir ve kosu sonunda birakir.
#include "benchmark/scene/gpu_verify.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>

#include "verify_cs.h"  // derleme zamani fxc ciktisi (g_verify_cs)

namespace ef::benchmark::gpu_verify {

using Microsoft::WRL::ComPtr;

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

bool is_software(const DXGI_ADAPTER_DESC1& d) {
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || (d.VendorId == 0x1414 && d.DeviceId == 0x8c);  // Microsoft Basic Render
}

struct Params {
    std::uint32_t words, seed, iterations, pad;
};
static_assert(sizeof(Params) == 16, "HLSL cbuffer VerifyParams ile ayni yerlesim");

Result finish(Result r, Status s, const char* err, Clock::time_point t0) {
    r.status = s;
    if (err && *err) r.error = err;
    r.total_ms = ms_since(t0);
    return r;
}
}  // namespace

Result run(const Config& c, const std::atomic<bool>& cancel) {
    const auto t0 = Clock::now();
    Result r;
    r.words = c.words;
    r.iterations = c.iterations;
    if (validate_config(c) != ConfigVerdict::Ok) return finish(r, Status::Invalid, config_reason(validate_config(c)), t0);
    r.bytes = planned_bytes(c);

    // 1) Donanim adaptoru (yazilim cizici dogrulama sayilmaz).
    ComPtr<IDXGIFactory6> fac;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) return finish(r, Status::Unsupported, "dxgi_factory", t0);
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (fac->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (is_software(d)) continue;
        adapter = a;
        desc = d;
        break;
    }
    if (!adapter) return finish(r, Status::Unsupported, "no_hardware_adapter", t0);
    r.adapter = desc.Description;
    r.vendor_id = desc.VendorId;
    r.device_id = desc.DeviceId;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};  // cs_5_0
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, want, 2, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
        return finish(r, Status::Unsupported, "d3d11_create_device", t0);
    auto lost = [&] { return dev->GetDeviceRemovedReason() != S_OK; };

    // 2) Kaynaklar: cikti (UAV), staging (okuma), sabit tampon, olay sorgusu.
    const UINT bytes = c.words * static_cast<UINT>(sizeof(std::uint32_t));
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11Buffer> out, staging, cb;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11Query> done;
    {
        D3D11_BUFFER_DESC od{bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                             sizeof(std::uint32_t)};
        D3D11_BUFFER_DESC sd{bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
        const Params p{c.words, c.seed, c.iterations, 0};
        D3D11_BUFFER_DESC cd{sizeof(Params), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA init{&p, 0, 0};
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = c.words;
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
        if (FAILED(dev->CreateComputeShader(g_verify_cs, sizeof(g_verify_cs), nullptr, &cs)) ||
            FAILED(dev->CreateBuffer(&od, nullptr, &out)) || FAILED(dev->CreateBuffer(&sd, nullptr, &staging)) ||
            FAILED(dev->CreateBuffer(&cd, &init, &cb)) || FAILED(dev->CreateUnorderedAccessView(out.Get(), &ud, &uav)) ||
            FAILED(dev->CreateQuery(&qd, &done)))
            return finish(r, lost() ? Status::DeviceRemoved : Status::SetupFailed, "gpu_resource_create", t0);
    }

    // 3) CPU referansi (hazirlik; tekrar suresine sayilmaz ama butceye dahil).
    std::vector<std::uint32_t> ref;
    std::vector<std::uint32_t> sentinel;
    try {
        reference_fill(ref, c.words, c.seed, c.iterations);
        sentinel.assign(c.words, kSentinel);
    } catch (...) {
        return finish(r, Status::SetupFailed, "reference_alloc", t0);
    }
    r.prepare_ms = ms_since(t0);

    ctx->CSSetShader(cs.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = {cb.Get()};
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = {uav.Get()};
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    const UINT groups = dispatch_groups(c.words);

    // 4) Tekrarlar: isaret doldur -> dispatch -> staging'e kopya -> bekle -> tam geri okuma -> karsilastir.
    double last_ms = 0;
    Status st = Status::Ok;
    const char* err = "";
    while (true) {
        if (cancel.load()) {
            st = Status::Cancelled;
            break;
        }
        if (!may_start_repeat(r.repeats_done, c, ms_since(t0), last_ms)) break;
        const auto tr = Clock::now();
        ctx->UpdateSubresource(out.Get(), 0, nullptr, sentinel.data(), 0, 0);
        ctx->Dispatch(groups, 1, 1);
        ctx->CopyResource(staging.Get(), out.Get());
        ctx->End(done.Get());
        HRESULT h;
        bool timed_out = false;
        while ((h = ctx->GetData(done.Get(), nullptr, 0, 0)) == S_FALSE) {
            if (ms_since(tr) > c.wait_timeout_ms) {
                timed_out = true;
                break;
            }
            SwitchToThread();
        }
        if (timed_out || FAILED(h)) {
            st = lost() || FAILED(h) ? Status::DeviceRemoved : Status::GpuTimeout;
            err = st == Status::DeviceRemoved ? "device_removed" : "gpu_timeout";
            break;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            st = lost() ? Status::DeviceRemoved : Status::ReadbackFailed;
            err = "map_failed";
            break;
        }
        const auto* got = static_cast<const std::uint32_t*>(m.pData);
        CompareResult cr;
        if (c.inject_fault_repeat >= 0 && static_cast<std::uint32_t>(c.inject_fault_repeat) == r.repeats_done) {
            // Yalniz test: hatali hesap enjeksiyonu (GPU'nun dondurdugu veri kopyalanir, bir bit cevrilir).
            std::vector<std::uint32_t> tampered(got, got + c.words);
            tampered[0] ^= 1u;
            cr = compare_words(ref.data(), ref.size(), tampered.data(), tampered.size(), Tolerance::exact());
        } else {
            cr = compare_words(ref.data(), ref.size(), got, c.words, Tolerance::exact());
        }
        ctx->Unmap(staging.Get(), 0);
        if (cr.mismatched_words) {
            r.mismatched_words += cr.mismatched_words;
            if (!r.first_mismatch_repeat) r.first_mismatch_repeat = r.repeats_done, r.first_mismatch_index = cr.first_mismatch_index;
        }
        ++r.repeats_done;
        last_ms = ms_since(tr);
    }
    // Kaynaklar birakilmadan once baglar cozulur.
    ID3D11UnorderedAccessView* none[] = {nullptr};
    ctx->CSSetUnorderedAccessViews(0, 1, none, nullptr);
    ctx->CSSetShader(nullptr, nullptr, 0);
    ctx->Flush();

    // Uyusmazlik iptal/hata ile kaybolmaz: bulunduysa sonuc Mismatch kalir (cihaz kaybi haric; o daha agir kayit).
    if (r.mismatched_words && st != Status::DeviceRemoved) return finish(r, Status::Mismatch, "", t0);
    if (st != Status::Ok) return finish(r, st, err, t0);
    if (r.repeats_done < c.min_repeats) return finish(r, Status::TimeLimit, "", t0);
    return finish(r, Status::Ok, "", t0);
}

}  // namespace ef::benchmark::gpu_verify
