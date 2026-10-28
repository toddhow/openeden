add_library(opas_reconcile
  src/CapsuleReconciler.cpp
  src/EventBridge.cpp
  src/BodyOwnership.cpp
  src/CharacterStateSnapshot.cpp
)

add_library(opas::reconcile ALIAS opas_reconcile)

target_include_directories(opas_reconcile
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(opas_reconcile
  PUBLIC
    opas_math
    opas_rig
    opas_arbiter
    opas_sensors
    opas_behaviors
    opas_motor
    opas_physics_interface
)

target_compile_features(opas_reconcile PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(opas_reconcile PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(opas_reconcile PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
