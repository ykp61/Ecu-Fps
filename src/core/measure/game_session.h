#pragma once
// GAME-03: kullanici oynarken dis olcum oturumu (saf; birim testlenir).
//  - Oyunun kendi benchmark/replay'i yoksa standart yol budur.
//  - Menu/yukleme ayrimi YALNIZ kullanici isareti (kisayol/dugme) veya dogrulanmis adaptorden gelir;
//    otomatik tahmin yapilmaz. Isaret yoksa sonuc "belirsiz: menu/yukleme dahil olabilir" etiketi tasir.
//  - Kare olaylari (FrameEvent) ortak ms ekseninde; isaretler ayni eksende.

#include <string>
#include <vector>

#include "core/measure/frame_stats.h"
#include "core/measure/frames.h"

namespace ef::core::measure {

enum class SegmentKind { Gameplay, Menu, Loading };

struct Marker {
    double t_ms = 0;
    SegmentKind kind = SegmentKind::Gameplay;  // bu andan itibaren gecerli bolum
    bool from_user = true;                      // false: dogrulanmis adaptor
};

enum class SegmentationSource { UserMarks, VerifiedAdapter, None };

struct SessionResult {
    FrameSummary frames;           // hedef PID ozeti (overlay/diger PID dislanmis)
    FrameStats gameplay;           // yalniz Gameplay bolumleri (isaret yoksa tum oturum)
    SegmentationSource segmentation = SegmentationSource::None;
    bool uncertain_includes_menu = true;   // isaret yoksa true
    double excluded_ms = 0;        // menu/yukleme olarak isaretlenip dislanan sure
};

// events: tum surecler; target_pid: dogrulanmis oyun sureci; markers: zaman sirali olmayabilir.
SessionResult evaluate_session(const std::vector<FrameEvent>& events, std::uint32_t target_pid, const StreamHealth& health,
                               std::vector<Marker> markers);

}  // namespace ef::core::measure
