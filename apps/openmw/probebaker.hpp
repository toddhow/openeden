#ifndef OPENMW_PROBEBAKER_H
#define OPENMW_PROBEBAKER_H

// OpenEden (task 0028, stage c): `openmw --bake-gi "<interior cell>"`. Runs inside the game, once per frame while the
// game is running, on the main thread: it waits for the cell to be loaded, then bakes light probes for it a few at a
// time (so the window stays alive), writes the file and asks the game to quit. The maths is in
// components/render/probebake.hpp; this is the part that knows the game.

#include <filesystem>
#include <memory>
#include <string>

namespace MWRender
{
    class RenderManager;
}

namespace OMW
{
    class ProbeBaker
    {
    public:
        // `outFolder`: a data folder layout; the file goes to <outFolder>/openeden/probes/<cellKey>.json.
        ProbeBaker(std::string cellName, std::filesystem::path outFolder, MWRender::RenderManager& rendering);
        ~ProbeBaker();

        // Call once per running frame. True when the baker has finished (or failed) and the game should quit.
        bool update();

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
