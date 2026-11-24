#pragma once
// BENCH-05: depolama alt testi (Win32). Kurallar storage_rules.h icindedir.
// Akis: hedef dogrulama -> surucu tipi/yonlendirme kontrolu -> bos alan alt siniri -> benzersiz klasor + isaret ->
// CREATE_NEW test dosyasi (son yol + link sayisi dogrulamasi) -> rastgele veriyle hazirlik -> profiller -> temizlik.
// Ham disk/volume taniticisi hic acilmaz; var olan dosya hic acilmaz (CREATE_NEW).

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/storage/storage_rules.h"

namespace ef::benchmark::storage {

struct RunConfig {
    std::wstring volume_root;  // kullanici secimi: "X:\"
    Limits limits;             // varsayilan == quick_limits(); TAM kosu icin full_limits()
    std::vector<Profile> profiles = default_profiles();
    bool verify = true;  // EF-X22: hazirliktan sonra sinirli yaz-oku-karsilastir (birlesik yukte kapali)
};

struct RunResult {
    Status status = Status::Failed;
    std::string reason;          // ASCII anahtar (ornek: free_space_floor, raw_device_refused)
    std::wstring volume_root;    // normalize kok
    std::wstring folder;         // olusturulan test klasoru (temizlik sonrasi var olmamali)
    std::uint64_t volume_total_bytes = 0;
    std::uint64_t volume_free_bytes = 0;
    std::uint64_t required_free_bytes = 0;
    std::uint32_t sector_bytes = 0;  // PhysicalBytesPerSectorForPerformance
    std::uint64_t bytes_written = 0;  // hazirlik + yazma profili (butceye dahil)
    std::uint64_t write_budget_bytes = 0;  // bu kosunun yazma ust siniri (onceden gosterilen deger)
    double preparation_seconds = 0.0;  // olcume karismaz, ayrica raporlanir
    std::vector<ProfileResult> profiles;
    bool cleanup_ok = false;  // klasor ve tum dosyalar silindi (ya da hic olusturulmadi)
    VerifyResult verify;  // EF-X22; NotRun = faz kosulmadi (iddia yok). bytes_written'a dahildir.
};

// cancel true olunca en gec bir G/C turu icinde durur; temizlik her durumda calisir (RAII).
RunResult run_storage_test(const RunConfig& cfg, std::atomic<bool>& cancel);

// ---- Dusuk seviye: guvenli yeni dosya (testler icin de acik) ----
enum class CreateOutcome { Created, Exists, FolderReparse, FolderRedirected, Escaped, HardLinked, Error };
const char* create_outcome_text(CreateOutcome o);

// Tasinabilir, yalniz-tasima tanitici sarmalayici (HANDLE'i basliga sizdirmaz).
class OwnedFile {
public:
    OwnedFile() = default;
    ~OwnedFile();
    OwnedFile(OwnedFile&& o) noexcept;
    OwnedFile& operator=(OwnedFile&& o) noexcept;
    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;
    void* handle() const { return h_; }
    bool valid() const { return h_ != nullptr; }
    void reset();
    void adopt(void* h);

private:
    void* h_ = nullptr;
};

enum class FileKind {
    Unbuffered,  // NO_BUFFERING | WRITE_THROUGH | OVERLAPPED | DELETE_ON_CLOSE, paylasim yok
    Buffered,    // tamponlu | DELETE_ON_CLOSE, paylasim yok
    Marker,      // tamponlu, yalniz okuma paylasimi (yasayan testin klasoru yetim sanilip silinemez)
};
// folder: mevcut, baglanti olmayan klasor; yolundaki hicbir bilesen reparse point olamaz.
// Dosya CREATE_NEW ile acilir; sonra son yol folder altinda ve link sayisi 1 olmali, degilse silinir ve reddedilir.
CreateOutcome create_owned_file(const std::wstring& folder, std::wstring_view name, FileKind kind, OwnedFile& out);

// ---- Yetim temizligi ----
struct OrphanReport {
    std::uint32_t scanned = 0;  // ad deseni uyan klasor
    std::uint32_t removed = 0;
    std::uint32_t kept = 0;     // isaretsiz/yabanci icerik/baglanti/kilitli
    std::vector<std::wstring> kept_folders;
};
// parent_dir altindaki EcuFps-bench-* klasorlerinden yalniz bizim isaretimizi ve bilinen dosyalarimizi
// iceren klasorleri siler. Normal kullanim: volume koku. Yasayan testin klasoru (kilitli) korunur.
OrphanReport cleanup_orphans(const std::wstring& parent_dir);

}  // namespace ef::benchmark::storage
