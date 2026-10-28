# Math types (headers only, standard library only).
add_library(opas_math INTERFACE)
add_library(opas::math ALIAS opas_math)
target_include_directories(opas_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(opas_math INTERFACE cxx_std_20)
