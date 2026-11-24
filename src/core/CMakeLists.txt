# Platformdan bagimsiz cekirdek: Win32 cagrisi icermez, birim testlenir.

# Metin tablolari derleme zamaninda gomulur (EXE yaninda dosya yok).
set(EF_STRINGS_TR ${PROJECT_SOURCE_DIR}/design/strings/tr-TR.json)
set(EF_STRINGS_EN ${PROJECT_SOURCE_DIR}/design/strings/en-US.json)
set(EF_STRINGS_CPP ${CMAKE_CURRENT_BINARY_DIR}/strings_embedded.cpp)
add_custom_command(
    OUTPUT ${EF_STRINGS_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EF_STRINGS_CPP} -DNAMESPACE=ef::core::i18n::embedded_data
            "-DENTRIES=tr_TR=${EF_STRINGS_TR}|en_US=${EF_STRINGS_EN}"
            -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EF_STRINGS_TR} ${EF_STRINGS_EN} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Metin tablolari gomuluyor"
    VERBATIM)

# Ayar katalogu (capability registry) gomulur.
set(EF_CATALOG_JSON ${PROJECT_SOURCE_DIR}/research/setting_catalog.json)
set(EF_CATALOG_CPP ${CMAKE_CURRENT_BINARY_DIR}/catalog_embedded.cpp)
add_custom_command(
    OUTPUT ${EF_CATALOG_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EF_CATALOG_CPP} -DNAMESPACE=ef::core::capabilities::embedded_data
            "-DENTRIES=setting_catalog=${EF_CATALOG_JSON}"
            -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EF_CATALOG_JSON} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Ayar katalogu gomuluyor"
    VERBATIM)

add_library(ef_core STATIC
    product_info.cpp
    serialization/json.cpp
    serialization/operation_codec.cpp
    logging/redact.cpp
    logging/log.cpp
    support/zip_writer.cpp
    support/bundle.cpp
    support/diag.cpp
    i18n/catalog.cpp
    i18n/turkish.cpp
    i18n/embedded.cpp
    inventory/smbios.cpp
    inventory/cpu_topology.cpp
    inventory/devices.cpp
    inventory/problem_devices.cpp
    inventory/power_security.cpp
    inventory/health.cpp
    inventory/disk_space.cpp
    capabilities/registry.cpp
    capabilities/device_verification.cpp
    roles.cpp
    ipc/protocol.cpp
    security/dispatch.cpp
    security/trust.cpp
    recovery/sha256.cpp
    recovery/journal.cpp
    recovery/engine.cpp
    recovery/reconcile.cpp
    recovery/conflicts.cpp
    recovery/service.cpp
    recovery/history_view.cpp
    recovery/restore_report.cpp
    legacy/user_data_migration.cpp
    legacy/memory_migration_fs.cpp
    legacy/legacy_install.cpp
    legacy/machine_journal_migration.cpp
    legacy/legacy_upgrade.cpp
    legacy/legacy_entry.cpp
    legacy/legacy_health.cpp
    recovery/uninstall.cpp
    vendors/dispatch.cpp
    adapters/nvidia/drs.cpp
    adapters/amd/amd3d.cpp
    adapters/intel/igcl3d.cpp
    adapters/windows/power.cpp
    adapters/windows/gaming_toggles.cpp
    adapters/windows/hags.cpp
    adapters/windows/tweak_catalog.cpp
    adapters/windows/tweak_adapter.cpp
    perf/tweak_plan.cpp
    perf/update_hold_plan.cpp
    adapters/windows/fast_startup.cpp
    adapters/windows/dns.cpp
    adapters/windows/power_subs.cpp
    adapters/windows/boot_options.cpp
    perf/boot_plan.cpp
    adapters/windows/appcompat_layers.cpp
    adapters/windows/gpu_preference.cpp
    perf/gpu_route_plan.cpp
    perf/feature_plan.cpp
    perf/defender_plan.cpp
    perf/game_priority_plan.cpp
    adapters/windows/ifeo_priority.cpp
    adapters/windows/appx_remove.cpp
    perf/debloat_plan.cpp
    adapters/windows/defender_exclusion.cpp
    adapters/windows/service_start_type.cpp
    broker/admin.cpp
    adapters/windows/display_process.cpp
    adapters/windows/background_process.cpp
    perf/background_candidates.cpp
    perf/background_plan.cpp
    perf/device_class.cpp
    perf/apply_progress.cpp
    perf/drift.cpp
    adapters/startup/startup.cpp
    adapters/startup/tasks_services.cpp
    adapters/startup/recommend.cpp
    adapters/windows/background.cpp
    guided/guided.cpp
    guided/bios_guide.cpp
    guided/undervolt_guide.cpp
    adapters/windows/update_guard.cpp
    adapters/windows/update_check.cpp
    perf/before_after.cpp
    perf/report_collector.cpp
    perf/rules.cpp
    perf/experiment.cpp
    perf/proof_experiment.cpp
    perf/regression.cpp
    perf/profile.cpp
    perf/system_check.cpp
    insights/history.cpp
    insights/comparison_policy.cpp
    insights/history_compare.cpp
    insights/update_compare.cpp
    insights/peer.cpp
    perf/controller.cpp
    perf/journal_backend.cpp
    perf/power_plan.cpp
    perf/vfx_plan.cpp
    perf/gaming_plan.cpp
    perf/admin_plan.cpp
    measure/view.cpp
    measure/run_marker.cpp
    measure/endurance_view.cpp
    measure/bench_cards.cpp
    measure/balance_view.cpp
    measure/balance_log.cpp
    measure/game_limit_view.cpp
    measure/bench_text.cpp
    measure/frames.cpp
    measure/frame_stats.cpp
    measure/polling_rate.cpp
    measure/telemetry.cpp
    measure/run_manifest.cpp
    measure/game_session.cpp
    measure/live_view.cpp
    diagnosis/components.cpp
    metrics/live.cpp
    metrics/process_engine.cpp
    metrics/sensor.cpp
    net/speed_test.cpp
    net/server_ping.cpp
    net/dns_bench.cpp
    games/consent.cpp
    games/exe_resolver.cpp
    games/adapter.cpp
    games/cs2.cpp
    games/config_risk.cpp
    games/launch_options.cpp
    games/process_risk.cpp
    games/upscaler.cpp
    games/vendor_guide.cpp
    games/quality.cpp
    games/watcher.cpp
    games/watch_state.cpp
    games/library_scan.cpp
    games/manual_games.cpp
    games/detection_coordinator.cpp
    games/protection_gate.cpp
    diagnostics/incident_engine.cpp
    diagnostics/health_test.cpp
    games/game_guide.cpp
    games/game_icon.cpp
    catalog/catalog.cpp
    catalog/envelope.cpp
    update/update_check.cpp
    update/staged_update.cpp
    setup/layout.cpp
    setup/arp.cpp
    setup/install_tx.cpp
    setup/acl_rules.cpp
    setup/setup_protocol.cpp
    setup/setup_flow.cpp
    service/client_check.cpp
    service/service_session.cpp
    service/channel_choice.cpp
    service/uac_copy.cpp
    recovery/restore_point.cpp
    maintenance/image_health.cpp
    maintenance/standby_purge.cpp
    readiness/readiness.cpp
    readiness/fix_plan.cpp
    readiness/load_test.cpp
    readiness/platform_security.cpp
    readiness/readiness_background.cpp
    readiness/readiness_compat.cpp
    readiness/readiness_cpu.cpp
    readiness/readiness_display.cpp
    readiness/readiness_gpu_pref.cpp
    readiness/readiness_graphics.cpp
    readiness/readiness_memory.cpp
    readiness/readiness_network.cpp
    readiness/readiness_mouse.cpp
    readiness/readiness_dns_bench.cpp
    readiness/readiness_storage.cpp
    readiness/readiness_system.cpp
    readiness/readiness_tweaks.cpp
    readiness/readiness_interrupts.cpp
    diagnosis/evidence.cpp
    diagnosis/compute.cpp
    diagnosis/combined_load.cpp
    diagnosis/drops.cpp
    diagnosis/graphics_api.cpp
    diagnosis/memory_storage.cpp
    diagnosis/platform.cpp
    diagnosis/report.cpp
    diagnosis/calibration.cpp
    diagnosis/calibration_io.cpp
    diagnosis/calibration_export.cpp
    diagnosis/balance.cpp
    diagnosis/game_limit.cpp
    diagnosis/mismatch.cpp
    settings/preferences.cpp
    settings/autostart.cpp
    drivers/driver_updates.cpp
    drivers/driver_install.cpp
    drivers/vendor_install.cpp
    brand/tray_variant.cpp
    cleanup/cleanup.cpp
    cleanup/session.cpp
    cleanup/admin_transfer.cpp
    startup/session.cpp
    ${EF_STRINGS_CPP}
    ${EF_CATALOG_CPP})
target_include_directories(ef_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Yalniz bu kaynak: kimlik degisince tum proje yeniden derlenmez.
set_source_files_properties(product_info.cpp PROPERTIES OBJECT_DEPENDS "${EF_BUILD_ID_HEADER}")
target_include_directories(ef_core PRIVATE ${EF_GENERATED_DIR})
add_dependencies(ef_core ef_build_id)
target_compile_definitions(ef_core PUBLIC
    EF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    EF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    EF_VERSION_PATCH=${PROJECT_VERSION_PATCH})
target_link_libraries(ef_core PUBLIC ef_contracts ef_options)
