# Shared support for data-file loaders and writers.
# opas::format          LoadError, the public error type every loader returns.
# opas::format_json     the JSON reader/writer helpers. It carries nlohmann/json,
#                       so modules link it PRIVATE and its header never appears
#                       in a public header (CLAUDE.md, "One direction").
add_library(opas_format
  src/LoadError.cpp
  src/JsonSupport.cpp
)
add_library(opas::format ALIAS opas_format)
target_include_directories(opas_format
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal
)
target_link_libraries(opas_format
  PUBLIC opas::math
  PRIVATE nlohmann_json::nlohmann_json
)
opas_set_project_options(opas_format)

add_library(opas_format_json INTERFACE)
add_library(opas::format_json ALIAS opas_format_json)
target_include_directories(opas_format_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(opas_format_json INTERFACE opas::format nlohmann_json::nlohmann_json)
