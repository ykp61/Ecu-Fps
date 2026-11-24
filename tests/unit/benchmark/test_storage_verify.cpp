// EF-X22 T02/T03/T04: depolama yaz-oku-karsilastir fazi (sahte blok aygiti; dosya sistemi yok).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "benchmark/storage/storage_rules.h"
#include "ef_test.h"

using namespace ef::benchmark::storage;

namespace {
constexpr std::uint32_t kBlk = 4096;
constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

class FakeDevice final : public BlockDevice {
public:
    explicit FakeDevice(std::uint64_t bytes) : media(static_cast<std::size_t>(bytes), 0) {}
    std::vector<unsigned char> media;
    std::uint64_t flip_byte = kNone;         // yazilan verinin bu ofsetteki bir biti ortamda bozulur
    std::uint64_t lost_write_block = kNone;  // yazim "basarili" doner, ortama gitmez
    std::uint64_t fail_write_block = kNone;
    std::uint64_t fail_read_block = kNone;
    std::uint64_t swap_a = kNone, swap_b = kNone;  // okumada iki blok adresi karisir
    bool reads_skip_copy = false;                  // okuma basarili doner, tamponu doldurmaz
    std::atomic<bool>* cancel = nullptr;
    std::uint64_t cancel_after_writes = kNone, cancel_after_reads = kNone;
    std::uint64_t writes = 0, reads = 0, max_end = 0;
    std::uint32_t err = 0;
    bool write_block(std::uint64_t off, const void* data, std::uint32_t bytes) override {
        const std::uint64_t b = off / kBlk;
        if (b == fail_write_block) {
            err = 1117;
            return false;
        }
        ++writes;
        max_end = std::max(max_end, off + bytes);
        if (cancel && writes == cancel_after_writes) cancel->store(true);
        if (b == lost_write_block) return true;
        std::memcpy(media.data() + static_cast<std::size_t>(off), data, bytes);
        if (flip_byte != kNone && flip_byte >= off && flip_byte < off + bytes) media[static_cast<std::size_t>(flip_byte)] ^= 0x10;
        return true;
    }
    bool read_block(std::uint64_t off, void* data, std::uint32_t bytes) override {
        std::uint64_t b = off / kBlk;
        if (b == fail_read_block) {
            err = 23;
            return false;
        }
        ++reads;
        max_end = std::max(max_end, off + bytes);
        if (cancel && reads == cancel_after_reads) cancel->store(true);
        if (reads_skip_copy) return true;
        if (b == swap_a) b = swap_b;
        else if (b == swap_b) b = swap_a;
        std::memcpy(data, media.data() + static_cast<std::size_t>(b * kBlk), bytes);
        return true;
    }
    std::uint32_t last_error() const override { return err; }
};

VerifyResult verify_fake(FakeDevice& d, std::uint64_t bytes, std::atomic<bool>& cancel, const std::function<bool()>& time_up = {}) {
    std::vector<std::uint64_t> w(kBlk / 8), r(kBlk / 8);
    return run_verify(d, bytes, kBlk, 0x1234, w.data(), r.data(), cancel, time_up);
}
}  // namespace

EF_TEST(storage_verify_clean_device_passes_every_block) {
    FakeDevice d(8 * kBlk);
    std::atomic<bool> c{false};
    const auto r = verify_fake(d, 8 * kBlk, c);
    EF_CHECK(r.status == VerifyStatus::Ok);
    EF_CHECK(r.blocks_planned == 8 && r.blocks_written == 8 && r.blocks_verified == 8 && r.mismatched_blocks == 0);
    EF_CHECK(!r.first_mismatch_offset.has_value());
    EF_CHECK(r.bytes_written == 8ull * kBlk && d.writes == 8 && d.reads == 8);
    EF_CHECK(d.max_end == 8ull * kBlk);  // bolge disina yazmaz/okumaz
}

EF_TEST(storage_verify_bit_flip_is_mismatch_not_pass) {  // EF-X22-T02
    FakeDevice d(8 * kBlk);
    d.flip_byte = 3ull * kBlk + 100;
    std::atomic<bool> c{false};
    const auto r = verify_fake(d, 8 * kBlk, c);
    EF_CHECK(r.status == VerifyStatus::Mismatch);
    EF_CHECK(r.mismatched_blocks == 1 && r.blocks_verified == 7 && d.reads == 8);  // her blok okunur
    EF_CHECK(r.first_mismatch_offset && *r.first_mismatch_offset == 3ull * kBlk + 96);  // 8 byte kelime hizali
}

EF_TEST(storage_verify_lost_write_and_unfilled_read_detected) {  // EF-X22-T02
    {
        FakeDevice d(8 * kBlk);
        d.lost_write_block = 5;
        std::atomic<bool> c{false};
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::Mismatch && r.mismatched_blocks == 1);
        EF_CHECK(r.first_mismatch_offset && *r.first_mismatch_offset == 5ull * kBlk);
    }
    {
        FakeDevice d(4 * kBlk);
        d.reads_skip_copy = true;  // tampon sifirlanir: eski icerik esleme sayilmaz
        std::atomic<bool> c{false};
        const auto r = verify_fake(d, 4 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::Mismatch && r.mismatched_blocks == 4 && r.blocks_verified == 0);
    }
}

EF_TEST(storage_verify_address_swap_detected) {  // EF-X22-T02: tekduze desen bunu goremezdi
    FakeDevice d(8 * kBlk);
    d.swap_a = 2;
    d.swap_b = 4;
    std::atomic<bool> c{false};
    const auto r = verify_fake(d, 8 * kBlk, c);
    EF_CHECK(r.status == VerifyStatus::Mismatch && r.mismatched_blocks == 2 && r.blocks_verified == 6);
}

EF_TEST(storage_verify_io_errors_stop_and_report_code) {
    {
        FakeDevice d(8 * kBlk);
        d.fail_write_block = 2;
        std::atomic<bool> c{false};
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::IoError && r.io_error == 1117);
        EF_CHECK(r.blocks_written == 2 && d.reads == 0 && r.blocks_verified == 0);
    }
    {
        FakeDevice d(8 * kBlk);
        d.fail_read_block = 6;
        std::atomic<bool> c{false};
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::IoError && r.io_error == 23 && r.blocks_verified == 6);
    }
}

EF_TEST(storage_verify_cancel_is_safe_and_keeps_found_mismatch) {  // EF-X22-T04 (iptal)
    {
        FakeDevice d(8 * kBlk);
        std::atomic<bool> c{false};
        d.cancel = &c;
        d.cancel_after_writes = 3;
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::Cancelled);
        EF_CHECK(d.writes == 3 && d.reads == 0 && r.blocks_verified == 0);  // iptal sonrasi G/C yok
    }
    {
        FakeDevice d(8 * kBlk);
        d.flip_byte = 8;  // blok 0
        std::atomic<bool> c{false};
        d.cancel = &c;
        d.cancel_after_reads = 2;
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::Mismatch && d.reads == 2 && r.mismatched_blocks == 1);  // uyari kaybolmaz
    }
    {
        FakeDevice d(8 * kBlk);
        std::atomic<bool> c{true};  // baslamadan iptal
        const auto r = verify_fake(d, 8 * kBlk, c);
        EF_CHECK(r.status == VerifyStatus::Cancelled && d.writes == 0);
    }
}

EF_TEST(storage_verify_time_limit_is_incomplete_not_ok) {
    FakeDevice d(8 * kBlk);
    std::atomic<bool> c{false};
    int calls = 0;
    const auto r = verify_fake(d, 8 * kBlk, c, [&] { return ++calls > 5; });
    EF_CHECK(r.status == VerifyStatus::TimeLimit && d.writes == 5 && d.reads == 0 && r.blocks_verified == 0);
}

EF_TEST(storage_verify_invalid_params_do_no_io) {
    FakeDevice d(8 * kBlk + 8);
    std::atomic<bool> c{false};
    std::vector<std::uint64_t> w(kBlk / 8 + 1), r(kBlk / 8 + 1);
    EF_CHECK(run_verify(d, 0, kBlk, 1, w.data(), r.data(), c, {}).status == VerifyStatus::Invalid);
    EF_CHECK(run_verify(d, 8 * kBlk, 4100, 1, w.data(), r.data(), c, {}).status == VerifyStatus::Invalid);
    EF_CHECK(run_verify(d, 8 * kBlk + 8, kBlk, 1, w.data(), r.data(), c, {}).status == VerifyStatus::Invalid);
    EF_CHECK(run_verify(d, 8 * kBlk, kBlk, 1, nullptr, r.data(), c, {}).status == VerifyStatus::Invalid);
    EF_CHECK(d.writes == 0 && d.reads == 0);
}

EF_TEST(storage_limits_verify_size_and_budget) {  // EF-X22-T03: ek bos alan istemez, butceyi asmaz
    const Limits q = quick_limits(), f = full_limits();
    EF_CHECK(q.verify_bytes == 64 * kMiB && f.verify_bytes == 256 * kMiB);
    EF_CHECK(validate_limits(q) == LimitsVerdict::Ok && validate_limits(f) == LimitsVerdict::Ok);
    EF_CHECK(q.verify_bytes % kVerifyBlockBytes == 0 && f.verify_bytes % kVerifyBlockBytes == 0);
    EF_CHECK(preparation_write_bytes(q) + q.verify_bytes <= max_host_write_bytes(q));
    EF_CHECK(preparation_write_bytes(f) + f.verify_bytes <= max_host_write_bytes(f));
    Limits a = q;
    a.verify_bytes = 0;
    EF_CHECK(validate_limits(a) == LimitsVerdict::Ok);
    EF_CHECK(preparation_write_bytes(a) == preparation_write_bytes(q));  // bos alan karari dogrulamadan bagimsiz
    a = q;
    a.verify_bytes = q.file_bytes + kMiB;
    EF_CHECK(validate_limits(a) == LimitsVerdict::VerifyInvalid);
    a = q;
    a.verify_bytes = 64 * kMiB + 4096;
    EF_CHECK(validate_limits(a) == LimitsVerdict::VerifyInvalid);
    a = q;
    a.write_budget_bytes = preparation_write_bytes(q) + q.verify_bytes - kMiB;
    EF_CHECK(validate_limits(a) == LimitsVerdict::BudgetBelowPreparation);
    a.write_budget_bytes = preparation_write_bytes(q) + q.verify_bytes;
    EF_CHECK(validate_limits(a) == LimitsVerdict::Ok);
    EF_CHECK(std::string(limits_reason(LimitsVerdict::VerifyInvalid)) == "verify_size_invalid");
}
