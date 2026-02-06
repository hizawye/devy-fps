cmake_minimum_required(VERSION 3.20)

project(devy_fps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DEVY_ENABLE_IMGUI "Enable Dear ImGui debug UI" ON)

# Dependencies (vcpkg manifest mode recommended)
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

find_package(enet CONFIG QUIET)
if(NOT enet_FOUND)
  find_package(ENet CONFIG REQUIRED)
endif()

if(DEVY_ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)
endif()

add_subdirectory(shared)
add_subdirectory(engine)
add_subdirectory(client)
add_subdirectory(server)
