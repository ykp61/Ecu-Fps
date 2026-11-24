#pragma once
// Spec 2026-11-12 B (D5): Oyunda Test "bu oyunda ne sinirladi" sonuc gorunumu (saf; birim testlenir).
//  - Metinler katalog anahtaridir; her hukum "Bu oyunda, bu ayarlarda" kapsamiyla sunulur (katalog metni).
//  - Yalniz olculen sayi tasinir: yuzde / FPS alanlari bossa yer tutucu doldurulmaz, 0 yazilmaz.
//  - Kare siniri suphesinde parca hukmu yoktur; "belirlenemedi" nedeniyle birlikte yazilir.
//  - HAGS acikken "GPU suresi yaklasik" notu eklenir. Kapsam notu her zaman vardir.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/game_limit.h"
#include "core/diagnosis/mismatch.h"

namespace ef::core::measure {

struct GameLimitView {
    diagnosis::GameLimitResult result;
    // Oyun surecinin kendi 3D motor kullanimi (en cok kullandigi bagdastirici, LUID bazinda), yakalama boyunca
    // okunan orneklerin ortalamasi (0..100). Sistem toplami degildir; okunamadiysa bos. Uyumluluk B4: ikincil alan.
    std::optional<double> game_gpu_pct;
    // Uyumluluk B4 (bosluk 15): oyunun GPU kullanimi, Gorev Yoneticisi yontemi: en yuksek bagdastiricida (LUID) en
    // yuklu motor turu (3D, Compute_N, ...), ayni orneklerin ortalamasi (0..100). Capraz denetim bunu kullanir.
    std::optional<double> game_gpu_max_pct;
    // Spec 2026-11-12 C3: oyunun 3D yukunu tasiyan bagdastirici yuksek performansli bagdastirici degilse
    // Advice (masaustu) / Info (dizustu); ayniysa ya da bilinmiyorsa None (not yazilmaz).
    diagnosis::GameAdapterLevel adapter_level = diagnosis::GameAdapterLevel::None;
};

// Bir metin satiri: katalog anahtari + olculen yer tutucular ({pct}, {fps}, {cap}); UI yerel bicimle yazar.
struct GameLimitLine {
    std::string key;
    std::optional<double> pct, fps, cap;
};

struct GameLimitTexts {
    GameLimitLine headline;             // "game_limit.headline.<hukum>"
    std::vector<GameLimitLine> lines;   // neden / olculen ek satirlar
    std::vector<std::string> note_keys; // "game_limit.note.*" (ikincil)
};
GameLimitTexts game_limit_texts(const GameLimitView& v);

// Gecmis kaydi icin duz metrikler (yalniz olculenler): gpu_full_frame_pct, gpu_waited_frame_pct, gpu_busy_ratio,
// game_gpu_3d_pct, game_adapter_other (1 = bilgi, 2 = oneri; yalniz farkli bagdastirici olculduyse).
std::map<std::string, double> game_limit_metrics(const GameLimitView& v);

}  // namespace ef::core::measure
