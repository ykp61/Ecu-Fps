#pragma once
// EF-X22: kucuk deterministik GPU hesap/geri okuma dogrulamasi (Uygulama Testi sahne alt testinin icinde;
// ayri bir mod degildir, sistem ayari yazmaz, TDR degistirmez).
//
// Is yuku: D3D11 compute shader (cs_5_0) `words` adet 32 bit kelime uretir. Her kelime ayni cekirdegin CPU
// kopyasi (reference_word) ile hesaplanir; GPU sonucu her tekrarda tamamen geri okunur ve CPU referansiyla
// karsilastirilir. Her tekrar oncesi cikti tamponu sabit bir isaretle (kSentinel) doldurulur: calismayan
// dispatch onceki tekrarin dogru sonucunu "gecti" diye gostermez.
//
// Tolerans: cekirdek yalniz 32 bit tamsayi islemleri ve 2^24 altinda tam temsil edilen float carpma/toplama
// kullanir (a*b+a <= 4095*4095+4095 < 2^24). D3D11 tamsayi aritmetigi tam tanimlidir; float ara degerleri
// tamsayi ve 2^24 altinda oldugu icin mad birlestirmesi veya yuvarlama kipi sonucu degistirmez. Bu yuzden
// bu cekirdekte izin verilen fark SIFIRDIR (Tolerance::exact). Satici/surucu bazli float tolerans tablosu
// YOKTUR: gercek GPU olcumu olmadan deger uydurulmaz (compare_words ULP toleransini destekler, kullanilmaz).
//
// Uyusmazlik "hesap tutarsizligi" uyarisidir: skor verilmez, ama kesin donanim arizasi karari degildir
// (surucu hatasi, asiri hiz ayari, isi veya baska neden olabilir). Cihaz kurulamazsa sonuc Unsupported'dir,
// "gecti" sayilmaz. Sure, bellek ve tekrar sayisi sinirlidir; iptal tekrar aralarinda denetlenir.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::benchmark::gpu_verify {

inline constexpr std::string_view kVerifyVersion = "gpuv1";
inline constexpr std::uint32_t kThreadsPerGroup = 64;          // HLSL [numthreads(64,1,1)] ile ayni
inline constexpr std::uint32_t kSentinel = 0xA5A5A5A5u;        // her tekrar oncesi cikti tamponu
inline constexpr std::uint32_t kMaxWords = 1u << 20;           // 4 MiB cikti tavani
inline constexpr std::uint32_t kMaxIterations = 4096;          // tek dispatch suresi (TDR guvenligi) tavani
inline constexpr std::uint32_t kMaxRepeats = 1024;
inline constexpr std::uint64_t kMaxBytes = 32ull << 20;        // toplam CPU+GPU tampon tavani

// Cekirdegin CPU kopyasi (HLSL shaders/verify.hlsl ile bire bir ayni).
std::uint32_t reference_word(std::uint32_t index, std::uint32_t seed, std::uint32_t iterations) noexcept;
void reference_fill(std::vector<std::uint32_t>& out, std::uint32_t words, std::uint32_t seed, std::uint32_t iterations);

// ---- Tolerans ----
struct Tolerance {
    enum class Kind { Exact, FloatUlp };
    Kind kind = Kind::Exact;
    std::uint32_t max_ulp = 0;  // yalniz FloatUlp
    static constexpr Tolerance exact() noexcept { return {}; }
};
// Iki float bit deseni arasi ULP mesafesi (isaret farki sifirdan gecerek sayilir). NaN veya sonsuz -> nullopt.
std::optional<std::uint64_t> ulp_distance(std::uint32_t a_bits, std::uint32_t b_bits) noexcept;
bool word_matches(std::uint32_t ref, std::uint32_t got, const Tolerance& t) noexcept;

struct CompareResult {
    std::uint64_t mismatched_words = 0;
    std::optional<std::uint32_t> first_mismatch_index;
};
// Uzunluklar farkliysa fazla/eksik kelimelerin tamami uyusmaz sayilir.
CompareResult compare_words(const std::uint32_t* ref, std::size_t ref_n, const std::uint32_t* got, std::size_t got_n,
                            const Tolerance& t) noexcept;

// ---- Yapilandirma ve butce ----
struct Config {
    std::uint32_t words = 1u << 16;     // 256 KiB cikti
    std::uint32_t iterations = 1024;    // kelime basi dongu
    std::uint32_t seed = 0x1D2C3B4Au;
    std::uint32_t min_repeats = 4;      // en az bu kadar tekrar dogrulanmadan "gecti" yok
    std::uint32_t max_repeats = 32;
    std::uint32_t time_budget_ms = 3000;     // hazirlik + tekrarlar
    std::uint32_t wait_timeout_ms = 5000;    // tek tekrar GPU beklemesi (asilirsa gpu_timeout)
    // Yalniz test: >= 0 ise bu tekrarin geri okumasinda kelime 0'in en dusuk biti cevrilir (hatali hesap enjeksiyonu).
    std::int32_t inject_fault_repeat = -1;
};
Config quick_config();
Config full_config();

enum class ConfigVerdict { Ok, WordsInvalid, IterationsInvalid, RepeatsInvalid, TimeInvalid, MemoryOverBudget };
ConfigVerdict validate_config(const Config& c) noexcept;
// Tahmini toplam bellek: CPU referansi + GPU cikti + staging (byte).
std::uint64_t planned_bytes(const Config& c) noexcept;
std::uint32_t dispatch_groups(std::uint32_t words) noexcept;

// Butce: verilen sure ve gozlenen tekrar suresiyle bir tekrar daha baslatilabilir mi (saf).
bool may_start_repeat(std::uint32_t done, const Config& c, double elapsed_ms, double last_repeat_ms) noexcept;

// ---- Sonuc ----
enum class Status { NotRun, Ok, Mismatch, Unsupported, SetupFailed, DeviceRemoved, GpuTimeout, ReadbackFailed, Cancelled, TimeLimit, Invalid };
std::string_view to_string(Status s) noexcept;

struct Result {
    Status status = Status::NotRun;
    std::string error;                    // teknik ayrinti (kayit); kullaniciya neden kodu reason()'dan
    std::wstring adapter;
    std::uint32_t vendor_id = 0, device_id = 0;
    std::uint32_t words = 0, iterations = 0;
    std::uint32_t repeats_done = 0;       // geri okunup karsilastirilan tekrar
    std::uint64_t mismatched_words = 0;   // tum tekrarlar toplami
    std::optional<std::uint32_t> first_mismatch_repeat, first_mismatch_index;
    double prepare_ms = 0, total_ms = 0;
    std::uint64_t bytes = 0;
};

// Sahne alt testine etkisi (saf eslem).
enum class Effect {
    Pass,         // dogrulandi: skor gecerli
    NoClaim,      // dogrulama yapilamadi: skor kalir ama "dogrulandi" denmez (metrik yazilmaz)
    Partial,      // sinira takildi: skor kismi sayilir
    FailNoScore,  // hesap/cihaz hatasi: skor verilmez
    Cancelled,
};
struct Verdict {
    Effect effect = Effect::NoClaim;
    std::string_view reason;  // bench.reason.<kod>; Pass icin bos
};
Verdict verdict(Status s) noexcept;
const char* config_reason(ConfigVerdict v) noexcept;

// Gercek GPU kosusu: yuksek performansli DONANIM adaptoru (WARP/Basic Render kullanilmaz -> Unsupported).
Result run(const Config& c, const std::atomic<bool>& cancel);

}  // namespace ef::benchmark::gpu_verify
