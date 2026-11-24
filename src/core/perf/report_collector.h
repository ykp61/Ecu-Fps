#pragma once
// OPT-08: Performans Acik oncesi/sonrasi orneklerini toplar (saf; birim testlenir).
//  - push(): her 1 Hz canli ornek. Son N ornek "once" adayi olarak tutulur.
//  - mark_before(): Evet verildi, uygulama basliyor -> once = son N ornek (uygulama sirasindaki
//    ornekler hicbir tarafa girmez).
//  - begin_after(): uygulama dogrulandi (Acik) -> sonraki N ornek "sonra".
//  - N ornek dolunca rapor compare_before_after ile uretilir. Uygulama basarisizsa cancel().
//  - Ornek kendisi uretilmez; pencere gizliyken ornek gelmez, toplama bekler.

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include "core/perf/before_after.h"

namespace ef::core::perf {

class ReportCollector {
public:
    explicit ReportCollector(std::size_t n = 10) : n_(n) {}
    void push(const ResourceSample& s);
    void mark_before();
    void begin_after();
    void cancel();

    enum class Stage { Idle, Applying, CollectingAfter, Done };
    Stage stage() const { return stage_; }
    std::size_t target() const { return n_; }
    std::size_t after_count() const { return after_.size(); }
    std::size_t before_count() const { return before_.size(); }
    const std::optional<BeforeAfterReport>& report() const { return report_; }

private:
    std::size_t n_;
    Stage stage_ = Stage::Idle;
    std::deque<ResourceSample> recent_;
    std::vector<ResourceSample> before_, after_;
    std::optional<BeforeAfterReport> report_;
};

}  // namespace ef::core::perf
