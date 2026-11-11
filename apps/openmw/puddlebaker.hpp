#ifndef OPENMW_PUDDLEBAKER_H
#define OPENMW_PUDDLEBAKER_H

// OpenEden (task 0095, stage W-b): `openmw --bake-puddles` bakes the puddle map of the loaded game data into a derived
// Eden Archive pack (<user data>/openeden/puddles/puddles.edenpak, or under --bake-gi-out): for every exterior cell
// with land, where water collects on the terrain (components/puddle). Nothing reads the pack at run time yet.
//
// Runs inside the game, once per frame while the game is running, on the main thread, like the other bakers: each call
// works for about 10 milliseconds (at least one cell) so that the window stays alive. Land is read through
// Terrain::Storage::getVertexData, the same path the live terrain is built from, so the bake sees the ground that is
// drawn. A bake that is killed is resumed from the cells already done (the work folder), a second run with the same
// land and settings writes nothing, and the result never depends on the order of the cells. Never commit the output:
// it is derived from the game files of the user.

#include <filesystem>
#include <memory>
#include <string>

namespace Resource
{
    class ResourceSystem;
}

namespace OMW
{
    /// The exit code of a game that was started with --bake-puddles and whose bake failed.
    inline constexpr int sPuddleBakeFailedExitCode = 4;

    class PuddleBaker
    {
    public:
        /// `range` is "all", "<x>,<y>" or "<x0>,<y0>:<x1>,<y1>".
        PuddleBaker(std::string range, std::filesystem::path outFolder, Resource::ResourceSystem& resources);
        ~PuddleBaker();

        /// Call once per running frame. True when the bake has finished (or failed) and the game should quit.
        bool update();

        /// True when the bake ended in a failure (bad range or settings, an error, an exception): the game then exits
        /// with `sPuddleBakeFailedExitCode`.
        bool failed() const;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
