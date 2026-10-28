# The physics interface: headers only, standard library only.
add_library(opas_physics_interface INTERFACE)
add_library(opas::physics_interface ALIAS opas_physics_interface)

target_include_directories(opas_physics_interface INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(opas_physics_interface INTERFACE cxx_std_20)
