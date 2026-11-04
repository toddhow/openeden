# AdvancedBodies, stage B1 (task 0063): the body contract (package model, JSON, validator, packed form).
# Engine-independent: it knows CreativeMotion's math, rig and skeleton profile, Eden Archive and nothing of the game
# (no ESM, VSG, Jolt, Qt or MyGUI). The Morrowind legacy adapter lives in apps/openmw/mwrender/bodylegacy.cpp.

add_library(eden_body STATIC
    bodycontract.cpp
    bodypackage.cpp
    bodyjson.cpp
    bodyvalidate.cpp
    bodypack.cpp
    bodytool.cpp
    bodymerge.cpp
    bodymesh.cpp
    bodycache.cpp
    bodyinfluences.cpp
    skeletonmap.cpp
    skeletonsuggest.cpp
    skeletonmapping.cpp
    mergedruntime.cpp
)
# Includes are written <components/body/...> like the rest of the engine.
target_include_directories(eden_body PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(eden_body
    PUBLIC opas_anim_clip opas_rig opas_format opas_math edenarchive
    # nlohmann/json stays out of the public headers (core/format/CLAUDE.md).
    PRIVATE opas_format_json
)
