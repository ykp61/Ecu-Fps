#include "core/guided/guided.h"

namespace ef::core::guided {

std::string_view uri(Action a) noexcept {
    switch (a) {
        case Action::GameModeSettings: return "ms-settings:gaming-gamemode";
        case Action::GraphicsSettings: return "ms-settings:display-advancedgraphics";
        case Action::CaptureSettings: return "ms-settings:gaming-gamedvr";
        case Action::WindowsUpdate: return "ms-settings:windowsupdate";
        case Action::StartupApps: return "ms-settings:startupapps";
        case Action::PowerSettings: return "ms-settings:powersleep";
        case Action::DisplaySettings: return "ms-settings:display";
        case Action::StorageSettings: return "ms-settings:storagesense";
    }
    return "";
}

std::string_view text_key(Action a) noexcept {
    switch (a) {
        case Action::GameModeSettings: return "guided.game_mode";
        case Action::GraphicsSettings: return "guided.graphics";
        case Action::CaptureSettings: return "guided.capture";
        case Action::WindowsUpdate: return "guided.windows_update";
        case Action::StartupApps: return "guided.startup_apps";
        case Action::PowerSettings: return "guided.power";
        case Action::DisplaySettings: return "guided.display";
        case Action::StorageSettings: return "guided.storage";
    }
    return "";
}

bool is_allowed_uri(std::string_view u) noexcept {
    for (auto a : {Action::GameModeSettings, Action::GraphicsSettings, Action::CaptureSettings, Action::WindowsUpdate,
                   Action::StartupApps, Action::PowerSettings, Action::DisplaySettings, Action::StorageSettings})
        if (uri(a) == u) return true;
    return false;
}

}  // namespace ef::core::guided
