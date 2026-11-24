#pragma once
// BENCH-07..09 (plan v3.5): Uygulama Testi sonucunun bilesen kartlari (saf; birim testlenir).
// Kosucunun urettigi gercek metrik anahtarlarindan (scene_*, cpu_*, mem_*, storage_*) kart uretir.
//  - Olculmeyen deger kartta "Olculemedi" olur; 0 yazilmaz.
//  - Atlanan/basarisiz alt test kartta durum metniyle gosterilir (neden anahtari ile).
//  - Disk yazma "burst" ise not dusulur (surekli hiz iddiasi yok).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ef::core::measure {

enum class TestMode { Quick, Full };
std::string_view to_string(TestMode m) noexcept;

struct CardMetric {
    std::string label_key;          // "bench.metric.avg_fps"
    std::optional<double> value;    // yoksa "Olculemedi"
    std::string unit_key;           // "unit.fps"
    int decimals = 0;
    std::string note_key;           // ornek "bench.note.burst" (bos = yok)
    // EF-X22 dogrulama karti: sayi yerine durum metni (bos = sayisal deger). Ton: "success" | "error" | "warning" | "".
    std::string value_key;
    std::string value_tone;
    std::map<std::string, std::string> note_args;  // not metnindeki {ad} yer tutuculari (yalniz gercek sayilar)
};

struct ComponentCard {
    std::string id;                 // "gpu" | "cpu" | "memory" | "storage" | "combined"
    std::string title_key;          // "bench.card.gpu"
    std::string status;             // "completed" | "partial" | "skipped" | "failed" | "unavailable" | "cancelled" | ""
    std::string reason;             // atlanma/hata nedeni (ASCII anahtar; UI "bench.reason.<x>" metnine cevirir)
    std::vector<CardMetric> metrics;
    std::map<std::string, std::string> args;  // {"tier":"high","res":"1920x1080","volume":"C:\\"}
};

struct SubtestInfo {
    std::string status, reason;
    // Kosucunun metin parametreleri. Depolama bos alan atlamasi (bosluk 7): kStorageNeedFreeBytesArg /
    // kStorageAvailFreeBytesArg varsa kart nedeni "free_space_short" olur ve args'a {need} (GB, yukari) / {free} (GB, asagi) yazilir.
    std::map<std::string, std::string> args;
};

// Bosluk 7: kosucu (runner.cpp) ile kart (bench_cards.cpp) arasindaki ortak arg anahtarlari; ikisi de buradan okur.
inline constexpr const char* kStorageNeedFreeBytesArg = "need_free_bytes";
inline constexpr const char* kStorageAvailFreeBytesArg = "avail_free_bytes";

// Kosucunun depolama sonucu -> SubtestResult.args eslemesi (saf). Yalniz atlanmis + bos alan nedeni
// ("free_space_floor" / "free_share_limit") + birim boyutu olculmus (volume_total_bytes > 0) ise iki anahtar uretir;
// aksi halde bos. Degerler olculmus byte'in ondalik metnidir.
std::vector<std::pair<std::string, std::string>> storage_free_space_args(bool skipped, std::string_view reason, std::uint64_t volume_total_bytes,
                                                                         std::uint64_t required_free_bytes, std::uint64_t volume_free_bytes);

// EF-X22: Uygulama Testi sonucunda "verify" karti (ayri mod degil): GPU / CPU / depolama dogrulamasi her biri
// gecti / hesap tutarsizligi / desteklenmiyor / tamamlanmadi / calismadi. Durum <onek>verify_state kodundan
// (verify_state.h); sayilar yalniz gercek sayac metriklerinden. Hicbir dogrulama anahtari olmayan (eski) kayitta kart yok.
// metrics: kosucu metrikleri; subtests: alt test kimligi -> durum/neden; args: kart parametreleri.
std::vector<ComponentCard> component_cards(const std::map<std::string, double>& metrics, const std::map<std::string, SubtestInfo>& subtests,
                                           const std::map<std::string, std::string>& args = {});

}  // namespace ef::core::measure
