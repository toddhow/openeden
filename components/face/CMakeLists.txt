# Facial speech animation, task 0071 stages b, c, e (pure parts): the opas.facecurves model, reader and writer, the
# importers and the curve sampler. Engine-independent and dependency-free: no game, renderer, JSON library, NVIDIA or
# ONNX code. The inference front end (stage c of the architecture D plan) is not here and needs toddhow's gate first.

add_library(eden_face STATIC
    facecurves.cpp
    facesample.cpp
    faceimport.cpp
)
# Includes are written <components/face/...> like the rest of the engine.
target_include_directories(eden_face PUBLIC ${CMAKE_SOURCE_DIR})
set_property(TARGET eden_face PROPERTY POSITION_INDEPENDENT_CODE ON)
