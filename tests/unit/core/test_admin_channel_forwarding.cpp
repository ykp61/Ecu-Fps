// Saha 2026-11-15: uretimdeki yonetici kanali ServiceFirstChannel'dir (app.h). AdminChannel'in her salt okuma sorgusu
// ServiceFirstChannel'da yeniden tanimlanip BrokerChannel'a iletilmelidir; aksi halde ust sinifin varsayilani ({} / false)
// doner ve ornegin makine kapsamindaki geri alma hatalari kurtarma panelinde, diag.json'da ve restore.log'da hic gorunmez.
// Uye isaretcisinin sinifi, islevin ServiceFirstChannel'da bildirildigini derleme aninda dogrular.
#include <type_traits>

#include "ef_test.h"
#include "platform/windows/service_channel.h"

namespace {
using ef::platform::BrokerChannel;
using ef::platform::ServiceFirstChannel;

template <typename T, typename C, typename R, typename... A>
constexpr bool owner_is(R (C::*)(A...)) {
    return std::is_same_v<T, C>;
}
}  // namespace

EF_TEST(service_first_channel_forwards_every_admin_read_query) {
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::active_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::recovery_needed));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::perf_recovery_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::perf_conflict_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::machine_failed_items));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::fix_active_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::fix_recovery_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::unreconciled_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::fix_conflict_ops));
    static_assert(owner_is<ServiceFirstChannel>(&ServiceFirstChannel::fix_target_lists));
    // Gercek uygulama da BrokerChannel'dadir (iletilen yer varsayilan degildir).
    static_assert(owner_is<BrokerChannel>(&BrokerChannel::machine_failed_items));
    static_assert(owner_is<BrokerChannel>(&BrokerChannel::perf_recovery_ops));
    static_assert(owner_is<BrokerChannel>(&BrokerChannel::recovery_needed));
    EF_CHECK(true);
}
