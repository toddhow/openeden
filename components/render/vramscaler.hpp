#ifndef OPENEDEN_RENDER_VRAMSCALER_H
#define OPENEDEN_RENDER_VRAMSCALER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "vramaccount.hpp"
#include "vramcontroller.hpp"
#include "vramorder.hpp"
#include "vrampoolrelease.hpp"
#include "vramsource.hpp"

namespace Render::VideoMemory
{
    /*
     * The video memory scale-back, wired up (task 0080, stages a to c): samples the memory source on a fixed
     * cadence, lets each adapter write what it holds into the ledger, asks the Controller what to do and carries a
     * step out through the adapter. No Vulkan and no engine type: the adapters and the source are std::functions the
     * render manager fills, so the whole loop is tested with fakes (testvramscaler.cpp).
     *
     * Main thread only; `tick` allocates only when it logs.
     */

    /// One system the scale-back may reduce (the adapters of the task spec). All callbacks run on the main thread.
    struct Adapter
    {
        std::string name; // as in the `order` setting
        int maxLevel = 1;
        /// Writes this system's entries into the ledger (`set`); called before every decision.
        std::function<void(Ledger&)> account;
        /// Puts the system at `level` (0 is full quality); false when it cannot now (the controller retries).
        std::function<bool(int level)> apply;
        /// What one step from `level` frees (down) or costs (up: back to `level - 1`), an estimate in bytes.
        std::function<std::int64_t(int level, bool down)> stepBytes;
    };

    /// What one attempt to hand VSG's unused device-memory pools back to the driver achieved.
    struct ReleaseResult
    {
        std::int64_t bytes = 0; // device memory returned
        double milliseconds = 0.0; // how long the call took
    };

    struct ScalerConfig
    {
        Params params;
        /// `auto`, or a number of MiB that stands for the card's memory.
        BudgetSetting budget;
        /// The share of the budget the game may fill.
        double budgetFraction = 0.85;
        /// Used as the card's memory when the driver gives no budget (device-local MiB in bytes); 0: unknown.
        std::int64_t fallbackBudgetBytes = 0;
        /// False: the pressure comes from the ledger alone and the driver's usage is ignored (a diagnostic: VSG's
        /// memory pools keep their size when something is freed, so the driver's usage may not fall after a step).
        bool useDriverUsage = true;
        double sampleInterval = 0.5;
        std::string order; // the `order` setting, a comma list of adapter names
        /// Hands VSG's unused device-memory pools back to the driver (main thread only). Called a few times after
        /// each reduction (PoolReleasePlanner), never otherwise. Empty: pools are never released.
        std::function<ReleaseResult()> releasePools;
        PoolReleaseParams releaseParams;
    };

    class Scaler
    {
    public:
        /// `log(warning, text)` receives every message (may be empty). `source` is called once per sample.
        using LogFn = std::function<void(bool warning, const std::string& text)>;

        Scaler(ScalerConfig config, std::vector<Adapter> adapters, std::function<Sample()> source, LogFn log);

        /// Adds `dt` seconds (0 while the game is paused) and takes a sample when the interval has passed.
        void tick(double dt);

        /// True when at least one listed system is known (otherwise the scaler does nothing).
        bool active() const { return mController != nullptr; }

        const Ledger& ledger() const { return mLedger; }

        // For the frame counters, in MiB (-1 before the first sample).
        double usedMiB() const { return mUsedMiB; }
        double budgetMiB() const { return mBudgetMiB; }
        double accountedMiB() const { return mAccountedMiB; }
        /// The sum of the levels of all systems: how many reductions are active.
        int levelCount() const { return mController ? mController->reductions() : 0; }
        double pressure() const { return mController ? mController->pressure() : 0.0; }

        int stepsDown() const { return mStepsDown; }
        int poolReleases() const { return mPoolReleases; }
        int stepsUp() const { return mStepsUp; }

        /// The summary for `[VideoMemory] report`: time at each reduction count, the steps, the highest pressure.
        std::string reportText() const;

    private:
        void sampleNow();
        void note(bool warning, const std::string& text) const;

        ScalerConfig mConfig;
        PoolReleasePlanner mPlanner;
        std::vector<Adapter> mAdapters; // all known
        std::vector<int> mAdapterOfSystem; // the controller's systems, in the user's order, as indices into mAdapters
        std::function<Sample()> mSource;
        LogFn mLog;
        Ledger mLedger;
        std::unique_ptr<Controller> mController;
        double mTime = 0.0;
        double mNextSample = 0.0;
        double mLastSampleTime = 0.0;
        bool mHaveSampleTime = false;
        bool mLoggedUnknownBudget = false;
        bool mLoggedFallback = false;
        double mUsedMiB = -1.0;
        double mBudgetMiB = -1.0;
        double mAccountedMiB = -1.0;
        int mPoolReleases = 0;
        std::int64_t mPoolBytesReleased = 0;
        double mLongestReleaseMs = 0.0;
        int mStepsDown = 0;
        int mStepsUp = 0;
        double mHighestPressure = 0.0;
        std::vector<double> mSecondsAtCount;
    };
}

#endif
