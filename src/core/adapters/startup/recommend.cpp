#include "core/adapters/startup/recommend.h"

#include <cctype>
#include <initializer_list>

namespace ef::core::adapters::startup {

namespace {
std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
bool any_of(const std::string& hay, std::initializer_list<std::string_view> needles) {
    for (auto n : needles)
        if (hay.find(n) != std::string::npos) return true;
    return false;
}

// Kural sirasi bilinclidir: once koruma kurallari (Keep), sonra kullanici secimi (NoAdvice),
// en son oneriler (RecommendDisable). Bir girdi hem koruma hem oneri desenine uyarsa korunur.
//
// Kanit/gerekce:
//  - Windows bileseni ve Windows Guvenlik: Microsoft "Configure Startup applications" (S66) yalniz
//    kullanicinin bildigi uygulamalari kapatmasini onerir; sistem bilesenleri urun kapsami disidir.
//  - Guvenlik/anti-cheat: AGENTS.md degismezi (guvenlik/anti-cheat kapatma yok). Anti-cheat tepsi
//    istemcisi (ornek: Vanguard vgtray) kapali iken oyun acilmayabilir.
//  - Surucu yardimcilari ve kontrol panelleri: ses efekti, GPU profili, fare/klavye makrosu, fan/RGB
//    profili oturum acilisinda yuklenir; kapatilirsa oyun ici davranis degisebilir.
//  - Erisilebilirlik: kullanicinin bilgisayari kullanabilmesi icin gerekebilir.
//  - Bulut esitleme: kapatmak dosya esitlemeyi durdurur; veri kaybi/cakisma riski kullanicinin karari.
//  - Oyun platformlari: oyun guncellemesi, bulut kaydi ve bazi oyunlarin baslatilmasi platforma bagli.
//  - Guncelleyiciler: uygulama acildiginda kendini gunceller; oturum acilisinda calismasi oyun icin
//    gerekmez (ornek: Adobe ARM, Java jusched, Google/Opera otomatik guncelleme).
//  - Istege bagli uygulamalar: muzik, sohbet, toplanti, indirme istemcileri oyun icin gerekmez;
//    kullanici ihtiyac duydugunda elle acabilir. Kapatmak uygulamayi kaldirmaz/silmez (WIN-03).
//  - Taninmayan her sey: NoAdvice (tahmin yok).
Recommendation classify(const StartupItem& it) {
    const std::string name = lower(it.name), pub = lower(it.publisher);
    // Startup klasoru kisayolunun yolu "\Microsoft\Windows\Start Menu\" icerir; bu, kisayolun hedefi
    // hakkinda bilgi vermez ve Microsoft/Windows kurali icin kanit sayilmaz.
    std::string cmd = lower(it.command);
    for (std::string_view sm : {std::string_view("\\microsoft\\windows\\start menu\\")})
        for (auto p = cmd.find(sm); p != std::string::npos; p = cmd.find(sm)) cmd.replace(p, sm.size(), "\\startmenu\\");
    const std::string all = name + " | " + cmd + " | " + pub;

    if (any_of(all, {"securityhealth", "windows defender", "windows security", "msmpeng", "\\avast", "\\avg\\", "avgui", "kaspersky",
                     "avp.exe", "avpui", "\\eset\\", "egui.exe", "norton", "mcafee", "bitdefender", "malwarebytes", "mbamtray", "sophos",
                     "trend micro", "f-secure", "webroot", "avira", "\\panda security\\", "comodo", "zonealarm", "g data"}))
        return {Advice::Keep, AdviceRule::Security, "startup.advice.security"};
    if (any_of(all, {"vanguard", "vgtray", "faceit", "battleye", "easyanticheat", "anticheat", "anti-cheat", "esea"}))
        return {Advice::Keep, AdviceRule::AntiCheat, "startup.advice.anticheat"};
    if (any_of(all, {"narrator", "magnify.exe", "osk.exe", "atbroker", "nvda", "\\jaws", "zoomtext", "accessibility"}))
        return {Advice::Keep, AdviceRule::Accessibility, "startup.advice.accessibility"};
    if (any_of(all, {"\\nvidia", "nvcontainer", "nvbackend", "\\amd\\", "radeon", "amdrs", "cncmd", "\\intel\\", "igfx", "realtek", "rtkaud",
                     "rthdvcpl", "nahimic", "waves maxx", "maxxaudio", "dolby", "sonic studio", "audio", "\\asus\\", "armoury", "\\msi\\",
                     "msi center", "dragon center", "\\gigabyte\\", "rgbfusion", "easytune", "logitech", "lghub", "razer", "synapse",
                     "corsair", "icue", "steelseries", "\\wacom", "synaptics", "\\elan", "hotkey", "nzxt", "hyperx", "tobii", "touchpad",
                     "elgato", "stream deck"}))
        return {Advice::Keep, AdviceRule::HardwareControl, "startup.advice.hardware"};
    // Windows klasoru altindaki ikili; rundll32 gibi bir Windows ikilisi ucuncu taraf DLL'i aciyorsa
    // (Program Files/AppData/ProgramData) Windows bileseni sayilmaz, sonraki kurallara duser.
    const bool in_windows = any_of(cmd, {":\\windows\\", "%windir%", "%systemroot%"});
    const bool third_party_arg = any_of(cmd, {"\\program files", "\\appdata\\", "\\programdata\\", "\\users\\"});
    if (in_windows && !third_party_arg) return {Advice::Keep, AdviceRule::WindowsComponent, "startup.advice.windows"};

    if (any_of(all, {"onedrive", "dropbox", "googledrivefs", "google drive", "icloud", "box drive", "megasync", "pcloud", "nextcloud",
                     "owncloud", "syncthing", "sync.com", "yandex.disk", "creative cloud"}))
        return {Advice::NoAdvice, AdviceRule::CloudSync, "startup.advice.cloud_sync"};
    if (any_of(all, {"steam", "epicgameslauncher", "epic games", "battle.net", "eadesktop", "ea desktop", "\\origin\\", "ubisoft", "upc.exe",
                     "uplay", "gog galaxy", "galaxyclient", "riotclient", "riot client", "rockstar", "xbox", "playnite", "amazon games", "itch"}))
        return {Advice::NoAdvice, AdviceRule::GameLauncher, "startup.advice.launcher"};
    if (any_of(cmd, {"\\microsoft\\", "\\microsoft "}) || pub.find("microsoft") != std::string::npos || name.rfind("microsoft", 0) == 0)
        return {Advice::NoAdvice, AdviceRule::MicrosoftApp, "startup.advice.microsoft_app"};

    if (any_of(all, {"updater", "update helper", "updatehelper", "update checker", "updatechecker", "autoupdate", "auto update", "auto-update",
                     "update scheduler", "update notifier", "softwareupdate", "jusched", "adobearm", "acrobat update", "googleupdate",
                     "update service"}))
        return {Advice::RecommendDisable, AdviceRule::Updater, "startup.advice.updater"};
    if (any_of(all, {"spotify", "discord", "skype", "zoom", "slack", "whatsapp", "telegram", "viber", "ccleaner", "utorrent", "qbittorrent",
                     "bittorrent", "ituneshelper", "itunes helper", "opera browser assistant", "lightshot", "greenshot", "sharex", "notion"}))
        return {Advice::RecommendDisable, AdviceRule::OptionalApp, "startup.advice.optional_app"};
    return {Advice::NoAdvice, AdviceRule::Unknown, "startup.advice.unknown"};
}
}  // namespace

std::string_view to_string(Advice a) noexcept {
    switch (a) {
        case Advice::RecommendDisable: return "recommend_disable";
        case Advice::Keep: return "keep";
        case Advice::NoAdvice: return "no_advice";
    }
    return "?";
}

std::string_view to_string(AdviceRule r) noexcept {
    switch (r) {
        case AdviceRule::WindowsComponent: return "windows_component";
        case AdviceRule::Security: return "security";
        case AdviceRule::AntiCheat: return "anticheat";
        case AdviceRule::HardwareControl: return "hardware_control";
        case AdviceRule::Accessibility: return "accessibility";
        case AdviceRule::CloudSync: return "cloud_sync";
        case AdviceRule::GameLauncher: return "game_launcher";
        case AdviceRule::MicrosoftApp: return "microsoft_app";
        case AdviceRule::Updater: return "updater";
        case AdviceRule::OptionalApp: return "optional_app";
        case AdviceRule::AlreadyDisabled: return "already_disabled";
        case AdviceRule::Unknown: return "unknown";
    }
    return "?";
}

StartupItem from_entry(const Entry& e, std::string publisher) {
    StartupItem it;
    it.name = e.name;
    it.command = e.command;
    it.publisher = std::move(publisher);
    it.enabled = e.approval == Approval::Enabled;
    it.location = e.location;
    return it;
}

Recommendation recommend(const StartupItem& item) {
    auto r = classify(item);
    // Zaten kapali girdi icin "kapat" denmez; koruma (Keep) bilgisi ise korunur.
    if (r.advice == Advice::RecommendDisable && !item.enabled) return {Advice::NoAdvice, AdviceRule::AlreadyDisabled, "startup.advice.already_disabled"};
    return r;
}

std::vector<Recommendation> recommend_all(const std::vector<StartupItem>& items) {
    std::vector<Recommendation> out;
    out.reserve(items.size());
    for (const auto& i : items) out.push_back(recommend(i));
    return out;
}

}  // namespace ef::core::adapters::startup
