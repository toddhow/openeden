#ifndef OPENMW_MWRENDER_TEXTUREMIPSDRIVER_H
#define OPENMW_MWRENDER_TEXTUREMIPSDRIVER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

#include <components/render/vramscaler.hpp>
#include <components/render/vramtexturemips.hpp>
#include <components/vsgutil/texturemips.hpp>

namespace vsgUtil
{
    class CompileContext;
}

namespace MWRender
{
    /*
     * Task 0080, stage d, first slice: the `texture mips` system of the video memory scale-back. At ladder level L
     * every texture of 2048 texels or more has its top L mip levels dropped (never below 1024 on the long side), by
     * vsgUtil::TextureMips::Swapper, a few swaps a frame (the plan's `maxChanges`). Main thread only; owned by
     * RenderManager, made only when `[VideoMemory] order` names `texture mips` or the test script
     * OPENEDEN_TEXTURE_MIPS_TEST is set. Nothing else in the game knows about it.
     *
     * OPENEDEN_TEXTURE_MIPS_TEST="5:1,15:0" (a test aid, no setting): at 5 s after the first frame put every
     * eligible texture at level 1, at 15 s back at level 0. Each swap is logged with its main-thread cost, and a
     * few seconds later the shared caches are pruned, VSG's unused memory pools are returned and the driver's
     * memory use before and after is logged.
     */
    class TextureMipsDriver
    {
    public:
        struct Services
        {
            vsg::ref_ptr<vsgUtil::CompileContext> compile; // contexts that compile for every view
            std::function<vsg::Node*()> root; // the scene graph to scan; may return null
            std::function<void()> pruneCaches; // ResourceSystem::updateCache: frees what only the caches hold
            std::function<Render::VideoMemory::Sample()> memory; // the driver's numbers; may be empty
            std::function<std::int64_t()> releasePools; // Engine::releaseUnusedDeviceMemory; may be empty
            // The models cached by the loaders' shared objects (they hold the binds of the scene too).
            std::function<std::vector<vsg::ref_ptr<vsg::Node>>()> cachedModels;
        };

        explicit TextureMipsDriver(Services services);

        /// The adapter's three functions for the scale-back's ladder (`texture mips`): RenderManager wraps them.
        void account(Render::VideoMemory::Ledger& ledger);
        std::int64_t stepBytes(int level, bool down) const;

        /// Once a frame, with the frame time in seconds (0 when paused).
        void update(float dt);

        /// Sets the ladder level at once (the adapter and the test script both end here).
        void setLevel(int level);
        int level() const { return mLevel; }

        /// Parses OPENEDEN_TEXTURE_MIPS_TEST; false when it is empty or not valid.
        bool setScript(const std::string& text);
        static constexpr int maxLevel = 2;

    private:
        void rescan();
        void applyPending();
        void logMemory(const char* when);

        Services mServices;
        vsgUtil::TextureMips::Swapper mSwapper;
        std::vector<Render::VideoMemory::TextureMips::Texture> mTextures;
        Render::VideoMemory::TextureMips::Params mParams;
        int mLevel = 0;
        std::uint64_t mFrame = 0;
        double mTime = 0.0;
        double mLastScan = -1000.0;
        bool mWorking = false; // more swaps are due for the current level
        bool mAnyReduced = false;
        double mLastScanMs = 0.0;

        struct Step
        {
            double at = 0.0;
            int level = 0;
            bool done = false;
        };
        std::vector<Step> mScript;
        double mPruneAt = -1.0;
        double mReleaseAt = -1.0;
        double mNextReport = 20.0;
    };
}

#endif
