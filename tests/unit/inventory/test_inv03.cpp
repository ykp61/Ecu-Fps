#include "core/inventory/devices.h"
#include "ef_test.h"

using namespace ef::core::inventory;
using ef::contracts::Field;
using ef::contracts::Provenance;

EF_TEST(gpu_classification_priority) {
    Provenance src{};
    EF_CHECK(classify_gpu({kVendorMicrosoft, 0x8C, true, false, std::nullopt, 0}, src) == GpuKind::Software);
    EF_CHECK(classify_gpu({kVendorNvidia, 0x2C02, false, true, false, 16000}, src) == GpuKind::Remote);
    // D3D12 UMA bilgisi sezgiselden onceliklidir
    EF_CHECK(classify_gpu({kVendorAmd, 0x13C0, false, false, true, 512}, src) == GpuKind::Integrated);
    EF_CHECK(src == Provenance::Api);
    EF_CHECK(classify_gpu({kVendorAmd, 0x744C, false, false, false, 512}, src) == GpuKind::Discrete);
    // UMA yoksa sezgisel ve Derived
    EF_CHECK(classify_gpu({kVendorNvidia, 0x2C02, false, false, std::nullopt, 16000}, src) == GpuKind::Discrete);
    EF_CHECK(src == Provenance::Derived);
    EF_CHECK(classify_gpu({kVendorIntel, 0xA780, false, false, std::nullopt, 128}, src) == GpuKind::Integrated);
    EF_CHECK(classify_gpu({0x1234, 1, false, false, std::nullopt, 512}, src) == GpuKind::Unknown);
}

EF_TEST(warp_vm_remote_not_benchmark_eligible) {
    GpuAdapter g;
    g.kind = GpuKind::Software;
    EF_CHECK(!hardware_bench_eligible(g, false, false).eligible);
    g.kind = GpuKind::Discrete;
    EF_CHECK(!hardware_bench_eligible(g, true, false).eligible);   // RDP
    EF_CHECK(!hardware_bench_eligible(g, false, true).eligible);   // hipervizor
    EF_CHECK(!hardware_bench_eligible(g, std::nullopt, false).eligible);
    EF_CHECK(hardware_bench_eligible(g, false, false).eligible);
    EF_CHECK(!hardware_bench_eligible(g, false, true).reason.empty());
}

EF_TEST(exe_on_usb_is_not_game_disk) {
    std::vector<VolumeInfo> vols(3);
    vols[0].root = "C:\\";
    vols[0].disk_number = Field<std::uint32_t>::known(0, Provenance::Api);
    vols[1].root = "E:\\";
    vols[1].disk_number = Field<std::uint32_t>::known(2, Provenance::Api);
    vols[2].root = "C:\\Games\\";  // baglama noktasi
    vols[2].disk_number = Field<std::uint32_t>::known(1, Provenance::Api);
    std::vector<StorageDevice> disks(3);
    disks[0].disk_number = 0; disks[0].bus = BusKind::Nvme;
    disks[1].disk_number = 1; disks[1].bus = BusKind::Sata; disks[1].media = MediaKind::Hdd;
    disks[2].disk_number = 2; disks[2].bus = BusKind::Usb;

    const auto* exe = volume_for_path(vols, "e:\\Tools\\EcuFps.exe");
    const auto* game = volume_for_path(vols, "C:\\Games\\CS2\\cs2.exe");
    const auto* sys = volume_for_path(vols, "C:/Windows/System32");
    EF_CHECK(exe && exe->root == "E:\\");
    EF_CHECK(game && game->root == "C:\\Games\\");   // en uzun kok
    EF_CHECK(sys && sys->root == "C:\\");
    EF_CHECK(disk_for_volume(disks, *exe)->bus == BusKind::Usb);
    EF_CHECK(disk_for_volume(disks, *game)->media == MediaKind::Hdd);
    EF_CHECK(disk_for_volume(disks, *game) != disk_for_volume(disks, *exe));
    EF_CHECK(volume_for_path(vols, "Z:\\x") == nullptr);
    VolumeInfo spanned;
    spanned.disk_number = Field<std::uint32_t>::unknown("spans");
    EF_CHECK(disk_for_volume(disks, spanned) == nullptr);
}
