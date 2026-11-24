#pragma once
// P2 WP2: arka plan yuku azaltma adaylari (SAF, salt okuma). Surec anlik goruntusunden "dusuk oncelige alinabilir"
// programlari secer. Hicbir sey yazmaz, surec acmaz, kapatmaz. Liste derlenmistir; kullanici yalniz GIZLEYEBILIR,
// yeni program EKLEYEMEZ. Esleme ad + imza yayincisi + yol parcasi UC KOSUL BIRDEN (docs/superpowers/plans/
// 2026-11-17-p2-background-load.md bolum 2). Liste icerigi (ad/yayinci/yol) sahip karari bekler; dogrulanmamis ad
// listede yoktur, tahmin edilmez.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::perf {

// Platform katmaninin doldurdugu olculmus surec bilgisi. Eksik bilgi "guvenli degil" yonunde okunur.
struct BgProcessInfo {
    std::uint32_t pid = 0;
    std::uint64_t creation = 0;     // FILETIME; PID yeniden kullanimina karsi kimligin parcasi
    std::string name;               // dosya adi (ornek "discord.exe"), buyuk/kucuk harf duyarsiz
    std::string path;               // tam goruntu yolu; bos = okunamadi
    std::string publisher;          // DOGRULANMIS imza yayincisi; bos = imzasiz/dogrulanamadi
    bool signature_valid = false;   // imza zinciri dogrulandi mi (yayinci yalniz o zaman guvenilir)
    double cpu_pct = 0.0;           // olculmus (diger sureclerin CPU'su gibi sayac farkindan), 0-100 genel
    std::uint64_t working_set_bytes = 0;
    bool foreground = false;        // on plan pencere sahibi
    bool user_session = true;       // calisan kullanicinin oturumunda mi (baska kullanici/oturum 0 = false)
    bool elevated = false;          // yonetici/yuksek butunluk
    bool is_service = false;
    bool is_self = false;           // bu uygulama
    bool is_game_or_launcher = false;  // algilanan oyun sureci ya da oyunun lansoru
    bool busy_transfer = false;     // indirme/yukleme/senkron/kayit suruyor (aktif is)
    bool recent_interaction = false;   // son 60 sn kullanici girdisi/pencere etkilesimi
    bool overlay_hint = false;      // oyun ustu katman (overlay) kullandigi biliniyor
};

enum class BgClass { CloudSync, Peripheral, Chat, Music, Launcher, Browser };
std::string_view to_string(BgClass c) noexcept;
// Kullaniciya gosterilen kisa neden (TR); katalog anahtari degil, ama sabit metin: olculmemis iddia icermez.
std::string_view reason_text_tr(BgClass c) noexcept;

// Surec neden aday DEGIL (testler ve gelecekte "neden yok" gosterimi icin).
enum class BgVerdict {
    Candidate,
    NotListed,         // izinli listede yok (varsayilan: dokunma)
    NeverHardName,     // guvenlik/anti-cheat/surucu/sistem/kabuk/kayit yazilimi
    Self,
    Foreground,        // on plan (program duzeyinde: ayni adli herhangi bir surec on plandaysa)
    GameOrLauncher,
    Service,
    OtherSession,
    Elevated,
    BusyTransfer,
    RecentInteraction,
    Overlay,
    NameMatchedButUnverified,  // ad listede ama imza yok/yayinci ya da yol uyusmuyor (taklit adaylari)
    BelowThreshold,    // olculen CPU esigin altinda (dusuk oncelik fayda getirmez)
    Hidden,            // kullanici gizledi
};
std::string_view to_string(BgVerdict v) noexcept;

// Satirda gosterilen sey: tek program (ayni kural, birden cok surec toplanir; ornek Discord/Chrome).
struct BgProcessRef {
    std::uint32_t pid = 0;
    std::uint64_t creation = 0;
    std::string path;  // eylemde hedef kimligine girer
};
struct BgCandidate {
    std::string rule_id;  // sabit, gizleme anahtari (ornek "discord")
    std::string display_name;
    BgClass cls = BgClass::Chat;
    double cpu_pct = 0.0;                     // toplam, olculmus
    std::uint64_t working_set_bytes = 0;      // toplam, olculmus
    std::vector<BgProcessRef> processes;
    // Bu gecis yalniz Yavaslat (dusuk oncelik + EcoQoS). Kapatma/askiya alma YOK; RAM/VRAM azalmaz.
    bool lower_priority_only = true;
};

struct BgOptions {
    std::vector<std::string> hidden_rule_ids;  // yalniz bilinen kural kimlikleri gecerli; bilinmeyen yok sayilir
    std::size_t max_rows = 5;
    double min_cpu_pct = 0.5;  // altinda dusuk oncelik olculebilir fark yaratmaz
    // Tarayicilar v1'de listede degil (sahip karari 1: sekme kaybi/kimlik). Baglanan akis false verir; saf siniflandirici
    // testleri icin varsayilan true kalir.
    bool include_browsers = true;
};

// Tek surecin hukmu (kural eslesmesi ve "asla" kapilari). `rule_out` yalniz Candidate/BelowThreshold/Hidden icin dolar.
BgVerdict classify_process(const BgProcessInfo& p, const BgOptions& opt, std::string* rule_out = nullptr);

// Tam secim: surecleri kural bazinda toplar, on plan programi program duzeyinde eler, esik+gizleme uygular, CPU'ya
// (esitlikte RAM'e) gore sirali en cok `max_rows` satir dondurur. Giris degismez.
std::vector<BgCandidate> select_background_candidates(const std::vector<BgProcessInfo>& snapshot, const BgOptions& opt = {});

// Dosya adi (buyuk/kucuk harf duyarsiz) izinli listede bir kuralin adi mi? Imza dogrulamasi pahali oldugundan platform
// katmani yalniz bu adlara imza sorar. Sinifi verir; eslesme YETMEZ (yayinci + yol hala gerekir).
std::optional<BgClass> bg_class_for_name(std::string_view file_name);

// Kural kimligi gecerli mi (gizleme listesi dogrulamasi).
bool is_known_rule_id(std::string_view id) noexcept;

}  // namespace ef::core::perf
