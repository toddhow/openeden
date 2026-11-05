#ifndef OPENEDEN_RENDER_VRAMCONTROLLER_H
#define OPENEDEN_RENDER_VRAMCONTROLLER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vramaccount.hpp"
#include "vramsource.hpp"

namespace Render::VideoMemory
{
    /*
     * The video memory scale-back's state machine (task 0080, stage b). Pure: no Vulkan, no clock (time and samples
     * are arguments), no randomness, no container whose order matters: the same samples and times give the same
     * actions. The rules are in docs/tasks/0080-vram-scale-back-and-mip-streaming.md, "The controller".
     *
     * Main thread only. `update` allocates nothing.
     */

    enum class Kind
    {
        None,
        Reduce,
        Restore,
        Exhausted
    };

    struct Action
    {
        Kind kind = Kind::None;
        int system = -1;
    };

    struct SystemInfo
    {
        std::string name;
        /// Level 0 is full quality; a system may be reduced up to this level.
        int maxLevel = 1;
        /// The level the user does not let it go past; 0 or less: no limit beyond maxLevel.
        int minimumLevel = 0;
    };

    struct Params
    {
        double highWater = 0.92;
        double lowWater = 0.75;
        double margin = 0.05;
        double smoothSeconds = 2.0;
        double dwell = 4.0;
        double restoreDwell = 30.0;
        double settleSeconds = 3.0;
        bool automaticRestore = true;
    };

    /// Rule 5, the hysteresis gap. Clamps `params` in place so that high water is in (0.3, 1] and low water lies
    /// between high water - 0.30 and high water - margin; returns one message per change, for the settings reader to
    /// log as a warning.
    inline std::vector<std::string> clampParams(Params& params)
    {
        std::vector<std::string> messages;
        if (!std::isfinite(params.highWater) || !(params.highWater > 0.3) || params.highWater > 1.0)
        {
            const double clamped = std::isfinite(params.highWater) ? std::clamp(params.highWater, 0.31, 1.0) : 0.92;
            messages.push_back("high water " + std::to_string(params.highWater) + " is outside 0.3 to 1, using "
                + std::to_string(clamped));
            params.highWater = clamped;
        }
        const double floorLow = std::max(0.0, params.highWater - 0.30);
        const double ceilingLow = params.highWater - params.margin;
        if (!std::isfinite(params.lowWater) || params.lowWater < floorLow || params.lowWater > ceilingLow)
        {
            const double clamped
                = std::isfinite(params.lowWater) ? std::clamp(params.lowWater, floorLow, ceilingLow) : floorLow;
            messages.push_back("low water " + std::to_string(params.lowWater)
                + " must be between high water - 0.30 and high water - 0.05, using " + std::to_string(clamped));
            params.lowWater = clamped;
        }
        return messages;
    }

    class Controller
    {
    public:
        /// What a step of `system` frees (down) or costs (up) at its current level, in bytes.
        using StepBytes = std::function<std::int64_t(int system, bool down)>;

        Controller(std::vector<SystemInfo> order, const Params& params)
            : mParams(params)
            , mSystems(order.size())
        {
            for (std::size_t i = 0; i < order.size(); ++i)
                mSystems[i].info = std::move(order[i]);
        }

        Action update(double nowSeconds, const Sample& sample, const Ledger& ledger, const StepBytes& stepBytes)
        {
            mNow = nowSeconds;
            mPending = Action{};
            if (sample.budgetBytes <= 0)
            {
                mBudgetUnknown = true;
                return Action{};
            }
            mBudgetUnknown = false;
            mBudget = static_cast<double>(sample.budgetBytes);
            const std::int64_t total = ledger.total();
            // The ledger plus what it cannot explain: the usage when it is known and larger, else the ledger alone.
            const double raw = static_cast<double>(total + ledger.untracked(sample.usageBytes)) / mBudget;

            if (!mHaveSample)
            {
                mPressure = raw;
                mHaveSample = true;
            }
            else
            {
                const double dt = std::max(0.0, nowSeconds - mLastUpdate);
                const double alpha = mParams.smoothSeconds > 0.0 ? 1.0 - std::exp(-dt / mParams.smoothSeconds) : 1.0;
                mPressure += alpha * (raw - mPressure);
            }
            mLastUpdate = nowSeconds;

            // Settling: the last reduce's relief counts as applied until the ledger shows it or settleSeconds pass.
            if (mSettling)
            {
                const std::int64_t shown = std::max<std::int64_t>(0, mTotalAtReduce - total);
                if (nowSeconds - mReduceTime >= mParams.settleSeconds || shown >= mReliefExpected / 2)
                    mSettling = false;
            }

            if (mPressure > mParams.highWater)
                ++mHighSamples;
            else
            {
                mHighSamples = 0;
                mExhaustedReported = false;
            }
            if (mPressure < mParams.lowWater)
            {
                if (mLowSince < 0.0)
                    mLowSince = nowSeconds;
            }
            else
                mLowSince = -1.0;

            if (mHighSamples >= 2 && !mSettling && nowSeconds - mLastReduce >= mParams.dwell)
            {
                bool anyEligible = false;
                for (std::size_t i = 0; i < mSystems.size(); ++i)
                {
                    const System& system = mSystems[i];
                    if (!canReduce(system))
                        continue;
                    anyEligible = true;
                    if (nowSeconds < system.holdUntil)
                        continue;
                    mPending = Action{ Kind::Reduce, static_cast<int>(i) };
                    mReliefExpected = std::max<std::int64_t>(0, stepBytes(static_cast<int>(i), true));
                    mTotalAtReduce = total;
                    return mPending;
                }
                if (!anyEligible && !mExhaustedReported)
                {
                    mExhaustedReported = true;
                    mPending = Action{ Kind::Exhausted, -1 };
                    return mPending;
                }
                return Action{};
            }

            if (mParams.automaticRestore && !mSettling && mLowSince >= 0.0
                && nowSeconds - mLowSince >= mParams.restoreDwell && nowSeconds - mLastAny >= mParams.dwell)
            {
                // The reverse of the order: the last listed system that is reduced.
                for (std::size_t k = mSystems.size(); k-- > 0;)
                {
                    const System& system = mSystems[k];
                    if (system.level <= 0 || system.failures >= maxFailures)
                        continue;
                    if (nowSeconds < system.holdUntil)
                        return Action{};
                    const std::int64_t cost = std::max<std::int64_t>(0, stepBytes(static_cast<int>(k), false));
                    // The no-oscillation rule: undo a step only if it still fits under high water, with a margin.
                    if (mPressure + static_cast<double>(cost) / mBudget > mParams.highWater - mParams.margin)
                        return Action{};
                    mPending = Action{ Kind::Restore, static_cast<int>(k) };
                    mRestoreCost = cost;
                    return mPending;
                }
            }
            return Action{};
        }

        /// The caller carried the action out; levels change here.
        void applied(int system, Kind kind)
        {
            if (system < 0 || static_cast<std::size_t>(system) >= mSystems.size())
                return;
            System& s = mSystems[static_cast<std::size_t>(system)];
            if (kind == Kind::Reduce)
            {
                ++s.level;
                s.failures = 0;
                mLastReduce = mNow;
                mLastAny = mNow;
                mReduceTime = mNow;
                mSettling = true;
                // The expected relief counts as done; the next samples correct it if it is not.
                if (mBudget > 0.0)
                    mPressure = std::max(0.0, mPressure - static_cast<double>(mReliefExpected) / mBudget);
                mHighSamples = 0;
            }
            else if (kind == Kind::Restore)
            {
                s.level = std::max(0, s.level - 1);
                s.failures = 0;
                mLastAny = mNow;
                // The step's cost is expected from now on, and the pressure must stay low for a whole dwell again.
                if (mBudget > 0.0)
                    mPressure += static_cast<double>(mRestoreCost) / mBudget;
                mLowSince = -1.0;
            }
            mPending = Action{};
        }

        /// The caller could not carry the action out (gated, or it failed): retry after `dwell`; after three failures
        /// the system is skipped for good.
        void notApplied(int system)
        {
            if (system < 0 || static_cast<std::size_t>(system) >= mSystems.size())
                return;
            System& s = mSystems[static_cast<std::size_t>(system)];
            ++s.failures;
            s.holdUntil = mNow + mParams.dwell;
            mPending = Action{};
        }

        int level(int system) const
        {
            if (system < 0 || static_cast<std::size_t>(system) >= mSystems.size())
                return 0;
            return mSystems[static_cast<std::size_t>(system)].level;
        }

        const std::string& name(int system) const { return mSystems[static_cast<std::size_t>(system)].info.name; }
        std::size_t systemCount() const { return mSystems.size(); }

        /// The smoothed pressure p.
        double pressure() const { return mPressure; }

        /// The sum of the levels.
        int reductions() const
        {
            int sum = 0;
            for (const System& s : mSystems)
                sum += s.level;
            return sum;
        }

        /// The last sample had no budget: the controller does nothing until one has (the caller logs it once).
        bool budgetUnknown() const { return mBudgetUnknown; }

        /// A system that failed three times is skipped for the session.
        bool skipped(int system) const
        {
            return system >= 0 && static_cast<std::size_t>(system) < mSystems.size()
                && mSystems[static_cast<std::size_t>(system)].failures >= maxFailures;
        }

    private:
        static constexpr int maxFailures = 3;

        struct System
        {
            SystemInfo info;
            int level = 0;
            int failures = 0;
            double holdUntil = -1.0;
        };

        bool canReduce(const System& system) const
        {
            if (system.failures >= maxFailures)
                return false;
            int limit = system.info.maxLevel;
            if (system.info.minimumLevel > 0)
                limit = std::min(limit, system.info.minimumLevel);
            return system.level < limit;
        }

        Params mParams;
        std::vector<System> mSystems;
        Action mPending;
        double mNow = 0.0;
        double mLastUpdate = 0.0;
        double mBudget = 0.0;
        double mPressure = 0.0;
        bool mHaveSample = false;
        bool mBudgetUnknown = false;
        int mHighSamples = 0;
        double mLowSince = -1.0;
        double mLastReduce = -1.0e18;
        double mLastAny = -1.0e18;
        bool mExhaustedReported = false;
        bool mSettling = false;
        double mReduceTime = 0.0;
        std::int64_t mReliefExpected = 0;
        std::int64_t mTotalAtReduce = 0;
        std::int64_t mRestoreCost = 0;
    };
}

#endif
