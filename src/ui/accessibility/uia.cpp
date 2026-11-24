#include "ui/accessibility/uia.h"

#include <uiautomationcoreapi.h>

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace ef::ui {

std::wstring uia_clean_text(std::wstring_view s) {
    const auto space = [](wchar_t c) {
        return c == L' ' || c == 0x09 || c == 0x0D || c == 0x0A || c == 0x00A0 || c == 0x202F || c == 0x200B;
    };
    std::size_t b = 0, e = s.size();
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return std::wstring(s.substr(b, e - b));
}

namespace {

// Kok/oge varligi kontrolu: pencere kapandiysa veya oge artik yoksa UIA_E_ELEMENTNOTAVAILABLE.
constexpr HRESULT kGone = static_cast<HRESULT>(0x80040201L);  // UIA_E_ELEMENTNOTAVAILABLE

// RDY-01: AutomationId'ler tek kaynaktan (hit_automation_id, kHitRanges) gelir.
std::wstring automation_id(Hit h) { return hit_automation_id(h); }

long control_type(AccRole r) {
    switch (r) {
        case AccRole::TabItem: return UIA_TabItemControlTypeId;
        case AccRole::ToggleSwitch: return UIA_ButtonControlTypeId;  // WinUI ToggleSwitch ile ayni: Button + Toggle deseni
        case AccRole::RadioButton: return UIA_RadioButtonControlTypeId;
        case AccRole::Button: return UIA_ButtonControlTypeId;
        case AccRole::ProgressBar: return UIA_ProgressBarControlTypeId;
        case AccRole::Text: return UIA_TextControlTypeId;
        case AccRole::CheckBox: return UIA_CheckBoxControlTypeId;
        case AccRole::Window: return UIA_WindowControlTypeId;
        case AccRole::Expander: return UIA_ButtonControlTypeId;  // WinUI Expander basligi: Button + ExpandCollapse
        case AccRole::Edit: return UIA_EditControlTypeId;
    }
    return UIA_ButtonControlTypeId;
}

// Spec 2026-11-12 E: kip penceresi acikken agac ic icedir — kok -> kip penceresi (Window) -> kip ogeleri. Isabet
// haritasinda o sirada yalniz kip ogeleri vardir (Screen::modal_layer).
constexpr long kUiaIsDialogPropertyId = 30174;  // UIA_IsDialogPropertyId (Windows 10 1809+; eski surum yok sayar)
bool modal_shown(const UiaModel& m) { return m.hits.meta.count(Hit::Modal) > 0; }

class Element;

class Root final : public IRawElementProviderSimple,
                   public IRawElementProviderFragment,
                   public IRawElementProviderFragmentRoot,
                   public ISelectionProvider {
public:
    explicit Root(std::shared_ptr<UiaModel> m) : m_(std::move(m)) {}
    std::shared_ptr<UiaModel> model() const { return m_; }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(ISelectionProvider)) *out = static_cast<ISelectionProvider*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
        *o = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** p) override {
        *p = nullptr;
        if (!m_->alive()) return kGone;
        // TH-04: kok, secim konteyneridir (sekme + radyo secenekleri). Oge tarafindaki
        // SelectionItem.get_SelectionContainer bu koku dondurur.
        if (id == UIA_SelectionPatternId) {
            *p = static_cast<ISelectionProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    // ISelectionProvider
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
        *out = nullptr;
        if (!m_->alive()) return kGone;
        const auto sel = selected_hits(m_->hits);
        SAFEARRAY* a = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(sel.size()));
        if (!a) return E_OUTOFMEMORY;
        LONG i = 0;
        for (Hit h : sel) {
            IRawElementProviderFragment* el = make(h);
            IRawElementProviderSimple* simple = nullptr;
            if (FAILED(el->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple)))) {
                el->Release();
                SafeArrayDestroy(a);
                return E_NOINTERFACE;
            }
            el->Release();
            SafeArrayPutElement(a, &i, static_cast<IUnknown*>(simple));
            simple->Release();
            ++i;
        }
        *out = a;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* b) override {
        *b = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* b) override {
        *b = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override {
        VariantInit(v);
        if (!m_->alive()) return kGone;
        if (id == UIA_NamePropertyId) {
            v->vt = VT_BSTR;
            v->bstrVal = SysAllocString(uia_clean_text(m_->window_name).c_str());
        } else if (id == UIA_AutomationIdPropertyId) {
            v->vt = VT_BSTR;
            v->bstrVal = SysAllocString(L"ecufps.main");
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** p) override {
        if (!m_->alive()) {
            *p = nullptr;
            return kGone;
        }
        return UiaHostProviderFromHwnd(m_->hwnd, p);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** id) override {
        *id = nullptr;  // kok: host saglar
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override {
        *r = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** r) override {
        *r = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** r) override {
        *r = this;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

    IRawElementProviderFragment* make(Hit h);

private:
    std::shared_ptr<UiaModel> m_;
    std::atomic<ULONG> ref_{1};
};

class Element final : public IRawElementProviderSimple,
                      public IRawElementProviderFragment,
                      public IInvokeProvider,
                      public IToggleProvider,
                      public ISelectionItemProvider,
                      public IRangeValueProvider,
                      public IWindowProvider,
                      public IExpandCollapseProvider,
                      public IValueProvider {
public:
    Element(Root* root, Hit h) : root_(root), h_(h) { root_->AddRef(); }
    ~Element() { root_->Release(); }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (riid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (riid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (riid == __uuidof(IWindowProvider)) *out = static_cast<IWindowProvider*>(this);
        else if (riid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
        else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    const HitMeta* meta() const {
        const auto& m = *root_->model();
        if (!m.alive()) return nullptr;
        const auto it = m.hits.meta.find(h_);
        return it == m.hits.meta.end() ? nullptr : &it->second;
    }
    const RectF* rect() const {
        for (const auto& [id, r] : root_->model()->hits.items)
            if (id == h_) return &r;
        return nullptr;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
        *o = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** p) override {
        *p = nullptr;
        const auto* mt = meta();
        if (!mt) return kGone;
        const bool sel = mt->role == AccRole::TabItem || mt->role == AccRole::RadioButton;
        const bool toggle = mt->role == AccRole::ToggleSwitch || mt->role == AccRole::CheckBox;
        if (id == UIA_TogglePatternId && toggle) *p = static_cast<IToggleProvider*>(this);
        else if (id == UIA_WindowPatternId && mt->role == AccRole::Window) *p = static_cast<IWindowProvider*>(this);
        else if (id == UIA_SelectionItemPatternId && sel) *p = static_cast<ISelectionItemProvider*>(this);
        else if (id == UIA_InvokePatternId && (mt->role == AccRole::Button || mt->role == AccRole::Expander)) *p = static_cast<IInvokeProvider*>(this);
        else if (id == UIA_ExpandCollapsePatternId && mt->role == AccRole::Expander) *p = static_cast<IExpandCollapseProvider*>(this);
        else if (id == UIA_ValuePatternId && mt->role == AccRole::Edit) *p = static_cast<IValueProvider*>(this);
        else if (id == UIA_RangeValuePatternId && mt->role == AccRole::ProgressBar) *p = static_cast<IRangeValueProvider*>(this);
        if (*p) AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override {
        VariantInit(v);
        const auto* mt = meta();
        if (!mt) return kGone;
        const auto& m = *root_->model();
        switch (id) {
            case UIA_NamePropertyId:
                v->vt = VT_BSTR;
                v->bstrVal = SysAllocString(uia_clean_text(mt->name).c_str());
                break;
            case UIA_AutomationIdPropertyId:
                v->vt = VT_BSTR;
                v->bstrVal = SysAllocString(automation_id(h_).c_str());
                break;
            case UIA_ControlTypePropertyId:
                v->vt = VT_I4;
                v->lVal = control_type(mt->role);
                break;
            case UIA_IsKeyboardFocusablePropertyId:  // devre disi, salt okunur ve roving disi radyo ogeleri Tab duragi degil
                v->vt = VT_BOOL;
                v->boolVal = mt->enabled && interactive(mt->role) && mt->tab_stop ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsEnabledPropertyId:  // DS-05: devre disi oge agacta kalir, IsEnabled=false
                v->vt = VT_BOOL;
                v->boolVal = mt->enabled ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                v->vt = VT_BOOL;
                v->boolVal = VARIANT_TRUE;
                break;
            case UIA_HelpTextPropertyId:  // RDY-12: satir durumu Name'e eklenmez, ek bilgi olarak okunur
                if (!mt->help.empty()) {
                    v->vt = VT_BSTR;
                    v->bstrVal = SysAllocString(uia_clean_text(mt->help).c_str());
                }
                break;
            case UIA_IsOffscreenPropertyId:  // RDY-10: kaydirma alani disindaki oge (Tab ile gelince kaydirilir)
                v->vt = VT_BOOL;
                v->boolVal = mt->offscreen ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_HasKeyboardFocusPropertyId:
                v->vt = VT_BOOL;
                v->boolVal = (m.focus == h_ && ::GetFocus() == m.hwnd) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_FrameworkIdPropertyId:
                v->vt = VT_BSTR;
                v->bstrVal = SysAllocString(L"EcuFpsD2D");
                break;
            case kUiaIsDialogPropertyId:  // kip penceresi: ekran okuyucu acilista baslik + govdeyi okur
                if (mt->role == AccRole::Window) {
                    v->vt = VT_BOOL;
                    v->boolVal = VARIANT_TRUE;
                }
                break;
            default: break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** p) override {
        *p = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override {
        *out = nullptr;
        if (!meta()) return kGone;
        const auto& items = root_->model()->hits.items;
        // Kip penceresi acikken: kok -> Window -> ogeler. Window'un kardesi yoktur; ogelerin ebeveyni Window'dur.
        const bool nested = modal_shown(*root_->model());
        if (nested && h_ == Hit::Modal) {
            if (dir == NavigateDirection_Parent) {
                *out = root_;
                root_->AddRef();
            } else if (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild) {
                const bool first = dir == NavigateDirection_FirstChild;
                for (std::size_t n = 0; n < items.size(); ++n) {
                    const Hit c = items[first ? n : items.size() - 1 - n].first;
                    if (c == Hit::Modal) continue;
                    *out = root_->make(c);
                    break;
                }
            }
            return S_OK;
        }
        if (dir == NavigateDirection_Parent) {
            if (nested) {
                *out = root_->make(Hit::Modal);
            } else {
                *out = root_;
                root_->AddRef();
            }
            return S_OK;
        }
        if (dir != NavigateDirection_NextSibling && dir != NavigateDirection_PreviousSibling) return S_OK;
        const std::ptrdiff_t step = dir == NavigateDirection_NextSibling ? 1 : -1;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].first != h_) continue;
            std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) + step;
            while (nested && j >= 0 && j < static_cast<std::ptrdiff_t>(items.size()) && items[static_cast<std::size_t>(j)].first == Hit::Modal) j += step;
            if (j >= 0 && j < static_cast<std::ptrdiff_t>(items.size())) *out = root_->make(items[static_cast<std::size_t>(j)].first);
            break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** id) override {
        int rid[2] = {UiaAppendRuntimeId, static_cast<int>(h_) + 1000};
        *id = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!*id) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*id, &i, &rid[i]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        *out = {};
        const auto& m = *root_->model();
        const auto* r = rect();
        if (!m.alive() || !r) return kGone;
        POINT pt{static_cast<LONG>(std::lround(r->x * m.scale)), static_cast<LONG>(std::lround(r->y * m.scale))};
        ClientToScreen(m.hwnd, &pt);
        *out = {static_cast<double>(pt.x), static_cast<double>(pt.y), r->w * m.scale, r->h * m.scale};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** r) override {
        *r = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** r) override {
        *r = root_;
        root_->AddRef();
        return S_OK;
    }

    // Desenler: is UI is parcaciginda ve kullanicinin tiklamasiyla ayni yoldan yurur (Evet/Hayir kurali korunur).
    HRESULT STDMETHODCALLTYPE Invoke() override { return run(); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return run(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* s) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *s = mt->toggle == 1 ? ToggleState_On : mt->toggle == 0 ? ToggleState_Off : ToggleState_Indeterminate;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select() override { return run(); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* b) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *b = mt->selected ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** c) override {
        *c = nullptr;
        if (!meta()) return kGone;
        // TH-04: secim konteyneri kok saglayicidir; kok ISelectionProvider desenini sunar.
        *c = static_cast<IRawElementProviderSimple*>(root_);
        root_->AddRef();
        return S_OK;
    }
    // RangeValue (yalniz ProgressBar): salt okunur 0..100.
    HRESULT STDMETHODCALLTYPE SetValue(double) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_Value(double* v) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *v = mt->range_value < 0 ? 0 : mt->range_value;
        return S_OK;
    }
    // RangeValue ve Value ortak: ProgressBar salt okunur; Edit etkinse yazilabilir.
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* b) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *b = mt->role == AccRole::Edit && mt->enabled ? FALSE : TRUE;
        return S_OK;
    }
    // Value (yalniz Edit): metin okunur; yazma pencerenin dogrulamasindan gecer (klavyeyle yazilmis gibi).
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR text) override {
        const auto& m = *root_->model();
        const auto* mt = meta();
        if (!mt) return kGone;
        if (mt->role != AccRole::Edit) return UIA_E_INVALIDOPERATION;
        if (!mt->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!m.set_value || !m.set_value(h_, text ? std::wstring(text) : std::wstring())) return E_INVALIDARG;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* v) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *v = SysAllocString(mt->value.c_str());
        return S_OK;
    }
    // ExpandCollapse (yalniz Expander): durum selected; Expand/Collapse durum farkliysa tiklamayla ayni yoldan.
    HRESULT STDMETHODCALLTYPE Expand() override {
        const auto* mt = meta();
        if (!mt) return kGone;
        return mt->selected ? S_OK : run();
    }
    HRESULT STDMETHODCALLTYPE Collapse() override {
        const auto* mt = meta();
        if (!mt) return kGone;
        return mt->selected ? run() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* st) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *st = mt->selected ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* v) override {
        *v = 100;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* v) override {
        *v = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* v) override {
        *v = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* v) override {
        *v = 0;
        return S_OK;
    }
    // Window (yalniz kip penceresi; spec 2026-11-12 E): IsModal = true. Close = Esc (Hayir); boyut / durum degismez.
    HRESULT STDMETHODCALLTYPE SetVisualState(WindowVisualState) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE Close() override {
        const auto& m = *root_->model();
        const auto* mt = meta();
        if (!mt) return kGone;
        if (mt->role != AccRole::Window) return UIA_E_INVALIDOPERATION;
        if (m.activate) m.activate(h_);  // MainWindow: Hit::Modal = pencereyi kapat (Esc ile ayni yol)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WaitForInputIdle(int, BOOL* b) override {
        *b = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanMaximize(BOOL* b) override {
        *b = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanMinimize(BOOL* b) override {
        *b = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsModal(BOOL* b) override {
        const auto* mt = meta();
        if (!mt) return kGone;
        *b = mt->role == AccRole::Window ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_WindowVisualState(WindowVisualState* s) override {
        *s = WindowVisualState_Normal;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_WindowInteractionState(WindowInteractionState* s) override {
        *s = meta() ? WindowInteractionState_ReadyForUserInteraction : WindowInteractionState_Closing;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTopmost(BOOL* b) override {
        *b = FALSE;
        return S_OK;
    }

private:
    HRESULT run() {
        const auto& m = *root_->model();
        const auto* mt = meta();
        if (!mt) return kGone;
        if (!interactive(mt->role)) return UIA_E_INVALIDOPERATION;
        if (!mt->enabled) return UIA_E_ELEMENTNOTENABLED;  // DS-05: devre disi oge calistirilmaz
        if (m.activate) m.activate(h_);
        return S_OK;
    }
    Root* root_;
    Hit h_;
    std::atomic<ULONG> ref_{1};
};

IRawElementProviderFragment* Root::make(Hit h) { return new Element(this, h); }

HRESULT Root::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!m_->alive()) return kGone;
    const auto& items = m_->hits.items;
    if (items.empty()) return S_OK;
    if (dir != NavigateDirection_FirstChild && dir != NavigateDirection_LastChild) return S_OK;
    if (modal_shown(*m_)) *out = make(Hit::Modal);  // kip penceresi acik: kokun tek cocugu pencerenin kendisi
    else *out = make(dir == NavigateDirection_FirstChild ? items.front().first : items.back().first);
    return S_OK;
}

HRESULT Root::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!m_->alive()) return kGone;
    POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(m_->hwnd, &pt);
    const Hit h = m_->hits.element_at(static_cast<float>(pt.x) / m_->scale, static_cast<float>(pt.y) / m_->scale);
    if (h != Hit::None) *out = make(h);
    return S_OK;
}

HRESULT Root::GetFocus(IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!m_->alive()) return kGone;
    if (m_->focus != Hit::None && m_->hits.meta.count(m_->focus)) *out = make(m_->focus);
    return S_OK;
}

}  // namespace

UiaHost::UiaHost(std::shared_ptr<UiaModel> model) : model_(std::move(model)) { root_ = new Root(model_); }

UiaHost::~UiaHost() {
    disconnect();
    if (root_) root_->Release();
}

LRESULT UiaHost::on_get_object(WPARAM wp, LPARAM lp) {
    if (static_cast<long>(lp) != static_cast<long>(UiaRootObjectId) || !model_->alive()) return 0;
    return UiaReturnRawElementProvider(model_->hwnd, wp, lp, root_);
}

void UiaHost::notify_focus(Hit h) {
    model_->focus = h;
    if (!model_->alive() || h == Hit::None || !UiaClientsAreListening() || !model_->hits.meta.count(h)) return;
    auto* root = static_cast<Root*>(root_);
    IRawElementProviderFragment* el = root->make(h);
    IRawElementProviderSimple* simple = nullptr;
    if (SUCCEEDED(el->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple)))) {
        UiaRaiseAutomationEvent(simple, UIA_AutomationFocusChangedEventId);
        simple->Release();
    }
    el->Release();
}

void UiaHost::notify_window_opened(Hit h) {
    if (!model_->alive() || h == Hit::None || !UiaClientsAreListening() || !model_->hits.meta.count(h)) return;
    auto* root = static_cast<Root*>(root_);
    IRawElementProviderFragment* el = root->make(h);
    IRawElementProviderSimple* simple = nullptr;
    if (SUCCEEDED(el->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple)))) {
        UiaRaiseAutomationEvent(simple, UIA_Window_WindowOpenedEventId);
        simple->Release();
    }
    el->Release();
}

void UiaHost::notify_structure_changed() {
    if (!model_->alive() || !root_ || !UiaClientsAreListening()) return;
    // ChildrenInvalidated: cocuk kimligi verilmez; istemci kokun cocuklarini yeniden sorar. Sonuc yok sayilir.
    (void)UiaRaiseStructureChangedEvent(root_, StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

void UiaHost::notify_selection(Hit h) {
    if (!model_->alive() || h == Hit::None || !UiaClientsAreListening() || !model_->hits.meta.count(h)) return;
    auto* root = static_cast<Root*>(root_);
    IRawElementProviderFragment* el = root->make(h);
    IRawElementProviderSimple* simple = nullptr;
    if (SUCCEEDED(el->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple)))) {
        const auto it = model_->hits.meta.find(h);
        if (it != model_->hits.meta.end() && it->second.selected)
            UiaRaiseAutomationEvent(simple, UIA_SelectionItem_ElementSelectedEventId);
        simple->Release();
    }
    el->Release();
    // Konteyner (kok) secim kumesi degisti: dinleyenler GetSelection ile yeni durumu okur.
    IRawElementProviderSimple* container = nullptr;
    if (SUCCEEDED(root->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&container)))) {
        UiaRaiseAutomationEvent(container, UIA_Selection_InvalidatedEventId);
        container->Release();
    }
}

bool UiaHost::announce(const std::wstring& text, bool completed, const std::wstring& activity_id) {
    if (text.empty() || !model_->alive() || !root_ || !UiaClientsAreListening()) return false;
    // Windows 10 1709+ (UIAutomationCore). Dogrudan ice aktarma eski surumde EXE yuklenmesini bozardi.
    using Fn = HRESULT(WINAPI*)(IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);
    static const Fn fn = [] {
        const HMODULE mod = GetModuleHandleW(L"uiautomationcore.dll");
        return mod ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, "UiaRaiseNotificationEvent"))) : nullptr;
    }();
    if (!fn) return false;
    BSTR t = SysAllocString(uia_clean_text(text).c_str());
    BSTR a = SysAllocString(activity_id.c_str());
    const HRESULT hr = (t && a) ? fn(root_, completed ? NotificationKind_ActionCompleted : NotificationKind_Other,
                                     NotificationProcessing_ImportantMostRecent, t, a)
                                : E_OUTOFMEMORY;
    SysFreeString(t);
    SysFreeString(a);
    return SUCCEEDED(hr);
}

void UiaHost::disconnect() {
    if (model_ && model_->hwnd) {
        UiaReturnRawElementProvider(model_->hwnd, 0, 0, nullptr);  // UIA onbelleklerini birak
        if (root_) UiaDisconnectProvider(root_);
        model_->hwnd = nullptr;
    }
}

}  // namespace ef::ui
