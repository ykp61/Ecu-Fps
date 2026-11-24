#include "core/cleanup/session.h"

#include <chrono>

#include "core/cleanup/admin_transfer.h"

namespace ef::core::cleanup {

const CategoryPlan* find_category(const CleanupPlan& p, CleanupCategory c) { return p.find(c); }

void CleanupSession::set(SessionPhase p) {
    phase_ = p;
    if (on_change) on_change();
}

void CleanupSession::scan() {
    if (phase_ == SessionPhase::Scanning || phase_ == SessionPhase::Running || phase_ == SessionPhase::Confirming) return;
    report_.reset();
    failure_.clear();
    files_open_.reset();
    admin_ready_.clear();  // yeni tarama eski yonetici yetkisini/onizlemesini kullanmaz
    scan_cancelled_ = false;
    scan_cancel_ = false;
    const std::uint64_t gen = ++scan_gen_;
    set(SessionPhase::Scanning);
    std::vector<CleanupCategory> all(kProductCategories.begin(), kProductCategories.end());
    PreviewOptions opt;
    opt.cancel = &scan_cancel_;
    d_.run_background([this, all, vol = volume_, gen, opt] {
        auto plan = d_.preview(all, vol, opt);
        d_.post_ui([this, plan = std::move(plan), gen]() mutable {
            // Iptal edilen veya yeni taramayla degistirilen sonuc uygulanmaz (gecersiz plan yok).
            if (gen != scan_gen_ || phase_ != SessionPhase::Scanning) return;
            plan_ = std::move(plan);
            // Ilk taramada varsayilanlar (motor bilgisi); kullanicinin sonraki secimi korunur.
            for (auto c : kProductCategories)
                if (!selection_.count(c)) selection_[c] = info(c).default_selected;
            set(SessionPhase::Ready);
        });
    });
}

void CleanupSession::cancel_scan() {
    if (phase_ != SessionPhase::Scanning) return;
    scan_cancel_ = true;
    ++scan_gen_;  // bekleyen tarama sonucunu gecersiz kil
    scan_cancelled_ = true;
    plan_ = {};
    progress_ = {};
    files_open_.reset();
    admin_ready_.clear();
    set(SessionPhase::Idle);  // hicbir dosya silinmedi; kullanici yeniden onizleyebilir
}

void CleanupSession::set_volume(std::wstring root) {
    if (root == volume_) return;
    if (phase_ == SessionPhase::Scanning || phase_ == SessionPhase::Running || phase_ == SessionPhase::Confirming) return;
    volume_ = std::move(root);
    if (phase_ == SessionPhase::Idle) {
        if (on_change) on_change();
        return;
    }
    scan();  // eski birimin plani bu birimde kullanilamaz
}

void CleanupSession::toggle_files(CleanupCategory c) {
    if (files_open_ == c) files_open_.reset();
    else if (plan_.find(c)) files_open_ = c;
    else return;
    if (on_change) on_change();
}

bool CleanupSession::selectable(CleanupCategory c) const {
    const auto* cp = plan_.find(c);
    if (!cp) return false;
    const auto& pr = cp->preview;
    if (pr.status != PreviewStatus::Ready) return false;
    if (pr.needs_admin && admin_ready_.count(c) == 0) return false;  // yukseltilmis tarama yok
    if (c == CleanupCategory::StandbyMemory) return true;  // T15: hazir = olculdu; secilebilir (sayi 0 olsa da bosaltma istenebilir)
    return pr.files > 0 || (c == CleanupCategory::RecycleBin && pr.bytes > 0);
}

void CleanupSession::adopt_admin_previews(const std::vector<CleanupPreview>& previews) {
    for (const auto& p : previews) {
        if (!is_admin_product_category(p.category)) continue;
        if (auto* existing = const_cast<CategoryPlan*>(plan_.find(p.category))) {
            existing->preview = p;
        } else {
            CategoryPlan cp;
            cp.preview = p;
            plan_.categories.push_back(std::move(cp));
        }
        if (p.status == PreviewStatus::Ready) admin_ready_.insert(p.category);
        else admin_ready_.erase(p.category);
    }
    if (on_change) on_change();
}

bool CleanupSession::admin_previewed(CleanupCategory c) const { return admin_ready_.count(c) != 0; }

void CleanupSession::clear_admin_previews() {
    admin_ready_.clear();
    for (auto c : admin_product_categories()) {
        if (auto* cp = const_cast<CategoryPlan*>(plan_.find(c))) {
            cp->preview.status = PreviewStatus::NeedsAdmin;
            cp->preview.files = 0;
            cp->preview.bytes = 0;
            cp->preview.allocated = 0;
            cp->preview.partial = false;
        }
    }
    if (on_change) on_change();
}

bool CleanupSession::selected(CleanupCategory c) const {
    const auto it = selection_.find(c);
    return selectable(c) && it != selection_.end() && it->second;
}

void CleanupSession::set_selected(CleanupCategory c, bool on) {
    if (phase_ != SessionPhase::Ready && phase_ != SessionPhase::Done) return;
    selection_[c] = on;
    if (on_change) on_change();
}

std::uint64_t CleanupSession::selected_bytes() const {
    std::uint64_t b = 0;
    for (auto c : kProductCategories)
        if (selected(c) && c != CleanupCategory::StandbyMemory) b += plan_.find(c)->preview.bytes;  // T15: bellek disk alani degildir
    return b;
}

std::size_t CleanupSession::selected_files() const {
    std::size_t n = 0;
    for (auto c : kProductCategories)
        if (selected(c)) n += plan_.find(c)->preview.files;
    return n;
}

bool CleanupSession::any_selected() const {
    for (auto c : kProductCategories)
        if (selected(c)) return true;
    return false;
}

void CleanupSession::request_run() {
    if (phase_ != SessionPhase::Ready || !any_selected()) return;
    set(SessionPhase::Confirming);
}

void CleanupSession::answer(bool yes) {
    if (phase_ != SessionPhase::Confirming) return;
    if (!yes) {
        set(SessionPhase::Ready);  // Hayir: hicbir dosya silinmedi
        return;
    }
    std::vector<CleanupCategory> chosen;
    for (auto c : kProductCategories)
        if (selected(c)) chosen.push_back(c);
    cancel_ = false;
    progress_ = {};
    gate_reason_.clear();
    set(SessionPhase::Running);
    d_.run_background([this, chosen] {
        // Evet'ten sonra ilk silme oncesi guncel durum: oyun acildiysa hicbir dosyaya dokunulmaz.
        if (d_.gate_denied) {
            if (auto why = d_.gate_denied(); !why.empty()) {
                d_.post_ui([this, why = std::move(why)] {
                    gate_reason_ = why;
                    set(SessionPhase::Ready);
                });
                return;
            }
        }
        RunOptions opt;
        opt.cancel = &cancel_;
        auto last_check = std::chrono::steady_clock::now();
        auto gate_abort = std::make_shared<std::string>();
        opt.on_progress = [this, last_check, gate_abort](const CleanupProgress& p) mutable {
            // Silme geri alinamaz: durum bozulunca sonraki dosyadan once durulur; silinenler raporda kalir.
            const auto now = std::chrono::steady_clock::now();
            if (d_.gate_denied && gate_abort->empty() && now - last_check >= std::chrono::milliseconds(d_.gate_poll_ms)) {
                last_check = now;
                *gate_abort = d_.gate_denied();
                if (!gate_abort->empty()) cancel_ = true;
            }
            d_.post_ui([this, p] {
                progress_ = p;
                if (on_change) on_change();
            });
        };
        auto r = d_.run(plan_, chosen, opt);
        d_.post_ui([this, r = std::move(r), gate_abort]() mutable {
            gate_reason_ = *gate_abort;
            if (!r.ok()) {
                failure_ = r.error().detail;
                set(SessionPhase::Failed);
                return;
            }
            report_ = std::move(r).value();
            if (gate_reason_.empty()) gate_reason_ = report_ ? report_->gate_reason : std::string();  // yalniz yukseltilmis taraf gorduyse de neden yazilir
            set(SessionPhase::Done);
        });
    });
}

}  // namespace ef::core::cleanup
