add_library(client_runtime
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

target_link_libraries(devy_client PRIVATE
  engine
  client_runtime
)
