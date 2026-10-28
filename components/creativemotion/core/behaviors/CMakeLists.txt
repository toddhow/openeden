# Behaviors (M5, docs/tasks/0017): balance now; stepping and hit reaction in
# later stages. Depends on the sensors, the motor solver and the physics
# interface, never Jolt.
add_library(opas_behaviors
  src/Balance.cpp
  src/BalanceDriver.cpp
  src/Stepping.cpp
  src/HitReaction.cpp
  src/StumbleController.cpp
  src/CatchFallController.cpp
  src/GrabController.cpp
  src/WritheInjury.cpp
  src/GetUpController.cpp
)
add_library(opas::behaviors ALIAS opas_behaviors)
target_include_directories(opas_behaviors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_behaviors PUBLIC opas::math opas::rig opas::motor opas::sensors opas::arbiter opas::ik opas::physics_interface)
opas_set_project_options(opas_behaviors)
