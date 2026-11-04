#ifndef OPENMW_BODYBAKER_H
#define OPENMW_BODYBAKER_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage c): `openmw --bake-bodies` builds the merged body of
// every race and sex from the loaded game data and publishes them as a derived cache; `openmw --check-bodies` reads
// that cache back and compares it with the stock parts. Neither changes the game or the data, and each ends the game
// when it is done. Nothing is drawn from the cache yet (stage d).
//
// BAKE. For every race and sex (third person) it takes the parts MWRender::getBodyParts selects, reads each part's NIF
// through components/bodynif (the same reader --analyse-bodies uses), hands the parts and the two exact seams
// (forearm to wrist and knee to ankle, tolerance 0.01 units: research note 0033, toddhow's decision of 2026-11-03) to
// Body::mergeParts, and writes the result as a `bodymesh` payload. The cache lives in <out>/openeden/bodies/ (<out> is
// --bake-gi-out or the user data folder), see components/body/bodycache.hpp: merged-<digest>.edenpak, `current`,
// `status.json`. An entry whose key (part file digests, records, skeleton, merger version, parameters) matches the
// one already published is copied, not merged again; a second bake of unchanged data publishes the same pack and says
// "up to date". A race the merger or the reader cannot handle is a line in the manifest and the status file, never a
// stop.
//
// CHECK. For every entry: the key is recomputed from the data now and must equal the manifest's; the payload must
// read; merging the stock parts again must give the same payload bytes; every stock vertex must have its merged
// vertex within the weld tolerance (and how many moved at all); and at five poses (the bind pose and four seeded
// ones, each bone moved rigidly) the skinned position of every stock vertex must equal the merged body's.
//
// Both run on the main thread, one race and sex per call (a few milliseconds each, so the window keeps drawing). The
// outputs depend on the data only (no clock, no machine path in a payload): two bakes give the same bytes. They name
// records and models of the user's own game files: never commit them.

#include <filesystem>
#include <memory>

namespace Resource
{
    class ResourceSystem;
}

namespace OMW
{
    class BodyBaker
    {
    public:
        enum class Mode
        {
            Bake,
            Check,
        };

        BodyBaker(Mode mode, std::filesystem::path outFolder, Resource::ResourceSystem& resources);
        ~BodyBaker();

        // Call once per running frame. True when the mode has finished (or failed) and the game should quit.
        bool update();

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
