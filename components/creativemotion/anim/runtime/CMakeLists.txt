# The animation runtime: players, blending, masks and inertialized transitions
# (task 0020, C3c).
add_library(opas_anim_runtime
  src/PoseBlend.cpp
  src/Inertialization.cpp
  src/ClipPlayer.cpp
  src/AnimationPlayer.cpp
  src/SwingTwist.cpp
  src/HelperSolver.cpp
)
add_library(opas::anim_runtime ALIAS opas_anim_runtime)
target_include_directories(opas_anim_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_anim_runtime PUBLIC opas::anim_clip opas::math opas::rig)
opas_set_project_options(opas_anim_runtime)
