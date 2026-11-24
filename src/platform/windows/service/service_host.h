#pragma once
// ADR-033 / spec 5: --service rolu (ayni EXE, LocalSystem yardimci hizmet). SCM tarafindan baslatilmadiysa
// (StartServiceCtrlDispatcherW basarisiz) kExitBadArgs doner; baska hicbir sey yapmaz.
//
// Guven ozeti (ayrinti core/service/client_check.h):
//  - Istemci = kurulum yolundaki EXE (yol + o yoldaki dosyanin kimligi) + etkilesimli, sifir olmayan oturum + guvenli
//    kurulum klasoru ACL'i. Surec tanitici baglanti boyunca acik (PID yeniden kullanimi yok).
//  - Pipe ilk ornek (ad gaspinda hizmet durur, istemci UAC broker'a doner); istemci sunucu PID'ini SCM PID'iyle eslestirir.
//  - Yalniz mevcut izin listesi (AdminExecutor::handle) yurutulur; ayni makine journal'lari, makine kilidi ve oyun kapisi.
//    Uygulama ici Evet/Hayir UI'da kalir. Kalan risk ADR-033'te kabul edildi; izin listesini GENISLETME.
//  - Baglantilar sirayla; bos 60 sn -> kapanir; baglanti basina en fazla 64 istek.

namespace ef::platform::service {

int run_service();

}  // namespace ef::platform::service
