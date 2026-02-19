find_package(Catch2 CONFIG REQUIRED)

add_executable(shared_unit_tests
  shared/InventoryTests.cpp
  shared/MatchTests.cpp
  shared/MovementTests.cpp
  shared/ProtocolTests.cpp
  shared/RuntimeServerInfoTests.cpp
  shared/WorldTests.cpp
  shared/WeaponsTests.cpp
)

target_link_libraries(shared_unit_tests PRIVATE
  shared
  Catch2::Catch2WithMain
)

add_test(NAME shared.unit COMMAND shared_unit_tests)

add_executable(server_unit_tests
  server/AuthoritativeLoopTests.cpp
  server/BlockInteractionTests.cpp
  server/CombatIntegrationTests.cpp
  server/CombatSimulationTests.cpp
  server/InventoryLootSimulationTests.cpp
  server/MatchStatePayloadTests.cpp
  server/MatchLifecycleSimulationTests.cpp
  server/MovementSimulationTests.cpp
  server/RuntimeTelemetryTests.cpp
  server/ServerConfigValidationTests.cpp
  server/SessionManagerTests.cpp
  server/WorldReplicationTests.cpp
)

target_link_libraries(server_unit_tests PRIVATE
  server_runtime
  Catch2::Catch2WithMain
)

add_test(NAME server.unit COMMAND server_unit_tests)

add_executable(client_unit_tests
  client/ChunkSyncApplierTests.cpp
  client/PredictionReconcilerTests.cpp
  client/WeaponFireEmitterTests.cpp
)

target_link_libraries(client_unit_tests PRIVATE
  client_runtime
  server_runtime
  Catch2::Catch2WithMain
)

add_test(NAME client.unit COMMAND client_unit_tests)

add_test(
  NAME server.smoke
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-server-smoke.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.smoke PROPERTIES TIMEOUT 15)

add_test(
  NAME server.smoke.playable_fast
  COMMAND ${CMAKE_COMMAND} -E env
          DEVY_TEST_CONFIG_PATH=${CMAKE_SOURCE_DIR}/config/server_playable_fast_smoke.json
          ${CMAKE_SOURCE_DIR}/tests/scripts/assert-server-smoke.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.smoke.playable_fast PROPERTIES TIMEOUT 20)

add_test(
  NAME server.smoke.port_fallback
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-server-port-fallback-smoke.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.smoke.port_fallback PROPERTIES TIMEOUT 40)

add_test(
  NAME server.telemetry.alert_dry_run
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-diagnostics-dry-run.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.telemetry.alert_dry_run PROPERTIES TIMEOUT 20)

add_test(
  NAME server.telemetry.health_file_dry_run
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-health-file-dry-run.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.telemetry.health_file_dry_run PROPERTIES TIMEOUT 20)

add_test(
  NAME server.config.invalid_tick_rate
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-invalid-config.sh
          $<TARGET_FILE:devy_server>
          ${CMAKE_SOURCE_DIR}/config/server_invalid_tick_rate.json
)
set_tests_properties(server.config.invalid_tick_rate PROPERTIES TIMEOUT 15)
set_tests_properties(server.config.invalid_tick_rate PROPERTIES
  WILL_FAIL FALSE
  PASS_REGULAR_EXPRESSION ""
  FAIL_REGULAR_EXPRESSION ""
)

add_test(
  NAME server.config.invalid_loot_drop
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-invalid-config.sh
          $<TARGET_FILE:devy_server>
          ${CMAKE_SOURCE_DIR}/config/server_invalid_loot_drop.json
)
set_tests_properties(server.config.invalid_loot_drop PROPERTIES TIMEOUT 15)
set_tests_properties(server.config.invalid_loot_drop PROPERTIES
  WILL_FAIL FALSE
  PASS_REGULAR_EXPRESSION ""
  FAIL_REGULAR_EXPRESSION ""
)

add_test(
  NAME server.config.invalid_json
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-invalid-config.sh
          $<TARGET_FILE:devy_server>
          ${CMAKE_SOURCE_DIR}/config/server_invalid_json.json
          "Configuration load failed"
)
set_tests_properties(server.config.invalid_json PROPERTIES TIMEOUT 15)
set_tests_properties(server.config.invalid_json PROPERTIES
  WILL_FAIL FALSE
  PASS_REGULAR_EXPRESSION ""
  FAIL_REGULAR_EXPRESSION ""
)

add_test(
  NAME server.reliability.watchdog_restart
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-watchdog-restart.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.reliability.watchdog_restart PROPERTIES TIMEOUT 30)

add_test(
  NAME server.reliability.chaos_drill
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-chaos-drill.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.reliability.chaos_drill PROPERTIES TIMEOUT 30)

add_test(
  NAME server.reliability.restart_recovery
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-restart-recovery.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.reliability.restart_recovery PROPERTIES TIMEOUT 30)

add_test(
  NAME server.reliability.soak_short
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-reliability-soak.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.reliability.soak_short PROPERTIES TIMEOUT 120)

add_test(
  NAME server.release.install_smoke
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-install-package-smoke.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.install_smoke PROPERTIES TIMEOUT 120)

add_test(
  NAME server.release.protocol_upgrade_downgrade
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-protocol-compat.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.protocol_upgrade_downgrade PROPERTIES TIMEOUT 60)

add_test(
  NAME server.release.rollback_rehearsal
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-rollback-rehearsal.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.rollback_rehearsal PROPERTIES TIMEOUT 180)

add_test(
  NAME server.release.alpha_acceptance_pack
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-alpha-acceptance-pack.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.alpha_acceptance_pack PROPERTIES TIMEOUT 120)

add_test(
  NAME server.release.alpha_endurance_short
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-alpha-endurance-short.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.alpha_endurance_short PROPERTIES TIMEOUT 180)

add_test(
  NAME server.release.release_notes_generation
  COMMAND ${CMAKE_SOURCE_DIR}/tests/scripts/assert-release-notes.sh
          ${CMAKE_SOURCE_DIR}
)
set_tests_properties(server.release.release_notes_generation PROPERTIES TIMEOUT 15)
