#pragma once
// Asgari yerel log: seviye, bilesen, redaksiyon zorunlu, kota/rotasyonlu dosya.
// Log EXE yanina yazilmaz; dizin cagiran tarafca verilir (%LOCALAPPDATA%\EcuFps\logs).

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/logging/redact.h"
#include "core/time/time.h"

namespace ef::core::logging {

enum class Level { Debug, Info, Warn, Error };
std::string_view to_string(Level l) noexcept;

// Unix ms -> "2026-11-04T00:27:21.123Z"
std::string format_utc(UtcTime t);

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write_line(const std::string& line) = 0;
};

// Testler icin bellekte toplayan sink.
class MemorySink final : public Sink {
public:
    void write_line(const std::string& line) override { lines.push_back(line); }
    std::vector<std::string> lines;
};

// Boyut kotali dosya: aktif dosya max_bytes'i asarsa .1, .2 ... kaydirilir; en fazla keep dosya.
class RotatingFileSink final : public Sink {
public:
    RotatingFileSink(std::filesystem::path dir, std::string base_name, std::uintmax_t max_bytes, int keep);
    void write_line(const std::string& line) override;
    std::filesystem::path active_path() const { return dir_ / (base_ + ".log"); }

private:
    void rotate();
    std::filesystem::path dir_;
    std::string base_;
    std::uintmax_t max_bytes_;
    int keep_;
};

class Logger {
public:
    Logger(Sink& sink, Clock& clock, RedactionContext ctx, Level min_level = Level::Info)
        : sink_(sink), clock_(clock), ctx_(std::move(ctx)), min_(min_level) {}

    void log(Level level, std::string_view component, std::string_view message);
    void info(std::string_view c, std::string_view m) { log(Level::Info, c, m); }
    void warn(std::string_view c, std::string_view m) { log(Level::Warn, c, m); }
    void error(std::string_view c, std::string_view m) { log(Level::Error, c, m); }

private:
    Sink& sink_;
    Clock& clock_;
    RedactionContext ctx_;
    Level min_;
    std::mutex mu_;
};

}  // namespace ef::core::logging
