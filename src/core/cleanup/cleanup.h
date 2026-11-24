#pragma once
// WIN-08: disk temizligi cekirdegi (Win32 cagrisi yok, birim testlenir).
// Temizlik GERI ALINAMAZ bir islemdir; ayar degil. Bu yuzden:
//  - yalniz yeniden olusturulabilir gecici/onbellek kategorileri (Geri Donusum Kutusu istisna, varsayilan secili degil),
//  - once onizleme (gercek dosya sayisi + bayt), sonra kategori bazinda acik secim,
//  - yalniz onizlemede gorulen dosyalar silinir (sonradan eklenen dosyaya dokunulmaz),
//  - kullanimdaki dosya atlanir ve sayilir, yeniden ayristirma noktasina (symlink/junction) inilmez,
//  - her yol izin listesindeki kategori kokunun ALTINDA olmali (kokun kendisi silinmez),
//  - kazanilan alan olculur: basariyla silinen dosya boyutlarinin toplami + birim bos alan once/sonra.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::core::cleanup {

enum class CleanupCategory {
    UserTemp,
    WindowsTemp,
    ShaderCacheDirectX,
    ShaderCacheNvidia,
    ShaderCacheAmd,
    ThumbnailCache,
    WerReportsUser,
    WerReportsSystem,
    DeliveryOptimization,
    RecycleBin,
    AppTestFiles,  // EXT-CLN: uygulamanin kendi yarim kalmis zamanli test klasorleri (EcuFps-bench-*)
    StandbyMemory, // T15: bekleme bellegi (dosya degil; tek seferlik bakim, broker; varsayilan secili degil, tartismali)
};
inline constexpr std::array<CleanupCategory, 12> kAllCategories = {
    CleanupCategory::UserTemp,          CleanupCategory::WindowsTemp,      CleanupCategory::ShaderCacheDirectX,
    CleanupCategory::ShaderCacheNvidia, CleanupCategory::ShaderCacheAmd,   CleanupCategory::ThumbnailCache,
    CleanupCategory::WerReportsUser,    CleanupCategory::WerReportsSystem, CleanupCategory::DeliveryOptimization,
    CleanupCategory::RecycleBin,        CleanupCategory::AppTestFiles,     CleanupCategory::StandbyMemory,
};
// Handoff v1 (docs 06_DISK_TEMIZLIGI) D-05: ilk surumde golgelendirici onbellegi KAPALI (oyun ilk acilista derler,
// koruma/oyun dosyasi sinirina yakin). Motor genel kalir; urun oturumu yalniz bu listeyi tarar ve gosterir.
// Shader kategorileri kAllCategories'te kalir (gelecek surum + motor testleri); urun yolu onlari asla istemez.
inline constexpr bool kShaderCacheEnabledInV1 = false;
inline constexpr std::array<CleanupCategory, 9> kProductCategories = {
    CleanupCategory::UserTemp,       CleanupCategory::AppTestFiles,     CleanupCategory::WindowsTemp,
    CleanupCategory::ThumbnailCache, CleanupCategory::WerReportsUser,   CleanupCategory::WerReportsSystem,
    CleanupCategory::DeliveryOptimization, CleanupCategory::RecycleBin, CleanupCategory::StandbyMemory,
};

// Kategori kokunda hangi dosyalarin aday oldugu.
enum class FileFilter {
    AllRecursive,       // kok altindaki tum dosyalar (alt klasorler dahil)
    ThumbcacheTopLevel, // yalniz kokteki thumbcache_*.db / iconcache_*.db (alt klasor yok)
};

struct CategoryInfo {
    CleanupCategory category;
    std::string_view id;         // kararli kimlik (gunluk/IPC)
    std::string_view title_key;  // i18n anahtari (design/strings/pending/WIN-08.json)
    std::string_view desc_key;
    bool needs_admin;
    bool default_selected;
    bool regenerable;     // icerik uygulama/surucu/OS tarafindan yeniden olusturulur
    bool path_documented; // klasor yolu resmi belgede aciklanmis mi (false: gozlem/ureticiye dayali, varsayilan secili degil)
    FileFilter filter;
    unsigned min_age_days = 0;  // 0 = yas kurali yok; >0: yalniz bu yastan eski dosyalar aday
};
const CategoryInfo& info(CleanupCategory c) noexcept;
std::string_view to_string(CleanupCategory c) noexcept;

// Enjekte edilen ortam (gercek degerler platform katmanindan; testte sahte).
struct CleanupEnv {
    std::wstring local_app_data;  // %LOCALAPPDATA%
    std::wstring temp;            // kullanici TEMP (uzun ad)
    std::wstring windir;          // %WINDIR%
    std::wstring program_data;    // %PROGRAMDATA%
    bool elevated = false;
    // EXT-CLN: dosya yasi icin "simdi" (FILETIME, 100 ns). 0 = yas kurali uygulanmaz (test varsayilani).
    std::uint64_t now_filetime = 0;
    // EXT-CLN: AppTestFiles taramasi icin yerel sabit birim kokleri ("C:\"). Bos + birim secilmemisse kategori bulunamaz.
    std::vector<std::wstring> local_roots;
    // Test/entegrasyon: kategori kokunu tamamen degistirir (gercek kullanici klasoru yerine gecici agac).
    std::map<CleanupCategory, std::vector<std::wstring>> root_override;
};

// Kategorinin izinli kokleri (normalize edilmis). Ortam degeri bos/gecersizse veya kok guvenlik kontrolunu
// gecmezse (surucu koku, Temp adi olmayan TEMP vb.) o kok listeye girmez. RecycleBin icin bos (kabuk API'si).
std::vector<std::wstring> category_roots(CleanupCategory c, const CleanupEnv& env);

// Sozcuksel normalizasyon: "\\?\" oneki atilir, '/' -> '\', '.' atilir, '..' cozulur; yalniz "X:\..." mutlak
// yerel yol kabul edilir. ADS (':'), joker karakter, sonu '.'/' ' ile biten bilesen ve kokten yukari cikan '..'
// reddedilir (nullopt). Sembolik baglanti cozumu YAPILMAZ (platform katmani son yolu ayrica dogrular).
std::optional<std::wstring> normalize_path(std::wstring_view p);

// candidate, root'un kesin ALTINDA mi (kokun kendisi degil). Iki taraf normalize edilir, ASCII buyuk/kucuk harf
// duyarsiz ve sonda ayirac ile karsilastirilir ("C:\Temp2" "C:\Temp" altinda degildir).
// ASCII disi harflerde katlama yapilmaz: fark varsa reddedilir (guvenli tarafta yanlis-negatif).
bool is_path_allowed(std::wstring_view root, std::wstring_view candidate);

// Thumbcache filtresi: thumbcache_*.db veya iconcache_*.db (buyuk/kucuk harf duyarsiz).
bool is_thumbcache_name(std::wstring_view name);

// ---- Dosya sistemi soyutlamasi ----

struct FsEntry {
    std::wstring name;  // yalniz ad (yol degil)
    bool is_dir = false;
    bool is_reparse = false;  // symlink/junction/bulut yer tutucu: asla inilmez/silinmez
    std::uint64_t size = 0;
    // Handoff v1 EXT-CLN-02: tarama anindaki kimlik. Silmeden hemen once tanitici uzerinden karsilastirilir.
    std::uint64_t last_write = 0;     // FILETIME (100 ns); 0 = bilinmiyor
    std::uint64_t file_id = 0;        // NTFS dosya kimligi (birim icinde)
    std::uint32_t volume_serial = 0;
    bool has_id = false;              // file_id/volume_serial okundu mu
    std::uint64_t allocation = 0;     // EXT-CLN-01: diskte ayrilan alan (kume yuvarlamasi; sikistirma/seyrek dosyada farkli)
};

// Silme aninda beklenen kimlik: biri tutmazsa dosya "degisti" sayilir ve atlanir (ezme/yanlis dosya yok).
struct FileExpect {
    std::uint64_t bytes = 0, last_write = 0, file_id = 0;
    std::uint32_t volume_serial = 0;
    bool has_id = false;
};

enum class DirState { Missing, Directory, Reparse, NotDirectory, Denied };

enum class DeleteOutcome {
    Deleted,     // silindi; bytes = tanitici uzerinden olculen boyut
    Locked,      // paylasim ihlali (baska surec kullaniyor)
    Denied,      // erisim reddedildi / salt okunur
    Missing,     // onizlemeden sonra kaybolmus
    Reparse,     // calistirma aninda yeniden ayristirma noktasi
    HardLink,    // birden fazla baglanti: silmek alan kazandirmaz, atlanir
    OutsideRoot, // son (cozulmus) yol kok disinda
    Changed,     // taramadan sonra degismis (kimlik/boyut/zaman): atlanir
    Error,
};
struct DeleteResult {
    DeleteOutcome outcome = DeleteOutcome::Error;
    std::uint64_t bytes = 0;
};

struct RecycleBinInfo {
    std::uint64_t items = 0;
    std::uint64_t bytes = 0;
};

// EXT-CLN-01: secilen birimin kimligi. Ayni surucu harfinin ayni birim oldugu varsayilmaz: calistirmadan hemen
// once seri numarasi yeniden okunur; tutmazsa (disk degisti/cikarildi) hicbir dosyaya dokunulmaz.
struct VolumeId {
    std::wstring root;              // "C:\"
    std::uint32_t serial = 0;       // birim seri numarasi
    std::uint32_t cluster = 0;      // kume boyutu (bayt)
    std::wstring fs;                // "NTFS", "ReFS" ...
    std::wstring label;
    bool known = false;             // seri okundu mu
};

class CleanupFs {
public:
    virtual ~CleanupFs() = default;
    virtual DirState probe_dir(const std::wstring& path) = 0;
    // Dizinin dogrudan girdileri ('.'/'..' yok). Dizin yeniden ayristirma noktasi ise Denied doner.
    virtual contracts::Result<std::vector<FsEntry>> list(const std::wstring& dir) = 0;
    // root altinda dosyayi siler; yeniden ayristirma/kok disi/hardlink kontrolu tanitici uzerinden yapilir.
    virtual DeleteResult delete_file(const std::wstring& root, const std::wstring& path, const FileExpect& expect) = 0;
    // Yalniz BOS ve yeniden ayristirma noktasi olmayan dizini kaldirir.
    virtual bool remove_empty_dir(const std::wstring& root, const std::wstring& path) = 0;
    // Sahipligi onceden dogrulanmis klasorun KENDISINI (kok) yalniz bos ise kaldirir (AppTestFiles).
    virtual bool remove_owned_empty_dir(const std::wstring& path) = 0;
    // Dosyanin ilk max_bytes baytini okur (yalniz sahiplik isareti dogrulamasi; icerik diske yazilmaz).
    virtual contracts::Result<std::string> read_prefix(const std::wstring& path, std::size_t max_bytes) = 0;
    // Yolun bulundugu birim koku ("C:\") ve bos alani; olculemezse nullopt.
    virtual std::optional<std::wstring> volume_of(const std::wstring& path) = 0;
    virtual std::optional<std::uint64_t> free_bytes(const std::wstring& volume) = 0;
    // Birim kimligi (seri/kume/dosya sistemi); okunamazsa nullopt.
    virtual std::optional<VolumeId> volume_id(const std::wstring& volume) = 0;
    // volume bos: tum birimler; "C:\": yalniz o birimin Geri Donusum Kutusu.
    virtual contracts::Result<RecycleBinInfo> query_recycle_bin(const std::wstring& volume) = 0;
    virtual contracts::Status empty_recycle_bin(const std::wstring& volume) = 0;  // arayuzsuz, sessiz
    // T15: bekleme bellegi boyutu (salt okuma; yonetici gerekmez). Okunamazsa hata: sayi gosterilmez.
    virtual contracts::Result<std::uint64_t> standby_bytes() { return contracts::Result<std::uint64_t>::fail(contracts::ErrorCode::Unsupported, "standby unsupported"); }
};

// ---- Onizleme / plan ----

enum class PreviewStatus {
    Ready,       // sayilar olculdu (files 0 olabilir)
    NotPresent,  // kok yok (ornek: NVIDIA yoksa NVIDIA onbellegi)
    NeedsAdmin,  // yukseltilmemis surecte olculmez/calismaz (sayilar yok, tahmin gosterilmez)
    OtherVolume, // EXT-CLN-01: kategori koku secilen birimde degil (bu birimde temizlenecek bir sey yok)
    NotScanned,  // EXT-CLN: tarama iptal/butce nedeniyle bu kategoriye hic girmedi (sayi yok, tahmin yok)
    Error,
};

struct CleanupPreview {
    CleanupCategory category = CleanupCategory::UserTemp;
    PreviewStatus status = PreviewStatus::Error;
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t allocated = 0;      // EXT-CLN-01: diskte ayrilan alan toplami (tahmini kazanim icin)
    std::size_t skipped_locked = 0;   // onizlemede kilit bilinmez (dosya acilmaz); calistirmada sayilir -> 0
    std::size_t skipped_reparse = 0;  // inilmeyen/silinmeyecek yeniden ayristirma girdileri
    std::size_t skipped_unreadable = 0;  // listelenemeyen alt dizinler (erisim yok vb.)
    std::size_t skipped_recent = 0;      // EXT-CLN: yas kuralindan yeni/bilinmeyen (silinmez)
    std::size_t skipped_unverified = 0;  // EXT-CLN: sahipligi dogrulanamayan uygulama test klasoru
    bool partial = false;             // tarama bu kategori ortasinda butce/iptal ile durdu
    bool needs_admin = false;
    std::string detail;
};

struct PlannedFile {
    std::size_t root = 0;  // CategoryPlan::roots indeksi
    std::wstring path;
    std::uint64_t bytes = 0;
    FileExpect expect;  // tarama anindaki kimlik
    std::uint64_t allocation = 0;
};

struct PlannedDir {
    std::size_t root = 0;
    std::wstring path;
};

struct CategoryPlan {
    CleanupPreview preview;
    std::vector<std::wstring> roots;
    std::vector<PlannedFile> files;
    std::vector<PlannedDir> dirs_post_order;  // alt dizinler, en derin once (kok dahil degil)
    bool remove_root_when_empty = false;      // AppTestFiles: sahipligi dogrulanmis klasor bos kalirsa kaldir
};

struct CleanupPlan {
    std::vector<CategoryPlan> categories;
    bool elevated = false;
    std::wstring system_root;  // %WINDIR%: Geri Donusum Kutusu icin bos alan olculen birim
    VolumeId volume;           // EXT-CLN-01: secilen birim (root bos: birim secilmedi, tum kokler)
    bool scan_stopped = false;  // EXT-CLN: tarama dosya/sure butcesine ulasti (sonuclar kismi)
    bool scan_cancelled = false;  // EXT-CLN: kullanici taramayi iptal etti (hicbir sey silinmez)
    const CategoryPlan* find(CleanupCategory c) const noexcept;
};

// Dosya listesi gorunumu (silmeden once): kategorinin en buyuk dosyalari, kok goreli yol. Salt okuma.
struct ListedFile {
    std::wstring relative;  // kok altindaki yol ("sub\a.tmp")
    std::uint64_t bytes = 0;
};
std::vector<ListedFile> largest_files(const CategoryPlan& cp, std::size_t max_items);

inline constexpr std::size_t kMaxDepth = 64;
// EXT-CLN: tarama butcesi. Uzun tarama UI'yi kilitlemez (oturum arka planda calistirir) ve sinirsiz surmez:
// dosya sayisi + sure siniri (varsayilan 200 000 dosya / 15 sn), kullanici iptali her adimda kontrol edilir.
inline constexpr std::size_t kDefaultScanMaxFiles = 200000;
inline constexpr std::uint64_t kDefaultScanBudgetMs = 15000;
struct PreviewOptions {
    const std::atomic<bool>* cancel = nullptr;  // true: tarama durur, plan "scan_cancelled"
    std::size_t max_files = kDefaultScanMaxFiles;   // 0 = sinirsiz (test)
    std::uint64_t time_budget_ms = kDefaultScanBudgetMs;  // 0 = sinirsiz (test)
    std::function<std::uint64_t()> now_ms;  // bos: steady_clock (testte enjekte edilir)
};

// Salt okuma: hicbir sey silinmez. volume verilirse (ornek D:) yalniz o birimdeki kokler taranir; digerleri
// OtherVolume olur. Geri Donusum Kutusu o birimin kutusudur. Butce/iptal plan.scan_stopped/scan_cancelled
// ve kategori bazinda preview.partial/NotScanned olarak raporlanir (uydurma sayi yok).
CleanupPlan preview(const std::vector<CleanupCategory>& categories, const CleanupEnv& env, CleanupFs& fs,
                    const std::wstring& volume = {}, const PreviewOptions& opt = {});

// ---- Calistirma ----

struct CleanupProgress {
    CleanupCategory category = CleanupCategory::UserTemp;
    std::size_t done = 0;   // islenen dosya (Geri Donusum Kutusu tek adim)
    std::size_t total = 0;  // secilen kategorilerdeki toplam dosya (sahte yuzde yok)
    int percent() const noexcept { return total ? static_cast<int>(done * 100 / total) : 0; }
};

struct RunOptions {
    std::function<void(const CleanupProgress&)> on_progress;
    const std::atomic<bool>* cancel = nullptr;  // true olursa sonraki dosyadan once durur (silinenler geri gelmez)
};

struct Tally {
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    void add(std::uint64_t b) noexcept {
        ++files;
        bytes += b;
    }
};

struct CategoryResult {
    CleanupCategory category = CleanupCategory::UserTemp;
    Tally deleted;   // bytes: olculen silinen boyut
    Tally locked;    // bytes: onizleme boyutu
    Tally denied;
    Tally missing;
    Tally reparse;
    Tally changed;   // taramadan sonra degismis: atlandi
    Tally other;     // hardlink / kok disi / hata
    std::size_t dirs_removed = 0;
    bool completed = false;  // iptal/hata olmadan sonuna kadar islendi
};

struct VolumeSpace {
    std::wstring volume;
    std::optional<std::uint64_t> free_before;
    std::optional<std::uint64_t> free_after;
};

struct CleanupReport {
    std::vector<CategoryResult> categories;
    std::vector<VolumeSpace> volumes;
    bool cancelled = false;
    bool incomplete = false;  // bir kategori grubu calisamadi; rapor yalniz tamamlanan kismi tasir
    // T15: bekleme bellegi bosaltma sonucu (broker bakim isi; dosya silme degil, bytes_deleted'a girmez).
    struct Standby {
        bool ran = false;
        bool ok = false;
        std::optional<std::uint64_t> before_bytes, after_bytes;  // yalniz ikisi de OLCULDUYSE dolu
    };
    Standby standby;
    std::string gate_reason;  // yukseltilmis yardimci oyun kapisi nedeniyle durdu (katalog anahtari); bos = yok
    std::size_t processed = 0;
    std::size_t total = 0;
    std::uint64_t bytes_deleted() const noexcept;
    std::size_t files_locked() const noexcept;
};

// selected: kullanicinin ayri ayri onayladigi kategoriler; plan'da Ready olmalidir.
// Yonetici gerektiren kategori yukseltilmemis surecte -> Denied (hicbir sey silinmez).
contracts::Result<CleanupReport> run(const CleanupPlan& plan, const std::vector<CleanupCategory>& selected, CleanupFs& fs,
                                     const RunOptions& opt = {});

// ---- Bellek ici sahte dosya sistemi (birim testleri) ----

class FakeCleanupFs final : public CleanupFs {
public:
    struct Node {
        bool is_dir = false;
        bool is_reparse = false;
        bool locked = false;
        bool denied = false;
        std::uint64_t size = 0;
        std::uint64_t id = 0, mtime = 1;
        std::string content;  // read_prefix icin (AppTestFiles isaret dosyasi)
    };
    std::map<std::wstring, VolumeId> volumes;  // birim -> kimlik (yoksa nullopt)
    std::wstring last_recycle_volume;
    // Yollar tam ve '\' ayiracli verilir; ust dizinler otomatik olusturulur.
    void add_dir(const std::wstring& path, bool reparse = false);
    void add_file(const std::wstring& path, std::uint64_t size, bool locked = false, std::string content = {});
    bool exists(const std::wstring& path) const { return nodes_.count(path) != 0; }
    // Taramadan sonra degisiklik benzetimi: ayni ad, yeni icerik (yeni kimlik) / yalniz zaman degisimi.
    void replace_file(const std::wstring& path, std::uint64_t size);
    void touch(const std::wstring& path);
    Node* node(const std::wstring& path);

    std::map<std::wstring, std::uint64_t> free_space;  // birim -> bos alan
    RecycleBinInfo recycle;
    bool recycle_emptied = false;
    std::function<void(const std::wstring&)> on_delete;  // her silme denemesinden once (iptal testi)
    std::function<void(const std::wstring&)> on_list;    // her list oncesi (tarama iptal/butce testi)
    std::size_t delete_calls = 0;
    std::size_t list_calls = 0;

    DirState probe_dir(const std::wstring& path) override;
    contracts::Result<std::vector<FsEntry>> list(const std::wstring& dir) override;
    DeleteResult delete_file(const std::wstring& root, const std::wstring& path, const FileExpect& expect) override;
    bool remove_empty_dir(const std::wstring& root, const std::wstring& path) override;
    bool remove_owned_empty_dir(const std::wstring& path) override;
    contracts::Result<std::string> read_prefix(const std::wstring& path, std::size_t max_bytes) override;
    std::optional<std::wstring> volume_of(const std::wstring& path) override;
    std::optional<std::uint64_t> free_bytes(const std::wstring& volume) override;
    std::optional<VolumeId> volume_id(const std::wstring& volume) override;
    contracts::Result<RecycleBinInfo> query_recycle_bin(const std::wstring& volume) override;
    contracts::Status empty_recycle_bin(const std::wstring& volume) override;
    contracts::Result<std::uint64_t> standby_bytes() override;
    std::optional<std::uint64_t> standby_value;  // yok = okunamaz

private:
    std::map<std::wstring, Node> nodes_;
    std::uint64_t next_id_ = 0;
};

}  // namespace ef::core::cleanup
