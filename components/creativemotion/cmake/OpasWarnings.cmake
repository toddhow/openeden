# Warning and floating-point settings applied to every target we own.
# Third-party targets (Jolt, GoogleTest) keep their own settings.

function(opas_set_project_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /WX
      /permissive-
      /utf-8
      # Strict IEEE behavior, no contraction into fused multiply-add, so
      # results are reproducible between runs and between compilers.
      /fp:precise
    )
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wconversion
      -Wsign-conversion
      -Wold-style-cast
      -Wnon-virtual-dtor
      -Werror
      # GCC and Clang may otherwise fuse a*b+c into one FMA instruction on some
      # targets and not others, which changes results across platforms.
      -ffp-contract=off
    )
  endif()
endfunction()
