#pragma once
// ADR-040 m.4 / R-B4 adim 0: dinamik tick BCD ogesi (disabledynamictick, 0x260000a5, winload Boolean) icin platform yolu.
//
// Yazma/silme: yalniz %SystemRoot%\System32\bcdedit.exe, tam yol GetSystemDirectoryW ile kurulur, kabuk YOK
// (CreateProcessW, lpApplicationName dolu), komut satiri derlenmis SABIT dizelerden biri (IPC'den hicbir parca gelmez):
//   /set {current} disabledynamictick yes   (deger "off")
//   /set {current} disabledynamictick no    (deger "on", yalniz geri alma)
//   /deletevalue {current} disabledynamictick   (oge yoktu -> sil)
// Basari yalniz cikis kodu 0 ile anlasilir (ciktisi yerel dile bagli oldugu icin AYRISTIRILMAZ).
//
// Okuma: DOGRULANMADI -> Unsupported (fail-closed). bcdedit /enum ciktisi yerellestirilmis metindir (ayristirilmaz);
// WMI BCD saglayicisi (Root\WMI BcdStore/BcdObject.GetElement) {current} nesnesinin GUID'ini ister ve bu GUID resmi
// belgede bulunamadi (uydurulmaz). Okuma olmadan capture basarisiz olur: hicbir yazma baslamaz, oge plana girmez.
// Bu PC'de calistirilmadi; gercek tur izole VM ister (R-B4 BLOCKER).

#include "core/adapters/windows/boot_options.h"

namespace ef::platform::adapters {

class WinBootOptionApi final : public core::adapters::windows::BootOptionApi {
public:
    contracts::Result<std::optional<std::string>> read(core::adapters::windows::BootElement e) override;
    contracts::Status write(core::adapters::windows::BootElement e, std::string_view value) override;
    contracts::Status remove(core::adapters::windows::BootElement e) override;
};

}  // namespace ef::platform::adapters
