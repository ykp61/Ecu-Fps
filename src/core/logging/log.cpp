#include "core/logging/log.h"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace ef::core::logging {

std::string_view to_string(Level l) noexcept {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

std::string format_utc(UtcTime t) {
    // Howard Hinnant civil_from_days
    std::int64_t ms = t.unix_ms;
    std::int64_t days = ms >= 0 ? ms / 86'400'000 : (ms - 86'399'999) / 86'400'000;
    std::int64_t rem = ms - days * 86'400'000;
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(rem / 3'600'000),
                  static_cast<long long>(rem / 60'000 % 60), static_cast<long long>(rem / 1000 % 60),
                  static_cast<long long>(rem % 1000));
    return buf;
}

RotatingFileSink::RotatingFileSink(std::filesystem::path dir, std::string base_name, std::uintmax_t max_bytes, int keep)
    : dir_(std::move(dir)), base_(std::move(base_name)), max_bytes_(max_bytes), keep_(keep < 1 ? 1 : keep) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
}

void RotatingFileSink::rotate() {
    std::error_code ec;
    const auto name = [&](int i) { return dir_ / (base_ + "." + std::to_string(i) + ".log"); };
    std::filesystem::remove(name(keep_ - 1), ec);
    for (int i = keep_ - 2; i >= 1; --i) std::filesystem::rename(name(i), name(i + 1), ec);
    if (keep_ > 1) std::filesystem::rename(active_path(), name(1), ec);
    else std::filesystem::remove(active_path(), ec);
}

void RotatingFileSink::write_line(const std::string& line) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(active_path(), ec);
    if (!ec && size + line.size() + 1 > max_bytes_) rotate();
    std::ofstream f(active_path(), std::ios::binary | std::ios::app);
    if (!f) return;  // log hatasi uygulamayi durdurmaz
    f << line << '\n';
}

void Logger::log(Level level, std::string_view component, std::string_view message) {
    if (level < min_) return;
    std::string line = format_utc(clock_.utc_now());
    line += ' ';
    line += to_string(level);
    line += ' ';
    line += redact(component, ctx_);
    line += ": ";
    line += redact(message, ctx_);
    std::lock_guard<std::mutex> lock(mu_);
    sink_.write_line(line);
}

}  // namespace ef::core::logging
