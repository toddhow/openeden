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
        virtual ~GameState(){}
        virtual bool /*keepRunning*/ run(float dt) = 0;

        std::optional<bool> showCursor;
        bool disableControls = false;
        bool disableEvents = false;
        bool requiresScene = true;
        bool requiresRender = true;
        // openeden-vsg: false while a video plays. Like OpenMW 0.51's blocking video loop, the sound manager is then not
        // updated: at the end of a video the movie's audio decoder waits for packets while holding the OpenAL stream
        // lock, and SoundManager::update would wait on that lock before the video could finish (a deadlock).
        bool updateSound = true;
    };
}

#endif
