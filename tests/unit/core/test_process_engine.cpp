// Spec 2026-11-12 B (D5): oyun surecinin kendi 3D motor kullanimi, bagdastirici (LUID) bazinda. PDH ornek adlari
// gercek bicimdedir ("pid_<pid>_luid_0x<hi>_0x<lo>_phys_<n>_eng_<k>_engtype_<tur>").
#include <cmath>
#include <string>
#include <vector>

#include "core/metrics/process_engine.h"
#include "ef_test.h"

using namespace ef::core::metrics;

namespace {
bool near_pct(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

EF_TEST(process_engine_luid_is_parsed_from_instance_name) {
    EF_CHECK(engine_instance_luid("pid_4242_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D") == "00000000:0000c9e5");
    EF_CHECK(engine_instance_luid("pid_1_luid_0x1_0xAB_phys_0_eng_3_engtype_3D") == "00000001:000000ab");  // kisa hane tamamlanir
    EF_CHECK(engine_instance_luid("PID_1_LUID_0X00000002_0X0001F00D_phys_0") == "00000002:0001f00d");      // harf buyuklugu
    EF_CHECK(engine_instance_luid("luid_0x00000000_0x00012345") == "00000000:00012345");                   // sonda biter
    // Cozulemeyen adlar bos doner (LUID uydurulmaz).
    for (const char* bad : {"", "pid_1_phys_0_eng_0_engtype_3D", "pid_1_luid_", "pid_1_luid_0x_0x1", "pid_1_luid_0x1", "pid_1_luid_0x1_1234",
                            "pid_1_luid_0x123456789_0x1", "pid_1_luid_0x1_0x1G", "pid_1_luid_1_0x1"})
        EF_CHECK(engine_instance_luid(bad).empty());
    EF_CHECK(engine_instance_is_3d("pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D"));
    EF_CHECK(engine_instance_is_3d("x_ENGTYPE_3d"));
    EF_CHECK(!engine_instance_is_3d("pid_1_luid_0x0_0x1_phys_0_eng_1_engtype_VideoDecode"));
    EF_CHECK(!engine_instance_is_3d("pid_1_luid_0x0_0x1_phys_0_eng_1_engtype_3D_extra") && !engine_instance_is_3d("3D"));
}

EF_TEST(process_engine_picks_the_adapter_the_process_uses_most) {
    // Ayrik kart (0xC9E5) uzerinde iki 3D motoru + tumlesik kartta (0xA1B2) kucuk 3D yuku + video motoru.
    const std::vector<EngineInstance> inst{
        {"pid_4242_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 71.5},
        {"pid_4242_luid_0x00000000_0x0000C9E5_phys_0_eng_5_engtype_3D", 20.0},
        {"pid_4242_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D", 6.0},
        {"pid_4242_luid_0x00000000_0x0000C9E5_phys_0_eng_2_engtype_VideoDecode", 90.0},  // 3D degil: sayilmaz
    };
    auto p = pick_process_3d_engine(inst);
    EF_CHECK(p.pct && std::fabs(*p.pct - 91.5) < 1e-9 && p.luid == "00000000:0000c9e5" && p.adapters == 2);
    // Bagdastiricilar birbirine eklenmez (toplam 97,5 degil); kirpilmamis toplam 100'u asabilir.
    auto over = inst;
    over[1].pct = 40.0;
    p = pick_process_3d_engine(over);
    EF_CHECK(p.pct && std::fabs(*p.pct - 111.5) < 1e-9);
    // Tumlesik kartta calisan oyun: o kart secilir.
    p = pick_process_3d_engine({{"pid_7_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 2.0},
                                {"pid_7_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D", 88.0}});
    EF_CHECK(p.pct && *p.pct == 88.0 && p.luid == "00000000:0000a1b2");
    // Esitlik: kucuk LUID (belirlenimci). 0 gecerli bir olcumdur (bosta).
    p = pick_process_3d_engine({{"pid_7_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 0.0},
                                {"pid_7_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D", 0.0}});
    EF_CHECK(p.pct && *p.pct == 0.0 && p.luid == "00000000:0000a1b2" && p.adapters == 2);
    // Ornek yok / yalniz gecersiz ornek: deger uretilmez.
    EF_CHECK(!pick_process_3d_engine({}).pct && pick_process_3d_engine({}).luid.empty());
    p = pick_process_3d_engine({{"pid_7_phys_0_eng_0_engtype_3D", 50.0},
                                {"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D", std::nan("")},
                                {"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D", -3.0},
                                {"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_Copy", 50.0}});
    EF_CHECK(!p.pct && p.luid.empty() && p.adapters == 0);
}

// D6 (A3): denge testinin arka plan GPU karistiricisi: test bagdastiricisinda BASKA sureclerin 3D kullanimi.
EF_TEST(process_engine_other_processes_on_the_test_adapter) {
    const std::string dgpu = "00000000:0000c9e5", igpu = "00000000:0000a1b2";
    const std::vector<EngineInstance> inst{
        {"pid_100_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 95.0},           // kendi surecimiz: sayilmaz
        {"pid_200_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 30.0},           // baska surec, ayni motor
        {"pid_300_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 12.5},           // baska surec, ayni motor: toplanir
        {"pid_300_luid_0x00000000_0x0000C9E5_phys_0_eng_5_engtype_3D", 20.0},           // baska motor: toplanmaz, en buyugu alinir
        {"pid_400_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D", 80.0},           // baska bagdastirici: sayilmaz
        {"pid_500_luid_0x00000000_0x0000C9E5_phys_0_eng_2_engtype_VideoDecode", 99.0},  // 3D degil: sayilmaz
    };
    auto v = other_process_3d_pct(inst, dgpu, 100);
    EF_CHECK(v && std::fabs(*v - 42.5) < 1e-9);
    // Kendi surecimiz baska bir surec olsaydi onun yuku de sayilirdi (ust sinir kirpilmaz).
    v = other_process_3d_pct(inst, dgpu, 999);
    EF_CHECK(v && std::fabs(*v - 137.5) < 1e-9);
    // Diger bagdastirici ayri degerlendirilir.
    v = other_process_3d_pct(inst, igpu, 100);
    EF_CHECK(v && *v == 80.0);
    // Sayac okundu ama o bagdastiricida baska surec yok: 0 (olculdu, bos).
    v = other_process_3d_pct(inst, "00000000:0000ffff", 100);
    EF_CHECK(v && *v == 0.0);
    v = other_process_3d_pct({{"pid_100_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 95.0}}, dgpu, 100);
    EF_CHECK(v && *v == 0.0);
    // Okunamadi: ornek yok / hicbir 3D ornegi cozulemiyor / LUID bos -> deger uretilmez (0 yazilmaz).
    EF_CHECK(!other_process_3d_pct({}, dgpu, 100));
    EF_CHECK(!other_process_3d_pct({{"pid_7_phys_0_eng_0_engtype_3D", 50.0}, {"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_Copy", 50.0}}, dgpu, 100));
    EF_CHECK(!other_process_3d_pct(inst, "", 100));
    // Sonlu olmayan / negatif ornek atlanir.
    v = other_process_3d_pct({{"pid_200_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", std::nan("")},
                              {"pid_200_luid_0x00000000_0x0000C9E5_phys_0_eng_1_engtype_3D", -4.0},
                              {"pid_300_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 7.0}},
                             dgpu, 100);
    EF_CHECK(v && *v == 7.0);
}

// denge_v2: kendi oturumumuzun masaustu bilesimcisi (dwm.exe) "baska program" sayilmaz.
EF_TEST(process_engine_compositor_is_exempt) {
    const std::string dgpu = "00000000:0000c9e5";
    const std::vector<EngineInstance> inst{
        {"pid_100_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 60.0},  // kendi surecimiz
        {"pid_1200_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 14.0},  // dwm.exe (240 Hz bilesim)
        {"pid_300_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 3.0},    // gercek baska program
    };
    auto v = other_process_3d_pct(inst, dgpu, 100);
    EF_CHECK(v && std::fabs(*v - 17.0) < 1e-9);  // eski kural: bilesimci dahil -> %17 (esik ustu)
    v = other_process_3d_pct(inst, dgpu, 100, {1200});
    EF_CHECK(v && std::fabs(*v - 3.0) < 1e-9);   // yalniz gercek baska program
    v = other_process_3d_pct(inst, dgpu, 100, {1200, 300});
    EF_CHECK(v && *v == 0.0);
}

// Uyumluluk B4 (bosluk 15): Gorev Yoneticisi yontemi. Tur basina toplam, bagdastirici degeri turlerin en buyugu.
EF_TEST(process_engine_busiest_engine_type_per_adapter) {
    EF_CHECK(engine_instance_type("pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D") == "3d");
    EF_CHECK(engine_instance_type("pid_1_luid_0x0_0x1_phys_0_eng_7_engtype_Compute_0") == "compute_0");
    EF_CHECK(engine_instance_type("pid_1_luid_0x0_0x1_phys_0_eng_7_ENGTYPE_VideoDecode") == "videodecode");
    EF_CHECK(engine_instance_type("pid_1_luid_0x0_0x1_phys_0_eng_7").empty() && engine_instance_type("x_engtype_").empty());

    // Async compute agirlikli oyun: 3D %55, Compute_0 iki motorda 40 + 45 = %85 -> GPU %85 (3D ikincil %55).
    const std::vector<EngineInstance> inst{
        {"pid_9_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 55.0},
        {"pid_9_luid_0x00000000_0x0000C9E5_phys_0_eng_3_engtype_Compute_0", 40.0},
        {"pid_9_luid_0x00000000_0x0000C9E5_phys_0_eng_4_engtype_Compute_0", 45.0},
        {"pid_9_luid_0x00000000_0x0000C9E5_phys_0_eng_5_engtype_Copy", 30.0},
        {"pid_9_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_VideoDecode", 60.0},  // tumlesik kart
    };
    auto p = pick_process_gpu_engine(inst);
    EF_CHECK(p.pct && near_pct(*p.pct, 85.0) && p.luid == "00000000:0000c9e5" && p.engine_type == "compute_0");
    EF_CHECK(p.pct_3d && near_pct(*p.pct_3d, 55.0) && p.adapters == 2);
    // Turler toplanmaz (55 + 85 + 30 degil); 3D baskinsa 3D secilir.
    auto d3 = inst;
    d3[0].pct = 92.0;
    p = pick_process_gpu_engine(d3);
    EF_CHECK(p.pct && near_pct(*p.pct, 92.0) && p.engine_type == "3d" && near_pct(*p.pct_3d, 92.0));
    // Yalniz 3D disi yuk olan bagdastirici secilirse 3D alani bos (uydurulmaz).
    p = pick_process_gpu_engine({{"pid_9_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_VideoDecode", 60.0},
                                 {"pid_9_luid_0x00000000_0x0000C9E5_phys_0_eng_0_engtype_3D", 10.0}});
    EF_CHECK(p.pct && *p.pct == 60.0 && p.luid == "00000000:0000a1b2" && !p.pct_3d);
    // Eski 3D secimi degismez (geri uyumluluk: C3 bagdastirici denetimi bunu kullanir).
    const auto old = pick_process_3d_engine(inst);
    EF_CHECK(old.pct && near_pct(*old.pct, 55.0) && old.luid == "00000000:0000c9e5" && old.adapters == 1);
    // Ornek yok / gecersiz: deger yok.
    EF_CHECK(!pick_process_gpu_engine({}).pct);
    p = pick_process_gpu_engine({{"pid_9_phys_0_eng_0_engtype_3D", 50.0},
                                 {"pid_9_luid_0x0_0x1_phys_0_eng_0", 50.0},
                                 {"pid_9_luid_0x0_0x1_phys_0_eng_0_engtype_3D", std::nan("")},
                                 {"pid_9_luid_0x0_0x1_phys_0_eng_0_engtype_3D", -1.0}});
    EF_CHECK(!p.pct && p.luid.empty() && p.adapters == 0);
}

// Uyumluluk B4 (bosluk 16): joker sayac yeniden genisletme karari.
EF_TEST(process_engine_counter_reexpand_policy) {
    ProcessGpuReexpand s;
    s.opened_ms = s.last_expand_ms = 1000;
    // Ornek goruluyor: 30 sn dolana dek yeniden genisletme yok.
    EF_CHECK(!process_gpu_reexpand_due(s, 1000 + kProcessGpuRetryMs, true));
    EF_CHECK(!process_gpu_reexpand_due(s, 1000 + kProcessGpuLateReexpandMs - 1, true));
    // ~30 sn: bir kez (ornek gorulse bile).
    EF_CHECK(process_gpu_reexpand_due(s, 1000 + kProcessGpuLateReexpandMs, true) && s.late_done);
    EF_CHECK(!process_gpu_reexpand_due(s, 1000 + 3 * kProcessGpuLateReexpandMs, true));  // bir daha yok
    // Ornek yok: en cok kProcessGpuRetryMs'de bir.
    ProcessGpuReexpand n;
    n.opened_ms = n.last_expand_ms = 0;
    EF_CHECK(!process_gpu_reexpand_due(n, kProcessGpuRetryMs - 1, false));
    EF_CHECK(process_gpu_reexpand_due(n, kProcessGpuRetryMs, false) && n.last_expand_ms == kProcessGpuRetryMs);
    EF_CHECK(!process_gpu_reexpand_due(n, kProcessGpuRetryMs + 1, false));
    EF_CHECK(process_gpu_reexpand_due(n, 2 * kProcessGpuRetryMs, false));
    // Gec genisletme de sayilir: ornek yokken 30 sn'de gec genisletme yapilir, sonraki deneme ondan itibaren olculur.
    EF_CHECK(process_gpu_reexpand_due(n, kProcessGpuLateReexpandMs, false) && n.late_done && n.last_expand_ms == kProcessGpuLateReexpandMs);
    EF_CHECK(!process_gpu_reexpand_due(n, kProcessGpuLateReexpandMs + kProcessGpuRetryMs - 1, false));
    EF_CHECK(process_gpu_reexpand_due(n, kProcessGpuLateReexpandMs + kProcessGpuRetryMs, false));
    static_assert(kProcessGpuRetryMs == 5'000 && kProcessGpuLateReexpandMs == 30'000);
}
