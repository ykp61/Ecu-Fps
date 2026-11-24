// CHK "Duzelt" turu yardimcisi (ctest disi; check_readiness_fix_tour.ps1 kullanir). Pencere acmaz.
//   list               : "fixes" kapsamindaki journal islemlerini yazar (salt okuma).
//   rollback <tx-id>   : <tx-id> tam 32 kucuk harf onaltilik karakter olmali (urunun random_nonce bicimi; yol veya
//                        baska kapsamin dosyasi yuklenemez). YALNIZ o islemi, YALNIZ Active ve tum adimlari "gaming:" (HKCU izin listesi) ise,
//                        uretim adaptor seti + TransactionEngine::rollback ile geri alir (snapshot birebir
//                        geri yazilir, geri okunur, journal "restored" olur). Ekran (Hz) veya baska kapsam reddedilir.
// Neden: urun arayuzunde "fixes" kapsami icin tek islem geri alma dugmesi yok (Performans Kapali yalniz "user"
// kapsamini geri alir); tur betigi geri almayi ayni journal/motor yolundan yapar ve bunu acikca raporlar.
// Uygulama kapaliyken calistirilmalidir (ayni journal'a iki yazan olmasin); betik bunu saglar.
#include <cstdio>
#include <cstring>
#include <string>

#include "app/adapter_set.h"
#include "platform/windows/recovery_host.h"

namespace {

ef::core::recovery::JournalScope fixes_scope(const ef::platform::RecoveryHost& h) {
    for (const auto& sc : h.scopes())
        if (sc.name == "fixes") return sc;
    return {};
}

void print_tx(const ef::core::recovery::JournalTx& tx) {
    std::printf("tx=%s state=%s created=%lld steps=%zu\n", tx.id.c_str(), std::string(ef::contracts::to_string(tx.state)).c_str(),
                static_cast<long long>(tx.created_unix_ms), tx.steps.size());
    for (const auto& st : tx.steps)
        std::printf("  step target=%s new=%s existed=%d old=%s written=%s step_state=%s\n", st.op.target.c_str(), st.op.new_value.c_str(),
                    st.snapshot.existed ? 1 : 0, st.snapshot.old_value.c_str(), st.last_written.c_str(),
                    std::string(ef::core::recovery::to_string(st.state)).c_str());
}

// Urun islem kimligi: 16 rastgele bayt -> 32 kucuk harf onaltilik (platform::random_nonce). Baska bir sey
// (yol ayiraci, "..", baska kapsamin dosya adi) store->load'a hic verilmez.
bool is_tx_id(const char* s) {
    if (std::strlen(s) != 32) return false;
    for (const char* c = s; *c; ++c)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "list") != 0 && std::strcmp(argv[1], "rollback") != 0) ||
        (std::strcmp(argv[1], "rollback") == 0 && argc != 3)) {
        std::printf("usage: ef_fix_journal list | rollback <tx-id>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "rollback") == 0 && !is_tx_id(argv[2])) {
        std::printf("fix_journal: refused: tx id must be exactly 32 lowercase hex characters\n");
        return 8;
    }
    ef::app::AdapterSet adapters;
    auto host = ef::platform::open_recovery_host(adapters.registry);
    const auto sc = host ? fixes_scope(*host) : ef::core::recovery::JournalScope{};
    if (!sc.store || !sc.engine) {
        std::printf("fix_journal: fixes journal unavailable\n");
        return 3;
    }
    if (std::strcmp(argv[1], "list") == 0) {
        const auto l = sc.store->scan();
        std::printf("fix_journal: transactions=%zu corrupt=%zu foreign=%zu newer=%zu\n", l.transactions.size(), l.corrupt.size(),
                    l.foreign.size(), l.newer.size());
        for (const auto& tx : l.transactions) print_tx(tx);
        return 0;
    }
    const std::string id = argv[2];
    auto tx = sc.store->load(id);
    if (!tx.ok()) {
        std::printf("fix_journal: tx %s not loadable: %s\n", id.c_str(), tx.error().detail.c_str());
        return 4;
    }
    if (tx.value().state != ef::contracts::TxState::Active) {
        std::printf("fix_journal: tx %s is %s, not active; nothing done\n", id.c_str(), std::string(ef::contracts::to_string(tx.value().state)).c_str());
        return 5;
    }
    for (const auto& st : tx.value().steps) {
        if (st.op.target.rfind("gaming:", 0) != 0) {
            std::printf("fix_journal: refused: step target %s is not a per-user gaming toggle\n", st.op.target.c_str());
            return 6;
        }
    }
    auto r = sc.engine->rollback(std::move(tx).value());
    if (!r.ok()) {
        std::printf("fix_journal: rollback error: %s\n", r.error().detail.c_str());
        return 7;
    }
    std::printf("rollback ");
    print_tx(r.value());
    return r.value().state == ef::contracts::TxState::Restored ? 0 : 1;
}
