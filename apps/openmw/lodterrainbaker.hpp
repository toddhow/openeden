#ifndef OPENMW_LODTERRAINBAKER_H
#define OPENMW_LODTERRAINBAKER_H

// OpenEden (task 0055, stage S1c): `openmw --bake-lod-terrain` bakes the terrain of the loaded game data into a
// derived Eden Archive pack of tiles (<user data>/openeden/lod/terrain.edenpak, or under --bake-gi-out). Layout
// version 1 (components/lod).
//
// Runs inside the game, once per frame while the game is running, on the main thread, like the other bakers: each
// call bakes tiles for about 10 milliseconds so that the window stays alive, and when all are done it writes the pack
// and says it has finished (the caller then quits). Land is read through the same terrain storage the live terrain
// uses (Terrain::Storage::getVertexData), so a tile is made from what the game would draw.
//
// Resuming: a bake is a few seconds of work, so there is no partial state. If a pack already exists that was made
// from the same land (the same content digest) with the same settings, nothing is written. The result never depends
// on the order tiles were baked in. Never commit the output: it is derived from the game files of the user.

#include <filesystem>
#include <memory>

namespace Resource
{
    class ResourceSystem;
}

namespace OMW
{
    class LodTerrainBaker
    {
    public:
        LodTerrainBaker(std::filesystem::path outFolder, Resource::ResourceSystem& resources);
        ~LodTerrainBaker();

        // Call once per running frame. True when the bake has finished (or failed) and the game should quit.
        bool update();

        struct State; // public so that the file's helpers can take it (task 0130)

    private:
        std::unique_ptr<State> mState;
    };
}

#endif
