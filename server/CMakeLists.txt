add_executable(devy_server
  src/main.cpp
)

if(TARGET enet::enet)
  set(ENET_TARGET enet::enet)
elseif(TARGET enet)
  set(ENET_TARGET enet)
elseif(TARGET ENet::ENet)
  set(ENET_TARGET ENet::ENet)
else()
  message(FATAL_ERROR "enet target not found. Ensure enet is installed via vcpkg.")
endif()

target_link_libraries(devy_server PRIVATE
  shared
  ${ENET_TARGET}
)
