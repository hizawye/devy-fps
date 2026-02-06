add_library(engine
  src/Application.cpp
  src/Input.cpp
  src/Renderer.cpp
  src/Physics.cpp
  src/Shader.cpp
  src/Camera.cpp
  src/Mesh.cpp
  src/Frustum.cpp
  src/PlayerController.cpp
)

target_include_directories(engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(BULLET_LIBS)
if(TARGET Bullet::BulletDynamics)
  list(APPEND BULLET_LIBS Bullet::BulletDynamics Bullet::BulletCollision Bullet::LinearMath)
elseif(TARGET BulletDynamics)
  list(APPEND BULLET_LIBS BulletDynamics BulletCollision LinearMath)
endif()

target_link_libraries(engine PUBLIC
  SDL2::SDL2
  OpenGL::GL
  glad::glad
  ${BULLET_LIBS}
  glm::glm
  shared
)

if(DEVY_ENABLE_IMGUI)
  target_link_libraries(engine PUBLIC imgui::imgui)
  target_compile_definitions(engine PUBLIC DEVY_ENABLE_IMGUI)
endif()
