# CreativeMotion in the game, stage 0 (task 0067): the measurement of animation transitions from the game's trace
# lines. Reads text only; it knows nothing of the engine, CreativeMotion or the renderer.

add_library(eden_animtrace STATIC
    tracemetrics.cpp
    tracetool.cpp
)
# Includes are written <components/animtrace/...> like the rest of the engine.
target_include_directories(eden_animtrace PUBLIC ${CMAKE_SOURCE_DIR})
