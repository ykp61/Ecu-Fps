#pragma once
// Uyumluluk B2 / bosluk 5: zorunlu cikista yonetici (UAC broker / yardimci hizmet) yaniti beklemesi birakilir.
// Yonetici isi broker/hizmet surecinde surer; makine journal'i ve makine kilidi ondadir. UI yalniz beklemeyi keser:
// birakildiktan sonra yeni istek gonderilmez (send/receive hemen basarisiz), yeni UAC baslatilmaz. Yarim kalan kayitlari
// sonraki oturum basindaki uzlastirma (begin_session / acilis kurtarmasi) kapatir. Yerel journal yazmasi bu kapidan
// etkilenmez: yerel yazma adimin ortasinda birakilmaz.

#include <atomic>
#include <mutex>

namespace ef::platform {

class AdminExitGate {
public:
    void abandon() noexcept { abandoned_.store(true, std::memory_order_release); }
    bool abandoned() const noexcept { return abandoned_.load(std::memory_order_acquire); }
    // Su an broker/hizmet cagrisi icindeki is parcacigi sayisi (UAC, baglanti, yanit beklemesi dahil).
    int active_calls() const noexcept { return active_.load(std::memory_order_acquire); }

    class Call {
    public:
        explicit Call(AdminExitGate& g) : g_(g) {
            std::lock_guard lk(g_.mu_);
            ++g_.active_;
        }
        ~Call() {
            std::lock_guard lk(g_.mu_);  // cikis karari verilirken cagridan cikilamaz (asagida run_if_in_call)
            --g_.active_;
        }
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;

    private:
        AdminExitGate& g_;
    };

    // Bir is parcacigi hala broker/hizmet cagrisindaysa f kilit altinda calisir (ornek: sureci sonlandir); bu sirada
    // cagridan cikip yerel yazmaya gecilemez. Cagrida kimse yoksa f cagrilmaz, false doner.
    template <class F>
    bool run_if_in_call(F&& f) {
        std::lock_guard lk(mu_);
        if (active_.load(std::memory_order_acquire) <= 0) return false;
        f();
        return true;
    }

private:
    std::atomic<bool> abandoned_{false};
    std::atomic<int> active_{0};
    std::mutex mu_;
};

// Surec genelinde tek kapi (yalniz cikista abandon edilir; geri acilmaz).
inline AdminExitGate& admin_exit_gate() {
    static AdminExitGate g;
    return g;
}

}  // namespace ef::platform
