#pragma once
// Spec 2026-11-12 H (tespit; ADR-031): Guncellemeler sayfasinin cekirdegi. Kaynak yalniz Windows Update'tir (Microsoft'un
// aygit kimligiyle eslestirip imzaladigi suruculer); uretici sitelerine otomatik sorgu yoktur, yalniz resmi sayfa
// baglantisi verilir. Arama yonetici gerektirmez, salt okumadir: hicbir sey indirilmez / kurulmaz.
//  - Windows Update Agent cagrilari DriverSearchSource arabiriminin arkasindadir (platform/windows/wua/); birim
//    testleri sahte kaynak kullanir.
//  - DriverScanner aramayi arka plan is parcaciginda calistirir: iptal edilebilir, zaman asimi vardir. Sonuc
//    hicbir zaman bos bir "Guncel" degildir: arama basarisizsa / zaman asimina ugradiysa / iptal edildiyse Failed.
//  - Firmware sinifi ({f2e7dd72-6468-4e36-b6f1-6488f42c1b52}, UEFI\RES_*) listede "Elle denetlenir" olarak durur; kurulabilir
//    sayilmaz (uygulama BIOS / firmware yazmaz).
// Platform cagrisi yok.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ef::core::drivers {

// Windows Update Agent arama olcutu (spec H; degismez): kurulu olmayan, gizlenmemis surucu guncellemeleri.
inline constexpr std::wstring_view kWuaDriverCriteria = L"IsInstalled=0 and Type='Driver' and IsHidden=0";
// Firmware aygit sinifi (spec H). WUA DriverClass alani sinif adini ("Firmware") ya da GUID'i tasiyabilir.
inline constexpr std::string_view kFirmwareClassGuid = "{f2e7dd72-6468-4e36-b6f1-6488f42c1b52}";
// Aramanin zaman asimi. Gercek cihazda yukseltilmemis arama ~8 sn surdu (2026-11-12); yavas baglantida pay birakilir.
inline constexpr std::chrono::milliseconds kSearchTimeout{120000};

// Windows Update'in onerdigi tek surucu guncellemesi (alanlar WUA'dan; bilinmeyen alan bos / 0 / nullopt).
struct DriverUpdate {
    std::string title;         // IUpdate::Title (UTF-8)
    std::string driver_class;  // IWindowsDriverUpdate::DriverClass (sinif adi ya da GUID)
    std::string hardware_id;   // DriverHardwareID
    std::string manufacturer;  // DriverManufacturer
    std::string model;         // DriverModel
    std::string provider;      // DriverProvider
    std::string version;       // basliktaki surum (version_from_title); bos = yok
    int year = 0, month = 0, day = 0;  // DriverVerDate (yerel tarih parcalari); 0 = yok
    std::optional<std::uint64_t> size_bytes;  // MaxDownloadSize; bilinmiyorsa nullopt
    std::string update_id;   // IUpdateIdentity::UpdateID (H2 kurulumu yeniden arar; arayuz verisine guvenmez)
    int revision = 0;        // IUpdateIdentity::RevisionNumber
    friend bool operator==(const DriverUpdate&, const DriverUpdate&) = default;
};

// Firmware kurali (spec H): sinif Firmware (ad ya da GUID; buyuk/kucuk harf ve suslu parantez farki yok) ya da donanim
// kimligi UEFI\RES_ ile basliyorsa oge yalniz listelenir ("Elle denetlenir"); "Kur" sunulmaz.
bool is_firmware(const DriverUpdate& u);
// Satir siniflandirmasi (spec H satir sirasi): ekran karti, ses, ag (LAN/Wi-Fi/Bluetooth), yonga seti, depolama
// denetleyicisi, diger, firmware (en sonda).
enum class DeviceKind { Display, Audio, Network, Chipset, Storage, Other, Firmware };
DeviceKind device_kind(const DriverUpdate& u);
// WUA basligindaki surum: sondaki parantez ici ya da son " - " parcasi surum gibiyse (rakam ve nokta, en az bir nokta)
// o parca; aksi halde bos. "Apple, Inc. USBDevice Driver Update (552.0.0.0)" -> "552.0.0.0",
// "Intel Corporation - System - 10.1.1.44" -> "10.1.1.44".
std::string version_from_title(std::string_view title);
// Satir adi: baslik, sondaki surum parcasi (" (1.2.3)" / " - 1.2.3") atilmis olarak (surum ayri sutundadir). Surum yoksa
// baslik aynen; baslik bossa model.
std::string display_name(const DriverUpdate& u);

enum class ScanState { NeverChecked, Checking, UpToDate, Found, Failed };
// Failed nedeni. Cancelled: kullanici durdurdu ya da oyun acildi. Error: digerleri (HRESULT saklanir).
enum class ScanFailure { None, Offline, ServiceDisabled, Policy, Timeout, Cancelled, Error };
// WUA / Win32 HRESULT -> neden (bilinmeyen basarisizlik Error; basari kodu None).
ScanFailure classify_hresult(std::int32_t hr);

// Kaynagin dondurdugu ham sonuc. ok=false ise failure ve (varsa) hresult doludur.
struct SearchOutcome {
    bool ok = false;
    ScanFailure failure = ScanFailure::None;
    std::int32_t hresult = 0;
    std::vector<DriverUpdate> updates;
};

// Windows Update aramasi (platformda WUA; testte sahte). stop() true dondugunde arama en kisa surede birakilir ve
// {ok=false, Cancelled} doner. Ayni nesne tek bir arama icindir.
class DriverSearchSource {
public:
    virtual ~DriverSearchSource() = default;
    virtual SearchOutcome search(const std::function<bool()>& stop) = 0;
};

// Sayfanin gordugu sonuc. updates siralidir (sort_updates). installable: firmware olmayan oge sayisi.
struct ScanView {
    ScanState state = ScanState::NeverChecked;
    ScanFailure failure = ScanFailure::None;
    std::int32_t hresult = 0;
    std::vector<DriverUpdate> updates;
    std::size_t installable = 0, list_only = 0;
    friend bool operator==(const ScanView&, const ScanView&) = default;
};
// Satir sirasi: DeviceKind sirasi, sonra baslik (kararli).
void sort_updates(std::vector<DriverUpdate>& v);
// Saf donusum: ok + bos -> UpToDate; ok + oge -> Found (sirali); basarisiz -> Failed (neden None ise Error). Basarisiz
// sonucta oge tasinmaz (yarim liste "guncel" ya da "N bulundu" diye gosterilmez).
ScanView scan_view_from(const SearchOutcome& o);

// Aramayi arka plan is parcaciginda calistirir. Zaman asimi: sure dolunca kaynaga stop() = true verilir ve sonuc
// Timeout olur; kullanici iptali Cancelled. Yikicida iptal + bekleme. on_done is parcaciginda cagrilir (cagiran UI'ye
// postalar). Ayni anda tek arama.
class DriverScanner {
public:
    using SourceFactory = std::function<std::unique_ptr<DriverSearchSource>()>;
    using Done = std::function<void(const ScanView&)>;
    explicit DriverScanner(SourceFactory factory, std::chrono::milliseconds timeout = kSearchTimeout);
    ~DriverScanner();
    DriverScanner(const DriverScanner&) = delete;
    DriverScanner& operator=(const DriverScanner&) = delete;
    // false: zaten calisiyor (ikinci arama baslatilmaz).
    bool start(Done on_done);
    void cancel();
    bool running() const { return running_.load(); }
    // Calisan aramanin bitmesini bekler (test / kapanis).
    void join();

private:
    SourceFactory factory_;
    std::chrono::milliseconds timeout_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::mutex mu_;
    std::thread worker_;
};

// ---- Resmi uretici sayfalari (yalniz kullanici tiklayinca varsayilan tarayicida acilir; otomatik sorgu yok) ----
struct VendorPage {
    std::string_view name;  // gorunen uretici adi ("NVIDIA", "ASUS"); bos = taninmadi
    std::string_view url;   // resmi destek / indirme sayfasi; bos = baglanti yok
};
// Ekran karti: PCI uretici kimligi (0x10DE / 0x1002 / 0x8086).
VendorPage gpu_vendor_page(std::uint32_t pci_vendor_id);
// Yonga seti: islemci ureticisi (CPUID "GenuineIntel" / "AuthenticAMD").
VendorPage chipset_vendor_page(std::string_view cpu_vendor);
// BIOS: anakart ureticisi (SMBIOS; ASUS / MSI / Gigabyte / ASRock taninir).
VendorPage board_vendor_page(std::string_view board_vendor);

}  // namespace ef::core::drivers
