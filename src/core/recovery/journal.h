#pragma once
// REC-01: dayanikli islem gunlugu. Her islem (transaction) tek dosya; atomik yazim, sema surumu,
// SHA-256 butunluk, makine baglama, kota. Journal yazilamazsa degisiklik BASLAMAZ.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/serialization/json.h"

namespace ef::core::recovery {

inline constexpr int kJournalSchema = 1;

// Ayar anlik goruntusu (docs/04 Snapshot gereksinimi)
struct Snapshot {
    bool existed = false;          // deger/anahtar onceden var miydi
    std::string old_value;         // kodlanmis eski deger (adaptor codec'i)
    std::string raw_type;          // "REG_DWORD", "guid", "int" ...
    bool inherited = false;        // varsayilan/global'den mi geliyordu (NVIDIA profili vb.)
    std::string source_version;    // OS/surucu/oyun surumu
    std::string raw;               // istege bagli: ham bayt (hex) birebir geri yazmak icin
};

// KeptExternal: catismada kullanici dis degeri korumayi secti (REC-04, "kismen geri alindi").
enum class StepState { Planned, Intent, Applied, Verified, RolledBack, Failed, KeptExternal };
std::string_view to_string(StepState s) noexcept;
std::optional<StepState> parse_step_state(std::string_view s) noexcept;

struct JournalStep {
    contracts::SettingOperation op;
    Snapshot snapshot;
    StepState state = StepState::Planned;
    std::string last_written;      // uygulamanin yazdigi son deger (cakisma tespiti icin)
    std::string error;
    // Saha 2026-11-15 (istege bagli; eski kayitlarda yok): basarisiz geri alma/dogrulamada OKUNAN deger ("<absent>" =
    // deger yok, bos = okunamadi/kaydedilmedi) ve hata kodu (contracts::to_string(ErrorCode); bos = kod yok). Destek
    // paketi "hangi ayar, beklenen, okunan, kod" bilgisini buradan alir. Adim geri alininca temizlenir.
    std::string observed;
    std::string error_code;
};

struct JournalTx {
    std::string id;                 // 32 hex
    int schema_version = kJournalSchema;
    std::string machine_id;         // makine kimligi ozeti (baska makinede uygulanmaz)
    std::string actor_sid;          // yurutenin hesabi
    std::string target_sid;         // oyuncu
    std::int64_t created_unix_ms = 0;
    contracts::TxState state = contracts::TxState::Prepared;
    std::uint64_t revision = 0;     // her kalici yazimda artar
    std::vector<JournalStep> steps;
};

json::Value to_json(const JournalTx& tx);
contracts::Result<JournalTx> from_json(const json::Value& v);

// Saha 2026-11-15: adim metinlerinin kayittaki ust siniri (okuma siniri error=512, observed=128). Daha uzun metin
// UTF-8 sinirinda kesilir; kayit her zaman geri okunabilir kalir.
inline constexpr std::size_t kStepErrorMax = 480;
inline constexpr std::size_t kStepObservedMax = 128;
std::string clamp_utf8(std::string_view s, std::size_t max_bytes);

// Dosya bicimi: {"schema_version":1,"sha256":"<hex>","tx":{...}}
std::string serialize(const JournalTx& tx);
// Bozuk ozet, yeni sema, sinir disi veri -> hata (bozuk journal ASLA basari sayilmaz)
contracts::Result<JournalTx> deserialize(std::string_view bytes);

// ---- Dosya sistemi soyutlamasi (testte hata enjekte edilir) ----
class JournalFs {
public:
    virtual ~JournalFs() = default;
    // Atomik: ya eski icerik ya yeni icerik kalir; basari = diske kalici yazildi.
    virtual contracts::Status write_atomic(const std::string& name, const std::string& bytes) = 0;
    virtual contracts::Result<std::string> read(const std::string& name) = 0;
    virtual contracts::Result<std::vector<std::string>> list() = 0;
    virtual contracts::Status remove(const std::string& name) = 0;
    // CORE-05: cokme sonrasi kalan commit edilmemis gecici dosyalar (*.json.tmp). Varsayilan: yok.
    virtual std::vector<std::string> temp_files() { return {}; }
};

struct StoreLimits {
    std::size_t max_transactions = 256;
    std::size_t max_file_bytes = 256 * 1024;
};

class JournalStore {
public:
    JournalStore(JournalFs& fs, std::string machine_id, StoreLimits lim = {})
        : fs_(fs), machine_id_(std::move(machine_id)), lim_(lim) {}

    // Yeni islemi kalici yazar. Basarisizsa cagiran HIC BIR degisiklik yapmamali.
    contracts::Status begin(JournalTx& tx);
    // Durum/adim guncellemesi (revision artar, atomik yeniden yazim).
    contracts::Status persist(JournalTx& tx);
    contracts::Result<JournalTx> load(const std::string& id);

    struct Listing {
        std::vector<JournalTx> transactions;  // gecerli kayitlar
        std::vector<std::string> corrupt;     // okunamayan/bozuk dosyalar (silinmez, kurtarma gerekir)
        std::vector<std::string> foreign;     // baska makineye ait (uygulanmaz)
        std::vector<std::string> newer;       // bilinmeyen yeni sema (yazma durur)
        std::vector<std::string> stale_temp;  // CORE-05: kalan *.json.tmp artiklari (rapor; silinmez)
    };
    Listing scan();

    // CORE-05: kalan *.json.tmp artiklarini siler (commit edilmemis gecici veri, journal kaydi degil).
    // Yeni islem baslamadan once cagrilir; silinemeyen dosya varsa begin yine de denenir (kota/durum).
    std::size_t discard_stale_temp();

    // Kota: yalniz terminal (restored) islemler en eskiden silinir. Aktif islem asla.
    contracts::Status enforce_quota();

    static std::string file_name(const std::string& tx_id) { return "tx-" + tx_id + ".json"; }

private:
    JournalFs& fs_;
    std::string machine_id_;
    StoreLimits lim_;
};

// Test ve REC-03 icin bellek ici dosya sistemi; belirli cagri sayisinda hata/cokme enjekte edilir.
class MemoryJournalFs final : public JournalFs {
public:
    contracts::Status write_atomic(const std::string& name, const std::string& bytes) override;
    contracts::Result<std::string> read(const std::string& name) override;
    contracts::Result<std::vector<std::string>> list() override;
    contracts::Status remove(const std::string& name) override;
    std::vector<std::string> temp_files() override;

    int fail_write_after = -1;   // N. yazimdan itibaren hata (disk dolu/ACL)
    int fail_only_write = -1;    // yalniz N. yazim hata (tek seferlik, sonra devam)
    int writes = 0;
    std::vector<std::pair<std::string, std::string>> files;
};

}  // namespace ef::core::recovery
