#pragma once
// UI-05: arayuz gorunurluk yasam dongusu. Telemetri/olcum toplayicilari (MEAS) ornekleme hizini buna
// gore secer: gorunur -> ekran hizinda (en fazla 1 Hz UI cizimi), simge durumunda/gizli -> ornekleme yok
// (aktif test haric). Cizim ve toplama ayri is parcaciklarindadir; bu sinif yalniz durumu tasir.

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace ef::ui {

enum class Visibility { Visible, Minimized, Hidden, Closed };

class Activity {
public:
    using Listener = std::function<void(Visibility)>;

    void set(Visibility v);
    Visibility get() const;
    // Abone olma; dondurulen kimlikle birakilir. Dinleyici UI is parcaciginda cagrilir.
    int subscribe(Listener l);
    void unsubscribe(int id);

    // Bos durumda toplayici ornek almali mi? Aktif test ornekleme kararini kendisi verir.
    static bool should_sample_idle(Visibility v) noexcept { return v == Visibility::Visible; }
    // Gorunur ekran icin UI yenileme araligi (ms). Gorunmezken 0 = guncelleme yok.
    static int ui_refresh_ms(Visibility v) noexcept { return v == Visibility::Visible ? 1000 : 0; }

private:
    mutable std::mutex mu_;
    Visibility v_ = Visibility::Hidden;
    int next_ = 1;
    std::vector<std::pair<int, Listener>> listeners_;
};

}  // namespace ef::ui
