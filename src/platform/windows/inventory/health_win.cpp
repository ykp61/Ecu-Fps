#include "platform/windows/inventory/health_win.h"

#include <windows.h>
// initguid, devpropdef/devpkey/pciprop anahtarlarinin TANIMI icin once gelmeli.
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>
#include <pciprop.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <cstdio>
#include <ctime>

#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

namespace ci = core::inventory;

namespace {

std::optional<std::uint32_t> dev_u32(HDEVINFO set, SP_DEVINFO_DATA& d, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    UINT32 v = 0;
    DWORD need = 0;
    if (SetupDiGetDevicePropertyW(set, &d, &key, &type, reinterpret_cast<PBYTE>(&v), sizeof(v), &need, 0) &&
        type == DEVPROP_TYPE_UINT32)
        return v;
    return std::nullopt;
}

// Aygitin atanmis bellek kaynaklarindaki en buyuk pencere (ReBAR acikken VRAM boyutuna yakindir).
std::optional<std::uint64_t> largest_memory_window(DEVINST inst) {
    LOG_CONF conf = 0;
    if (CM_Get_First_Log_Conf(&conf, inst, ALLOC_LOG_CONF) != CR_SUCCESS) return std::nullopt;
    std::uint64_t best = 0;
    for (RESOURCEID rid : {ResType_Mem, ResType_MemLarge}) {
        RES_DES des = 0;
        RESOURCEID got = 0;
        if (CM_Get_Next_Res_Des(&des, conf, rid, &got, 0) != CR_SUCCESS) continue;
        for (int guard = 0; guard < 64; ++guard) {
            ULONG size = 0;
            if (CM_Get_Res_Des_Data_Size(&size, des, 0) == CR_SUCCESS && size > 0 && size < 4096) {
                std::string buf(size, '\0');
                if (CM_Get_Res_Des_Data(des, buf.data(), size, 0) == CR_SUCCESS) {
                    std::uint64_t len = 0;
                    if (rid == ResType_Mem && size >= sizeof(MEM_DES)) {
                        const auto* m = reinterpret_cast<const MEM_DES*>(buf.data());
                        len = m->MD_Alloc_End - m->MD_Alloc_Base + 1;
                    } else if (rid == ResType_MemLarge && size >= sizeof(MEM_LARGE_DES)) {
                        const auto* m = reinterpret_cast<const MEM_LARGE_DES*>(buf.data());
                        len = m->MLD_Alloc_End - m->MLD_Alloc_Base + 1;
                    }
                    if (len > best) best = len;
                }
            }
            RES_DES next = 0;
            const auto r = CM_Get_Next_Res_Des(&next, des, rid, &got, 0);
            CM_Free_Res_Des_Handle(des);
            if (r != CR_SUCCESS) { des = 0; break; }
            des = next;
        }
        if (des) CM_Free_Res_Des_Handle(des);
    }
    CM_Free_Log_Conf_Handle(conf);
    if (best == 0) return std::nullopt;
    return best >> 20;
}

}  // namespace

std::optional<GpuBusInfo> read_gpu_bus(std::uint32_t vendor_id, std::uint32_t device_id) {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return std::nullopt;
    char want[32];
    std::snprintf(want, sizeof want, "VEN_%04X&DEV_%04X", vendor_id, device_id);
    std::optional<GpuBusInfo> out;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d) && !out; ++i) {
        wchar_t id[512];
        if (!SetupDiGetDeviceInstanceIdW(set, &d, id, 512, nullptr)) continue;
        const std::string sid = to_utf8(id);
        if (sid.find(want) == std::string::npos) continue;
        GpuBusInfo g;
        g.instance_id_hint = want;
        g.pcie.current_width = dev_u32(set, d, DEVPKEY_PciDevice_CurrentLinkWidth);
        g.pcie.max_width = dev_u32(set, d, DEVPKEY_PciDevice_MaxLinkWidth);
        g.pcie.current_gen = dev_u32(set, d, DEVPKEY_PciDevice_CurrentLinkSpeed);
        g.pcie.max_gen = dev_u32(set, d, DEVPKEY_PciDevice_MaxLinkSpeed);
        g.largest_mem_window_mib = largest_memory_window(d.DevInst);
        out = g;
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::optional<GpuLinkReading> read_gpu_link(std::uint32_t vendor_id, std::uint32_t device_id) {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return std::nullopt;
    char want[32];
    std::snprintf(want, sizeof want, "VEN_%04X&DEV_%04X", vendor_id, device_id);
    std::optional<GpuLinkReading> out;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d) && !out; ++i) {
        wchar_t id[512];
        if (!SetupDiGetDeviceInstanceIdW(set, &d, id, 512, nullptr)) continue;
        if (to_utf8(id).find(want) == std::string::npos) continue;
        GpuLinkReading g;
        g.pcie.current_width = dev_u32(set, d, DEVPKEY_PciDevice_CurrentLinkWidth);
        g.pcie.max_width = dev_u32(set, d, DEVPKEY_PciDevice_MaxLinkWidth);
        g.pcie.current_gen = dev_u32(set, d, DEVPKEY_PciDevice_CurrentLinkSpeed);
        g.pcie.max_gen = dev_u32(set, d, DEVPKEY_PciDevice_MaxLinkSpeed);
        // Ust kopru (kok port / anahtar alt portu): baglantinin karsi ucu. Azamisi yuvanin azamisidir.
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, d.DevInst, 0) == CR_SUCCESS) {
            const auto node_u32 = [parent](const DEVPROPKEY& key) -> std::optional<std::uint32_t> {
                DEVPROPTYPE type = 0;
                UINT32 v = 0;
                ULONG size = sizeof(v);
                if (CM_Get_DevNode_PropertyW(parent, &key, &type, reinterpret_cast<PBYTE>(&v), &size, 0) == CR_SUCCESS && type == DEVPROP_TYPE_UINT32)
                    return v;
                return std::nullopt;
            };
            g.slot_max_width = node_u32(DEVPKEY_PciDevice_MaxLinkWidth);
            g.slot_max_gen = node_u32(DEVPKEY_PciDevice_MaxLinkSpeed);
        }
        out = g;
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

ci::HealthInput collect_health_input() {
    ci::HealthInput in;
    const auto mem = read_memory();
    for (const auto& m : mem.smbios.populated) {
        ci::RamModuleInput r;
        r.speed_mts = m.speed_mts.value;
        r.configured_mts = m.configured_mts.value;
        r.part_number = m.part_number.value.value_or("");
        r.type = m.type.value.value_or("");
        in.ram.push_back(r);
    }
    for (const auto& g : read_gpus()) {
        if (g.kind != ci::GpuKind::Discrete || !g.vendor_id.has() || !g.device_id.has()) continue;
        if (auto bus = read_gpu_bus(*g.vendor_id.value, *g.device_id.value)) {
            in.dgpu_pcie = bus->pcie;
            if (bus->largest_mem_window_mib) in.rebar_enabled = *bus->largest_mem_window_mib > 256;
        }
        break;  // birincil harici GPU
    }
    const auto board = read_board();
    if (board.bios_date.has()) in.bios_date = *board.bios_date.value;
    in.today_unix_days = static_cast<std::int64_t>(std::time(nullptr) / 86400);
    for (const auto& v : read_volumes()) in.volumes.push_back({v.root, v.total_gib.value, v.free_gib.value});
    return in;
}

core::json::Value health_json() {
    const auto in = collect_health_input();
    auto o = core::json::Value::object();
    if (in.dgpu_pcie) {
        auto p = core::json::Value::object();
        auto u = [](const std::optional<std::uint32_t>& v) {
            return v ? core::json::Value::integer(*v) : core::json::Value::null();
        };
        p.set("current_width", u(in.dgpu_pcie->current_width));
        p.set("max_width", u(in.dgpu_pcie->max_width));
        p.set("current_gen", u(in.dgpu_pcie->current_gen));
        p.set("max_gen", u(in.dgpu_pcie->max_gen));
        o.set("dgpu_pcie", std::move(p));
    } else {
        o.set("dgpu_pcie", core::json::Value::null());
    }
    o.set("rebar_enabled", in.rebar_enabled ? core::json::Value::boolean(*in.rebar_enabled) : core::json::Value::null());
    auto arr = core::json::Value::array();
    for (const auto& f : ci::evaluate_health(in)) {
        auto e = core::json::Value::object();
        e.set("code", core::json::Value::string(std::string(ci::to_string(f.code))));
        e.set("text_key", core::json::Value::string(std::string(ci::text_key(f.code))));
        e.set("confidence", core::json::Value::string(std::string(contracts::to_string(f.confidence))));
        e.set("evidence", core::json::Value::string(f.evidence));
        arr.push(std::move(e));
    }
    o.set("findings", std::move(arr));
    return o;
}

}  // namespace ef::platform::inventory
