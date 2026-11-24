#pragma once
// MEAS-02: kare olaylarindan hedef secimi ve gecerlilik (saf; ETW/PresentData'dan bagimsiz, birim testlenir).
//  - Hedef surec kimligi PID + olusturma zamani (+ yol) ile dogrulanir; PID yeniden kullanimi reddedilir.
//  - Overlay/yakalama surecleri (Discord, Steam overlay, GameBar, OBS, RTSS...) hedef olamaz.
//  - Ayni surecte birden cok swapchain varsa baskin olan (en cok sunum) secilir; digerleri ayri sayilir.
//  - Render (uygulamanin sundugu), display (ekranda gosterilen) ve generated (surucu/kare uretimi) ayri.
//  - Kayip ETW olayi/tamponu veya bilinmeyen runtime varsa sonuc gecersiz/dusuk guvenli isaretlenir;
//    eksik olaydan iyimser FPS uretilmez.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ef::core::measure {

struct ProcessIdentity {
    std::uint32_t pid = 0;
    std::uint64_t create_time = 0;  // FILETIME (100 ns); 0 = bilinmiyor
    std::string image_path;         // kucuk harf, utf-8; bos = bilinmiyor (yetkisiz oturum "<unknown>")
};
std::string_view exe_name(std::string_view image_path) noexcept;  // son '\' sonrasi

// Bilinen overlay/yakalama ikilileri (kucuk harf exe adi).
bool is_overlay_process(std::string_view exe_lower) noexcept;

enum class Runtime { Dxgi, D3d9, Other, Unknown };
// EF-X18: API basina guven. DXGI/D3D9: runtime olaylari var. Other (Vulkan/OpenGL vb.): yalniz
// cekirdek/ekran olaylari; daha az enstrumantasyon. Unknown: iddia yok. Renderer sezgisinden bagimsiz.
enum class ApiConfidence { RuntimeInstrumented, KernelOnly, Unknown };
std::string_view to_string(ApiConfidence c) noexcept;  // "runtime_instrumented" | "kernel_only" | "unknown"
ApiConfidence api_confidence(Runtime r) noexcept;
enum class FrameType { Application, Generated, Unknown };

struct FrameEvent {
    std::uint32_t pid = 0;
    std::uint64_t swapchain = 0;
    double present_ms = 0;      // ortak zaman ekseni (ms, monoton)
    bool displayed = false;     // ekranda gosterildi mi
    double display_ms = 0;      // gosterildiyse ekran zamani
    FrameType type = FrameType::Application;
    Runtime runtime = Runtime::Unknown;
    // Spec 2026-11-12 B: bu sunumun GPU mesgul suresi (ms; PresentData GPUDuration). < 0 = GPU izleme yok
    // (0 olculmus bir degerdir: GPU paketi gorulmedi). Yalniz uygulama sunumlarinda dolar.
    double gpu_busy_ms = -1;
};

struct StreamHealth {
    std::uint32_t events_lost = 0;
    std::uint32_t buffers_lost = 0;
    bool hags_known = false, hags_on = false;
};

enum class TargetError { None, NoCandidate, IdentityMismatch, Overlay, UnknownProcess };
std::string_view to_string(TargetError e) noexcept;

// Onaylanan oyun kimligine (consent) gore hedef dogrulama.
TargetError check_target(const ProcessIdentity& expected, const ProcessIdentity& observed) noexcept;

enum class Validity { Valid, LowConfidence, Invalid };
std::string_view to_string(Validity v) noexcept;

struct FrameSummary {
    std::uint64_t swapchain = 0;          // secilen baskin swapchain
    std::size_t other_swapchains = 0;     // ayni surecte dislanan swapchain sayisi
    std::size_t render_frames = 0;        // uygulama sunumu
    std::size_t displayed_frames = 0;     // ekranda gosterilen (uygulama + uretilen)
    std::size_t generated_frames = 0;     // uretilen kare (ayri)
    std::vector<double> render_intervals_ms;   // ardisik uygulama sunumlari arasi
    std::vector<double> display_intervals_ms;  // ardisik gosterimler arasi
    // Spec 2026-11-12 B: render_intervals_ms ile BIRE BIR hizali kare basi GPU mesgul suresi (ms): i. deger, i. araligi
    // bitiren sunumun GPU suresidir. Bos = GPU izlenmedi ya da bir karede eksikti (kismi seri tasinmaz, uydurulmaz).
    // GPU suresi surec bazinda olculur: ayni surecin baska takas zincirindeki (ya da ayni zamanli, aralik uretmeyen)
    // sunumuna yazilan pay, izleyen araliga eklenir; boylece seri toplami surecin araliklar boyunca GPU isidir.
    std::vector<double> render_gpu_busy_ms;
    Runtime runtime = Runtime::Unknown;
    Validity validity = Validity::Invalid;
    std::vector<std::string> reasons;     // gecerlilik dusurme nedenleri (metin anahtari degil, kod)
};

// PresentData PresentEvent'in ilkel kopyasi (cekirdek PresentData'ya bagimli olmasin). Sayisal degerler
// yukari akis enum degerleridir (PresentEventEnums.hpp v2.6.0): runtime Other=0 DXGI=1 D3D9=2;
// final_state Unknown=0 Presented=1 Discarded=2; frame_type NotSet=0 Unspecified=1 Application=2
// Repeated=3 Intel_XEFG=50 AMD_AFMF=100.
struct RawPresent {
    std::uint32_t pid = 0;
    std::uint64_t swapchain = 0;
    std::uint64_t start_qpc = 0;
    int runtime = 0;
    int final_state = 0;
    std::vector<std::pair<int, std::uint64_t>> displayed;  // (frame_type, ekran QPC)
    // Spec 2026-11-12 B: PresentEvent::GPUDuration (QPC tik; surecin DMA paketlerinin GPU'da calistigi sure,
    // onceki kare tamamlanmasindan bu kareye). gpu_tracked=false ise GPU izleme kapaliydi (deger anlamsiz).
    std::uint64_t gpu_busy_qpc = 0;
    bool gpu_tracked = false;
};
// Uygulama sunumu bir olay (ilk uygulama gosterimiyle); her uretilen/tekrar gosterim ayri Generated olay.
// Discarded/Unknown sonuc ekranda gosterilmedi sayilir. qpc_to_ms: QPC -> ortak ms ekseni.
void append_frame_events(const RawPresent& p, double ms_per_tick, std::uint64_t qpc_origin, std::vector<FrameEvent>& out);

// Yalniz hedef PID'nin olaylarini ozetler. Diger PID'ler (overlay dahil) yok sayilir.
FrameSummary summarize(const std::vector<FrameEvent>& events, std::uint32_t target_pid, const StreamHealth& health);

}  // namespace ef::core::measure
