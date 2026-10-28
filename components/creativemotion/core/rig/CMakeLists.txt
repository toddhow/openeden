# Skeleton, rig and profile model, with their JSON loaders and writers.
# JSON support (opas::format_json, which carries nlohmann/json) is PRIVATE:
# no JSON types appear in public headers.
add_library(opas_rig
  src/Skeleton.cpp
  src/Rig.cpp
  src/RigFile.cpp
  src/Profile.cpp
  src/ProfileFile.cpp
)
add_library(opas::rig ALIAS opas_rig)
target_include_directories(opas_rig PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_rig
  PUBLIC opas::math opas::format
  PRIVATE opas::format_json
)
opas_set_project_options(opas_rig)
