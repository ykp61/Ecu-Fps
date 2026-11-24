#include "benchmark/storage/storage_test.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <random>
#include <thread>

namespace ef::benchmark::storage {

namespace {

// ---- Yol yardimcilari ----
std::wstring strip_long_prefix(std::wstring p) {
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) return {};  // ag yolu kabul edilmez
    if (p.rfind(L"\\\\?\\", 0) == 0) p.erase(0, 4);
    return p;
}

std::wstring final_path_of(HANDLE h) {
    std::wstring buf(32768, L'\0');
    const DWORD n =
        GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return strip_long_prefix(std::move(buf));
}

std::wstring long_path_of(const std::wstring& p) {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetLongPathNameW(p.c_str(), buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return buf;
}

bool iequal(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
               CSTR_EQUAL;
}

std::wstring trim_trailing_sep(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

std::wstring join(const std::wstring& dir, std::wstring_view name) {
    std::wstring s = dir;
    if (!s.empty() && s.back() != L'\\') s += L'\\';
    s += name;
    return s;
}

// Yolun her bileseni (kok dahil) reparse point olmamali; eksik bilesen de reddedilir.
bool any_component_reparse(const std::wstring& path) {
    const std::wstring p = trim_trailing_sep(path);
    for (std::size_t i = 0; i <= p.size(); ++i) {
        if (i != p.size() && p[i] != L'\\') continue;
        std::wstring prefix = p.substr(0, i);
        if (prefix.size() == 2 && prefix[1] == L':') prefix += L'\\';  // "C:" -> "C:\"
        if (prefix.empty() || (i < p.size() && prefix.size() < 3)) continue;
        const DWORD a = GetFileAttributesW(prefix.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
    }
    return false;
}

HANDLE open_dir_no_follow(const std::wstring& dir, DWORD share) {
    return CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, share, nullptr, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

bool handle_is_plain_dir(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION bi{};
    return GetFileInformationByHandle(h, &bi) && (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Tanitici uzerinden silme isareti (ad kapanista kalkar).
bool mark_delete(HANDLE h) {
    FILE_DISPOSITION_INFO di{};
    di.DeleteFile = TRUE;
    return SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof di) != 0;
}

std::uint64_t random_u64() {
    std::random_device rd;  // MSVC: isletim sistemi rastgele kaynagi
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

// ---- Zaman ----
using Clock = std::chrono::steady_clock;
double seconds_since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

// ---- Hizali tampon ----
struct AlignedBuffer {
    void* p = nullptr;
    std::size_t size = 0;
    explicit AlignedBuffer(std::size_t n) : size(n) {
        p = VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);  // sayfa (4 KiB) hizali
    }
    ~AlignedBuffer() {
        if (p) VirtualFree(p, 0, MEM_RELEASE);
    }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
};

// ---- G/C motoru (overlapped + G/C tamamlanma portu, QD = slot sayisi) ----
// Tamamlanmalar porttan alinir (GetQueuedCompletionStatusEx): biten her slot, hangisi olursa olsun, geldigi
// sirayla zaman damgasi alir ve yeniden gonderilir. Olay + WaitForMultipleObjects(FALSE) her seferinde en dusuk
// isaretli slotu dondurdugu icin QD>1'de yuksek slotlar ac kalir ve gecikme/kuyruk derinligi bozulurdu.
// Gonderim cagrisi (ReadFile/WriteFile) 4 KiB rastgele G/C'de onlarca us CPU surebilir; tek is parcacigi
// QD32'yi dolu tutamaz (gonderim sirasi bekleyen slot cihazda degildir). Bu yuzden QD>1'de birden fazla is
// parcacigi ayni porttan tamamlanma alir; bir slotu, tamamlanmasini alan is parcacigi yeniden gonderir.
// Bir tanitici omru boyunca yalniz bir porta baglanabilir: port test dosyasi ile birlikte bir kez olusur.
class CompletionPort {
public:
    CompletionPort() = default;
    ~CompletionPort() {
        if (port_) CloseHandle(port_);
    }
    CompletionPort(const CompletionPort&) = delete;
    CompletionPort& operator=(const CompletionPort&) = delete;
    bool attach(HANDLE file) {
        // Eszamanli is parcacigi siniri 0 = islemci sayisi (cok is parcacikli gonderim icin).
        port_ = CreateIoCompletionPort(file, nullptr, 0, 0);
        if (!port_) return false;
        // Dosya tanitici olayi kullanilmaz; tamamlanma yalniz porttan izlenir.
        SetFileCompletionNotificationModes(file, FILE_SKIP_SET_EVENT_ON_HANDLE);
        return true;
    }
    HANDLE get() const { return port_; }

private:
    HANDLE port_ = nullptr;
};

struct IoRun {
    std::uint64_t bytes = 0;
    std::uint64_t ops = 0;
    double seconds = 0.0;
    std::uint32_t threads = 1;  // gonderim/tamamlama is parcacigi sayisi
    std::vector<std::uint64_t> slot_ops;  // slot basina tamamlanan G/C (tamponsuz motor)
    std::optional<double> slot_cycle_cv;  // slot bazinda gecikme CV'lerinin en buyugu (slot_fairness tabani)
    enum class Abort { None, OutOfMemory, Internal } abort = Abort::None;  // istisna yolu (G/C hatasi degil)
    std::uint64_t submit_ns = 0;          // ReadFile/WriteFile cagrisi icinde gecen toplam sure
    bool cancelled = false;
    bool deadline_hit = false;
    bool cap_hit = false;
    bool failed = false;
    DWORD error = 0;
};

struct IoParams {
    HANDLE h = nullptr;
    HANDLE port = nullptr;  // h'nin bagli oldugu tamamlanma portu
    Pattern pattern = Pattern::Sequential;
    Op op = Op::Read;
    std::uint32_t block = 0;
    std::uint32_t qd = 1;
    std::uint64_t file_bytes = 0;
    std::uint64_t byte_cap = 0;  // en fazla bu kadar byte
    Clock::time_point deadline;
    std::atomic<bool>* cancel = nullptr;
    std::uint64_t* rng = nullptr;
    std::uint64_t* stamp = nullptr;  // yazma damgasi sayaci
    LatencyHistogram* lat = nullptr;  // bos degilse her tamamlanan G/C gecikmesi eklenir
};

std::uint64_t ns_between(Clock::time_point a, Clock::time_point b) {
    const auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    return d > 0 ? static_cast<std::uint64_t>(d) : 0;
}

// QD>1: en fazla min(QD/2, islemci/2, 4) is parcacigi. QD1 tek is parcacigi (cagiran).
std::uint32_t io_threads_for(std::uint32_t qd) {
    if (qd <= 1) return 1;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const std::uint32_t cap = std::clamp<std::uint32_t>(si.dwNumberOfProcessors / 2, 1, 4);
    return std::clamp<std::uint32_t>(qd / 2, 1, cap);
}

constexpr ULONG_PTR kDoneKey = 0x1D0E;  // bitis isareti paketi (OVERLAPPED yok)
constexpr std::uint64_t kStallNs = 10'000'000'000ull;  // 10 s hic tamamlanma yok -> hata

// Slot basina, yalniz slotun o anki sahibi (tamamlanmasini porttan alan is parcacigi) tarafindan yazilan durum.
// Farkli is parcaciklari komsu slotlari yazar: her slot ayri onbellek satirinda (yanlis paylasim yok).
#pragma warning(push)
#pragma warning(disable : 4324)  // alignas dolgusu bilerek
struct alignas(64) SlotState {
    Clock::time_point issued_at{};
    std::uint64_t ops = 0;
    SlotLatency lat;  // mikrosaniye
};
#pragma warning(pop)
static_assert(sizeof(SlotState) % 64 == 0);

IoRun run_io(const IoParams& p) {
    IoRun r;
    const std::uint32_t qd = std::clamp<std::uint32_t>(p.qd, 1, 32);
    if (!p.port || p.block == 0 || p.file_bytes < p.block) {
        r.failed = true;
        r.error = ERROR_INVALID_PARAMETER;
        return r;
    }
    std::vector<std::unique_ptr<AlignedBuffer>> bufs;
    // OVERLAPPED dizisi yigindadir: hata yolunda bekleyen G/C bosalmazsa bilerek sizdirilir (cekirdek serbest
    // birakilmis bellege yazmasin).
    auto ovs_holder = std::make_unique<std::vector<OVERLAPPED>>(qd);
    std::vector<OVERLAPPED>& ovs = *ovs_holder;
    // Slot sahipligi tamamlanma ile devredilir: slotu yalniz tamamlanmasini porttan alan is parcacigi kullanir
    // (SlotState de yalniz sahip tarafindan yazilir; G/C gonderimi/port alimi sirayi saglar).
    std::vector<SlotState> slots(qd);
    for (std::uint32_t i = 0; i < qd; ++i) {
        bufs.push_back(std::make_unique<AlignedBuffer>(p.block));
        if (!bufs.back()->p) {
            r.failed = true;
            r.error = GetLastError();
            return r;
        }
        fill_random(bufs.back()->p, p.block, *p.rng);  // okumada da sifir olmayan baslangic
    }
    const std::uint64_t blocks = p.file_bytes / p.block;
    const std::uint32_t nthreads = io_threads_for(qd);

    struct Worker {
        std::uint64_t rng = 1;
        std::uint64_t bytes = 0;
        std::uint64_t ops = 0;
        std::uint64_t submit_ns = 0;
        OVERLAPPED_ENTRY entry{};  // uyanis basina tek paket; dongude tahsis yok (istisna kaynagi olmasin)
        LatencyHistogram lat;
    };
    std::vector<std::unique_ptr<Worker>> workers;
    for (std::uint32_t k = 0; k < nthreads; ++k) {
        workers.push_back(std::make_unique<Worker>());
        workers.back()->rng = (*p.rng ^ (0x9E3779B97F4A7C15ull * (k + 1))) | 1;
    }

    std::atomic<std::uint64_t> next_seq{0};
    std::atomic<std::uint64_t> issued_bytes{0};
    std::atomic<std::uint64_t> stamp{*p.stamp};
    std::atomic<std::uint32_t> inflight{0};  // gonderilmis ya da tamamlanmasi islenen slot sayisi
    std::atomic<bool> stopping{false}, done{false};
    std::atomic<bool> cancelled{false}, deadline_hit{false}, cap_hit{false}, failed{false};
    std::atomic<DWORD> first_error{0};
    const auto t0 = Clock::now();
    std::atomic<std::uint64_t> last_progress_ns{0};  // t0'dan beri son tamamlanma

    // Ilk hatayi kaydeder; bu cagri ilk hata ise (first_error CAS'ini kazandiysa) true doner.
    auto fail = [&](DWORD e) -> bool {
        DWORD z = 0;
        const bool first = first_error.compare_exchange_strong(z, e ? e : ERROR_GEN_FAILURE);
        failed.store(true);
        stopping.store(true);
        return first;
    };
    auto signal_done = [&] {
        done.store(true);
        for (std::uint32_t k = 0; k < nthreads; ++k) PostQueuedCompletionStatus(p.port, 0, kDoneKey, nullptr);
    };
    // Slot birakildi (yeniden gonderilmedi): son slot ise tum is parcaciklari uyandirilir.
    auto release = [&] {
        if (inflight.fetch_sub(1) == 1) signal_done();
    };
    // Gonderim: durma kosulu ya da hata -> false (slot birakilmalidir).
    auto issue = [&](std::uint32_t i, Worker& w) -> bool {
        if (stopping.load(std::memory_order_relaxed)) return false;
        if (p.cancel->load(std::memory_order_relaxed)) {
            cancelled.store(true);
            stopping.store(true);
            return false;
        }
        if (Clock::now() >= p.deadline) {
            deadline_hit.store(true);
            stopping.store(true);
            return false;
        }
        std::uint64_t cur = issued_bytes.load(std::memory_order_relaxed);
        do {
            if (cur + p.block > p.byte_cap) {
                cap_hit.store(true);
                stopping.store(true);
                return false;
            }
        } while (!issued_bytes.compare_exchange_weak(cur, cur + p.block, std::memory_order_relaxed));
        std::uint64_t blk;
        if (p.pattern == Pattern::Sequential) {
            blk = next_seq.fetch_add(1, std::memory_order_relaxed) % blocks;
        } else {
            std::uint64_t& s = w.rng;
            s ^= s >> 12;
            s ^= s << 25;
            s ^= s >> 27;
            blk = (s * 0x2545F4914F6CDD1Dull) % blocks;
        }
        const std::uint64_t off = blk * p.block;
        OVERLAPPED& ov = ovs[i];
        ov = OVERLAPPED{};
        ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(off >> 32);
        ov.hEvent = nullptr;  // tamamlanma porttan; senkron basari da porta paket birakir
        if (p.op == Op::Write) stamp_sectors(bufs[i]->p, p.block, stamp.fetch_add(1));  // damga zamanlama disi
        const auto t_issue = Clock::now();
        slots[i].issued_at = t_issue;
        const BOOL ok = p.op == Op::Write ? WriteFile(p.h, bufs[i]->p, p.block, nullptr, &ov)
                                          : ReadFile(p.h, bufs[i]->p, p.block, nullptr, &ov);
        w.submit_ns += ns_between(t_issue, Clock::now());  // gonderim CPU suresi (bilgi; gecikmeye zaten dahil)
        if (!ok) {
            const DWORD e = GetLastError();
            if (e != ERROR_IO_PENDING) {
                fail(e);
                return false;
            }
        }
        return true;
    };
    // Port paketinden slot: OVERLAPPED adresi ovs dizisinde olmali.
    auto slot_of = [&](const OVERLAPPED* o) -> std::uint32_t {
        const auto base = reinterpret_cast<std::uintptr_t>(ovs.data());
        const auto addr = reinterpret_cast<std::uintptr_t>(o);
        if (!o || addr < base || (addr - base) % sizeof(OVERLAPPED) != 0) return qd;
        const auto idx = (addr - base) / sizeof(OVERLAPPED);
        return idx < qd ? static_cast<std::uint32_t>(idx) : qd;
    };
    // Bir uyanista tek paket: alinan slot hemen yeniden gonderilir. Toplu alimda partideki son slot,
    // onundekilerin gonderim CPU suresi kadar (olculmeyen) bos bekler; kuyruk derinligi QD'nin altina duser.
    // Portta bekleyen tamamlanma ise gecikmeye dahildir (gonderim -> uygulamaca alinma).
    const ULONG per_wake = 1;
    auto work = [&](Worker& w) {
        OVERLAPPED_ENTRY* entries = &w.entry;
        while (!done.load()) {
            ULONG got = 0;
            if (!GetQueuedCompletionStatusEx(p.port, entries, per_wake, &got, 1000, FALSE)) {
                const DWORD e = GetLastError();
                const std::uint64_t now_ns = ns_between(t0, Clock::now());
                const std::uint64_t last = last_progress_ns.load();
                const std::uint64_t idle = now_ns > last ? now_ns - last : 0;
                if (e != WAIT_TIMEOUT) Sleep(1);  // port hatasi: sicak dongu olmasin
                if (e != WAIT_TIMEOUT || idle >= kStallNs) {
                    // Takilma/port hatasi: hepsini iptal et; iptal edilen G/C'ler yine porttan gelir ve birakilir.
                    if (!failed.load()) fail(e == WAIT_TIMEOUT ? ERROR_TIMEOUT : e);
                    CancelIoEx(p.h, nullptr);
                    last_progress_ns.store(ns_between(t0, Clock::now()));
                }
                continue;
            }
            // Uyanis basina tek paket (per_wake = 1): gozlem ani bu paketin alinma anidir.
            const auto done_at = Clock::now();
            last_progress_ns.store(ns_between(t0, done_at), std::memory_order_relaxed);
            for (ULONG k = 0; k < got; ++k) {
                if (entries[k].lpOverlapped == nullptr) continue;  // bitis isareti
                const std::uint32_t i = slot_of(entries[k].lpOverlapped);
                if (i >= qd) continue;  // olmamali
                DWORD n = 0;
                if (!GetOverlappedResult(p.h, &ovs[i], &n, FALSE)) {
                    fail(GetLastError());
                } else if (n == p.block) {
                    w.bytes += n;
                    ++w.ops;
                    const std::uint64_t lat_ns = ns_between(slots[i].issued_at, done_at);
                    ++slots[i].ops;
                    slots[i].lat.add(static_cast<double>(lat_ns) * 1e-3);
                    w.lat.add_ns(lat_ns);
                } else {
                    fail(ERROR_HANDLE_EOF);
                }
                if (!issue(i, w)) release();
            }
        }
    };

    for (std::uint32_t i = 0; i < qd; ++i) {
        inflight.fetch_add(1);
        if (!issue(i, *workers[0])) {
            inflight.fetch_sub(1);
            break;
        }
    }
    // Istisna yolu: work() bir istisna atarsa (beklenmez; dongude tahsis yok) is parcacigindan/ana is parcacigindan
    // disari tasinmaz. Kosu hata sayilir, tum G/C iptal edilir ve herkes bitis isaretiyle uyandirilir; boylece
    // birlesmemis std::thread yikimi (std::terminate) ve diger is parcaciklarinin sonsuz beklemesi olmaz.
    // Neden durust eslenir: bad_alloc -> bellek yetersiz; digerleri -> ic hata (G/C hatasi diye sunulmaz).
    std::atomic<bool> aborted{false};
    std::atomic<int> abort_kind{static_cast<int>(IoRun::Abort::None)};
    auto on_abort = [&](IoRun::Abort kind, DWORD code) noexcept {
        // Neden yalniz bu istisna ilk hata ise yazilir: once gercek bir G/C hatasi olduysa first_error o kodu tutar
        // ve neden de io_error_<kod> kalmalidir (aksi halde kod ile neden birbirini tutmaz).
        if (fail(code)) abort_kind.store(static_cast<int>(kind));
        aborted.store(true);
        CancelIoEx(p.h, nullptr);
        signal_done();
    };
    auto guarded = [&](Worker& w) noexcept {
        try {
            work(w);
        } catch (const std::bad_alloc&) {
            on_abort(IoRun::Abort::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
        } catch (...) {
            on_abort(IoRun::Abort::Internal, ERROR_INTERNAL_ERROR);
        }
    };
    bool leak = false;
    if (inflight.load() == 0) {
        done.store(true);
    } else {
        std::vector<std::thread> extra;
        for (std::uint32_t k = 1; k < nthreads; ++k) {
            try {
                extra.emplace_back(guarded, std::ref(*workers[k]));
            } catch (...) {
                break;  // daha az is parcacigi ile devam
            }
        }
        r.threads = 1 + static_cast<std::uint32_t>(extra.size());
        guarded(*workers[0]);
        for (auto& t : extra) t.join();
        if (aborted.load()) {
            // Erken bitis: bekleyen G/C kalmis olabilir. Tamponlar serbest birakilmadan once hepsinin bitmesi beklenir
            // (iptal edildi; en fazla kStallNs). Bitmezse tampon + OVERLAPPED bilerek sizdirilir.
            CancelIoEx(p.h, nullptr);
            const auto wait_start = Clock::now();
            for (;;) {
                bool all_done = true;
                for (const auto& ov : ovs) all_done = all_done && HasOverlappedIoCompleted(&ov);
                if (all_done) break;
                if (ns_between(wait_start, Clock::now()) >= kStallNs) {
                    leak = true;
                    break;
                }
                Sleep(1);
            }
        }
        // Kalan bitis isareti ve (hata yolunda) iptal paketleri temizlenir.
        OVERLAPPED_ENTRY e[8];
        ULONG got = 0;
        while (GetQueuedCompletionStatusEx(p.port, e, 8, &got, 0, FALSE) && got > 0) {
        }
    }
    r.seconds = seconds_since(t0);
    for (const auto& w : workers) {
        r.bytes += w->bytes;
        r.ops += w->ops;
        r.submit_ns += w->submit_ns;
        if (p.lat) p.lat->merge(w->lat);
    }
    r.slot_ops.reserve(qd);
    std::vector<SlotLatency> lats;
    lats.reserve(qd);
    for (const auto& s : slots) {
        r.slot_ops.push_back(s.ops);
        lats.push_back(s.lat);
    }
    r.slot_cycle_cv = fairness_cycle_cv(lats);
    r.abort = static_cast<IoRun::Abort>(abort_kind.load());
    if (leak) {
        (void)ovs_holder.release();
        for (auto& b : bufs) (void)b.release();
    }
    *p.rng = workers[0]->rng;
    *p.stamp = stamp.load();
    r.cancelled = cancelled.load();
    r.deadline_hit = deadline_hit.load();
    r.cap_hit = cap_hit.load();
    r.failed = failed.load();
    r.error = first_error.load();
    return r;
}

// Tamponlu (OS onbellegi) sirali okuma: senkron ReadFile, dosya sonunda basa sarar.
IoRun run_buffered_read(HANDLE h, std::uint32_t block, std::uint64_t file_bytes, Clock::time_point deadline,
                        std::uint64_t byte_cap, std::atomic<bool>& cancel, LatencyHistogram* lat = nullptr) {
    IoRun r;
    AlignedBuffer buf(block);
    if (!buf.p) {
        r.failed = true;
        return r;
    }
    const auto t0 = Clock::now();
    std::uint64_t off = 0;
    while (true) {
        if (cancel.load(std::memory_order_relaxed)) {
            r.cancelled = true;
            break;
        }
        if (Clock::now() >= deadline) {
            r.deadline_hit = true;
            break;
        }
        if (r.bytes + block > byte_cap) {
            r.cap_hit = true;
            break;
        }
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(off);
        DWORD n = 0;
        const auto ti = Clock::now();
        if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN) || !ReadFile(h, buf.p, block, &n, nullptr) || n != block) {
            r.failed = true;
            r.error = GetLastError();
            break;
        }
        if (lat) lat->add_ns(ns_between(ti, Clock::now()));
        r.bytes += n;
        ++r.ops;
        off += block;
        if (off + block > file_bytes) off = 0;
    }
    r.seconds = seconds_since(t0);
    return r;
}

// ---- EF-X22: dogrulama fazi icin senkron blok G/C ----
// test_file tamamlanma portuna baglidir: OVERLAPPED.hEvent'in dusuk biti 1 iken tamamlanma porta paket birakmaz
// (Microsoft Learn, GetQueuedCompletionStatus Remarks); sonraki run_io portunda yabanci paket olmaz. Bekleme kendi
// olayimizda; kStallNs icinde bitmezse G/C iptal edilir ve en fazla kStallNs daha beklenir (bosluk 19: INFINITE
// yok; arizali/yavas HDD-USB test ve cikisi dakikalarca kilitlemez). OVERLAPPED, olay ve G/C tamponu yigindaki
// tek bir yuvadadir; cagiranin tamponu cekirdege hic verilmez (kopyalanir). Iptalden sonra da bitmezse yuva
// bilerek sizdirilir (cekirdek sonra yazsa bile serbest bellege dokunmaz) ve cihaz sonraki G/C'leri reddeder.
class HandleBlockDevice final : public BlockDevice {
public:
    explicit HandleBlockDevice(HANDLE h) : h_(h), slot_(new (std::nothrow) Slot) {
        if (!slot_) return;
        slot_->ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        slot_->buf = VirtualAlloc(nullptr, kVerifyBlockBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);  // sayfa hizali
    }
    ~HandleBlockDevice() override {
        if (!slot_) return;  // sizdirildi (G/C hala cekirdekte) veya ayrilamadi
        if (slot_->ev) CloseHandle(slot_->ev);
        if (slot_->buf) VirtualFree(slot_->buf, 0, MEM_RELEASE);
    }
    HandleBlockDevice(const HandleBlockDevice&) = delete;
    HandleBlockDevice& operator=(const HandleBlockDevice&) = delete;
    bool ok() const { return slot_ && slot_->ev && slot_->buf; }
    bool write_block(std::uint64_t off, const void* data, std::uint32_t bytes) override {
        if (!usable(bytes)) return false;
        std::memcpy(slot_->buf, data, bytes);
        arm(off);
        return complete(WriteFile(h_, slot_->buf, bytes, nullptr, &slot_->ov), bytes);
    }
    bool read_block(std::uint64_t off, void* data, std::uint32_t bytes) override {
        if (!usable(bytes)) return false;
        arm(off);
        if (!complete(ReadFile(h_, slot_->buf, bytes, nullptr, &slot_->ov), bytes)) return false;
        std::memcpy(data, slot_->buf, bytes);
        return true;
    }
    std::uint32_t last_error() const override { return err_; }

private:
    struct Slot {
        OVERLAPPED ov{};
        HANDLE ev = nullptr;
        void* buf = nullptr;
    };
    bool usable(std::uint32_t bytes) {
        if (!ok()) {
            err_ = ERROR_TIMEOUT;  // onceki G/C takildi (yuva sizdirildi) veya kurulum eksik
            return false;
        }
        if (bytes > kVerifyBlockBytes) {
            err_ = ERROR_INVALID_PARAMETER;
            return false;
        }
        return true;
    }
    void arm(std::uint64_t off) {
        slot_->ov = OVERLAPPED{};
        slot_->ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFFu);
        slot_->ov.OffsetHigh = static_cast<DWORD>(off >> 32);
        slot_->ov.hEvent = reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(slot_->ev) | 1);
    }
    bool complete(BOOL issued, std::uint32_t bytes) {
        if (!issued) {
            const DWORD e = GetLastError();
            if (e != ERROR_IO_PENDING) {
                err_ = e;
                return false;
            }
        }
        constexpr auto kStallMs = static_cast<std::uint32_t>(kStallNs / 1'000'000ull);
        Slot* const s = slot_.get();
        const auto outcome = bounded_io_wait([s](std::uint32_t ms) { return WaitForSingleObject(s->ev, ms) == WAIT_OBJECT_0; },
                                             [this, s] { CancelIoEx(h_, &s->ov); }, kStallMs, kStallMs);
        if (outcome == IoWaitOutcome::Stuck) {
            (void)slot_.release();  // bilerek sizdirilir: cekirdek OVERLAPPED/olay/tampona hala yazabilir
            err_ = ERROR_TIMEOUT;
            return false;
        }
        if (outcome == IoWaitOutcome::CancelledDone) {
            err_ = ERROR_TIMEOUT;
            return false;
        }
        DWORD n = 0;
        if (!GetOverlappedResult(h_, &s->ov, &n, FALSE)) {
            err_ = GetLastError();
            return false;
        }
        if (n != bytes) {
            err_ = ERROR_HANDLE_EOF;
            return false;
        }
        return true;
    }
    HANDLE h_ = nullptr;
    std::unique_ptr<Slot> slot_;
    std::uint32_t err_ = 0;
};

// ---- Test klasoru (RAII temizlik) ----
class BenchFolder {
public:
    ~BenchFolder() { remove(); }
    bool create(const std::wstring& root, std::string& reason) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const std::wstring path = root + bench_folder_name(random_u64());
            if (CreateDirectoryW(path.c_str(), nullptr)) {
                path_ = path;
                break;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                reason = "folder_create_failed";
                return false;
            }
        }
        if (path_.empty()) {
            reason = "folder_name_collision";
            return false;
        }
        // Silme paylasimi yok: test boyunca klasor yeniden adlandirilip baglanti ile degistirilemez.
        dir_ = open_dir_no_follow(path_, FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (dir_ == INVALID_HANDLE_VALUE || !handle_is_plain_dir(dir_)) {
            reason = "folder_not_plain_directory";
            return false;
        }
        final_ = final_path_of(dir_);
        if (!iequal(final_, path_)) {
            reason = "folder_redirected";
            return false;
        }
        const auto oc = create_owned_file(path_, kMarkerName, FileKind::Marker, marker_);
        if (oc != CreateOutcome::Created) {
            reason = std::string("marker_") + create_outcome_text(oc);
            return false;
        }
        const std::string content = make_marker_content(GetCurrentProcessId());
        DWORD n = 0;
        if (!WriteFile(marker_.handle(), content.data(), static_cast<DWORD>(content.size()), &n, nullptr) ||
            n != content.size()) {
            reason = "marker_write_failed";
            return false;
        }
        return true;
    }
    const std::wstring& path() const { return path_; }
    // Not: veri dosyalari DELETE_ON_CLOSE ile acilir; cagiran once onlari kapatir.
    bool remove() {
        if (path_.empty()) return true;
        if (marker_.valid()) {
            mark_delete(marker_.handle());
            marker_.reset();
        }
        if (dir_ != INVALID_HANDLE_VALUE) {
            CloseHandle(dir_);
            dir_ = INVALID_HANDLE_VALUE;
        }
        // Kalan bilinen dosyalar (onceki hata yolundan) tek tek; yabanci icerik varsa klasor kalir.
        for (const auto name : {kTestFileName, kCacheFileName, kMarkerName}) {
            const std::wstring f = join(path_, name);
            HANDLE h = CreateFileW(f.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            BY_HANDLE_FILE_INFORMATION bi{};
            if (GetFileInformationByHandle(h, &bi) && !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                bi.nNumberOfLinks == 1)
                mark_delete(h);
            CloseHandle(h);
        }
        const bool ok = RemoveDirectoryW(path_.c_str()) != 0 || GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES;
        if (ok) path_.clear();
        return ok;
    }

private:
    std::wstring path_;
    std::wstring final_;
    HANDLE dir_ = INVALID_HANDLE_VALUE;
    OwnedFile marker_;
};

void finish_profile(ProfileResult& pr, const IoRun& io, bool total_deadline) {
    pr.bytes = io.bytes;
    pr.ops = io.ops;
    pr.seconds = io.seconds;
    pr.io_threads = io.threads;
    pr.slot = slot_fairness(io.slot_ops, io.slot_cycle_cv);
    if (!io.slot_ops.empty() && io.seconds > 0 && io.threads > 0)
        pr.submit_busy = static_cast<double>(io.submit_ns) * 1e-9 / (io.seconds * io.threads);
    if (io.failed) {
        pr.status = Status::Failed;
        // Istisna yolu G/C hata koduna donusturulmez: neden ayri anahtardir.
        if (io.abort == IoRun::Abort::OutOfMemory) pr.reason = "io_out_of_memory";
        else if (io.abort == IoRun::Abort::Internal) pr.reason = "io_internal_error";
        else pr.reason = "io_error_" + std::to_string(io.error);
    } else if (io.cancelled) {
        pr.status = Status::Cancelled;
        pr.reason = "user_cancel";
    } else if (io.cap_hit) {
        pr.status = Status::Ok;  // butceli byte miktari tamamlandi; sure kisaldi, bilgi olarak saklanir
        pr.reason = "byte_cap_reached";
        pr.label = label_after_run(pr.label, true, io.seconds);
    } else if (total_deadline) {
        pr.status = Status::Partial;
        pr.reason = "total_time_limit";
    } else {
        pr.status = Status::Ok;
    }
    if (pr.status == Status::Ok && io.ops == 0) {
        pr.status = Status::Failed;
        pr.reason = "no_io_completed";
    }
}

}  // namespace

// ---- OwnedFile ----
OwnedFile::~OwnedFile() { reset(); }
OwnedFile::OwnedFile(OwnedFile&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
OwnedFile& OwnedFile::operator=(OwnedFile&& o) noexcept {
    if (this != &o) {
        reset();
        h_ = o.h_;
        o.h_ = nullptr;
    }
    return *this;
}
void OwnedFile::reset() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
}
void OwnedFile::adopt(void* h) {
    reset();
    h_ = h;
}

const char* create_outcome_text(CreateOutcome o) {
    switch (o) {
        case CreateOutcome::Created: return "created";
        case CreateOutcome::Exists: return "exists_refused";
        case CreateOutcome::FolderReparse: return "folder_reparse_refused";
        case CreateOutcome::FolderRedirected: return "folder_redirected_refused";
        case CreateOutcome::Escaped: return "final_path_escaped";
        case CreateOutcome::HardLinked: return "hardlink_refused";
        case CreateOutcome::Error: return "error";
    }
    return "unknown";
}

CreateOutcome create_owned_file(const std::wstring& folder_in, std::wstring_view name, FileKind kind, OwnedFile& out) {
    out.reset();
    if (name.empty() || name.find_first_of(L"\\/:") != std::wstring_view::npos || name == L"." || name == L"..")
        return CreateOutcome::Error;
    const std::wstring folder = trim_trailing_sep(folder_in);
    if (any_component_reparse(folder)) return CreateOutcome::FolderReparse;
    HANDLE dir = open_dir_no_follow(folder, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (dir == INVALID_HANDLE_VALUE) return CreateOutcome::Error;
    const bool plain = handle_is_plain_dir(dir);
    const std::wstring dir_final = plain ? final_path_of(dir) : std::wstring{};
    CloseHandle(dir);
    if (!plain) return CreateOutcome::FolderReparse;
    const std::wstring expected = long_path_of(folder);
    if (dir_final.empty() || expected.empty() || !iequal(trim_trailing_sep(dir_final), trim_trailing_sep(expected)))
        return CreateOutcome::FolderRedirected;

    const std::wstring path = join(dir_final, name);
    DWORD access = GENERIC_READ | GENERIC_WRITE | DELETE;
    DWORD share = 0;
    DWORD flags = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    switch (kind) {
        case FileKind::Unbuffered:
            flags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE;
            break;
        case FileKind::Buffered: flags |= FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN; break;
        case FileKind::Marker: share = FILE_SHARE_READ; break;
    }
    // CREATE_NEW: ad varsa (kullanici dosyasi, symlink, hardlink) hic acilmaz ve dokunulmaz.
    HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, CREATE_NEW, flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) ? CreateOutcome::Exists : CreateOutcome::Error;
    }
    // Olusturma sonrasi dogrulama: son yol klasor altinda, tek link, reparse degil.
    BY_HANDLE_FILE_INFORMATION bi{};
    CreateOutcome oc = CreateOutcome::Created;
    if (!GetFileInformationByHandle(h, &bi)) {
        oc = CreateOutcome::Error;
    } else if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        oc = CreateOutcome::FolderReparse;
    } else if (bi.nNumberOfLinks != 1) {
        oc = CreateOutcome::HardLinked;
    } else {
        const std::wstring fp = final_path_of(h);
        if (!path_is_under(fp, dir_final) || !iequal(fp, path)) oc = CreateOutcome::Escaped;
    }
    if (oc != CreateOutcome::Created) {
        mark_delete(h);  // yalniz az once CREATE_NEW ile olusturdugumuz dosya
        CloseHandle(h);
        return oc;
    }
    out.adopt(h);
    return oc;
}

RunResult run_storage_test(const RunConfig& cfg, std::atomic<bool>& cancel) {
    RunResult res;
    res.cleanup_ok = true;
    const Limits& lim = cfg.limits;
    const auto t_start = Clock::now();
    const auto total_deadline = t_start + std::chrono::milliseconds(lim.total_ms);
    res.write_budget_bytes = max_host_write_bytes(lim);

    if (const auto lv = validate_limits(lim); lv != LimitsVerdict::Ok) {
        res.status = Status::Refused;
        res.reason = limits_reason(lv);
        return res;
    }
    if (const auto tv = validate_volume_root(cfg.volume_root); tv != TargetVerdict::Ok) {
        res.status = Status::Refused;
        res.reason = verdict_reason(tv);
        return res;
    }
    res.volume_root = normalize_volume_root(cfg.volume_root);
    const UINT dt = GetDriveTypeW(res.volume_root.c_str());
    if (dt != DRIVE_FIXED && dt != DRIVE_REMOVABLE) {
        res.status = Status::Refused;
        res.reason = dt == DRIVE_NO_ROOT_DIR ? "volume_not_found" : "drive_type_unsupported";
        return res;
    }
    // Kok yonlendirmesi (SUBST/baglanti): kokun son yolu kendisi olmali.
    {
        HANDLE h = open_dir_no_follow(res.volume_root, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        if (h == INVALID_HANDLE_VALUE) {
            res.status = Status::Refused;
            res.reason = "volume_root_unreadable";
            return res;
        }
        const bool plain = handle_is_plain_dir(h);
        const std::wstring fp = final_path_of(h);
        CloseHandle(h);
        if (!plain || !iequal(trim_trailing_sep(fp), res.volume_root)) {
            res.status = Status::Refused;
            res.reason = "volume_root_redirected";
            return res;
        }
    }
    ULARGE_INTEGER avail{}, total{}, freeb{};
    if (!GetDiskFreeSpaceExW(res.volume_root.c_str(), &avail, &total, &freeb)) {
        res.status = Status::Failed;
        res.reason = "free_space_query_failed";
        return res;
    }
    res.volume_total_bytes = total.QuadPart;
    res.volume_free_bytes = avail.QuadPart;
    res.required_free_bytes = required_free_for(lim, total.QuadPart);  // iki kuralin buyugu (atlama metni)
    if (!free_space_sufficient(avail.QuadPart, total.QuadPart, preparation_write_bytes(lim))) {
        res.status = Status::Skipped;
        res.reason = "free_space_floor";
        return res;
    }
    if (!within_free_share(lim.file_bytes, avail.QuadPart, lim.max_free_permille)) {
        res.status = Status::Skipped;
        res.reason = "free_share_limit";  // dosya bos alanin %10'undan buyuk: kirpilmaz, atlanir
        return res;
    }
    if (cancel.load()) {
        res.status = Status::Cancelled;
        res.reason = "user_cancel";
        return res;
    }

    // Dosyalar klasorden once kapanmali: bildirim sirasi = ters yikim sirasi.
    BenchFolder folder;
    CompletionPort port;  // test_file'a bir kez baglanir; tum tamponsuz profiller kullanir
    OwnedFile test_file;
    OwnedFile cache_file;
    auto finish = [&](Status s, std::string reason) {
        res.status = s;
        if (res.reason.empty()) res.reason = std::move(reason);
        test_file.reset();
        cache_file.reset();
        res.cleanup_ok = folder.remove();
        return res;
    };

    std::string why;
    if (!folder.create(res.volume_root, why)) {
        res.folder = folder.path();
        return finish(Status::Failed, why);
    }
    res.folder = folder.path();
    if (const auto oc = create_owned_file(folder.path(), kTestFileName, FileKind::Unbuffered, test_file);
        oc != CreateOutcome::Created)
        return finish(oc == CreateOutcome::Error ? Status::Failed : Status::Refused,
                      std::string("test_file_") + create_outcome_text(oc));
    if (!port.attach(static_cast<HANDLE>(test_file.handle())))
        return finish(Status::Failed, "io_port_failed_" + std::to_string(GetLastError()));

    FILE_STORAGE_INFO si{};
    res.sector_bytes = GetFileInformationByHandleEx(static_cast<HANDLE>(test_file.handle()), FileStorageInfo, &si, sizeof si)
                           ? si.PhysicalBytesPerSectorForPerformance
                           : 4096;
    if (res.sector_bytes == 0) res.sector_bytes = 4096;
    // 4 KiB'den buyuk sektorde 4 KiB profili buyutulmez; asagida "profile_not_sector_aligned" ile atlanir.

    std::uint64_t rng = random_u64() | 1;
    std::uint64_t stamp = 1;

    // Hazirlik: dosyayi rastgele veriyle doldur (olcum disi; sifir verisi ile yaniltici hiz yok).
    {
        const auto tp = Clock::now();
        IoParams p;
        p.h = static_cast<HANDLE>(test_file.handle());
        p.port = port.get();
        p.pattern = Pattern::Sequential;
        p.op = Op::Write;
        p.block = static_cast<std::uint32_t>(kMiB);
        p.qd = 1;
        p.file_bytes = lim.file_bytes;
        p.byte_cap = clamp_write(lim.write_budget_bytes, res.bytes_written, lim.file_bytes, p.block);
        p.deadline = total_deadline;
        p.cancel = &cancel;
        p.rng = &rng;
        p.stamp = &stamp;
        const IoRun io = run_io(p);
        res.bytes_written += io.bytes;
        res.preparation_seconds = seconds_since(tp);
        if (io.failed) return finish(Status::Failed, "preparation_io_error_" + std::to_string(io.error));
        if (io.cancelled) return finish(Status::Cancelled, "user_cancel");
        if (io.bytes != lim.file_bytes) return finish(Status::Partial, "preparation_time_limit");
    }

    // EF-X22: sinirli yaz-oku-karsilastir. Dosya NO_BUFFERING|WRITE_THROUGH acik; ofset/boy 1 MiB, tampon sayfa
    // hizali. Yazma butceye dahil, dosya disina yazmaz. Uyusmazlikta profil kosulmaz (puan yok).
    if (cfg.verify && lim.verify_bytes > 0 && kVerifyBlockBytes % res.sector_bytes == 0) {
        if (clamp_write(lim.write_budget_bytes, res.bytes_written, lim.verify_bytes, kVerifyBlockBytes) != lim.verify_bytes)
            return finish(Status::Skipped, "write_budget_exhausted");
        AlignedBuffer wb(kVerifyBlockBytes), rb(kVerifyBlockBytes);
        HandleBlockDevice dev(static_cast<HANDLE>(test_file.handle()));
        if (!wb.p || !rb.p || !dev.ok()) return finish(Status::Failed, "storage_verify_setup_failed");
        res.verify = run_verify(dev, lim.verify_bytes, kVerifyBlockBytes, random_u64(), static_cast<std::uint64_t*>(wb.p),
                                static_cast<std::uint64_t*>(rb.p), cancel, [&] { return Clock::now() >= total_deadline; });
        res.bytes_written += res.verify.bytes_written;
        switch (res.verify.status) {
            case VerifyStatus::Ok:
            case VerifyStatus::NotRun: break;
            case VerifyStatus::Mismatch: return finish(Status::Failed, "storage_verify_mismatch");
            case VerifyStatus::IoError: return finish(Status::Failed, "storage_verify_io_error");
            case VerifyStatus::Cancelled: return finish(Status::Cancelled, "user_cancel");
            case VerifyStatus::TimeLimit: return finish(Status::Partial, "storage_verify_incomplete");
            case VerifyStatus::Invalid: return finish(Status::Failed, "storage_verify_invalid");
        }
    }

    bool any_partial = false;
    // Tamponlu cache profili varsa onun hazirlik yazimi butceden ayrilir (yazma profili hepsini tuketmez).
    std::uint64_t cache_reserve = 0;
    for (const Profile& prof : cfg.profiles)
        if (prof.cache == CacheMode::Buffered) cache_reserve = lim.cache_file_bytes;
    // Yazma profilleri kalan butceyi esit paylasir; atlanan/erken biten profilin payi sonrakilere kalir.
    std::uint32_t writers_left = count_write_profiles(cfg.profiles);
    for (const Profile& prof : cfg.profiles) {
        ProfileResult pr;
        pr.profile = prof;
        pr.label = label_of(prof);
        const bool writer = prof.op == Op::Write && prof.cache == CacheMode::Unbuffered;
        const std::uint32_t writers_here = writer && writers_left > 0 ? writers_left-- : 0;
        LatencyHistogram lat;
        const auto now = Clock::now();
        if (cancel.load()) {
            pr.status = Status::Cancelled;
            pr.reason = "user_cancel";
            res.profiles.push_back(pr);
            continue;
        }
        if (now >= total_deadline) {
            pr.status = Status::Skipped;
            pr.reason = "total_time_limit";
            any_partial = true;
            res.profiles.push_back(pr);
            continue;
        }
        const auto profile_deadline = std::min(total_deadline, now + std::chrono::milliseconds(lim.profile_ms));
        const bool cut_by_total = profile_deadline == total_deadline;
        if (prof.block_bytes == 0 || prof.block_bytes % 4096 != 0 || prof.block_bytes % res.sector_bytes != 0 ||
            prof.queue_depth == 0 || prof.queue_depth > 32) {
            pr.status = Status::Skipped;
            pr.reason = "profile_not_sector_aligned";
            res.profiles.push_back(pr);
            continue;
        }
        IoRun io;
        if (prof.cache == CacheMode::Buffered) {
            if (prof.op != Op::Read) {
                pr.status = Status::Skipped;
                pr.reason = "buffered_write_not_supported";
                res.profiles.push_back(pr);
                continue;
            }
            cache_reserve = 0;
            const auto oc = create_owned_file(folder.path(), kCacheFileName, FileKind::Buffered, cache_file);
            if (oc != CreateOutcome::Created) {
                pr.status = Status::Failed;
                pr.reason = std::string("cache_file_") + create_outcome_text(oc);
                res.profiles.push_back(pr);
                continue;
            }
            // Hazirlik: rastgele veri yaz + bir isinma okumasi (olcum disi).
            const std::uint64_t allowed =
                clamp_write(lim.write_budget_bytes, res.bytes_written, lim.cache_file_bytes, prof.block_bytes);
            if (allowed != lim.cache_file_bytes) {
                cache_file.reset();
                pr.status = Status::Skipped;
                pr.reason = "write_budget_exhausted";
                res.profiles.push_back(pr);
                continue;
            }
            AlignedBuffer b(prof.block_bytes);
            bool ok = b.p != nullptr;
            for (std::uint64_t off = 0; ok && off < lim.cache_file_bytes; off += prof.block_bytes) {
                fill_random(b.p, prof.block_bytes, rng);
                DWORD n = 0;
                ok = WriteFile(static_cast<HANDLE>(cache_file.handle()), b.p, prof.block_bytes, &n, nullptr) &&
                     n == prof.block_bytes;
                if (ok) res.bytes_written += n;
            }
            std::atomic<bool> never{false};
            if (ok) {
                const IoRun warm = run_buffered_read(static_cast<HANDLE>(cache_file.handle()), prof.block_bytes,
                                                     lim.cache_file_bytes, profile_deadline, lim.cache_file_bytes, never);
                ok = !warm.failed;
            }
            if (!ok) {
                cache_file.reset();
                pr.status = Status::Failed;
                pr.reason = "cache_preparation_failed";
                res.profiles.push_back(pr);
                continue;
            }
            io = run_buffered_read(static_cast<HANDLE>(cache_file.handle()), prof.block_bytes, lim.cache_file_bytes,
                                   profile_deadline, UINT64_MAX, cancel, &lat);
            cache_file.reset();  // DELETE_ON_CLOSE
        } else {
            IoParams p;
            p.h = static_cast<HANDLE>(test_file.handle());
            p.port = port.get();
            p.pattern = prof.pattern;
            p.op = prof.op;
            p.block = prof.block_bytes;
            p.qd = prof.queue_depth;
            p.file_bytes = lim.file_bytes;
            p.byte_cap = prof.op == Op::Write ? write_share(lim.write_budget_bytes, res.bytes_written, cache_reserve,
                                                            writers_here, prof.block_bytes)
                                              : UINT64_MAX;
            p.deadline = profile_deadline;
            p.cancel = &cancel;
            p.rng = &rng;
            p.stamp = &stamp;
            p.lat = &lat;
            if (prof.op == Op::Write) pr.byte_cap = p.byte_cap;
            if (p.byte_cap == 0) {
                pr.status = Status::Skipped;
                pr.reason = "write_budget_exhausted";
                res.profiles.push_back(pr);
                continue;
            }
            io = run_io(p);
            if (prof.op == Op::Write) res.bytes_written += io.bytes;
        }
        finish_profile(pr, io, cut_by_total && io.deadline_hit);
        pr.set_latency(lat);
        if (pr.status == Status::Partial) any_partial = true;
        res.profiles.push_back(pr);
    }

    if (cancel.load()) return finish(Status::Cancelled, "user_cancel");
    bool any_failed = false;
    for (const auto& pr : res.profiles) any_failed |= pr.status == Status::Failed;
    if (any_failed) return finish(Status::Partial, "profile_failed");
    return finish(any_partial ? Status::Partial : Status::Ok, any_partial ? "partial_profile" : "");
}

OrphanReport cleanup_orphans(const std::wstring& parent_in) {
    OrphanReport rep;
    const std::wstring parent = trim_trailing_sep(parent_in);
    if (parent.empty() || any_component_reparse(parent)) return rep;
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = join(parent, std::wstring(kFolderPrefix) + L"*");
    HANDLE f = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return rep;
    std::vector<std::pair<std::wstring, bool>> candidates;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!is_bench_folder_name(fd.cFileName)) continue;
        candidates.emplace_back(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    } while (FindNextFileW(f, &fd));
    FindClose(f);

    for (const auto& [name, reparse_attr] : candidates) {
        ++rep.scanned;
        const std::wstring dir = join(parent, name);
        auto keep = [&] {
            ++rep.kept;
            rep.kept_folders.push_back(dir);
        };
        bool folder_reparse = reparse_attr;
        HANDLE dh = INVALID_HANDLE_VALUE;
        if (!folder_reparse) {
            dh = open_dir_no_follow(dir, FILE_SHARE_READ | FILE_SHARE_WRITE);  // silme/yeniden adlandirma engellenir
            folder_reparse = dh == INVALID_HANDLE_VALUE || !handle_is_plain_dir(dh);
        }
        std::vector<OrphanEntry> entries;
        bool marker_ok = false;
        if (!folder_reparse) {
            WIN32_FIND_DATAW e{};
            HANDLE hfind = FindFirstFileExW(join(dir, L"*").c_str(), FindExInfoBasic, &e, FindExSearchNameMatch, nullptr, 0);
            if (hfind != INVALID_HANDLE_VALUE) {
                do {
                    const std::wstring en = e.cFileName;
                    if (en == L"." || en == L"..") continue;
                    OrphanEntry oe;
                    oe.name = en;
                    oe.is_dir = (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    oe.is_reparse = (e.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    oe.link_count = 0;
                    if (!oe.is_dir && !oe.is_reparse) {
                        HANDLE h = CreateFileW(join(dir, en).c_str(), FILE_READ_ATTRIBUTES,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                        BY_HANDLE_FILE_INFORMATION bi{};
                        if (h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(h, &bi)) oe.link_count = bi.nNumberOfLinks;
                        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
                    }
                    entries.push_back(std::move(oe));
                } while (FindNextFileW(hfind, &e));
                FindClose(hfind);
            }
            HANDLE mh = CreateFileW(join(dir, kMarkerName).c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (mh != INVALID_HANDLE_VALUE) {
                char buf[256];
                DWORD n = 0;
                if (ReadFile(mh, buf, sizeof buf, &n, nullptr)) marker_ok = marker_content_valid(std::string_view(buf, n));
                CloseHandle(mh);
            }
        }
        const OrphanDecision d = decide_orphan(name, folder_reparse, marker_ok, entries);
        if (d != OrphanDecision::Delete) {
            if (dh != INVALID_HANDLE_VALUE) CloseHandle(dh);
            keep();
            continue;
        }
        // Once veri dosyalari, en son isaret: yarida kalirsa klasor hala bizim olarak taninir.
        bool ok = true;
        for (const auto fname : {kTestFileName, kCacheFileName, kMarkerName}) {
            if (!ok) break;
            const std::wstring fpath = join(dir, fname);
            HANDLE h = CreateFileW(fpath.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                const DWORD e = GetLastError();
                ok = e == ERROR_FILE_NOT_FOUND;  // yoksa sorun degil; kilitli (yasayan test) ise dur
                continue;
            }
            BY_HANDLE_FILE_INFORMATION bi{};
            ok = GetFileInformationByHandle(h, &bi) && !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                 !(bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && bi.nNumberOfLinks == 1 && mark_delete(h);
            CloseHandle(h);
        }
        if (dh != INVALID_HANDLE_VALUE) CloseHandle(dh);
        if (ok && RemoveDirectoryW(dir.c_str())) {
            ++rep.removed;
        } else {
            keep();
        }
    }
    return rep;
}

}  // namespace ef::benchmark::storage
