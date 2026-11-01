# Run by the openeden_spirv_val_rejects_bad_input target (files/shaders/CMakeLists.txt): spirv-val must reject a file
# that is not SPIR-V. The build fails if it accepts it, which would mean the check on the Slang shaders proves nothing.
# Inputs: SPIRV_VAL (the validator), WORK_DIR (a folder to write the broken file in).
set(bad "${WORK_DIR}/not_spirv.spv")
file(WRITE "${bad}" "This is text, not SPIR-V; the first word is not the SPIR-V magic number.")
execute_process(COMMAND "${SPIRV_VAL}" --target-env vulkan1.0 "${bad}"
                RESULT_VARIABLE result ERROR_VARIABLE errors OUTPUT_VARIABLE output)
file(REMOVE "${bad}")
if (result EQUAL 0)
    message(FATAL_ERROR "spirv-val accepted a file that is not SPIR-V")
endif()
if (NOT "${errors}${output}" MATCHES "[Mm]agic")
    message(FATAL_ERROR "spirv-val rejected the broken file, but not for the expected reason: ${errors}${output}")
endif()
