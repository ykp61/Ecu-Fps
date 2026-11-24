#pragma once
// ADR-030: core::settings::RunKeyStore'un Windows uygulamasi — HKCU altindaki tek anahtarin REG_SZ degerleri. Anahtar
// yolu enjekte edilir (Windows testleri ayri bir test anahtari kullanir); birim testleri bunun yerine sahte depo
// kullanir ve kayit defterine hic dokunmaz.

#include <string>
#include <string_view>

#include "core/settings/autostart.h"

namespace ef::platform {

class WinRunKeyStore final : public core::settings::RunKeyStore {
public:
    explicit WinRunKeyStore(std::wstring key_path);
    // Uretim: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
    static WinRunKeyStore production();

    contracts::Result<std::optional<std::wstring>> read(std::wstring_view name) override;
    contracts::Status write(std::wstring_view name, std::wstring_view command) override;
    contracts::Status remove(std::wstring_view name) override;

private:
    std::wstring key_;
};

// Tani: kullanicinin okuyabilecegi %LOCALAPPDATA%/EcuFps/logs/autostart.log satiri (UTC zaman + PID + satir).
// Kota 32 KB, 2 dosya; destek paketine (logs/) girer. Yazilamazsa sessizce atlanir (davranisi etkilemez).
void append_autostart_log(std::string_view line);
// Saha teshisi: cikis asamalari (%LOCALAPPDATA%\EcuFps\logs\exit*.log, 32 KiB x 2). Kisisel veri yok.
void append_exit_log(std::string_view line);
// Uyumluluk B8 (bosluk 8): denge testi adim satirlari (%LOCALAPPDATA%\EcuFps\logs\balance*.log, 64 KiB x 2). Kisisel veri yok.
void append_balance_log(std::string_view line);
// Saha 2026-11-15: Performans geri alma sonucu ve geri alinamamis ayarlar (%LOCALAPPDATA%\EcuFps\logs\restore*.log,
// 64 KiB x 2, donen). Satirlar recovery::restore_log_line ile redakte; kisisel veri yok.
void append_restore_log(std::string_view line);

}  // namespace ef::platform
