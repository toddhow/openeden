#ifndef OPENMW_LODOBJECTBAKER_H
#define OPENMW_LODOBJECTBAKER_H

// OpenEden (task 0059, stage S4c): `openmw --bake-lod-objects` bakes the far-visible statics of the loaded game data
// into a derived Eden Archive pack of object clusters (<user data>/openeden/lod/objects.edenpak, or under
// --bake-gi-out). DRAFT layout version 0 (components/lod), not approved.
//
// Runs inside the game once per frame while the game is running, on the main thread, like the terrain baker: each
// call does about 10 milliseconds of work (collecting references, reading models, building tiles) so that the window
// stays alive, and when all is done it writes the pack and says it has finished (the caller then quits). The
// references come from the very function the live distant-object chunks use (MWRender::collectDistantReferences).
//
// Resuming: if a pack on disk was made from the same references, models and settings (the same content digest),
// nothing is written. The result never depends on the order tiles were built in. Never commit the output: it is
// derived from the user's game files.

#include <filesystem>
#include <memory>

namespace Resource
{
    class ResourceSystem;
}

namespace OMW
{
    class LodObjectBaker
    {
    public:
        LodObjectBaker(std::filesystem::path outFolder, Resource::ResourceSystem& resources);
        ~LodObjectBaker();

        // Call once per running frame. True when the bake has finished (or failed) and the game should quit.
        bool update();

    struct State; // public so that the file's helpers can take it (task 0133)

    private:
        std::unique_ptr<State> mState;
    };
}

#endif
