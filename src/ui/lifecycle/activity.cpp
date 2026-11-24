#include "ui/lifecycle/activity.h"

#include <algorithm>

namespace ef::ui {

void Activity::set(Visibility v) {
    std::vector<Listener> call;
    {
        std::lock_guard lk(mu_);
        if (v_ == v) return;
        v_ = v;
        for (const auto& [id, l] : listeners_) call.push_back(l);
    }
    for (const auto& l : call) l(v);  // kilit disinda: dinleyici yeniden abone olabilir
}

Visibility Activity::get() const {
    std::lock_guard lk(mu_);
    return v_;
}

int Activity::subscribe(Listener l) {
    std::lock_guard lk(mu_);
    const int id = next_++;
    listeners_.push_back({id, std::move(l)});
    return id;
}

void Activity::unsubscribe(int id) {
    std::lock_guard lk(mu_);
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& p) { return p.first == id; }),
                     listeners_.end());
}

}  // namespace ef::ui
