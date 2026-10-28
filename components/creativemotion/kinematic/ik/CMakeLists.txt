# Inverse kinematics.
add_library(opas_ik
  src/TwoBoneIk.cpp
  src/FingerCurl.cpp
)
add_library(opas::ik ALIAS opas_ik)
target_include_directories(opas_ik PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_ik PUBLIC opas::math)
opas_set_project_options(opas_ik)
