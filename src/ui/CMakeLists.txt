# UI-01: yerel arayuz (Direct2D/DirectWrite). Tasarim token'lari derleme zamaninda gomulur.
set(EF_TOKENS_JSON ${PROJECT_SOURCE_DIR}/design/tokens.json)
set(EF_TOKENS_CPP ${CMAKE_CURRENT_BINARY_DIR}/tokens_embedded.cpp)
add_custom_command(
    OUTPUT ${EF_TOKENS_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EF_TOKENS_CPP} -DNAMESPACE=ef::ui::embedded_data
            "-DENTRIES=tokens=${EF_TOKENS_JSON}"
            -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EF_TOKENS_JSON} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Tasarim tokenlari gomuluyor"
    VERBATIM)

# BRAND-01: onayli marka (2.0.0 "Boost Line" EF) SVG'leri gomulur (EXE yaninda dosya yok; yalniz gereken 8 SVG).
set(EF_BRAND_DIR ${PROJECT_SOURCE_DIR}/assets/branding/ecu-fps)
set(EF_BRAND_CPP ${CMAKE_CURRENT_BINARY_DIR}/brand_embedded.cpp)
set(EF_BRAND_LOGO ${EF_BRAND_DIR}/logo/svg)
set(EF_BRAND_OPTICAL ${EF_BRAND_DIR}/tray/svg/optical)
set(EF_BRAND_SVGS
    ${EF_BRAND_LOGO}/ecu-fps-lockup-dark.svg ${EF_BRAND_LOGO}/ecu-fps-lockup-light.svg ${EF_BRAND_LOGO}/ecu-fps-lockup-white.svg
    ${EF_BRAND_LOGO}/ef-mark.svg ${EF_BRAND_LOGO}/ef-mark-currentcolor.svg ${EF_BRAND_OPTICAL}/tray-16-active.svg
    ${EF_BRAND_OPTICAL}/tray-20-active.svg ${EF_BRAND_OPTICAL}/tray-24-active.svg)
add_custom_command(
    OUTPUT ${EF_BRAND_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EF_BRAND_CPP} -DNAMESPACE=ef::ui::brand::embedded_data
            "-DENTRIES=lockup_dark=${EF_BRAND_LOGO}/ecu-fps-lockup-dark.svg|lockup_light=${EF_BRAND_LOGO}/ecu-fps-lockup-light.svg|lockup_white=${EF_BRAND_LOGO}/ecu-fps-lockup-white.svg|mark_color=${EF_BRAND_LOGO}/ef-mark.svg|mark_current=${EF_BRAND_LOGO}/ef-mark-currentcolor.svg|mark_optical16=${EF_BRAND_OPTICAL}/tray-16-active.svg|mark_optical20=${EF_BRAND_OPTICAL}/tray-20-active.svg|mark_optical24=${EF_BRAND_OPTICAL}/tray-24-active.svg"
            -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EF_BRAND_SVGS} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Marka SVG'leri gomuluyor"
    VERBATIM)

# BOOST Panel: JetBrains Mono 2.304 (SIL OFL 1.1; third_party/fonts/jetbrains-mono) altkumesi EXE'ye gomulur ve
# DirectWrite bellek ici font koleksiyonu olarak yuklenir (EXE yaninda font dosyasi yok).
set(EF_FONT_DIR ${PROJECT_SOURCE_DIR}/third_party/fonts/jetbrains-mono)
set(EF_FONT_CPP ${CMAKE_CURRENT_BINARY_DIR}/font_embedded.cpp)
add_custom_command(
    OUTPUT ${EF_FONT_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EF_FONT_CPP} -DNAMESPACE=ef::ui::font_data
            "-DENTRIES=mono_medium=${EF_FONT_DIR}/JetBrainsMono-Medium-subset.ttf|mono_semibold=${EF_FONT_DIR}/JetBrainsMono-SemiBold-subset.ttf"
            -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EF_FONT_DIR}/JetBrainsMono-Medium-subset.ttf ${EF_FONT_DIR}/JetBrainsMono-SemiBold-subset.ttf ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "JetBrains Mono gomuluyor"
    VERBATIM)

add_library(ef_ui STATIC
    theme/theme.cpp
    theme/system_palette.cpp
    theme/ramp.cpp
    accessibility/uia.cpp
    lifecycle/activity.cpp
    layout/layout.cpp
    motion/motion.cpp
    render/graphics.cpp
    render/icons.cpp
    shell/screen.cpp
    shell/hit_ids.cpp
    shell/modal.cpp
    shell/panel_text.cpp
    shell/system_page.cpp
    shell/perf_card.cpp
    shell/measure_page.cpp
    shell/measure_page_model.cpp
    shell/measure_result_card.cpp
    shell/measure_cards.cpp
    shell/measure_history_card.cpp
    shell/settings_page.cpp
    shell/updates_page.cpp
    shell/live_card.cpp
    shell/speed_card.cpp
    shell/update_screen.cpp
    shell/game_banner.cpp
    shell/games_page.cpp
    shell/history_page.cpp
    shell/readiness_page.cpp
    shell/cleanup_page.cpp
    shell/startup_page.cpp
    shell/before_after_card.cpp
    shell/last_test_card.cpp
    shell/balance_card.cpp
    shell/live_test_view.cpp
    shell/diagnosis_card.cpp
    shell/check_card.cpp
    tray/tray.cpp
    shell/main_window.cpp
    setup/setup_view.cpp
    brand/svg_path.cpp
    brand/brand.cpp
    ${EF_BRAND_CPP}
    ${EF_FONT_CPP}
    ${EF_TOKENS_CPP})
target_include_directories(ef_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ef_ui PUBLIC ef_core ef_contracts PRIVATE ef_options d2d1 dwrite windowscodecs dwmapi uiautomationcore oleaut32 comctl32 shell32 advapi32)

# Onay pencereleri (spec 2026-11-12 E): uygulama ici kip penceresi (MainWindow::run_modal) uzerinden sorulur; Windows
# TaskDialog / MessageBox kullanilmaz. Ayri kutuphane: yalniz uygulama ve onay probu baglar.
add_library(ef_ui_dialogs STATIC consent/consent_dialog.cpp consent/fix_dialogs.cpp)
target_link_libraries(ef_ui_dialogs PUBLIC ef_ui PRIVATE ef_options)
