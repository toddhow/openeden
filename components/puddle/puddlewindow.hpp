#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEWINDOW_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEWINDOW_H

// OpenEden puddles (task 0095, stage W-b). The run-time side of the puddle map, CPU part: a window of the cells round
// the player, read from the pack. Stale or unreadable cells are ignored (with one warning per session in all) so the
// caller falls back to the old noise puddles for them. The GPU array texture and the cell index table that the shader
// needs are stage W-d; this class is what feeds them.
//
// Threads: `update` reads files and is meant for a worker thread; `find` may be called from any thread. All state is
// behind one mutex; a cell is shared, so a reader keeps a cell alive after the window has dropped it.

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "puddlepack.hpp"

namespace Puddle
{
    class PuddleWindow
    {
    public:
        /// The digest of the land a cell's bake depends on, as the game sees it now (`inputDigest` over the game's
        /// land source); nothing when it cannot be computed (the cell is then not used).
        using DigestFunction = std::function<std::optional<Digest>(std::int32_t, std::int32_t)>;
        using WarnFunction = std::function<void(const std::string&)>;

        /// The window holds the cells within `radius` cells of the centre (3 gives 7 x 7).
        PuddleWindow(std::shared_ptr<const PuddlePack> pack, DigestFunction digest, WarnFunction warn, int radius = 3);

        /// Loads the cells of the window that are in the pack and not loaded, drops those outside it.
        void update(std::int32_t centreX, std::int32_t centreY);

        std::shared_ptr<const CellMap> find(std::int32_t cellX, std::int32_t cellY) const;
        std::size_t loadedCount() const;
        /// Cells ignored because their land changed, they are for other settings, or they could not be read.
        std::size_t ignoredCount() const;

    private:
        std::shared_ptr<const PuddlePack> mPack;
        DigestFunction mDigest;
        WarnFunction mWarn;
        Digest mParameters;
        int mRadius;
        mutable std::mutex mMutex;
        std::map<std::pair<std::int32_t, std::int32_t>, std::shared_ptr<const CellMap>> mLoaded;
        std::set<std::pair<std::int32_t, std::int32_t>> mIgnored;
        bool mWarned = false;
    };
}

#endif
