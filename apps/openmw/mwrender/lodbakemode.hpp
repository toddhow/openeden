#ifndef OPENMW_MWRENDER_LODBAKEMODE_H
#define OPENMW_MWRENDER_LODBAKEMODE_H

namespace MWRender
{
    // OpenEden (task 0055): true while `openmw --bake-lod-terrain` or `--bake-lod-objects` runs. The bakers only need
    // the game data, so the render side keeps to the plain [Camera] viewing distance instead of the long
    // [WorldLod] view distance (a new game with 40000 units of distant terrain would slow the bake down tenfold).
    // Set once in main before the engine starts, read on the main thread.
    inline bool sLodBakeRun = false;
}

#endif
