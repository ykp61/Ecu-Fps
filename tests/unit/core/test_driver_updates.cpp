// Spec 2026-11-12 H (tespit; ADR-031): Guncellemeler cekirdegi. Sahte arama kaynagi; Windows Update'e baglanilmaz.
// Durumlar: hic denetlenmedi, denetleniyor, guncel, N bulundu, denetlenemedi (cevrimdisi / hizmet kapali / ilke / zaman
// asimi / durduruldu / hata). Bos ya da basarisiz arama asla "Guncel" degildir. Firmware kurali: yalniz listelenir.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#include "core/drivers/driver_updates.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::drivers;
using namespace std::chrono_literals;

namespace {

DriverUpdate upd(std::string title, std::string cls, std::string hwid = {}) {
    DriverUpdate u;
    u.title = std::move(title);
    u.driver_class = std::move(cls);
    u.hardware_id = std::move(hwid);
    u.version = version_from_title(u.title);
    return u;
}

// Sahte kaynak: davranis secilir; stop() geri cagrisini gercek kaynak gibi yoklar.
class FakeSource final : public DriverSearchSource {
public:
    enum class Mode { Result, WaitForStop, LateSuccess, Throw };
    FakeSource(Mode m, SearchOutcome o, std::atomic<int>* started = nullptr) : mode_(m), out_(std::move(o)), started_(started) {}
    SearchOutcome search(const std::function<bool()>& stop) override {
        if (started_) ++*started_;
        switch (mode_) {
            case Mode::Result: return out_;
            case Mode::Throw: throw std::runtime_error("wua");
            case Mode::WaitForStop: {
                for (int i = 0; i < 5000 && !stop(); ++i) std::this_thread::sleep_for(1ms);
                SearchOutcome c;
                c.failure = ScanFailure::Cancelled;
                return c;
            }
            case Mode::LateSuccess: {
                // stop()'u dinlemeyen kaynak: sure dolduktan sonra basariyla doner (sonuc kabul edilmemeli).
                for (int i = 0; i < 5000 && !stop(); ++i) std::this_thread::sleep_for(1ms);
                return out_;
            }
        }
        return out_;
    }

private:
    Mode mode_;
    SearchOutcome out_;
    std::atomic<int>* started_;
};

struct Waiter {
    std::mutex mu;
    std::condition_variable cv;
    std::optional<ScanView> got;
    DriverScanner::Done done() {
        return [this](const ScanView& v) {
            std::lock_guard lk(mu);
            got = v;
            cv.notify_all();
        };
    }
    ScanView wait() {
        std::unique_lock lk(mu);
        cv.wait_for(lk, 10s, [&] { return got.has_value(); });
        return got.value_or(ScanView{});
    }
};

SearchOutcome ok(std::vector<DriverUpdate> v) {
    SearchOutcome o;
    o.ok = true;
    o.updates = std::move(v);
    return o;
}
SearchOutcome failed(ScanFailure f, std::int32_t hr = 0) {
    SearchOutcome o;
    o.failure = f;
    o.hresult = hr;
    return o;
}

ScanView run(FakeSource::Mode m, SearchOutcome o, std::chrono::milliseconds timeout = 5s) {
    DriverScanner sc([m, o] { return std::make_unique<FakeSource>(m, o); }, timeout);
    Waiter w;
    EF_CHECK(sc.start(w.done()));
    const auto v = w.wait();
    sc.join();
    EF_CHECK(!sc.running());
    return v;
}

}  // namespace

EF_TEST(driver_updates_criteria_and_firmware_class_are_the_spec_values) {
    EF_CHECK(kWuaDriverCriteria == L"IsInstalled=0 and Type='Driver' and IsHidden=0");
    EF_CHECK(kFirmwareClassGuid == "{f2e7dd72-6468-4e36-b6f1-6488f42c1b52}");
    EF_CHECK(kSearchTimeout.count() > 0);
}

EF_TEST(driver_updates_firmware_rule_lists_only) {
    EF_CHECK(is_firmware(upd("UEFI", "Firmware")));
    EF_CHECK(is_firmware(upd("UEFI", "firmware")));
    EF_CHECK(is_firmware(upd("UEFI", "{F2E7DD72-6468-4E36-B6F1-6488F42C1B52}")));
    EF_CHECK(is_firmware(upd("UEFI", "f2e7dd72-6468-4e36-b6f1-6488f42c1b52")));
    EF_CHECK(is_firmware(upd("System Firmware 1.2", "OtherHardware", "UEFI\\RES_{a1b2c3d4-0000-0000-0000-000000000000}")));
    EF_CHECK(is_firmware(upd("System Firmware 1.2", "", "uefi\\res_{a1b2}")));
    EF_CHECK(!is_firmware(upd("NVIDIA - Display - 32.0.15.6094", "Display", "PCI\\VEN_10DE&DEV_2C02")));
    EF_CHECK(!is_firmware(upd("x", "System", "ACPI\\UEFI")));  // kimlikte UEFI geciyor ama UEFI\RES_ ile baslamiyor
    // Firmware ogesi kurulabilir sayilmaz; listede kalir (ok sonucta Found, installable 0).
    const auto v = scan_view_from(ok({upd("UEFI - Firmware - 1.0.2", "Firmware")}));
    EF_CHECK(v.state == ScanState::Found);
    EF_CHECK(v.updates.size() == 1 && v.installable == 0 && v.list_only == 1);
    EF_CHECK(device_kind(v.updates[0]) == DeviceKind::Firmware);
}

EF_TEST(driver_updates_rows_sorted_by_spec_order_firmware_last) {
    const auto v = scan_view_from(ok({upd("Fw - 1.0", "Firmware"), upd("Realtek - Net - 10.1", "Net"), upd("Apple USB (552.0.0.0)", "OtherHardware"),
                                       upd("Intel - System - 10.1.1.44", "System"), upd("NVIDIA - Display - 32.0.15.6094", "Display"),
                                       upd("Realtek Audio - 6.0", "MEDIA"), upd("Samsung NVMe - 3.3", "SCSIAdapter"), upd("BT - 1.1", "Bluetooth")}));
    EF_CHECK(v.state == ScanState::Found);
    EF_CHECK(v.updates.size() == 8);
    EF_CHECK(v.installable == 7 && v.list_only == 1);
    const DeviceKind want[] = {DeviceKind::Display, DeviceKind::Audio, DeviceKind::Network, DeviceKind::Network,
                               DeviceKind::Chipset, DeviceKind::Storage, DeviceKind::Other,   DeviceKind::Firmware};
    for (std::size_t i = 0; i < v.updates.size() && i < std::size(want); ++i) EF_CHECK(device_kind(v.updates[i]) == want[i]);
    // Sinif GUID'i ile de ayni siniflandirma.
    EF_CHECK(device_kind(upd("x", "{4d36e968-e325-11ce-bfc1-08002be10318}")) == DeviceKind::Display);
    EF_CHECK(device_kind(upd("x", "{4D36E96A-E325-11CE-BFC1-08002BE10318}")) == DeviceKind::Storage);
}

EF_TEST(driver_updates_version_and_name_from_real_titles) {
    // Gercek WUA basliklari (2026-11-13, bu bilgisayarda yukseltilmemis arama).
    EF_CHECK(version_from_title("Apple, Inc. USBDevice Driver Update (552.0.0.0)") == "552.0.0.0");
    EF_CHECK(version_from_title("Microsoft Corporation AudioProcessingObject Driver Update (1.0.4.55794)") == "1.0.4.55794");
    EF_CHECK(version_from_title("Intel Corporation - System - 10.1.1.44") == "10.1.1.44");
    EF_CHECK(version_from_title("Realtek Semiconductor Corp. - Extension").empty());
    EF_CHECK(version_from_title("Driver (beta)").empty());
    EF_CHECK(version_from_title("x - 12").empty());  // nokta yok: surum sayilmaz
    EF_CHECK(version_from_title("").empty());
    EF_CHECK(display_name(upd("Apple, Inc. USBDevice Driver Update (552.0.0.0)", "OtherHardware")) == "Apple, Inc. USBDevice Driver Update");
    EF_CHECK(display_name(upd("Intel Corporation - System - 10.1.1.44", "System")) == "Intel Corporation - System");
    EF_CHECK(display_name(upd("Realtek - Extension", "Extension")) == "Realtek - Extension");
    DriverUpdate m;
    m.model = "Model X";
    EF_CHECK(display_name(m) == "Model X");
}

EF_TEST(driver_updates_hresult_classification) {
    EF_CHECK(classify_hresult(0) == ScanFailure::None);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x8024402Cu)) == ScanFailure::Offline);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x80072EE7u)) == ScanFailure::Offline);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x80070422u)) == ScanFailure::ServiceDisabled);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x8024001Eu)) == ScanFailure::ServiceDisabled);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x8024002Eu)) == ScanFailure::Policy);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x800705B4u)) == ScanFailure::Timeout);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x8024000Bu)) == ScanFailure::Cancelled);
    EF_CHECK(classify_hresult(static_cast<std::int32_t>(0x80004005u)) == ScanFailure::Error);
}

EF_TEST(driver_updates_view_states_never_an_empty_up_to_date_on_failure) {
    EF_CHECK(ScanView{}.state == ScanState::NeverChecked);  // varsayilan: hic denetlenmedi
    const auto up = scan_view_from(ok({}));
    EF_CHECK(up.state == ScanState::UpToDate && up.updates.empty() && up.failure == ScanFailure::None);
    const auto found = scan_view_from(ok({upd("A - 1.0", "Display"), upd("B - 2.0", "Net")}));
    EF_CHECK(found.state == ScanState::Found && found.updates.size() == 2 && found.installable == 2);
    // Basarisiz arama: oge tasimaz, "Guncel" olmaz; neden korunur, bilinmeyen neden Error.
    auto partial = failed(ScanFailure::Offline, static_cast<std::int32_t>(0x8024402Cu));
    partial.updates = {upd("A - 1.0", "Display")};
    const auto off = scan_view_from(partial);
    EF_CHECK(off.state == ScanState::Failed && off.failure == ScanFailure::Offline && off.updates.empty());
    EF_CHECK(off.hresult == static_cast<std::int32_t>(0x8024402Cu));
    const auto none = scan_view_from(failed(ScanFailure::None));
    EF_CHECK(none.state == ScanState::Failed && none.failure == ScanFailure::Error);
    for (auto f : {ScanFailure::ServiceDisabled, ScanFailure::Policy, ScanFailure::Timeout, ScanFailure::Cancelled, ScanFailure::Error}) {
        const auto v = scan_view_from(failed(f));
        EF_CHECK(v.state == ScanState::Failed && v.failure == f);
    }
}

EF_TEST(driver_scanner_success_up_to_date_and_failures) {
    const auto found = run(FakeSource::Mode::Result, ok({upd("Intel - System - 10.1.1.44", "System")}));
    EF_CHECK(found.state == ScanState::Found && found.updates.size() == 1);
    const auto up = run(FakeSource::Mode::Result, ok({}));
    EF_CHECK(up.state == ScanState::UpToDate);
    const auto off = run(FakeSource::Mode::Result, failed(ScanFailure::Offline, static_cast<std::int32_t>(0x8024402Cu)));
    EF_CHECK(off.state == ScanState::Failed && off.failure == ScanFailure::Offline);
    const auto svc = run(FakeSource::Mode::Result, failed(ScanFailure::ServiceDisabled, static_cast<std::int32_t>(0x80070422u)));
    EF_CHECK(svc.state == ScanState::Failed && svc.failure == ScanFailure::ServiceDisabled);
    const auto pol = run(FakeSource::Mode::Result, failed(ScanFailure::Policy));
    EF_CHECK(pol.state == ScanState::Failed && pol.failure == ScanFailure::Policy);
    const auto thr = run(FakeSource::Mode::Throw, ok({}));
    EF_CHECK(thr.state == ScanState::Failed && thr.failure == ScanFailure::Error);
    // Kaynak uretilemedi (WUA yok): Error, Guncel degil.
    DriverScanner none([] { return std::unique_ptr<DriverSearchSource>{}; });
    Waiter w;
    EF_CHECK(none.start(w.done()));
    const auto nv = w.wait();
    EF_CHECK(nv.state == ScanState::Failed && nv.failure == ScanFailure::Error);
    // Fabrika firlatti (bellek / COM): surec sonlanmaz, Error.
    DriverScanner boom([]() -> std::unique_ptr<DriverSearchSource> { throw std::runtime_error("factory"); });
    Waiter wb;
    EF_CHECK(boom.start(wb.done()));
    const auto bv = wb.wait();
    EF_CHECK(bv.state == ScanState::Failed && bv.failure == ScanFailure::Error);
    // Ne kullanici durdurdu ne sure doldu, ama hizmet aramayi birakti (WU_E_CALL_CANCELLED): zaman asimi / "durduruldu"
    // denmez; hata kodu korunur.
    const auto svc_cancel = run(FakeSource::Mode::Result, failed(ScanFailure::Cancelled, static_cast<std::int32_t>(0x8024000Bu)));
    EF_CHECK(svc_cancel.state == ScanState::Failed && svc_cancel.failure == ScanFailure::Error &&
              svc_cancel.hresult == static_cast<std::int32_t>(0x8024000Bu));
}

EF_TEST(driver_scanner_timeout_and_late_success_are_failed) {
    // Kaynak stop()'u dinler: sure dolunca durur -> Timeout ("Denetlenemedi"), kullanici iptali sayilmaz.
    const auto to = run(FakeSource::Mode::WaitForStop, ok({}), 30ms);
    EF_CHECK(to.state == ScanState::Failed && to.failure == ScanFailure::Timeout);
    // Sure dolduktan sonra gelen basari kabul edilmez (yarim / bayat sonuc gosterilmez).
    const auto late = run(FakeSource::Mode::LateSuccess, ok({upd("A - 1.0", "Display")}), 30ms);
    EF_CHECK(late.state == ScanState::Failed && late.failure == ScanFailure::Timeout && late.updates.empty());
}

EF_TEST(driver_scanner_cancel_and_single_search) {
    std::atomic<int> started{0};
    DriverScanner sc([&started] { return std::make_unique<FakeSource>(FakeSource::Mode::WaitForStop, SearchOutcome{}, &started); }, 10s);
    Waiter w;
    EF_CHECK(sc.start(w.done()));
    for (int i = 0; i < 2000 && started.load() == 0; ++i) std::this_thread::sleep_for(1ms);
    EF_CHECK(sc.running());
    EF_CHECK(!sc.start([](const ScanView&) {}));  // ayni anda tek arama
    sc.cancel();
    const auto v = w.wait();
    EF_CHECK(v.state == ScanState::Failed && v.failure == ScanFailure::Cancelled);
    sc.join();
    EF_CHECK(started.load() == 1);
    // Iptal sonrasi yeni arama baslatilabilir ("Yeniden dene") ve iptal bayragi sifirlanir.
    Waiter w2;
    EF_CHECK(sc.start(w2.done()));
    for (int i = 0; i < 2000 && started.load() < 2; ++i) std::this_thread::sleep_for(1ms);
    sc.cancel();
    EF_CHECK(w2.wait().failure == ScanFailure::Cancelled);
    // Yikici calisan aramayi iptal edip bekler (kapanis kilitlenmez).
    {
        DriverScanner busy([] { return std::make_unique<FakeSource>(FakeSource::Mode::WaitForStop, SearchOutcome{}); }, 10s);
        EF_CHECK(busy.start([](const ScanView&) {}));
    }
}

EF_TEST(driver_updates_vendor_pages_official_only) {
    EF_CHECK(gpu_vendor_page(0x10DE).name == "NVIDIA" && gpu_vendor_page(0x10DE).url == "https://www.nvidia.com/Download/index.aspx");
    EF_CHECK(gpu_vendor_page(0x1002).name == "AMD" && !gpu_vendor_page(0x1002).url.empty());
    EF_CHECK(gpu_vendor_page(0x8086).name == "Intel" && !gpu_vendor_page(0x8086).url.empty());
    EF_CHECK(gpu_vendor_page(0x1414).url.empty() && gpu_vendor_page(0).name.empty());  // Microsoft Basic / bilinmeyen
    EF_CHECK(chipset_vendor_page("GenuineIntel").name == "Intel");
    EF_CHECK(chipset_vendor_page("AuthenticAMD").name == "AMD");
    EF_CHECK(chipset_vendor_page("").url.empty());
    EF_CHECK(board_vendor_page("ASUSTeK COMPUTER INC.").name == "ASUS");
    EF_CHECK(board_vendor_page("Micro-Star International Co., Ltd.").name == "MSI");
    EF_CHECK(board_vendor_page("Gigabyte Technology Co., Ltd.").url == "https://www.gigabyte.com/Support");
    EF_CHECK(board_vendor_page("ASRock").name == "ASRock");
    EF_CHECK(board_vendor_page("Ornek Kart Ltd.").url.empty());  // taninmayan uretici: baglanti uydurulmaz
    for (const auto& vp : {gpu_vendor_page(0x10DE), gpu_vendor_page(0x1002), gpu_vendor_page(0x8086), chipset_vendor_page("AuthenticAMD"),
                           board_vendor_page("asus"), board_vendor_page("msi"), board_vendor_page("gigabyte"), board_vendor_page("asrock")})
    {
        EF_CHECK(vp.url.rfind("https://", 0) == 0);
        EF_CHECK(ef::core::readiness::action_uri_allowed(vp.url));  // App yalniz izin listesindekini acar: dugme olu kalmaz
    }
}
