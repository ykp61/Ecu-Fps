#pragma once
// Yetenek ve hazirlik durumlari. Desteklenmeyen, bilinmeyen ve izin reddi ayri tutulur.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::contracts {

// Bir cihaz/surucu/OS ozelliginin durumu.
enum class SupportState { Supported, Unsupported, Unknown, Denied };
std::string_view to_string(SupportState s) noexcept;
std::optional<SupportState> parse_support_state(std::string_view s) noexcept;

// Gerekli yetki duzeyi.
enum class Privilege { User, Admin, Reboot };
std::string_view to_string(Privilege p) noexcept;
std::optional<Privilege> parse_privilege(std::string_view s) noexcept;

// Gelistirme gorevi durumu (backlog). Ozellik hazirligiyla karistirilmaz.
enum class TaskStatus { Planned, InProgress, Blocked, Review, Done };
std::string_view to_string(TaskStatus s) noexcept;
std::optional<TaskStatus> parse_task_status(std::string_view s) noexcept;

// Urun ozelligi hazirligi (release flag). Gorev done olsa bile her cihazda dogrulanmis demek degildir.
enum class FeatureReadiness { NotStarted, Experimental, VerifiedSupported, ReleaseEnabled, Disabled };
std::string_view to_string(FeatureReadiness r) noexcept;
std::optional<FeatureReadiness> parse_feature_readiness(std::string_view s) noexcept;

// Kanit seviyesi: mock, gercek cihaz kaniti sayilmaz.
enum class EvidenceLevel { Mock, Unit, WindowsIntegration, Hardware, ReleaseArtifact };
std::string_view to_string(EvidenceLevel e) noexcept;
std::optional<EvidenceLevel> parse_evidence_level(std::string_view s) noexcept;
constexpr bool is_real_device_evidence(EvidenceLevel e) noexcept {
    return e == EvidenceLevel::Hardware || e == EvidenceLevel::ReleaseArtifact;
}

struct Capability {
    std::string feature_id;        // ornek: "W01", "G02"
    std::string device_scope;      // ornek: "gpu:0", "system"
    SupportState state = SupportState::Unknown;
    std::string provider;          // API/saglayici adi
    std::string provider_version;  // gozlenen surum; tahmin yazilmaz
    std::string reason;            // Unsupported/Unknown/Denied icin zorunlu aciklama
    Privilege required = Privilege::User;
    EvidenceLevel evidence = EvidenceLevel::Mock;
};

// Supported disinda bir durum gerekce olmadan kabul edilmez.
bool capability_is_well_formed(const Capability& c) noexcept;

// Destek kaniti: tam test edilen kombinasyon ve kanit seviyesi.
struct SupportEvidence {
    std::string feature_id;       // ornek: "W01"
    std::string combination;      // ornek: "win11-26H2 / RTX5080 drv 32.0.16.2101"
    SupportState result = SupportState::Unknown;
    EvidenceLevel level = EvidenceLevel::Mock;
    std::string commit;           // kaynak commit veya binary hash
    std::string procedure;        // calistirilan komut/prosedur
    std::string evidence_path;    // artifacts/evidence/...
};

// Release bayragi yalniz gercek cihaz kaniti (Hardware/ReleaseArtifact) + Supported ile acilabilir.
// Mock/unit/integration kaniti tek basina yetmez; ayni ozellikte Denied/Unsupported gercek kanit varsa
// karar cihaz bazli kalir (genel acma yok).
bool may_enable_release(std::string_view feature_id, const std::vector<SupportEvidence>& evidence) noexcept;

}  // namespace ef::contracts
