#ifndef OPENMW_SKYVISBAKER_H
#define OPENMW_SKYVISBAKER_H

// OpenEden (task 0036, stage b): `openmw --bake-gi-exterior "<x>,<y>"` (one exterior cell) or "<x0>,<y0>:<x1>,<y1>" (a
// rectangle of cells). Runs inside the game, once per frame while the game is running, on the main thread: it moves
// the player to each cell in turn (sorted by y, then x), waits for the cell and its neighbours to be loaded, bakes the
// cell's sky visibility a few probes at a time (so the window stays alive), writes the file and goes on to the next;
// when the last is done it asks the game to quit. A cell whose file already exists with the same settings is skipped,
// and a stopped bake resumes. The maths is in components/render/skyvisbake.hpp; this is the part that knows the game.

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MWRender
{
    class RenderManager;
}

namespace OMW
{
    // The cells named by the command line argument, sorted by y then x. Empty (and `error` set) when it is not "x,y"
    // or "x0,y0:x1,y1", or names more than `maxCells` cells.
    std::vector<std::pair<int, int>> parseSkyBakeCells(const std::string& argument, std::string& error);

    class SkyVisBaker
    {
    public:
        // `outFolder`: a data folder layout; each file goes to <outFolder>/openeden/probes/exterior/<x>_<y>.skyvis.json.
        SkyVisBaker(std::string argument, std::filesystem::path outFolder, MWRender::RenderManager& rendering);
        ~SkyVisBaker();

        // Call once per running frame. True when the baker has finished (or failed) and the game should quit.
        bool update();

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
