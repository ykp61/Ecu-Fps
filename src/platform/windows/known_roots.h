#pragma once
// ADR-033: kurulum yerlesimi icin bilinen klasor kokleri ve kendi EXE'nin son yolu (salt okuma; yazma yok).

#include <filesystem>

#include "core/setup/layout.h"

namespace ef::platform {

// SHGetKnownFolderPath (ProgramFiles, ProgramData, CommonPrograms, LocalAppData); basarisiz olan kok bos kalir.
core::setup::KnownRoots known_roots();

// Kendi imaj dosyasinin GetFinalPathNameByHandleW yolu (baglanti/kisa ad cozulmus); olmazsa self_exe_path().
std::filesystem::path normalized_self_exe();

}  // namespace ef::platform
