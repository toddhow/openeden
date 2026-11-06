#ifndef OPENMW_MWRENDER_TEXTUREMIPSDRIVER_H
#define OPENMW_MWRENDER_TEXTUREMIPSDRIVER_H

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
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
            std::function<vsg::dvec3()> camera; // where the camera is (the use signal); may be empty
            std::function<void()> pruneCaches; // ResourceSystem::updateCache: frees what only the caches hold
            std::function<Render::VideoMemory::Sample()> memory; // the driver's numbers; may be empty
            std::function<std::pair<std::int64_t, std::int64_t>()> poolStats; // reserved, available; may be empty
            std::function<std::int64_t()> releasePools; // Engine::releaseUnusedDeviceMemory; may be empty
            // The terrain's layer arrays (all may be empty): drop cached batches nothing draws with before an array
            // swap; the Builder hands the new descriptor to later batches; caches that hold binds follow a step.
            std::function<void()> beforeArraySwap;
            std::function<void()> afterArraySwap;
            std::function<void(const vsg::DescriptorImage*, vsg::ref_ptr<vsg::DescriptorImage>)>
                arrayDescriptorReplaced;
            std::function<void(const std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>&)>
                bindsReplaced;
            // The models cached by the loaders' shared objects (they hold the binds of the scene too).
            std::function<std::vector<vsg::ref_ptr<vsg::Node>>()> cachedModels;
        };

        explicit TextureMipsDriver(Services services);

        /// The adapter's three functions for the scale-back's ladder (`texture mips`): RenderManager wraps them.
        void account(Render::VideoMemory::Ledger& ledger);
        std::int64_t stepBytes(int level, bool down) const;

        /// Once a frame, with the frame time in seconds (0 when paused).
        void update(float dt);

        /// The scale-back controller's level (the adapter and the test script both end here). Ignored for as long
        /// as a manual level is set.
        void setLevel(int level);
        int level() const { return mLevel; }

        /// The `texturemips` console command: `status` (or nothing), `off` (drops a manual level: the controller's
        /// level, 0 without one, applies again) or a level 0 to 2 (a manual level that wins over the controller and
        /// ignores the hysteresis). Returns the line to print.
        std::string command(const std::string& argument);
        /// One line: the level and who set it, textures reduced, ledger numbers, the driver's heap use.
        std::string status();

        /// Parses OPENEDEN_TEXTURE_MIPS_TEST; false when it is empty or not valid.
        bool setScript(const std::string& text);
        static constexpr int maxLevel = 2;

    private:
        void rescan();
        void applyPending();
        void logSwap(const vsgUtil::TextureMips::SwapResult& result);
        void logMemory(const char* when);

        Services mServices;
        vsgUtil::TextureMips::Swapper mSwapper;
        std::vector<Render::VideoMemory::TextureMips::Texture> mTextures;
        Render::VideoMemory::TextureMips::Params mParams;
        int mLevel = 0; // the level in force: the manual one, else the controller's
        int mControllerLevel = 0;
        std::optional<int> mManual;
        std::uint64_t mFrame = 0;
        double mTime = 0.0;
        double mLastScan = -1000.0;
        bool mWorking = false; // more swaps are due for the current level
        std::deque<Render::VideoMemory::TextureMips::Change> mQueue; // swaps planned, not begun
        std::uint64_t mJobTexture = 0; // the texture the running swap belongs to
        int mJobFrom = 0;
        int mJobTo = 0;
        double mJobMs = 0.0; // main thread time of the running swap so far
        int mJobSteps = 0;
        bool mHoldQueue = false; // after an array swap: no new swap before the pools are released
        bool mAnyReduced = false;
        bool mReportIdle = false; // log the status once the swaps of a manual command are done
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
