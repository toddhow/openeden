# Ragdoll building and the motor solver. Depends on the physics interface
# only (never on Jolt), as CLAUDE.md allows core to.
add_library(opas_motor
  src/Ragdoll.cpp
  src/MotorSolver.cpp
  src/Handover.cpp
)
add_library(opas::motor ALIAS opas_motor)
target_include_directories(opas_motor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_motor PUBLIC opas::math opas::rig opas::physics_interface)
opas_set_project_options(opas_motor)
