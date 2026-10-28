# Sanitizer support for the `asan` preset.
#
# Flags are added globally (add_compile_options / add_link_options) rather than
# per target because every library in the process, including Jolt and
# GoogleTest, has to be built the same way: mixing instrumented and
# uninstrumented code gives false reports, especially with MSVC.

function(opas_enable_sanitizers_globally)
  if(NOT OPAS_SANITIZE)
    return()
  endif()

  if(MSVC)
    # MSVC supports AddressSanitizer only; UBSan is checked on Linux in CI.
    add_compile_options(/fsanitize=address /Zi)
    # Incremental linking is incompatible with /fsanitize=address.
    add_link_options(/INCREMENTAL:NO)
  else()
    set(flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_compile_options(${flags})
    add_link_options(${flags})
  endif()
endfunction()

# MSVC's /fsanitize=address links against a runtime DLL that lives next to
# cl.exe and is only on PATH inside a Developer Command Prompt. Elsewhere every
# launch of the executable fails with a "clang_rt.asan_dynamic-x86_64.dll was
# not found" dialog (one per test). Copying the DLL next to the executable
# makes it start from any shell. Assumes x64 cl.exe (the only MSVC setup the
# project builds today); clang-cl or other targets would need another DLL name.
function(opas_copy_sanitizer_runtime target)
  if(NOT OPAS_SANITIZE OR NOT MSVC)
    return()
  endif()
  get_filename_component(compilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(runtime "${compilerDir}/clang_rt.asan_dynamic-x86_64.dll")
  if(NOT EXISTS "${runtime}")
    message(FATAL_ERROR "AddressSanitizer runtime not found at ${runtime}")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime}" "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()
