# Jolt backend. This is the only target allowed to link Jolt or include its
# headers; Jolt is linked PRIVATE so it does not leak to anything that links us.
add_library(opas_physics_jolt
  src/BackendInfo.cpp
  src/JoltRuntime.cpp
  src/JoltWorld.cpp
)
add_library(opas::physics_jolt ALIAS opas_physics_jolt)

target_include_directories(opas_physics_jolt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(opas_physics_jolt
  PUBLIC opas::physics_interface
  PRIVATE Jolt
)
opas_set_project_options(opas_physics_jolt)
