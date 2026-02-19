add_library(shared
  src/Config.cpp
  src/Log.cpp
  src/RuntimeServerInfo.cpp
  src/net/Protocol.cpp
  src/voxel/Chunk.cpp
  src/voxel/World.cpp
  src/game/Weapons.cpp
  src/game/Treasure.cpp
  src/game/Inventory.cpp
  src/game/Match.cpp
  src/game/Movement.cpp
)

target_include_directories(shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(shared PUBLIC
  nlohmann_json::nlohmann_json
  glm::glm
)

target_compile_definitions(shared PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
