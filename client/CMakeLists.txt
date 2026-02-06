add_executable(devy_client
  src/main.cpp
)

target_link_libraries(devy_client PRIVATE
  engine
  shared
)
