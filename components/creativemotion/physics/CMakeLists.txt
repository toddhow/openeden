# physics/interface is headers only (standard library only) and always built:
# core/motor depends on it. physics/jolt implements it and fetches Jolt, so it
# is built only with OPAS_BUILD_PHYSICS.
add_subdirectory(interface)
if(OPAS_BUILD_PHYSICS)
  add_subdirectory(jolt)
endif()
