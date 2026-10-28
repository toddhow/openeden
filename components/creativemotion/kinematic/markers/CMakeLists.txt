# Interaction markers, their JSON file format, and body measurements.
# JSON support (opas::format_json, which carries nlohmann/json) is PRIVATE:
# no JSON types appear in public headers.
add_library(opas_markers
  src/MarkerFile.cpp
  src/BodyMeasurements.cpp
  src/Alignment.cpp
  src/BodyMapFile.cpp
  src/PoseClipFile.cpp
  src/ContactDetection.cpp
  src/Validation.cpp
  src/CapsuleFit.cpp
  src/PresetFile.cpp
  src/AutoMarkers.cpp
)
add_library(opas::markers ALIAS opas_markers)
target_include_directories(opas_markers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_markers
  PUBLIC opas::math opas::rig opas::ik opas::format
  PRIVATE opas::format_json
)
opas_set_project_options(opas_markers)
