#pragma once
// ADR-033 / spec 7: kurulu surumde zorunlu guncelleme degisimi yardimci hizmet uzerinden (saf mantik; ag, dosya, Win32 yok).
//  UI imzali zarfi ve indirdigi EXE baytlarini parcalar halinde hizmete yollar; hizmet UI'ya HICBIR SEY icin guvenmez:
//  zarf imzasi (gomulu anahtar), kReleaseRepoPrefix, arch/kanal/OS/yayinci, surum > hizmetin kendi surumu, kalici
//  tekrar oynatma tabani, boyut <= kExeMaxBytes ve manifest boyutu, alinan baytlarin SHA-256'si (diskten geri okuma) ve
//  degisim kopyasinin SHA-256'si yeniden denetlenir. Geri donus durumu her degisim adimindan once kalici yazilir.
//  Saglik sinyali yalniz dogrulanmis kurulu istemciden (platform check_client) ve Swapped + ayni surumde kabul edilir.
//
//  Akis: Idle -begin-> Receiving -commit-> [state Swapped, run_swap] -UpdateHealthy-> UiHealthy -> hizmet sifir olmayan
//  cikisla durur, SCM yeni EXE ile yeniden baslatir -> on_service_start Commit (yedek silinir) -> Idle.
//  Swapped iken 60 sn icinde saglik yok (on_tick) veya hizmet Swapped durumunda yeniden basladi (cokme/guc kaybi):
//  RollBack (recover_swap(false): eski EXE geri gelir).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/catalog/envelope.h"
#include "core/ipc/protocol.h"

namespace ef::core::update {

inline constexpr std::size_t kMaxChunkBytes = 48 * 1024;     // hizmetin kabul ettigi en buyuk parca
inline constexpr std::size_t kClientChunkBytes = 32 * 1024;  // UI'nin gonderdigi parca (base64 + JSON < ipc::kMaxPayload)
inline constexpr std::int64_t kServiceHealthWaitMs = 60000;  // Swapped -> saglik bekleme (sonra RollBack)

enum class StagePhase { Idle, Receiving, Swapped, UiHealthy, Committed };

struct StagedUpdateState {
    StagePhase phase = StagePhase::Idle;
    std::string version, from_version;
    std::uint64_t highest_manifest_version = 0;  // asla azalmaz (next_highest_seen)
    std::int64_t swapped_at_ms = 0;
};

std::string encode_state(const StagedUpdateState& s);
contracts::Result<StagedUpdateState> decode_state(std::string_view json);

enum class StageError { None, BadEnvelope, NotNewer, Replay, BadUrl, WrongTarget, TooLarge, OutOfOrder, Overflow, SizeMismatch, HashMismatch,
                        WrongPhase, WriteFailed,
                        Halted };  // P9/B4: imzali acil fren manifesti (sona eklendi)
std::string_view to_string(StageError e) noexcept;

// Hizmet tarafi .part yazicisi (platform: ProgramData\EcuFps\updates\<version>.part, yalniz SY+BA ACL'li klasor).
struct StageSink {
    virtual ~StageSink() = default;
    virtual bool open_part(std::string_view version) = 0;  // yeniden yazmak icin olusturur (eski parca atilir)
    virtual bool write(std::string_view bytes) = 0;
    virtual bool finish() = 0;                       // FlushFileBuffers + kapat
    virtual std::string sha256_hex_readback() = 0;   // diskteki .part yeniden okunup hash'lenir
    virtual void discard() = 0;                      // kapat + sil
};

// Hizmet tarafi alici: ayni anda tek aktarim. Herhangi bir hata .part'i atar ve Idle'a doner.
class UpdateReceiver {
public:
    // ctx.current_version = hizmetin kendi surumu; ctx.last_manifest_version = replay_floor(state.highest_manifest_version).
    UpdateReceiver(catalog::ReleaseContext ctx, catalog::KeyLookup keys, catalog::RawVerifier verify, StageSink& sink)
        : ctx_(std::move(ctx)), keys_(std::move(keys)), verify_(std::move(verify)), sink_(sink) {}

    StageError begin(std::string_view envelope_json);
    StageError chunk(std::uint64_t offset, std::string_view bytes);
    StageError commit();
    void abort();  // baglanti koptu: .part atilir

    bool receiving() const noexcept { return receiving_; }
    bool committed() const noexcept { return committed_; }
    std::uint64_t received() const noexcept { return received_; }
    const catalog::ReleaseManifest& manifest() const noexcept { return manifest_; }

private:
    StageError fail(StageError e);

    catalog::ReleaseContext ctx_;
    catalog::KeyLookup keys_;
    catalog::RawVerifier verify_;
    StageSink& sink_;
    catalog::ReleaseManifest manifest_;
    std::uint64_t received_ = 0;
    bool receiving_ = false, committed_ = false, used_ = false;
};

enum class SupervisorAction { None, RollBack, RestartSelfAfterCommit, Commit };
// Guncelleme sonrasi hizmet durmus kaldiysa (SCM yeniden baslatmadi) yukseltilmis onarim hizmeti baslatmadan once cagirir:
// durum Swapped ve hedef surum CALISAN (kurulum yapan) EXE'nin surumuyse yeni EXE ayakta ve kullanici onayiyla onariliyor demektir;
// durum UiHealthy'ye cekilir (hizmet acilisinda Commit; aksi halde Swapped -> RollBack calisan yeni EXE'yi eskisiyle ezerdi).
// Baska durum / farkli surum degismez. true = durum degisti (kaydedilmeli).
bool adopt_swapped_as_healthy(StagedUpdateState& s, std::string_view running_version);
SupervisorAction on_service_start(const StagedUpdateState& s);  // Swapped -> RollBack; UiHealthy -> Commit; aksi None
SupervisorAction on_tick(const StagedUpdateState& s, std::int64_t now_ms, std::int64_t wait_ms = kServiceHealthWaitMs);

// ---- Yuk kodekleri (standart base64; kesin alan kumesi) ----
std::string base64_encode(std::string_view bytes);
std::optional<std::string> base64_decode(std::string_view text);

ipc::Frame make_update_begin(std::string_view envelope_json, std::uint64_t seq);
ipc::Frame make_update_chunk(std::uint64_t offset, std::string_view bytes, std::uint64_t seq);
ipc::Frame make_update_commit(std::uint64_t seq);
ipc::Frame make_update_healthy(std::string_view version, std::uint64_t seq);

struct ChunkMsg {
    std::uint64_t offset = 0;
    std::string data;  // cozulmus baytlar
};
contracts::Result<std::string> decode_update_begin(std::string_view payload);  // zarf JSON metni
contracts::Result<ChunkMsg> decode_update_chunk(std::string_view payload);
bool decode_update_commit(std::string_view payload);
contracts::Result<std::string> decode_update_healthy(std::string_view payload);  // surum

enum class StatusState { Accepted, Swapped, Committed, Error };
struct UpdateStatusMsg {
    StatusState state = StatusState::Error;
    std::string error;  // yalniz Error (StageError adi veya kisa neden)
};
std::string encode_status(const UpdateStatusMsg& m);
contracts::Result<UpdateStatusMsg> decode_status(std::string_view payload);

// ---- Hizmet tarafi koordinator (saf; platform dosya/saat islerini UpdateHost ile verir) ----
struct UpdateHost {
    virtual ~UpdateHost() = default;
    virtual bool load_state(StagedUpdateState& out) = 0;        // false: yok/bozuk (Idle sayilir; taban 0)
    virtual bool save_state(const StagedUpdateState& s) = 0;    // atomik (tmp + MoveFileEx WRITE_THROUGH) ve flush
    virtual bool stage_part(const std::string& staged) = 0;     // .part -> staged kopyasi (flush)
    virtual bool file_digest(const std::string& path, std::uint64_t& size, std::string& sha256_hex) = 0;  // diskten yeniden
    virtual void remove_part() = 0;
    virtual std::int64_t now_ms() = 0;
};

class UpdateCoordinator {
public:
    // base: current_version (hizmet surumu), arch, kanal, yayinci, os_build, trusted_release_key. last_manifest_version
    // her begin'de kalici durumdan (replay_floor) yeniden hesaplanir.
    UpdateCoordinator(catalog::ReleaseContext base, catalog::KeyLookup keys, catalog::RawVerifier verify, StageSink& sink, UpdateHost& host,
                      catalog::SwapFs fs, catalog::SwapPaths paths);

    // Hizmet acilisi (ServiceMain). RollBack: eski EXE geri getirildi, cagiran sifir olmayan cikisla durmali (eski ikiliye
    // yeniden baslama). Commit: yedek silindi, durum Idle.
    SupervisorAction start();
    // Saniyede bir. RollBack: Swapped iken bekleme doldu, eski EXE geri getirildi.
    SupervisorAction tick();
    // Guncelleme cercevesi (8..11) -> UpdateStatus yaniti; diger turler nullopt (oturum reddeder).
    std::optional<ipc::Frame> on_frame(const ipc::Frame& in);
    void on_disconnect();  // yarim aktarim atilir
    // UpdateHealthy kabul edildi: yanittan sonra hizmet sifir olmayan cikisla durmali (SCM yeni EXE ile baslatir).
    bool stop_requested() const noexcept { return stop_requested_; }
    const StagedUpdateState& state() const noexcept { return state_; }

private:
    ipc::Frame status(StatusState st, std::string error = {});
    ipc::Frame error(StageError e);
    ipc::Frame finish_commit();
    bool roll_back();    // false: eski EXE geri konamadi, durum Swapped kalir (yeniden denenir)
    bool commit_swap();  // false: yedek silinemedi, durum UiHealthy kalir (yeniden denenir)
    bool save(const StagedUpdateState& s);

    catalog::ReleaseContext base_;
    catalog::KeyLookup keys_;
    catalog::RawVerifier verify_;
    StageSink& sink_;
    UpdateHost& host_;
    catalog::SwapFs fs_;
    catalog::SwapPaths paths_;
    StagedUpdateState state_;
    std::optional<UpdateReceiver> rx_;
    bool stop_requested_ = false;
};

}  // namespace ef::core::update
