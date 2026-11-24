#pragma once
// ADR-033 / spec 7: yardimci hizmetin guncelleme deposu (yalniz hizmet icinde, LocalSystem). Klasor
// ProgramData\EcuFps\updates kurulumda SY+BA-yalniz DACL ile olusturulur (WinSetupOps); burada ACL degistirilmez.
//  - FileStageSink: <version>.part paylasimsiz yazilir, FlushFileBuffers, ozet diskten yeniden okunarak hesaplanir.
//  - FileUpdateHost: state.json atomik (tmp + FlushFileBuffers + MoveFileExW REPLACE_EXISTING|WRITE_THROUGH);
//    .part -> exe.new kopyasi flush'lanir; ozet yeniden okunur. Ag kullanilmaz.
//  - win_swap_fs: ayni birimde MoveFileExW WRITE_THROUGH (calisan EXE'nin adi degistirilebilir).
// Bu oturumda calistirilmaz (gercek hizmet + ProgramData yazimi); saf mantik core/update/staged_update.h'de testlidir.

#include <filesystem>
#include <string>

#include "core/catalog/envelope.h"
#include "core/update/staged_update.h"

namespace ef::platform::service {

class FileStageSink final : public core::update::StageSink {
public:
    explicit FileStageSink(std::filesystem::path dir) : dir_(std::move(dir)) {}
    ~FileStageSink() override;
    bool open_part(std::string_view version) override;
    bool write(std::string_view bytes) override;
    bool finish() override;
    std::string sha256_hex_readback() override;
    void discard() override;
    const std::filesystem::path& part_path() const noexcept { return path_; }

private:
    void close();
    std::filesystem::path dir_, path_;
    void* h_ = nullptr;
};

class FileUpdateHost final : public core::update::UpdateHost {
public:
    FileUpdateHost(std::filesystem::path state_file, const FileStageSink& sink) : state_(std::move(state_file)), sink_(sink) {}
    bool load_state(core::update::StagedUpdateState& out) override;
    bool save_state(const core::update::StagedUpdateState& s) override;
    bool stage_part(const std::string& staged) override;
    bool file_digest(const std::string& path, std::uint64_t& size, std::string& sha256_hex) override;
    void remove_part() override;
    std::int64_t now_ms() override;

private:
    std::filesystem::path state_;
    const FileStageSink& sink_;
};

core::catalog::SwapFs win_swap_fs();

}  // namespace ef::platform::service
