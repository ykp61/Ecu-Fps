#pragma once
// OPT-06: oyun baslatma izleyicisi (saf durum makinesi; birim testlenir).
//  - Girdi: periyodik surec anlik goruntusu (PID + olusturma zamani + exe yolu) ve kurulu oyun klasorleri.
//  - Oyun = exe yolu kurulu bir oyun klasoru altinda ve overlay/yardimci degil. Kimlik PID + olusturma
//    zamani (PID tekrar kullanimi yeni oturum sayilir).
//  - Olay: Started(oyun) / Stopped(oyun). Izleyici oyun surecine DOKUNMAZ (enjeksiyon, okuma, sinyal yok);
//    anti-cheat korumali surecler de yalniz listede gorulur.
//  - Ayni anda birden cok oyun: ilk baslayan "etkin"; o kapanana kadar digerleri olay uretmez (tek profil).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::games {

// Surec listesinin guvenilirligi. Ok: liste tam okundu ve oyun kimligi icin gereken
// alanlar erisilebilen sureclerde tam. Failed: liste/numaralandirma okunamadi; "oyun yok"
// sayilmaz. Incomplete: liste okundu ama bilinen bir oyun EXE adina sahip bir surecin yolu
// veya olusturma zamani okunamadi; oyun yok/oldu karari verilemez.
// Unknown: ilk tarama henuz tamamlanmadi.
enum class DetectionState { Unknown, Ok, Failed, Incomplete };
std::string_view to_string(DetectionState s) noexcept;

struct ProcessInfo {
    std::uint32_t pid = 0;
    std::uint64_t create_time = 0;
    std::string exe_path;   // kucuk harf, utf-8; bos = erisilemedi
    std::string exe_name;   // PROCESSENTRY32W dosya adi (kucuk harf); yol okunamasa da dolar
    bool path_readable = false;  // QueryFullProcessImageNameW basarili mi
};

struct ProcessSnapshot {
    DetectionState state = DetectionState::Failed;
    std::vector<ProcessInfo> processes;
    std::size_t inaccessible_count = 0;  // yolu okunamayan surec sayisi (normal sistem surecleri olabilir)
};

struct GameStart {
    std::string game_id;   // normalize exe yolu (ConsentStore kimligi)
    std::string install_dir;
    std::uint32_t pid = 0;
    std::uint64_t create_time = 0;
};

enum class WatchEventKind { Started, Stopped };
struct WatchEvent {
    WatchEventKind kind;
    GameStart game;
};

struct WatchResult {
    std::vector<WatchEvent> events;       // yalniz guvenilir (Ok) taramada uretilir
    std::vector<GameStart> running;       // guvenilir taramada gorulen TUM kurulu oyunlar
    DetectionState detection = DetectionState::Unknown;
    std::size_t inaccessible_count = 0;
    // Gozlemin ALINDIGI monoton zaman (ms; surec listesi okunmadan once). UI kuyrugunda islendigi zaman
    // degildir: gec islenen eski gozlem yeniymis gibi kabul edilmez. -1 = zaman yok (tazelik kaniti sayilmaz).
    std::int64_t observed_at_ms = -1;
};

// Oyun sayilmayan yardimci ikililer (baslatici, coken-rapor, anti-cheat servis istemcisi...).
bool is_helper_exe(std::string_view exe_lower_name) noexcept;
// Yol veya dosya adindan kucuk harfli basename (bilinen oyun EXE adlari icin).
std::string exe_basename_lower(std::string_view path);

// Yalniz guvenilir ve oyunsuz tarama "oyun yok" kanitidir; Failed/Incomplete/Unknown kanit degildir.
bool poll_proves_no_game(const WatchResult& r) noexcept;

// Kitaplik yeniden kurulumu: oyun klasoru ve surucu profili okumalari (deep) yalniz oyun yokken yapilir.
//  must:   ilk kurulum veya kitaplik degisti; liste guncellenmek ZORUNDA (oyun varsa sig kurulum, derin is borclu kalir).
//  wanted: yeni EXE, yapilan islem sonrasi yenileme veya borclu derin is; oyun varken beklenir.
struct LibraryRebuildInput {
    bool must = false;
    bool wanted = false;
    bool no_game = false;
};
struct LibraryRebuild {
    bool rebuild = false;
    bool deep = false;
};
LibraryRebuild decide_library_rebuild(const LibraryRebuildInput& in) noexcept;

// Oyun klasorundeki her surec oyunun kendisi degildir (on-yukleyici, kurulum adimi): EXE yolu ancak surec
// kesintisiz kMinRunMs boyunca guvenilir taramalarda gorulduyse kalici kaydedilir. Her yol bir kez dondurulur.
class ObservedExeTracker {
public:
    static constexpr std::int64_t kMinRunMs = 60000;
    std::vector<std::string> update(const WatchResult& r, std::int64_t now_ms);

private:
    struct Seen {
        std::string id;
        std::uint32_t pid = 0;
        std::uint64_t create_time = 0;
        std::int64_t first_ms = 0;
        bool reported = false;
    };
    std::vector<Seen> seen_;
};

class GameWatcher {
public:
    // game_exe_names: kurulu oyunlardan bilinen EXE dosya adlari (kucuk harf basename). Yolu
    // okunamayan bir surec bu adlardan biriyle veya DAHA ONCE oyun olarak gozlenen bir adla
    // eslesirse tarama Incomplete olur; her erisim reddi oyun sayilmaz.
    explicit GameWatcher(std::vector<std::string> install_dirs, std::vector<std::string> game_exe_names = {});
    // Kitaplik degisti: etkin oyun ve gozlem gecmisi KORUNUR (yeniden kurulum "Stopped" olayini yutmasin).
    void set_library(std::vector<std::string> install_dirs, std::vector<std::string> game_exe_names);
    WatchResult update(const ProcessSnapshot& snapshot);
    const std::optional<GameStart>& active() const { return active_; }
    bool has_games() const { return !dirs_.empty(); }

private:
    std::optional<std::string> match_dir(const std::string& exe) const;
    bool known_game_exe(std::string_view name) const;
    // B2: erisim kaybinda kimlik/ad korunur. Onceki guvenilir taramalarda oyun olarak gorulen
    // surecler burada tutulur; belirsizlik "yok" sayilmaz.
    struct ObservedProcess {
        std::uint32_t pid = 0;
        std::string exe_name;
    };
    bool observed_name(std::string_view name) const;
    bool observed_pid(std::uint32_t pid, std::string_view name) const;
    void remember_observed(std::uint32_t pid, std::string name);
    std::vector<std::string> dirs_;
    std::vector<std::string> exe_names_;
    std::vector<ObservedProcess> observed_;
    std::optional<GameStart> active_;
};

}  // namespace ef::core::games
