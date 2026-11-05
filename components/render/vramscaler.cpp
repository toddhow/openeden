#include "vramscaler.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace Render::VideoMemory
{
    namespace
    {
        constexpr double mebibyte = 1024.0 * 1024.0;
    }

    Scaler::Scaler(ScalerConfig config, std::vector<Adapter> adapters, std::function<Sample()> source, LogFn log)
        : mConfig(std::move(config))
        , mAdapters(std::move(adapters))
        , mSource(std::move(source))
        , mLog(std::move(log))
    {
        for (const std::string& message : clampParams(mConfig.params))
            note(true, "[VideoMemory] " + message);

        std::vector<std::string> known;
        for (const Adapter& adapter : mAdapters)
            known.push_back(adapter.name);
        const ParsedOrder parsed = parseOrder(mConfig.order, known);
        for (const std::string& problem : parsed.problems)
            note(true, "[VideoMemory] order: " + problem);

        std::vector<SystemInfo> systems;
        for (const std::string& name : parsed.names)
        {
            for (std::size_t i = 0; i < mAdapters.size(); ++i)
            {
                if (mAdapters[i].name != name)
                    continue;
                SystemInfo info;
                info.name = name;
                info.maxLevel = mAdapters[i].maxLevel;
                systems.push_back(std::move(info));
                mAdapterOfSystem.push_back(static_cast<int>(i));
                break;
            }
        }
        if (systems.empty())
        {
            note(true, "[VideoMemory] none of the systems in 'order' can be reduced here; nothing will be scaled back");
            return;
        }
        mController = std::make_unique<Controller>(std::move(systems), mConfig.params);
        mSecondsAtCount.assign(1, 0.0);
    }

    void Scaler::note(bool warning, const std::string& text) const
    {
        if (mLog)
            mLog(warning, text);
    }

    void Scaler::tick(double dt)
    {
        if (!mController)
            return;
        mTime += dt;
        if (mTime < mNextSample)
            return;
        mNextSample = mTime + mConfig.sampleInterval;
        sampleNow();
    }

    void Scaler::sampleNow()
    {
        // Time spent at the current reduction count, for the report.
        if (mHaveSampleTime)
        {
            const std::size_t count = static_cast<std::size_t>(mController->reductions());
            if (mSecondsAtCount.size() <= count)
                mSecondsAtCount.resize(count + 1, 0.0);
            mSecondsAtCount[count] += mTime - mLastSampleTime;
        }
        mLastSampleTime = mTime;
        mHaveSampleTime = true;

        const Sample raw = mSource();
        Sample sample;
        sample.usageBytes = mConfig.useDriverUsage ? raw.usageBytes : -1;
        double budgetBytes = -1.0;
        if (!mConfig.budget.autoBudget)
            budgetBytes = static_cast<double>(mConfig.budget.mib) * mebibyte;
        else if (raw.budgetBytes > 0)
            budgetBytes = static_cast<double>(raw.budgetBytes);
        else if (mConfig.fallbackBudgetBytes > 0)
        {
            budgetBytes = static_cast<double>(mConfig.fallbackBudgetBytes);
            if (!mLoggedFallback)
            {
                mLoggedFallback = true;
                note(false,
                    "[VideoMemory] the driver gives no memory budget (VK_EXT_memory_budget); using the device-local "
                    "memory times budget fraction");
            }
        }
        if (budgetBytes > 0.0)
            sample.budgetBytes = static_cast<std::int64_t>(budgetBytes * mConfig.budgetFraction);

        for (const int index : mAdapterOfSystem)
            if (mAdapters[static_cast<std::size_t>(index)].account)
                mAdapters[static_cast<std::size_t>(index)].account(mLedger);

        const std::int64_t total = mLedger.total();
        mAccountedMiB = static_cast<double>(total) / mebibyte;
        mUsedMiB = static_cast<double>(std::max<std::int64_t>(total, sample.usageBytes)) / mebibyte;
        mBudgetMiB = sample.budgetBytes > 0 ? static_cast<double>(sample.budgetBytes) / mebibyte : -1.0;

        const Action action = mController->update(mTime, sample, mLedger, [this](int system, bool down) {
            const Adapter& adapter
                = mAdapters[static_cast<std::size_t>(mAdapterOfSystem[static_cast<std::size_t>(system)])];
            if (!adapter.stepBytes)
                return std::int64_t{ 0 };
            return adapter.stepBytes(mController->level(system), down);
        });
        mHighestPressure = std::max(mHighestPressure, mController->pressure());

        if (mController->budgetUnknown())
        {
            if (!mLoggedUnknownBudget)
            {
                mLoggedUnknownBudget = true;
                note(true, "[VideoMemory] the memory budget is unknown, so nothing will be scaled back");
            }
            return;
        }

        const auto describe = [&] {
            std::ostringstream out;
            out << "(used " << static_cast<long long>(mUsedMiB) << " MiB of budget "
                << static_cast<long long>(mBudgetMiB) << " MiB)";
            return out.str();
        };

        if (action.kind == Kind::Exhausted)
        {
            note(true, "VRAM scale-back: video memory is full and nothing is left to reduce " + describe());
            return;
        }
        if (action.kind != Kind::Reduce && action.kind != Kind::Restore)
            return;

        Adapter& adapter
            = mAdapters[static_cast<std::size_t>(mAdapterOfSystem[static_cast<std::size_t>(action.system)])];
        const int level = mController->level(action.system);
        const int target = action.kind == Kind::Reduce ? level + 1 : level - 1;
        bool done = false;
        if (adapter.apply)
            done = adapter.apply(target);
        if (!done)
        {
            mController->notApplied(action.system);
            note(false,
                "VRAM scale-back: could not " + std::string(action.kind == Kind::Reduce ? "reduce " : "restore ")
                    + adapter.name + " to step " + std::to_string(target) + ", will retry");
            return;
        }
        mController->applied(action.system, action.kind);
        if (action.kind == Kind::Reduce)
        {
            ++mStepsDown;
            note(false,
                "VRAM scale-back: reduced " + adapter.name + " to step " + std::to_string(target) + " " + describe());
        }
        else
        {
            ++mStepsUp;
            note(false,
                "VRAM scale-back: restored " + adapter.name + " to step " + std::to_string(target) + " " + describe());
        }
    }

    std::string Scaler::reportText() const
    {
        std::ostringstream out;
        out << "VRAM scale-back report: " << mStepsDown << " steps down, " << mStepsUp << " steps up, highest pressure "
            << mHighestPressure << "; seconds at 0.." << (mSecondsAtCount.empty() ? 0 : mSecondsAtCount.size() - 1)
            << " active reductions:";
        for (const double seconds : mSecondsAtCount)
            out << " " << static_cast<long long>(seconds);
        return out.str();
    }
}
