#ifndef OPENEDEN_GAME_SKYRIMSE_MUSIC_DATA_H
#define OPENEDEN_GAME_SKYRIMSE_MUSIC_DATA_H

// The small interface between the music code (music.hpp) and the world view that owns its data (task 0209). Kept apart
// so that world_view.hpp does not include the conditions engine.

#include <string>

namespace game_skyrimse::music
{
    class MusicRuntime;

    /// Implemented by the world view: where the runtime lives. The cell source finds it by dynamic_cast.
    class MusicDataSource
    {
    public:
        virtual ~MusicDataSource() = default;
        /// The runtime, created and started on the first call; null when the view has no plugins to read.
        virtual MusicRuntime* musicRuntime() = 0;
        /// Does a music file exist in the session's VFS ("music/tavern/mus_tavern_01.xwm")?
        virtual bool musicFileExists(const std::string& vfsPath) = 0;
    };
}

#endif
