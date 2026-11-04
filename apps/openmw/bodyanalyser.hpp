#ifndef OPENMW_BODYANALYSER_H
#define OPENMW_BODYANALYSER_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage a): `openmw --analyse-bodies` measures the stock body
// parts of the loaded game data and writes a report (<out>/analysis/bodies.json and bodies.txt, where <out> is
// --bake-gi-out or the user data folder). Read-only: nothing in the game or the data changes, and the mode ends the
// game when it is done.
//
// For every race, male and female, it takes the parts MWRender::getBodyParts selects (the engine's own function, so
// it cannot disagree with the game), reads each part's NIF with the engine's skin filter through components/bodynif
// (the engine's scene graph reader cannot give plain arrays; see components/bodynif/skinnedshape.hpp for the two
// places it differs), and measures the parts and the seams between them (components/bodynif/seamanalysis.hpp).
// The head records of the race and sex are measured against the neck.
//
// Runs on the main thread, one race and sex per call (a few milliseconds each, so the window keeps drawing). The
// report depends on the data only (no clock, no machine path), so two runs give the same bytes. It names records and
// models of the user's own game files: never commit it.

#include <filesystem>
#include <memory>

namespace Resource
{
    class ResourceSystem;
}

namespace OMW
{
    class BodyAnalyser
    {
    public:
        BodyAnalyser(std::filesystem::path outFolder, Resource::ResourceSystem& resources);
        ~BodyAnalyser();

        // Call once per running frame. True when the analysis has finished (or failed) and the game should quit.
        bool update();

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
