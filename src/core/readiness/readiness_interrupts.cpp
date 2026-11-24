#include "core/readiness/readiness_interrupts.h"

namespace ef::core::readiness {

// ADR-040 m.5: YALNIZ OKUMA. Bulgu durumu soyler; tavsiye, sayi vaadi ve "Duzelt" yoktur (ActionKind::None).
//  - Okunamadi (readable=false) veya tanimsiz MSISupported degeri (0/1 disinda)  -> "veri yetersiz" (Level::Unknown).
//  - Okundu ama MSISupported degeri yok                                          -> "desteklenmiyor" (ayri anahtar).
//  - 1 -> Acik, 0 -> Kapali. DevicePriority yalniz olculduyse ayri metin varyantiyla eklenir (IRQ_PRIORITY ham degeri).
std::vector<Check> interrupt_checks(const std::vector<InterruptDeviceInfo>& devices) {
    std::vector<Check> out;
    for (const auto& d : devices) {
        Check c;
        const bool gpu = d.role == InterruptDeviceRole::GameGpu;
        c.id = gpu ? "interrupt.gpu.msi" : "interrupt.net.msi";
        c.section = gpu ? Section::Graphics : Section::Network;
        c.args["name"] = d.name;
        const bool defined = d.msi_supported && *d.msi_supported <= 1;
        if (!d.readable || (d.msi_supported && !defined)) {
            c.level = Level::Unknown;
            c.text_key = "chk.interrupt.no_data";
        } else if (!d.msi_supported) {
            c.level = Level::Info;
            c.text_key = "chk.interrupt.unsupported";
        } else {
            const bool on = *d.msi_supported == 1;
            c.level = Level::Info;
            if (d.priority) {
                c.text_key = on ? "chk.interrupt.msi_on_prio" : "chk.interrupt.msi_off_prio";
                c.args["priority"] = std::to_string(*d.priority);
            } else {
                c.text_key = on ? "chk.interrupt.msi_on" : "chk.interrupt.msi_off";
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ef::core::readiness
