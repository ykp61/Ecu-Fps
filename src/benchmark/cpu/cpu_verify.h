#pragma once
// EF-X22: islemci hesap dogrulamasi (degisken yuk + CPUID etiketli SIMD varyantlari). Uygulama Testi CPU alt
// testinin icinde, CPU olcumunden SONRA kosar; ayri bir mod degildir, sistem ayari yazmaz, oncelik/affinity
// degistirmez.
//
// Is yuku: her "blok" kWordsPerBlock adet 32 bit kelimedir. Girdi kelimeleri yalniz (tohum, blok, indeks)
// degerlerinden skaler olarak turetilir; ardindan ayni donusum (xorshift + tur sabiti + dondurme, yalniz
// tamsayi kaydirma/xor/toplama) secilen komut seti varyantiyla uygulanir:
//   scalar : duz C++ (vektorlestirme kapali)
//   sse2   : 128 bit SSE2 (x64'te her zaman var, yine de CPUID ile okunur)
//   avx2   : 256 bit AVX2 (CPUID.7:EBX[5] + CPUID.1:ECX[AVX,OSXSAVE] + XCR0 YMM durumu)
//   avx512 : 512 bit AVX-512F (CPUID.7:EBX[16] + OSXSAVE + XCR0 opmask/ZMM durumu)
// Her blokta varyant sonucu bagimsiz skaler referansla KELIME KELIME TAM ESITLIKLE karsilastirilir (tamsayi
// aritmetigi: tolerans yok). Desteklenmeyen varyant "desteklenmiyor" olarak raporlanir; basarisizlik sayilmaz
// ve calistirilmaz (gecersiz komut hatasi yok).
//
// Degisken yuk: toplam sure dort esit faza bolunur; her fazda is parcaciklari slice_ms periyotta yuk payi kadar
// calisir, kalaninda uyur (%100, %25, %75, %50). Hesap her fazda ayni dogrulamadan gecer: yuk gecislerinde
// (saat/gerilim degisimi) ortaya cikan tutarsizlik da yakalanir.
//
// Uyusmazlik "hesap tutarsizligi" uyarisidir: CPU skoru verilmez, ama kesin donanim arizasi karari DEGILDIR
// (hiz/gerilim ayari, isi veya baska neden olabilir). Sure, bellek (is parcacigi basina 3 blok tamponu) ve blok
// sayisi sinirlidir; iptal her bloktan once denetlenir.

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ef::benchmark::cpu_verify {

inline constexpr std::string_view kVerifyVersion = "cpuv1";
inline constexpr std::uint32_t kWordsPerBlock = 1024;  // 4 KiB; 16'nin kati (AVX-512 serit sayisi)
inline constexpr std::uint32_t kMaxRounds = 4096;
inline constexpr std::uint32_t kMaxThreads = 64;
inline constexpr std::uint32_t kMaxTimeBudgetMs = 60'000;

enum class Isa : std::uint32_t { Scalar = 0, Sse2 = 1, Avx2 = 2, Avx512 = 3 };
inline constexpr std::uint32_t kIsaCount = 4;
inline constexpr std::uint32_t isa_bit(Isa i) { return 1u << static_cast<std::uint32_t>(i); }
inline constexpr std::uint32_t kAllIsas = (1u << kIsaCount) - 1u;
// Kararli ASCII ad (metrik anahtari): "scalar", "sse2", "avx2", "avx512".
std::string_view isa_name(Isa i) noexcept;

// ---- CPUID / isletim sistemi destegi ----
struct CpuidBits {
    bool cpuid_ok = false;  // CPUID okunabildi (x64 disi derlemede false)
    bool sse2 = false;      // CPUID.1:EDX[26]
    bool osxsave = false;   // CPUID.1:ECX[27]: isletim sistemi XSAVE/XGETBV kullaniyor
    bool avx = false;       // CPUID.1:ECX[28]
    bool avx2 = false;      // CPUID.(7,0):EBX[5]
    bool avx512f = false;   // CPUID.(7,0):EBX[16]
    std::uint64_t xcr0 = 0; // XGETBV(0); yalniz osxsave ise okunur
};
// Gercek islemciden okur (salt okuma, yetki gerekmez).
CpuidBits read_cpuid() noexcept;
// Saf: varyant bu islemci + isletim sistemi durumunda calistirilabilir mi. AVX2: avx + avx2 + osxsave + XCR0[1,2];
// AVX-512F: avx512f + osxsave + XCR0[1,2,5,6,7]. Scalar her zaman; SSE2 CPUID.1:EDX[26].
bool isa_supported(Isa i, const CpuidBits& b) noexcept;

// ---- Hesap cekirdegi ----
// Blok girdisi (skaler; tum varyantlar ayni girdiyi kullanir).
void fill_input(std::uint32_t* out, std::uint32_t seed, std::uint64_t block) noexcept;
// Tur sabiti (skaler hesaplanir, varyantlara yayinlanir).
std::uint32_t round_constant(std::uint32_t seed, std::uint32_t round) noexcept;
// Bagimsiz skaler referans (kelime kelime); test bunu ayrica elle yazilmis bir referansla dogrular.
std::uint32_t reference_word(std::uint32_t input, std::uint32_t seed, std::uint32_t rounds) noexcept;
// Varyant donusumu: in -> out, kWordsPerBlock kelime. Desteklenmeyen varyant cagirilmamalidir (cagiran denetler).
void transform(Isa isa, const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept;

// ---- Yapilandirma ve butce ----
struct Config {
    std::uint32_t seed = 0x5EED'C0DEu;
    std::uint32_t rounds = 64;              // kelime basi tur
    std::uint32_t threads = 0;              // 0 = tum islemci gruplarindaki mantiksal CPU (kMaxThreads tavani)
    std::uint32_t time_budget_ms = 3000;    // dort fazin toplami
    std::uint32_t slice_ms = 40;            // yuk payi periyodu
    std::uint64_t max_blocks = 0;           // 0 = yalniz sure siniri
    std::uint32_t min_blocks_per_isa = 4;   // desteklenen her varyant en az bu kadar blok dogrulanmadan "gecti" yok
    std::uint32_t isa_mask = kAllIsas;      // calistirilacak varyantlar (destek yine CPUID'den)
    // Yalniz test kancalari:
    std::optional<CpuidBits> cpuid_override;  // CPUID yerine bu bitler (desteklenmeyen varyant benzetimi)
    std::int64_t inject_fault_block = -1;     // >= 0: bu blogun varyant ciktisinda kelime 0'in en dusuk biti cevrilir
    std::int32_t inject_thread_start_failure = -1;  // >= 0: bu siradaki is parcacigi baslatilamamis gibi (istisna)
    bool inject_worker_throw = false;               // ilk is parcacigi calisirken istisna (bellek ayrilamadi benzetimi)
};
Config quick_config();  // ~3 sn
Config full_config();   // ~8 sn

enum class ConfigVerdict { Ok, RoundsInvalid, ThreadsInvalid, TimeInvalid, SliceInvalid, NoIsa };
ConfigVerdict validate_config(const Config& c) noexcept;
const char* config_reason(ConfigVerdict v) noexcept;  // "cpu_verify_invalid" ya da ""
// Dort yuk fazinin payi (yuzde): 100, 25, 75, 50.
inline constexpr std::array<std::uint32_t, 4> kPhaseDutyPercent{100, 25, 75, 50};
// Saf: gecen sureye gore faz indeksi (0..3); butce asildiysa 4.
std::uint32_t phase_at(double elapsed_ms, std::uint32_t budget_ms) noexcept;
// Saf: periyot icindeki konuma gore calisma payinda mi.
bool busy_at(double elapsed_ms, std::uint32_t slice_ms, std::uint32_t duty_percent) noexcept;

// ---- Sonuc ----
enum class Status { NotRun, Ok, Mismatch, Incomplete, Cancelled, Invalid };
std::string_view to_string(Status s) noexcept;

struct IsaResult {
    Isa isa = Isa::Scalar;
    bool supported = false;        // CPUID + OS destegi var ve isa_mask secili
    std::uint64_t blocks = 0;      // karsilastirilan blok
    std::uint64_t mismatched_blocks = 0;
    std::uint64_t mismatched_words = 0;
};

struct Result {
    Status status = Status::NotRun;
    std::array<IsaResult, kIsaCount> isas{};
    std::array<std::uint64_t, 4> phase_blocks{};  // faz basina karsilastirilan blok
    std::uint64_t blocks = 0;
    std::uint64_t mismatched_blocks = 0;
    std::optional<std::uint64_t> first_mismatch_block;
    std::optional<Isa> first_mismatch_isa;
    std::uint32_t threads = 0;
    double total_ms = 0;
    bool cancelled = false;
    // Is parcacigi baslatilamadi veya calisirken istisna (bellek vb.): dogrulama eksik, "gecti" denmez.
    bool worker_failed = false;
};

// Saf: sayaclardan durum. Uyusmazlik iptalde de korunur; desteklenen bir varyant min_blocks_per_isa altinda
// kaldiysa veya bir is parcacigi baslatilamadi/istisna verdiyse Incomplete ("gecti" denmez).
Status decide(const Result& r, const Config& c, bool cancelled) noexcept;

// Gercek kosu (std::thread; oncelik/affinity degistirilmez).
Result run(const Config& c, const std::atomic<bool>& cancel);

}  // namespace ef::benchmark::cpu_verify
