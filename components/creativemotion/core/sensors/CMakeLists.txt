# Balance sensors (M5a, docs/tasks/0017): centre of mass, foot contact, support
# area, capture point. Depends on the physics interface only, never Jolt.
add_library(opas_sensors
  src/Geometry.cpp
  src/BalanceSensor.cpp
  src/ImpactSensor.cpp
  src/GroundProbeSensor.cpp
  src/GrabProbeSensor.cpp
)
add_library(opas::sensors ALIAS opas_sensors)
target_include_directories(opas_sensors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_sensors PUBLIC opas::math opas::rig opas::arbiter opas::motor opas::physics_interface)
opas_set_project_options(opas_sensors)
