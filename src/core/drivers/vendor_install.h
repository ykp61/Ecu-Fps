#pragma once
// ADR-032 (Guncellemeler sayfasi, 2026-11-13): ekran karti ureticisinin RESMI kurulum paketini uygulama icinden kurma.
// Uretici sitelerinden otomatik sorgu / indirme YOKTUR (NVIDIA / Intel site kosullari otomatik sorguyu yasaklar; AMD
// indirmesi amd.com yonlendirmesi + kullanicinin EULA tiklamasi ister). Paketi kullanici kendisi indirir ve uygulamada
// "Dosyadan kur" ile secer; uygulama:
//  1. dosyayi kendi klasorune (%LOCALAPPDATA%\EcuFps\drivers\<16 hex>.exe) .part uzerinden kopyalar (boyut siniri,
//     iptal; yarim kopya hicbir zaman tamam sayilmaz) ve imzayi + imzalayani on denetler (yalniz geri bildirim),
//  2. tek uygulama ici Evet/Hayir -> UAC -> yukseltilmis yardimci (broker) tek izinli islemi calistirir:
//     OperationId::VendorDriverInstall, hedef "vdrv:<nvidia|amd|intel>:<16 hex>", deger "install".
//  3. Yardimci arayuze guvenmez: dosyayi yalniz o klasorden, kimlikten kurdugu adla alir, yonetici ACL'li makine klasorune
//     kopyalar, Authenticode imzasini (WinVerifyTrust) ve imzalayan adini (tam esitlik, kVendorSigner) KOPYA uzerinde
//     yeniden dogrular, boyutu yeniden denetler, journal baslangic kaydi + geri yukleme noktasi + oyun kapisindan sonra
//     paketi ureticinin resmi sessiz anahtarlariyla calistirir, cikis kodunu esler ve kurulu surucu surumunu geri okur.
// Yonga seti ve BIOS bu yolda YOKTUR: AMD yonga seti paketinin resmi sessiz anahtari yok, Intel yonga seti arastirilmadi;
// BIOS hicbir zaman yazilmaz. Bu dosyada platform cagrisi yoktur (arabirimler testte sahte).
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/drivers/driver_install.h"
#include "core/recovery/journal.h"
#include "core/security/trust.h"

namespace ef::core::drivers {

enum class GpuVendor { Nvidia, Amd, Intel };
std::optional<GpuVendor> gpu_vendor_from_pci(std::uint32_t pci_vendor_id) noexcept;
std::string_view to_string(GpuVendor v) noexcept;  // "nvidia" / "amd" / "intel"
std::optional<GpuVendor> parse_gpu_vendor(std::string_view s) noexcept;
std::uint32_t pci_vendor_id(GpuVendor v) noexcept;
// Gorunen ad ("NVIDIA" / "AMD" / "Intel").
std::string_view display_name(GpuVendor v) noexcept;
// Authenticode imzalayan sertifikanin konu adi (CERT_NAME_SIMPLE_DISPLAY_TYPE, tam esitlik; buyuk/kucuk harf dahil).
// Arastirma 2026-11-13: uretici yayinci adlari; GERCEK bir paket uzerinde OKUNMADI (ilk kurulumda Get-AuthenticodeSignature
// ile dogrulanir; state/PENDING_USER.md).
std::string_view vendor_signer(GpuVendor v) noexcept;
// Resmi sessiz kurulum anahtarlari (arastirma kaynaklari ADR-032):
//  NVIDIA: "-s -n Display.Driver" (NVIDIA veri merkezi kurulum kilavuzu; -n yeniden baslatma sormaz)
//  AMD:    "-INSTALL" (AMD komut satiri kurulum PDF'i; -boot otomatik yeniden baslatir, KULLANILMAZ)
//  Intel:  "-s" (Intel makale 000006773; --overwrite surum dusurmeye izin verir, KULLANILMAZ)
std::vector<std::wstring> silent_args(GpuVendor v);

// Paket boyutu siniri: en az 1 MiB, en cok 3 GiB (guncel tam paketler ~0.7-1.7 GiB).
inline constexpr std::uint64_t kPackageMinBytes = 1ull << 20;
inline constexpr std::uint64_t kPackageMaxBytes = 3ull << 30;
bool package_size_ok(std::uint64_t bytes) noexcept;
// Kurulum isinin ust siniri (baslamis kurulum yarida KESILMEZ; sure dolarsa sonuc "zaman asimi", durum dogrulanmaz).
// Yardimci yanit suresi (1 sa) icinde kalir: kopya + imza + geri yukleme noktasi icin pay birakilir.
inline constexpr std::uint32_t kInstallerTimeoutMs = 45u * 60u * 1000u;

inline constexpr std::string_view kVendorTargetPrefix = "vdrv:";
inline constexpr std::string_view kVendorInstallValue = "install";
// Paket kimligi: tam 16 kucuk onaltilik rakam. Dosya adi kimlikten kurulur (<id>.exe): yol, ".." ya da surucu harfi
// tasiyamaz; klasor disindaki bir dosya hic adreslenemez.
bool valid_package_id(std::string_view id) noexcept;
std::string package_file_name(std::string_view id);  // "<id>.exe"; gecersiz kimlikte bos
std::string package_part_name(std::string_view id);  // "<id>.part" (kopya surerken; tamamlaninca yeniden adlandirilir)
struct VendorRef {
    GpuVendor vendor = GpuVendor::Nvidia;
    std::string id;
    friend bool operator==(const VendorRef&, const VendorRef&) = default;
};
std::string vendor_target(GpuVendor v, std::string_view id);  // gecersiz kimlikte bos
std::optional<VendorRef> parse_vendor_target(std::string_view target);

// Imza karari: yalniz Valid + imzalayan tam olarak ureticinin yayinci adi.
enum class SignerVerdict { Ok, Unsigned, Invalid, Untrusted, Unknown, WrongSigner };
SignerVerdict check_signer(security::SignatureStatus status, std::string_view signer, GpuVendor v) noexcept;

// Cikis kodu eslemesi (yalniz arastirmanin dogruladigi kodlar ozel anlam tasir; digerleri Failed):
//  NVIDIA: 0 Ok, 1 OkReboot.
//  Intel:  0 Ok, 3010 / 14 OkReboot (topluluk; dogrulanmadi), 2 NotAdmin, 5 NoDevice, 7 Downgrade, 9 Cancelled.
//  AMD:    0 Ok, 3010 OkReboot (Windows kurallari; AMD resmi listesi yok).
enum class InstallerExit { Ok, OkReboot, NotAdmin, NoDevice, Downgrade, Cancelled, Failed };
InstallerExit map_installer_exit(GpuVendor v, std::uint32_t code) noexcept;

// ---- Kopya (UI tarafi, yukseltme yok; yardimci da ayni kurali makine klasorune kopyalarken kullanir) ----
class ByteSource {
public:
    virtual ~ByteSource() = default;
    // En cok n bayt okur; 0 = dosya sonu; nullopt = okuma hatasi.
    virtual std::optional<std::size_t> read(std::uint8_t* buf, std::size_t n) = 0;
};
class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual bool write(const std::uint8_t* buf, std::size_t n) = 0;
};
enum class CopyOutcome { Ok, TooSmall, TooLarge, Cancelled, ReadFailed, WriteFailed };
struct CopyRun {
    CopyOutcome outcome = CopyOutcome::ReadFailed;
    std::uint64_t bytes = 0;
};
// Sinir asilinca hemen durur (fazlasi yazilmaz). progress(kopyalanan, beklenen toplam; 0 = bilinmiyor). stop() her
// bloktan once sorulur.
CopyRun copy_package(ByteSource& src, ByteSink& dst, std::uint64_t expected_total, const std::function<bool()>& stop,
                     const std::function<void(std::uint64_t, std::uint64_t)>& progress, std::size_t block = 1u << 20);

// ---- Yardimci (broker) kurulumu ----
enum class StageStatus { Ok, NotFound, TooSmall, TooLarge, IoError };
struct StageResult {
    StageStatus status = StageStatus::IoError;
    std::uint64_t bytes = 0;
};
struct SignatureCheck {
    security::SignatureStatus status = security::SignatureStatus::Unknown;
    std::string signer;  // imzalayan konu adi (UTF-8); okunamadiysa bos
};
struct LaunchResult {
    bool started = false;
    bool timed_out = false;
    std::uint32_t exit_code = 0;
};
// Platformda: kullanici klasorunden makine klasorune kopya, WinVerifyTrust, surec, DXGI surum okuma; testte sahte.
class VendorInstallApi {
public:
    virtual ~VendorInstallApi() = default;
    // <kullanici klasoru>\<id>.exe -> <makine klasoru>\<id>.exe (yalniz bu kimlik; boyut siniri kopya sirasinda).
    virtual StageResult stage(const VendorRef& ref) = 0;
    // Makine klasorundeki KOPYANIN imzasi ve imzalayani.
    virtual SignatureCheck verify(const VendorRef& ref) = 0;
    virtual RestorePointState restore_point() = 0;
    // Ureticinin kurulu ekran karti surucusu surumu; okunamadiysa bos.
    virtual std::string installed_version(GpuVendor v) = 0;
    virtual LaunchResult run_installer(const VendorRef& ref, const std::vector<std::wstring>& args, std::uint32_t timeout_ms) = 0;
    // Makine klasorundeki kopyayi siler (her sonucta).
    virtual void discard(const VendorRef& ref) = 0;
};

enum class VendorOutcome {
    Installed,        // cikis kodu basari ve surum degisti (ya da okunamadi; bkz. version_after)
    SameVersion,      // cikis kodu basari ama kurulu surum degismedi
    // Red (hicbir sey calistirilmadi):
    BadTarget, NotFound, TooSmall, TooLarge, Unsigned, BadSignature, Untrusted, WrongSigner,
    // Baslamadi / basarisiz:
    StageFailed, JournalFailed, GateBlocked, Cancelled, LaunchFailed, Timeout, NotAdmin, NoDevice, Downgrade, InstallerCancelled,
    InstallerFailed,
};
std::string_view to_string(VendorOutcome o) noexcept;
std::optional<VendorOutcome> parse_vendor_outcome(std::string_view s) noexcept;
bool is_rejection(VendorOutcome o) noexcept;

struct VendorRun {
    VendorOutcome outcome = VendorOutcome::BadTarget;
    std::uint32_t exit_code = 0;
    bool reboot_required = false;
    RestorePointState restore_point = RestorePointState::NotAttempted;
    bool previous_interrupted = false;
    std::string version_before, version_after;  // bos = okunamadi
};
// StepResult::detail (kullanici metni degil): "vd:<outcome>:ec=<n>:rp=<state>:v=<after>[:reboot][:prev]". Surum yalniz
// rakam ve nokta (en cok 32), aksi halde bos.
std::string encode_vendor_detail(const VendorRun& r);
std::optional<VendorRun> decode_vendor_detail(std::string_view detail);

struct VendorInstallDeps {
    VendorInstallApi* api = nullptr;
    recovery::JournalStore* journal = nullptr;  // makine "vendor_drivers" journal'i; yoksa kurulum baslamaz
    std::function<std::string()> new_tx_id;
    std::function<std::int64_t()> now_unix_ms;
    std::function<bool()> cancel;   // true: kurulum baslamadan birak
    std::function<bool()> gate_ok;  // oyun kapisi (kurulumdan hemen once yeniden sorulur)
    std::string actor_sid, target_sid;
};
// Sira: hedef -> kalici kayit hazir mi -> kopya (boyut) -> imza + imzalayan (kopya uzerinde) -> oyun kapisi -> journal
// baslangic kaydi -> geri yukleme noktasi -> oyun kapisi -> kurulu surum -> kurulum -> cikis kodu -> surum geri okuma ->
// journal kapanir -> kopya silinir. Red ve erken cikista paket hic calistirilmaz.
VendorRun run_vendor_install(const VendorInstallDeps& d, std::string_view target);

}  // namespace ef::core::drivers
