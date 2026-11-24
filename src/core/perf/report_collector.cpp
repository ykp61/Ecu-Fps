#include "core/perf/report_collector.h"

namespace ef::core::perf {

void ReportCollector::push(const ResourceSample& s) {
    switch (stage_) {
        case Stage::Applying: return;  // uygulama sirasindaki ornek iki tarafa da girmez
        case Stage::CollectingAfter:
            after_.push_back(s);
            if (after_.size() >= n_) {
                report_ = compare_before_after(before_, after_);
                stage_ = Stage::Done;
            }
            return;
        case Stage::Idle:
        case Stage::Done:
            recent_.push_back(s);
            while (recent_.size() > n_) recent_.pop_front();
            return;
    }
}

void ReportCollector::mark_before() {
    before_.assign(recent_.begin(), recent_.end());
    after_.clear();
    report_.reset();
    stage_ = Stage::Applying;
}

void ReportCollector::begin_after() {
    if (stage_ != Stage::Applying) return;
    after_.clear();
    stage_ = Stage::CollectingAfter;
}

void ReportCollector::cancel() {
    if (stage_ == Stage::Applying || stage_ == Stage::CollectingAfter) stage_ = Stage::Idle;
    before_.clear();
    after_.clear();
    recent_.clear();  // eski durum ornekleri yeni bir "once" olarak kullanilmaz
}

}  // namespace ef::core::perf
