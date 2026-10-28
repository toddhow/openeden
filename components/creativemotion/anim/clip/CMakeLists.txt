# The internal clip model, sampling and skeleton profiles (task 0020, C3a);
# clip sidecars (C3b).
add_library(opas_anim_clip
  src/Clip.cpp
  src/SkeletonProfile.cpp
  src/ClipBinding.cpp
  src/ClipSidecar.cpp
  src/Interpolate.cpp
)
add_library(opas::anim_clip ALIAS opas_anim_clip)
target_include_directories(opas_anim_clip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_anim_clip
  PUBLIC opas::math opas::rig opas::format
  PRIVATE opas::format_json
)
opas_set_project_options(opas_anim_clip)
