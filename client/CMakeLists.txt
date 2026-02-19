add_library(client_runtime
  src/AuthoritativeEventFeed.cpp
  src/AuthoritativeHudModel.cpp
  src/ChunkSyncApplier.cpp
  src/PredictionReconciler.cpp
  src/WeaponFireEmitter.cpp
)

target_include_directories(client_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(client_runtime PUBLIC
  shared
)

add_executable(devy_client
  src/main.cpp
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

target_link_libraries(devy_client PRIVATE
  engine
  client_runtime
  ${ENET_TARGET}
)
