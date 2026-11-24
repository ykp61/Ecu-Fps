#pragma once
// Windows testleri icin dosya tabanli ayar adaptoru: hedef = <dir>\<target>.val (yok = deger yok).
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "core/recovery/engine.h"

namespace ef_test {
namespace fs = std::filesystem;
using ef::core::recovery::Snapshot;
namespace contracts = ef::contracts;
namespace core = ef::core;

class FileAdapter final : public core::recovery::SettingAdapter {
public:
    explicit FileAdapter(fs::path d) : dir(std::move(d)) { fs::create_directories(dir); }
    fs::path file(const contracts::SettingOperation& op) const { return dir / (op.target + ".val"); }
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override {
        std::ifstream in(file(op), std::ios::binary);
        if (!in) return std::optional<std::string>{};
        std::stringstream ss;
        ss << in.rdbuf();
        return std::optional<std::string>{ss.str()};
    }
    contracts::Result<Snapshot> capture(const contracts::SettingOperation& op) override {
        Snapshot s;
        auto v = read_back(op).value();
        s.existed = v.has_value();
        if (v) s.old_value = *v;
        s.raw_type = "file";
        return s;
    }
    void write(const contracts::SettingOperation& op, const std::string& v) {
        std::ofstream(file(op), std::ios::binary | std::ios::trunc) << v;
    }
    contracts::Status apply(const contracts::SettingOperation& op) override {
        write(op, op.new_value);
        return contracts::ok_status();
    }
    contracts::Status restore(const contracts::SettingOperation& op, const Snapshot& s) override {
        if (s.existed) write(op, s.old_value);
        else fs::remove(file(op));
        return contracts::ok_status();
    }
    fs::path dir;
};

}  // namespace ef_test
