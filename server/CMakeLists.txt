add_library(server_runtime
  src/AuthoritativeLoop.cpp
  src/BlockInteraction.cpp
  src/CombatEvents.cpp
  src/CombatSimulation.cpp
  src/InventoryLootSimulation.cpp
  src/MatchStatePayload.cpp
  src/MatchLifecycleSimulation.cpp
  src/MovementSimulation.cpp
  src/RuntimeTelemetry.cpp
  src/ServerConfigValidation.cpp
  src/SessionManager.cpp
  src/WorldReplication.cpp
)

target_include_directories(server_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(server_runtime PUBLIC
  shared
)

add_executable(devy_server
  src/main.cpp
)

add_executable(devy_load_client
  ../tools/devy_load_client.cpp
)

if(TARGET enet::enet)
  set(ENET_TARGET enet::enet)
elseif(TARGET unofficial::enet::enet)
  set(ENET_TARGET unofficial::enet::enet)
elseif(TARGET enet)
  set(ENET_TARGET enet)
elseif(TARGET ENet::ENet)
  set(ENET_TARGET ENet::ENet)
else()
  message(FATAL_ERROR "enet target not found. Ensure enet is installed via vcpkg.")
endif()

target_link_libraries(devy_server PRIVATE
  server_runtime
  ${ENET_TARGET}
)

target_link_libraries(devy_load_client PRIVATE
  shared
  ${ENET_TARGET}
)
