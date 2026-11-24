#pragma once
// ADR-033 / spec 5: yardimci hizmetin istemci guven karari (saf). Platform (service/client_facts.cpp) gercekleri toplar;
// karar burada verilir ve birim testlenir.
//
// Guven modeli (degistirmeden once ADR-033'u oku):
//  - Istemci = kurulu EXE: surec goruntu yolu == kurulum yolu (yalniz yoneticiler yazabilir) VE goruntu dosyasinin
//    kimligi (birim seri + dosya dizini) == SU AN kurulum yolundaki dosyanin kimligi (sert bag / yeniden adlandirma
//    hilesi yok). Hizmetin kendi goruntusuyle karsilastirilmaz: guncellemeden sonra yeni EXE de baglanabilmeli.
//  - Etkilesimli, sifir olmayan oturum (belirtecte S-1-5-4 INTERACTIVE; oturum 0 hizmetleri reddedilir).
//  - Sahip karari K27 (2026-11-13): istemci hesabi BUILTIN\Administrators (S-1-5-32-544) uyesi olmali (UAC ile
//    suzulmus belirtecte deny-only giris de sayilir). Standart hesaplar hizmeti KULLANMAZ; UAC aracisina (kimlik
//    sorusu) duser. Boylece hizmet hicbir standart hesaba yeni yetki vermez.
//  - Kurulum klasoru ACL'i guvenli (install_dir_acl_ok); degilse hizmet her istegi reddeder.
//  - Surec tanitici baglanti boyunca acik tutulur (PID yeniden kullanimi yok); pipe ilk ornek + istemci tarafinda
//    sunucu PID'i == SCM PID'i (ad gaspi yok).
//  - Yalniz mevcut izin listesi (AdminExecutor::handle) yurutulur; ayni journal'lar ve oyun kapisi; uygulama ici
//    Evet/Hayir UI'da kalir. Kalan risk (ayni yonetici hesabindaki kodun kurulu uygulamaya enjeksiyonu; o
//    hesap zaten UAC onayi ile yukselebilir) ADR-033'te kabul edildi: izin listesini GENISLETME.

#include <cstdint>
#include <string>
#include <string_view>

namespace ef::core::service {

struct FileId {
    std::uint64_t volume_serial = 0, file_index = 0;
    friend bool operator==(const FileId&, const FileId&) = default;
};

struct ClientFacts {
    std::uint32_t pid = 0, session = 0;
    bool image_ok = false;
    std::wstring image;  // QueryFullProcessImageNameW
    bool file_ok = false;
    FileId file;  // goruntu dosyasinin kimligi
    bool token_ok = false;
    bool interactive = false;  // belirtecte etkin S-1-5-4
    bool admin_member = false;  // belirtecte S-1-5-32-544 (herhangi bir oznitelikle, deny-only dahil)
    std::string user_sid;      // belirtec kullanicisi (hedef kullanici)
};

struct ServiceSelf {
    std::wstring installed_exe;  // make_layout(known_roots()).exe
    bool installed_file_ok = false;
    FileId installed_file;  // baglanti aninda kurulum yolundaki dosya
    bool install_dir_acl_ok = false;
};

enum class ClientVerdict { Allow, InstallDirInsecure, QueryFailed, WrongImage, WrongFile, SessionZero, NotInteractive,
                           NotAdmin };
std::string_view to_string(ClientVerdict v) noexcept;

// Sira: InstallDirInsecure, QueryFailed (herhangi bir *_ok false, bos/kullanici olmayan SID, sifir dosya kimligi), WrongImage (same_path), WrongFile,
// SessionZero, NotInteractive, NotAdmin (K27). Yalniz hepsi gecerse Allow.
ClientVerdict check_client(const ClientFacts& c, const ServiceSelf& s);

}  // namespace ef::core::service
