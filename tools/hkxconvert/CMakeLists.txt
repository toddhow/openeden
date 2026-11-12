# hkxconvert (task 0119): converts the player's own Havok .hkx clips with the clean-room decoder in components/havokpack.
# Built only with -DOPENEDEN_BUILD_HKXCONVERT=ON (off by default); not installed.
openmw_add_executable(hkxconvert main.cpp)
target_link_libraries(hkxconvert PRIVATE components)
if (WIN32)
    target_sources(hkxconvert PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()
