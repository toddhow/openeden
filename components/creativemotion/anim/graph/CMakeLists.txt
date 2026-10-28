# The animation graph: format, merging, validation and the per-character
# instance (task 0020, C3d).
add_library(opas_anim_graph
  src/AnimGraph.cpp
  src/GraphInstance.cpp
)
add_library(opas::anim_graph ALIAS opas_anim_graph)
target_include_directories(opas_anim_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_anim_graph
  PUBLIC opas::anim_runtime opas::anim_clip opas::format
  PRIVATE opas::format_json
)
opas_set_project_options(opas_anim_graph)
