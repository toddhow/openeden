# The glTF 2.0 clip reader (task 0020, C3b). cgltf is private to this module.

# cgltf is one header; its implementation is compiled once here, without our
# warnings-as-errors (we do not own its warnings), and its include folder is
# SYSTEM so the header never trips our warnings either.
add_library(opas_cgltf STATIC src/CgltfImplementation.cpp)
target_include_directories(opas_cgltf SYSTEM PUBLIC ${cgltf_SOURCE_DIR})

add_library(opas_anim_gltf src/GltfReader.cpp)
add_library(opas::anim_gltf ALIAS opas_anim_gltf)
target_include_directories(opas_anim_gltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opas_anim_gltf
  PUBLIC opas::anim_clip opas::rig opas::format
  PRIVATE opas_cgltf
)
opas_set_project_options(opas_anim_gltf)
