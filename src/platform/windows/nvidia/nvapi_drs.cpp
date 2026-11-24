#include "platform/windows/nvidia/nvapi_drs.h"

#include <windows.h>

#include <cstring>
#include <string>

#pragma warning(push, 0)
#include "nvapi.h"
#include "nvapi_interface.h"
#pragma warning(pop)

#include "platform/windows/vendor_runtime.h"
#include "platform/windows/win_env.h"

namespace ef::platform::nvidia {

using contracts::ErrorCode;
using contracts::Status;
using core::adapters::nvidia::DrsLocation;
using core::adapters::nvidia::DrsTarget;
using core::adapters::nvidia::DrsValue;

namespace {
using QueryFn = void*(__cdecl*)(unsigned int);

unsigned int interface_id(const char* name) {
    for (const auto& e : nvapi_interface_table)
        if (std::strcmp(e.func, name) == 0) return e.id;
    return 0;
}

std::string nverr(const char* what, NvAPI_Status st) {
    return std::string(what) + " failed (" + std::to_string(static_cast<int>(st)) + ")";
}
}  // namespace

struct Nvapi::Impl {
    LoadedRuntime rt;
    QueryFn query = nullptr;
    template <typename F>
    F fn(const char* name) const {
        const unsigned id = interface_id(name);
        return id && query ? reinterpret_cast<F>(query(id)) : nullptr;
    }
    ~Impl() {
        if (auto unload = fn<NvAPI_Status(__cdecl*)()>("NvAPI_Unload")) unload();
        unload_vendor_runtime(rt);
    }
};

Nvapi::Nvapi(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}
Nvapi::~Nvapi() = default;

contracts::Result<std::unique_ptr<Nvapi>> Nvapi::open() {
    using R = contracts::Result<std::unique_ptr<Nvapi>>;
    auto impl = std::make_unique<Impl>();
    impl->rt = load_vendor_runtime(core::vendors::Vendor::Nvidia);
    using core::vendors::RuntimeState;
    if (impl->rt.info.state == RuntimeState::Missing) return R::fail(ErrorCode::Unsupported, "NVIDIA runtime not installed");
    if (impl->rt.info.state == RuntimeState::Untrusted) return R::fail(ErrorCode::Denied, impl->rt.info.detail);
    if (!impl->rt.module) return R::fail(ErrorCode::IoError, impl->rt.info.detail);
    impl->query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(impl->rt.module), "nvapi_QueryInterface")));
    if (!impl->query) return R::fail(ErrorCode::Unsupported, "nvapi_QueryInterface missing");
    auto init = impl->fn<NvAPI_Status(__cdecl*)()>("NvAPI_Initialize");
    if (!init) return R::fail(ErrorCode::Unsupported, "NvAPI_Initialize missing");
    if (const auto st = init(); st != NVAPI_OK) return R::fail(ErrorCode::Unsupported, nverr("NvAPI_Initialize", st));
    return std::unique_ptr<Nvapi>(new Nvapi(std::move(impl)));
}

std::string Nvapi::driver_version() const {
    auto f = impl_->fn<NvAPI_Status(__cdecl*)(NvU32*, NvAPI_ShortString)>("NvAPI_SYS_GetDriverAndBranchVersion");
    NvU32 ver = 0;
    NvAPI_ShortString branch = {};
    if (!f || f(&ver, branch) != NVAPI_OK) return {};
    return std::to_string(ver / 100) + "." + (ver % 100 < 10 ? "0" : "") + std::to_string(ver % 100);
}

GpuTelemetry Nvapi::telemetry() const {
    GpuTelemetry t;
    auto enumg = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle*, NvU32*)>("NvAPI_EnumPhysicalGPUs");
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvU32 n = 0;
    if (!enumg || enumg(gpus, &n) != NVAPI_OK || n == 0) return t;
    if (auto name = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvAPI_ShortString)>("NvAPI_GPU_GetFullName")) {
        NvAPI_ShortString s = {};
        if (name(gpus[0], s) == NVAPI_OK) t.name = s;
    }
    if (auto th = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32, NV_GPU_THERMAL_SETTINGS*)>("NvAPI_GPU_GetThermalSettings")) {
        NV_GPU_THERMAL_SETTINGS ts{};
        ts.version = NV_GPU_THERMAL_SETTINGS_VER;
        if (th(gpus[0], NVAPI_THERMAL_TARGET_ALL, &ts) == NVAPI_OK && ts.count > 0) t.temperature_c = static_cast<int>(ts.sensor[0].currentTemp);
    }
    if (auto ps = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_DYNAMIC_PSTATES_INFO_EX*)>("NvAPI_GPU_GetDynamicPstatesInfoEx")) {
        NV_GPU_DYNAMIC_PSTATES_INFO_EX di{};
        di.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
        if (ps(gpus[0], &di) == NVAPI_OK && di.utilization[0].bIsPresent) t.gpu_utilization_pct = static_cast<int>(di.utilization[0].percentage);
    }
    // F-61: surucunun guncel performans dususu nedeni (isi/guc vb.). Yalniz okunan deger nullopt degil.
    if (auto pd = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32*)>("NvAPI_GPU_GetPerfDecreaseInfo")) {
        NvU32 mask = 0;
        if (pd(gpus[0], &mask) == NVAPI_OK) t.perf_decrease_mask = mask;
    }
    // F-61: grafik motoru guncel saat (kHz -> MHz). Desteklenmeyen alan nullopt kalir.
    if (auto cf = impl_->fn<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_CLOCK_FREQUENCIES*)>(
            "NvAPI_GPU_GetAllClockFrequencies")) {
        NV_GPU_CLOCK_FREQUENCIES clk{};
        clk.version = NV_GPU_CLOCK_FREQUENCIES_VER;
        clk.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
        if (cf(gpus[0], &clk) == NVAPI_OK && clk.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent &&
            clk.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency > 0)
            t.graphics_clock_mhz =
                static_cast<double>(clk.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency) / 1000.0;
    }
    return t;
}

namespace {
class RealDrs final : public core::adapters::nvidia::DrsApi {
public:
    RealDrs(const Nvapi::Impl& impl, NvDRSSessionHandle s) : n_(impl), s_(s) {}
    ~RealDrs() override {
        if (auto destroy = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>("NvAPI_DRS_DestroySession")) destroy(s_);
    }

    contracts::Result<NvDRSProfileHandle> profile(const DrsTarget& t) {
        using R = contracts::Result<NvDRSProfileHandle>;
        NvDRSProfileHandle p = nullptr;
        if (t.global) {
            auto g = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle*)>("NvAPI_DRS_GetCurrentGlobalProfile");
            if (!g) return R::fail(ErrorCode::Unsupported, "GetCurrentGlobalProfile");
            const auto st = g(s_, &p);
            if (st != NVAPI_OK) return R::fail(ErrorCode::IoError, nverr("GetCurrentGlobalProfile", st));
            return p;
        }
        auto f = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvAPI_UnicodeString, NvDRSProfileHandle*, NVDRS_APPLICATION*)>(
            "NvAPI_DRS_FindApplicationByName");
        if (!f) return R::fail(ErrorCode::Unsupported, "FindApplicationByName");
        NvAPI_UnicodeString name = {};
        const std::wstring w = to_wide(t.app);
        for (std::size_t i = 0; i < w.size() && i + 1 < NVAPI_UNICODE_STRING_MAX; ++i) name[i] = static_cast<NvU16>(w[i]);
        NVDRS_APPLICATION app{};
        app.version = NVDRS_APPLICATION_VER;
        const auto st = f(s_, name, &p, &app);
        if (st == NVAPI_EXECUTABLE_NOT_FOUND) return R::fail(ErrorCode::MissingValue, "application profile not found");
        if (st != NVAPI_OK) return R::fail(ErrorCode::IoError, nverr("FindApplicationByName", st));
        return p;
    }

    contracts::Result<DrsValue> get(const DrsTarget& t) override {
        using R = contracts::Result<DrsValue>;
        auto p = profile(t);
        if (!p.ok()) return R(p.error());
        auto f = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING*)>("NvAPI_DRS_GetSetting");
        if (!f) return R::fail(ErrorCode::Unsupported, "DRS_GetSetting");
        NVDRS_SETTING s{};
        s.version = NVDRS_SETTING_VER;
        const auto st = f(s_, p.value(), t.setting, &s);
        if (st == NVAPI_SETTING_NOT_FOUND) return DrsValue{0, DrsLocation::Default};
        if (st != NVAPI_OK) return R::fail(ErrorCode::IoError, nverr("DRS_GetSetting", st));
        DrsLocation loc = DrsLocation::Default;
        switch (s.settingLocation) {
            case NVDRS_CURRENT_PROFILE_LOCATION: loc = s.isCurrentPredefined ? DrsLocation::Default : DrsLocation::CurrentProfile; break;
            case NVDRS_GLOBAL_PROFILE_LOCATION: loc = DrsLocation::Global; break;
            case NVDRS_BASE_PROFILE_LOCATION: loc = DrsLocation::Base; break;
            default: loc = DrsLocation::Default;
        }
        return DrsValue{s.u32CurrentValue, loc};
    }

    Status set(const DrsTarget& t, std::uint32_t value) override {
        auto p = profile(t);
        if (!p.ok()) return Status(p.error());
        auto f = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*)>("NvAPI_DRS_SetSetting");
        if (!f) return Status::fail(ErrorCode::Unsupported, "DRS_SetSetting");
        NVDRS_SETTING s{};
        s.version = NVDRS_SETTING_VER;
        s.settingId = t.setting;
        s.settingType = NVDRS_DWORD_TYPE;
        s.u32CurrentValue = value;
        const auto st = f(s_, p.value(), &s);
        if (st == NVAPI_INVALID_USER_PRIVILEGE) return Status::fail(ErrorCode::Denied, "DRS write requires administrator");
        return st == NVAPI_OK ? contracts::ok_status() : Status::fail(ErrorCode::IoError, nverr("DRS_SetSetting", st));
    }

    Status remove(const DrsTarget& t) override {
        auto p = profile(t);
        if (!p.ok()) return Status(p.error());
        auto f = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32)>("NvAPI_DRS_DeleteProfileSetting");
        if (!f) return Status::fail(ErrorCode::Unsupported, "DRS_DeleteProfileSetting");
        const auto st = f(s_, p.value(), t.setting);
        if (st == NVAPI_SETTING_NOT_FOUND) return contracts::ok_status();
        if (st == NVAPI_INVALID_USER_PRIVILEGE) return Status::fail(ErrorCode::Denied, "DRS write requires administrator");
        return st == NVAPI_OK ? contracts::ok_status() : Status::fail(ErrorCode::IoError, nverr("DRS_DeleteProfileSetting", st));
    }

    Status save() override {
        auto f = n_.fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>("NvAPI_DRS_SaveSettings");
        if (!f) return Status::fail(ErrorCode::Unsupported, "DRS_SaveSettings");
        const auto st = f(s_);
        if (st == NVAPI_INVALID_USER_PRIVILEGE) return Status::fail(ErrorCode::Denied, "DRS save requires administrator");
        return st == NVAPI_OK ? contracts::ok_status() : Status::fail(ErrorCode::IoError, nverr("DRS_SaveSettings", st));
    }

private:
    const Nvapi::Impl& n_;
    NvDRSSessionHandle s_;
};
}  // namespace

std::unique_ptr<core::adapters::nvidia::DrsApi> Nvapi::drs() {
    auto create = impl_->fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle*)>("NvAPI_DRS_CreateSession");
    auto load = impl_->fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>("NvAPI_DRS_LoadSettings");
    if (!create || !load) return nullptr;
    NvDRSSessionHandle s = nullptr;
    if (create(&s) != NVAPI_OK) return nullptr;
    if (load(s) != NVAPI_OK) {
        if (auto destroy = impl_->fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>("NvAPI_DRS_DestroySession")) destroy(s);
        return nullptr;
    }
    return std::make_unique<RealDrs>(*impl_, s);
}

core::adapters::nvidia::DrsApi* LazyDrs::ensure() {
    if (drs_) return drs_.get();
    if (!nv_) {
        if (tried_) return nullptr;  // calisma zamani yok: tekrar tekrar yukleme denenmez
        tried_ = true;
        auto n = Nvapi::open();
        if (!n.ok()) return nullptr;
        nv_ = std::move(n).value();
    }
    drs_ = nv_->drs();
    return drs_.get();
}

void LazyDrs::refresh() { drs_.reset(); }

bool LazyDrs::available() { return ensure() != nullptr; }

contracts::Result<core::adapters::nvidia::DrsValue> LazyDrs::get(const core::adapters::nvidia::DrsTarget& t) {
    if (auto* d = ensure()) return d->get(t);
    return contracts::Result<core::adapters::nvidia::DrsValue>::fail(contracts::ErrorCode::Unsupported, "NVIDIA DRS unavailable");
}

contracts::Status LazyDrs::set(const core::adapters::nvidia::DrsTarget& t, std::uint32_t value) {
    if (auto* d = ensure()) return d->set(t, value);
    return contracts::Status::fail(contracts::ErrorCode::Unsupported, "NVIDIA DRS unavailable");
}

contracts::Status LazyDrs::remove(const core::adapters::nvidia::DrsTarget& t) {
    if (auto* d = ensure()) return d->remove(t);
    return contracts::Status::fail(contracts::ErrorCode::Unsupported, "NVIDIA DRS unavailable");
}

contracts::Status LazyDrs::save() {
    if (auto* d = ensure()) return d->save();
    return contracts::Status::fail(contracts::ErrorCode::Unsupported, "NVIDIA DRS unavailable");
}

}  // namespace ef::platform::nvidia
