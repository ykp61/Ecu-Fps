#include "platform/windows/diagnostics/event_reader.h"

#include <windows.h>
#include <winevt.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/windows/win_env.h"

namespace ef::platform::diagnostics {

namespace cd = core::diagnostics;

namespace {

struct EvtCloser {
    void operator()(EVT_HANDLE h) const noexcept {
        if (h) EvtClose(h);
    }
};
using EvtPtr = std::unique_ptr<std::remove_pointer_t<EVT_HANDLE>, EvtCloser>;

std::wstring time_filter(int days) {
    const auto ms = static_cast<unsigned long long>(days) * 86400000ull;
    return L" and System[TimeCreated[timediff(@SystemTime) <= " + std::to_wstring(ms) + L"]]]";
}

std::wstring severe_query(int days) {
    return L"*[System[(Provider[@Name='Microsoft-Windows-Kernel-Power'] and EventID=41)"
           L" or (Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] and EventID=1001)"
           L" or (Provider[@Name='EventLog'] and EventID=6008)"
           L" or (Provider[@Name='Microsoft-Windows-WHEA-Logger'] and (Level=1 or Level=2 or Level=0))"
           L" or (Provider[@Name='Display'] and EventID=4101)"
           L" or (Provider[@Name='disk'] and (EventID=7 or EventID=51))]" + time_filter(days);
}

std::wstring corrected_query(int days) {
    return L"*[System[Provider[@Name='Microsoft-Windows-WHEA-Logger'] and Level=3]" + time_filter(days);
}

constexpr DWORD kNextTimeoutMs = 5000;
constexpr ULONGLONG kPassDeadlineMs = 15000;
constexpr std::size_t kCorrectedBudget = 500;  // sel, ciddi kayitlari silemez

enum class Pass { Done, Limit, TimedOut, Failed };

Pass read_pass(const std::wstring& q, cd::EventReadResult& out, std::size_t max_events, std::size_t& total, std::vector<wchar_t>& buf) {
    EvtPtr query(EvtQuery(nullptr, L"System", q.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!query) {
        const DWORD e = GetLastError();
        out.status = e == ERROR_ACCESS_DENIED ? cd::ReadStatus::AccessDenied : cd::ReadStatus::ReadError;
        out.detail = "EvtQuery " + std::to_string(e);
        return Pass::Failed;
    }
    const auto start = GetTickCount64();
    std::size_t taken = 0;
    for (;;) {
        EVT_HANDLE raw[64] = {};
        DWORD got = 0;
        if (!EvtNext(query.get(), 64, raw, kNextTimeoutMs, 0, &got)) {
            const DWORD e = GetLastError();
            if (e == ERROR_NO_MORE_ITEMS) return Pass::Done;
            if (e == ERROR_TIMEOUT) {
                if (GetTickCount64() - start < kPassDeadlineMs) continue;
                return Pass::TimedOut;  // okunanlar korunur: kismi sonuc
            }
            out.status = e == ERROR_ACCESS_DENIED ? cd::ReadStatus::AccessDenied : cd::ReadStatus::ReadError;
            out.detail = "EvtNext " + std::to_string(e);
            return Pass::Failed;
        }
        std::vector<EvtPtr> batch;
        batch.reserve(got);
        for (DWORD i = 0; i < got; ++i) batch.emplace_back(raw[i]);
        for (const auto& ev : batch) {
            if (taken >= max_events || out.events.size() >= cd::kMaxEvents || total >= cd::kMaxTotalBytes) return Pass::Limit;
            DWORD used = 0, props = 0;
            if (!EvtRender(nullptr, ev.get(), EvtRenderEventXml, static_cast<DWORD>(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props)) {
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || used > cd::kMaxEventXml * sizeof(wchar_t)) {
                    ++out.rejected;  // asiri buyuk olay okunmaz, sayilir
                    continue;
                }
                buf.resize(used / sizeof(wchar_t) + 1);
                if (!EvtRender(nullptr, ev.get(), EvtRenderEventXml, static_cast<DWORD>(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props)) {
                    ++out.rejected;
                    continue;
                }
            }
            const auto xml = to_utf8(std::wstring_view(buf.data()));
            total += xml.size();
            ++taken;
            if (auto parsed = cd::parse_event_xml(xml)) out.events.push_back(std::move(*parsed));
            else ++out.rejected;
        }
    }
}

// Gunlugun en eski kaydi: dongusel/temizlenmis gunluk "7 gun temiz" diye sunulmaz.
std::optional<std::int64_t> oldest_record_ms(std::vector<wchar_t>& buf) {
    EvtPtr q(EvtQuery(nullptr, L"System", L"*", EvtQueryChannelPath | EvtQueryForwardDirection));
    if (!q) return std::nullopt;
    EVT_HANDLE raw = nullptr;
    DWORD got = 0;
    if (!EvtNext(q.get(), 1, &raw, kNextTimeoutMs, 0, &got) || got == 0) return std::nullopt;
    EvtPtr ev(raw);
    DWORD used = 0, props = 0;
    if (!EvtRender(nullptr, ev.get(), EvtRenderEventXml, static_cast<DWORD>(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props)) return std::nullopt;
    const auto parsed = cd::parse_event_xml(to_utf8(std::wstring_view(buf.data())));
    return parsed ? parsed->time_unix_ms : std::nullopt;
}

}  // namespace

FatalWheaMark newest_fatal_whea() {
    cd::EventReadResult out;
    std::vector<wchar_t> buf(16 * 1024);
    std::size_t total = 0;
    const auto st = read_pass(L"*[System[Provider[@Name='Microsoft-Windows-WHEA-Logger'] and (Level=1 or Level=2)]]", out, 1, total, buf);
    FatalWheaMark m;
    if (st == Pass::Failed || st == Pass::TimedOut) return m;
    m.ok = true;
    if (!out.events.empty()) m.record = out.events.front().record_id;
    return m;
}

ProcessorSpeedLimitEvents count_processor_speed_limit_events_since_boot() {
    ProcessorSpeedLimitEvents out;
    const std::wstring q = L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Processor-Power'] and EventID=37 and TimeCreated[timediff(@SystemTime) <= " +
                           std::to_wstring(GetTickCount64()) + L"]]]";
    EvtPtr query(EvtQuery(nullptr, L"System", q.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!query) return out;
    const auto start = GetTickCount64();
    for (;;) {
        EVT_HANDLE raw[64] = {};
        DWORD got = 0;
        if (!EvtNext(query.get(), 64, raw, kNextTimeoutMs, 0, &got)) {
            const DWORD e = GetLastError();
            if (e == ERROR_NO_MORE_ITEMS) break;
            if (e == ERROR_TIMEOUT && GetTickCount64() - start < kPassDeadlineMs) continue;
            return out;  // okunamadi: sayi bildirilmez
        }
        for (DWORD i = 0; i < got; ++i) EvtClose(raw[i]);
        out.count += got;
        if (out.count >= kMaxSpeedLimitEvents) {
            out.count = kMaxSpeedLimitEvents;
            out.capped = true;
            break;
        }
    }
    out.ok = true;
    return out;
}

cd::EventReadResult read_system_incidents(int days) {
    days = cd::normalize_incident_days(days);  // XPath'e yalniz 7/30 gider
    cd::EventReadResult out;
    std::vector<wchar_t> buf(16 * 1024);
    std::size_t total = 0;
    const auto severe = read_pass(severe_query(days), out, cd::kMaxEvents, total, buf);
    if (severe == Pass::Failed) return out;
    out.status = cd::ReadStatus::Ok;
    out.truncated = severe != Pass::Done;
    const auto remaining = cd::kMaxEvents - std::min(cd::kMaxEvents, out.events.size());
    const auto corrected = read_pass(corrected_query(days), out, std::min(kCorrectedBudget, remaining), total, buf);
    if (corrected == Pass::Failed) {
        out.status = cd::ReadStatus::Ok;  // ciddi kayitlar okundu; duzeltilmis kisim eksik
        out.truncated = true;
    } else if (corrected != Pass::Done) {
        out.corrected_truncated = true;
    }
    out.oldest_ms = oldest_record_ms(buf);
    return out;
}

}  // namespace ef::platform::diagnostics
