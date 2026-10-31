set(FFMPEG_VIDEOPLAYER_LIBRARY "ffmpeg-videoplayer")

# Sources

set(FFMPEG_VIDEOPLAYER_SOURCE_FILES
    videoplayer.cpp
    videostate.cpp
    audiodecoder.cpp
    include/ffmpeg-videoplayer/audiodecoder.hpp
    include/ffmpeg-videoplayer/audiofactory.hpp
    include/ffmpeg-videoplayer/libavformatdefines.hpp
    include/ffmpeg-videoplayer/libavutildefines.hpp
    include/ffmpeg-videoplayer/videodefs.hpp
    include/ffmpeg-videoplayer/videoplayer.hpp
    include/ffmpeg-videoplayer/videostate.hpp
)

add_library(${FFMPEG_VIDEOPLAYER_LIBRARY} STATIC ${FFMPEG_VIDEOPLAYER_SOURCE_FILES})
target_link_libraries(${FFMPEG_VIDEOPLAYER_LIBRARY} ${FFmpeg_LIBRARIES})
target_link_libraries(${FFMPEG_VIDEOPLAYER_LIBRARY} SDL2::SDL2)

target_include_directories(${FFMPEG_VIDEOPLAYER_LIBRARY} SYSTEM PUBLIC include ${FFmpeg_INCLUDE_DIRS})
target_include_directories(${FFMPEG_VIDEOPLAYER_LIBRARY} PRIVATE include/ffmpeg-videoplayer)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(${FFMPEG_VIDEOPLAYER_LIBRARY} PUBLIC
        <memory>
        <string>
        <vector>
    )

    target_precompile_headers(${FFMPEG_VIDEOPLAYER_LIBRARY} PRIVATE <algorithm>)
endif()
