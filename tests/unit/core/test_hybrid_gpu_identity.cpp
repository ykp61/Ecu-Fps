// EF-X24.02 kabul: "Hibrit GPU kimlikleri karismaz." Sahte envanter (AMD iGPU + NVIDIA dGPU), sahte PDH motor ornekleri
// ve sahte surucu sicaklik saglayicisi (TimedSensor) ile canli GPU kutucugunun ad / kullanim / sicaklik kimligi.
// Gercek cihaz kaniti DEGILDIR (birim).
#include <string>
#include <vector>

#include "core/metrics/live.h"
#include "core/metrics/sensor.h"
#include "ef_test.h"

namespace cm = ef::core::metrics;
using ef::contracts::Measurement;
using ef::contracts::Unit;

namespace {
// Envanter sirasi: DXGI 0 = iGPU (ekran iGPU'ya bagli dizustu duzeni), 1 = dGPU.
const std::vector<cm::LiveGpuCard> kHybrid{
    {"AMD Radeon(TM) Graphics", "AMD Radeon(TM) Graphics", "00000000:0000a001"},
    {"NVIDIA GeForce RTX 4070 Laptop GPU", "NVIDIA GeForce RTX 4070 Laptop GPU · 8 GB", "00000000:0000b002"},
};
constexpr const char* kIgpu = "luid_0x00000000_0x0000A001";
constexpr const char* kDgpu = "luid_0x00000000_0x0000B002";
cm::GpuEngineSample eng(unsigned pid, const char* luid, unsigned eng_no, const char* type, double pct) {
    return {"pid_" + std::to_string(pid) + "_" + luid + "_phys_0_eng_" + std::to_string(eng_no) + "_engtype_" + type, pct};
}
// Sahte surucu sicakligi: NVIDIA surucusunun bildirdigi ad ile (gercek kod: app.cpp TimedSensor + nvapi telemetry).
cm::SensorValue read_fake_driver_temp(const char* device, double c) {
    cm::TimedSensor s([=] {
        cm::SensorValue v;
        v.source = "nvapi";
        v.device = device;
        v.captured_ms = 1000;
        v.value = Measurement::of(c, Unit::Celsius);
        return v;
    });
    return s.read(1000);
}
}  // namespace

// Ayni phys/eng numarasi iki kartta olsa da motorlar LUID'e gore ayri kalir; iGPU yuku dGPU'ya yazilmaz.
EF_TEST(x24_02_usage_never_mixes_adapters) {
    const std::vector<cm::GpuEngineSample> s{eng(10, kIgpu, 0, "3D", 60), eng(20, kDgpu, 0, "3D", 60), eng(30, kDgpu, 0, "3D", 10)};
    const auto by = cm::gpu_usage_by_adapter(s);
    EF_CHECK(by.size() == 2);
    if (by.size() != 2) return;
    EF_CHECK(by[0].luid == "00000000:0000a001" && by[0].pct == 60.0);
    EF_CHECK(by[1].luid == "00000000:0000b002" && by[1].pct == 70.0);
    // Halka degeri (en yuklu GPU) karisik toplam degil: 60+70 ya da 120 degil, 70.
    const auto all = cm::aggregate_gpu(s);
    EF_CHECK(all.has_value() && *all.value() == 70.0);
}

// Oyun dGPU'da: ad dGPU, surucu sicakligi dGPU'ya ait -> ad + sicaklik ayni kartta.
EF_TEST(x24_02_game_on_dgpu_names_dgpu_with_its_temp) {
    const auto t = read_fake_driver_temp("NVIDIA GeForce RTX 4070 Laptop GPU", 61);
    const auto usage = cm::gpu_usage_by_adapter({eng(1, kIgpu, 0, "3D", 4), eng(2, kDgpu, 0, "3D", 97)});
    const auto p = cm::pick_live_gpu(kHybrid, usage, t.device, t.value.has_value());
    EF_CHECK(p.card.has_value() && *p.card == 1);
    EF_CHECK(p.show_temp);
}

// Yuk iGPU'da (ornek: video): halka iGPU yukunu gosterir, ad iGPU olur ve NVIDIA sicakligi iGPU'ya YAZILMAZ.
EF_TEST(x24_02_igpu_busy_hides_dgpu_temp) {
    const auto t = read_fake_driver_temp("NVIDIA GeForce RTX 4070 Laptop GPU", 45);
    const auto usage = cm::gpu_usage_by_adapter({eng(1, kIgpu, 3, "VideoDecode", 80), eng(2, kDgpu, 0, "3D", 2)});
    const auto p = cm::pick_live_gpu(kHybrid, usage, t.device, true);
    EF_CHECK(p.card.has_value() && *p.card == 0);
    EF_CHECK(!p.show_temp);
}

// Eski hata: ad = "envanterin ilk karti". Ilk kart iGPU iken sicaklik yalniz kendi kartinda gosterilir.
EF_TEST(x24_02_first_inventory_card_does_not_take_driver_temp) {
    const auto p = cm::pick_live_gpu(kHybrid, {}, "NVIDIA GeForce RTX 4070 Laptop GPU", true);  // kullanim okunamadi
    EF_CHECK(p.card.has_value() && *p.card == 1);  // sicakligin sahibi adlandirilir, ilk kart degil
    EF_CHECK(p.show_temp);
}

// Bosta esitlikte (0,5 puan) sicakligin karti secilir: iki kart arasinda ad titremez, sicaklik dogru kartta.
EF_TEST(x24_02_idle_tie_prefers_temp_owner) {
    const auto usage = cm::gpu_usage_by_adapter({eng(1, kIgpu, 0, "3D", 1.2), eng(2, kDgpu, 0, "3D", 1.0)});
    const auto p = cm::pick_live_gpu(kHybrid, usage, "NVIDIA GeForce RTX 4070 Laptop GPU", true);
    EF_CHECK(p.card.has_value() && *p.card == 1 && p.show_temp);
}

// Surucu adi kartlardan hicbiriyle eslesmiyor ya da bos: cok kartli sistemde sicaklik hicbir karta mal edilmez.
EF_TEST(x24_02_unmatched_or_unnamed_temp_is_not_attributed) {
    const auto usage = cm::gpu_usage_by_adapter({eng(2, kDgpu, 0, "3D", 50)});
    const auto a = cm::pick_live_gpu(kHybrid, usage, "Baska Kart", true);
    EF_CHECK(a.card.has_value() && *a.card == 1 && !a.show_temp);
    const auto b = cm::pick_live_gpu(kHybrid, usage, "", true);
    EF_CHECK(!b.show_temp);
    // Tek kartli sistemde adsiz okuma o karta aittir.
    const std::vector<cm::LiveGpuCard> one{{"NVIDIA GeForce RTX 5080", "NVIDIA GeForce RTX 5080 · 16 GB", "00000000:0000b002"}};
    const auto c = cm::pick_live_gpu(one, usage, "", true);
    EF_CHECK(c.card.has_value() && *c.card == 0 && c.show_temp);
}

// Yuk envanterde olmayan bir bagdastiricida (ornek yazilim/uzak): hicbir kart adlandirilmaz, sicaklik da yazilmaz.
EF_TEST(x24_02_unknown_luid_names_no_card) {
    const auto usage = cm::gpu_usage_by_adapter({eng(1, "luid_0x00000000_0x0000FFFF", 0, "3D", 90), eng(2, kDgpu, 0, "3D", 5)});
    const auto p = cm::pick_live_gpu(kHybrid, usage, "NVIDIA GeForce RTX 4070 Laptop GPU", true);
    EF_CHECK(!p.card.has_value());
    EF_CHECK(!p.show_temp);
}

// Surucu adi "NVIDIA " oneki olmadan gelebilir (eski surum): sozcuk sinirinda son ek eslesmesi.
EF_TEST(x24_02_driver_name_without_vendor_prefix_matches) {
    const auto p = cm::pick_live_gpu(kHybrid, {}, "GeForce RTX 4070 Laptop GPU", true);
    EF_CHECK(p.card.has_value() && *p.card == 1 && p.show_temp);
    const auto q = cm::pick_live_gpu(kHybrid, {}, "4070 Laptop GPU X", true);
    EF_CHECK(!q.show_temp);
}
