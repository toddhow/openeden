# Debug overlays as data (headers only, standard library only).
add_library(opas_debug INTERFACE)
add_library(opas::debug ALIAS opas_debug)
target_include_directories(opas_debug INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_debug INTERFACE opas::math)
target_compile_features(opas_debug INTERFACE cxx_std_20)
