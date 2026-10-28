# Style layer (task 0023, milestone C5): personality, idle life, additive layers.
add_library(opas_anim_style
  src/StyleFile.cpp
  src/Noise.cpp
  src/IdleGenerator.cpp
  src/StyleModifier.cpp
  src/ContextController.cpp
)
add_library(opas::anim_style ALIAS opas_anim_style)
target_include_directories(opas_anim_style PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_anim_style
  PUBLIC opas::math opas::rig opas::format
  PRIVATE opas::format_json
)
opas_set_project_options(opas_anim_style)
