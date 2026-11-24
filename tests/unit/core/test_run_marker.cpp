// EF-X22: kesik kosu kaydi (T04) ve adrese bagli bellek dogrulamasi (T01). Hata enjeksiyonu sentetiktir.
#include <vector>

#include "core/measure/run_marker.h"
#include "ef_test.h"

using namespace ef::core::measure;

EF_TEST(x22_t04_interrupted_run_is_recorded_not_restarted) {
    const RunMarker m{"builtin", 1700000000000};
    const auto d = decode_run_marker(encode_run_marker(m));
    EF_CHECK(d && d->source == "builtin" && d->started_unix_ms == m.started_unix_ms);
    EF_CHECK(!decode_run_marker("{bozuk") && !decode_run_marker(R"({"source":"rm -rf","started_unix_ms":1})"));
    const auto e = interrupted_entry(*d);
    EF_CHECK(!e.run_valid && e.fingerprint.empty() && e.exclusions.size() == 1 && e.exclusions[0] == "run.interrupted" && e.metrics.empty());
}

EF_TEST(x22_t01_address_pattern_catches_single_bit_and_aliasing) {
    std::vector<std::uint64_t> w(4096);
    fill_address_pattern(w.data(), w.size(), 7);
    EF_CHECK(!verify_address_pattern(w.data(), w.size(), 7));
    w[1234] ^= 1ull << 17;  // tek bit hatasi
    EF_CHECK(verify_address_pattern(w.data(), w.size(), 7) == std::optional<std::size_t>(1234));
    fill_address_pattern(w.data(), w.size(), 7);
    w[2000] = w[1000];  // adres karisikligi: tekduze desen bunu goremez
    EF_CHECK(verify_address_pattern(w.data(), w.size(), 7) == std::optional<std::size_t>(2000));
    EF_CHECK(address_pattern(5, 1) != address_pattern(5, 2) && address_pattern(5, 1) != address_pattern(6, 1));
}
