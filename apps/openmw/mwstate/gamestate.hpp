#ifndef VSGOPENMW_MWSTATE_GAMESTATE_H
#define VSGOPENMW_MWSTATE_GAMESTATE_H

#include <optional>

namespace MWState
{
    /*
     * Provides frame update entrypoint.
     */
    class GameState
    {
    public:
        virtual ~GameState() {}
        virtual bool /*keepRunning*/ run(float dt) = 0;

        std::optional<bool> showCursor;
        bool disableControls = false;
        bool disableEvents = false;
        bool requiresScene = true;
        bool requiresRender = true;
        // openeden-vsg: false while a video plays. Like OpenMW 0.51's blocking video loop, the sound manager is then
        // not updated: at the end of a video the movie's audio decoder waits for packets while holding the OpenAL
        // stream lock, and SoundManager::update would wait on that lock before the video could finish (a deadlock).
        bool updateSound = true;
        // openeden-vsg: false while loading, saving, changing cells or playing a video. OpenMW 0.51 did these
        // synchronously, so Lua scripts got no frame until they finished; a script that requests a new game, a load or
        // a save and yields one frame relies on that (as the Lua integration tests do).
        bool updateLua = true;
    };
}

#endif
