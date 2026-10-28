# The arbiter (M5d, docs/tasks/0017): the character state machine and its
# blend fraction. No physics and no behaviors; core/behaviors uses it.
add_library(opas_arbiter
  src/Arbiter.cpp
  src/BodyPartMap.cpp
)
add_library(opas::arbiter ALIAS opas_arbiter)
target_include_directories(opas_arbiter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_arbiter PUBLIC opas::rig)
opas_set_project_options(opas_arbiter)
