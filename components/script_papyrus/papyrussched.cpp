// Task 0093 stage d: the scheduler of the Papyrus runtime.
//
// In plain words: a latent native (Utility.Wait and relatives) does not finish at once. The host's handler says how
// many seconds of script time the call takes (or that the host will say when it is done) and the runtime parks the
// stack: its frames and values stay exactly as they are (a stack is plain data), and the other stacks go on. tick(time)
// wakes the parked stacks whose time has come, in (wake time, creation sequence) order, before it runs the events
// queued since the last tick. A stack that parks again during a tick is not woken in the same tick, even for a wait of
// zero.
//
// The parked stacks are a binary heap on (wake, seq). The runtime reads no clock: the only time it knows is the one
// tick() was given. Main thread only.

#include <algorithm>
#include <cmath>
#include <limits>

#include "papyrusvm.hpp"

namespace ScriptPapyrus::Vm
{
    namespace
    {
        constexpr std::size_t kMaxStacksPerTick = 1u << 20; ///< Stacks one tick starts, whatever the budget.

        // A "later" comparison, so that std::push_heap and friends keep the earliest (wake, seq) at the front.
        bool later(const WaitEntry& a, const WaitEntry& b)
        {
            if (a.wake != b.wake)
            {
                return a.wake > b.wake;
            }
            return a.seq > b.seq;
        }
    }

    void Runtime::reserveForPark()
    {
        // The due entries a tick took out go back if the tick stops early, so the room is for them as well.
        const std::size_t need = mWaiting.size() + (mDue.size() - mDueHead) + 1;
        if (mWaiting.capacity() < need)
        {
            ++mStats.schedulerGrowths;
        }
        mWaiting.reserve(need);
    }

    void Runtime::parkStack(Stack& stack)
    {
        // The room was made by reserveForPark() before the latent handler ran: nothing here can throw.
        stack.wake = mSuspendWake;
        mWaiting.push_back(WaitEntry{ mSuspendWake, stack.seq, &stack });
        std::push_heap(mWaiting.begin(), mWaiting.end(), later);
        ++mSuspendedCount;
        ++mStats.latentSuspended;
    }

    int Runtime::executeStack(
        Stack& stack, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed)
    {
        mSuspendRequested = false;
        if (!execute(stack, maxInstructions, maxDepth, executed))
        {
            mSuspendRequested = false;
            return 2;
        }
        if (mSuspendRequested)
        {
            mSuspendRequested = false;
            return 3;
        }
        return 0;
    }

    double Runtime::acceptTime(double scriptTime)
    {
        if (!std::isfinite(scriptTime) || scriptTime < mNow)
        {
            ++mStats.timeMovedBackwards;
            if (mLoggedOnce.insert("time-backwards").second)
            {
                worldLog(openeden::script::Severity::Warning,
                    "tick: the script time went backwards or was not a number; the last time is used instead");
            }
            return mNow;
        }
        return scriptTime;
    }

    bool Runtime::completeLatent(std::uint64_t token) noexcept
    {
        try
        {
            if (mInNative || token == 0)
            {
                return false;
            }
            for (WaitEntry& entry : mWaiting)
            {
                if (entry.seq == token)
                {
                    // Before every timer: the host's completion is the oldest news. Among completed stacks the
                    // creation order decides.
                    entry.wake = -std::numeric_limits<double>::infinity();
                    entry.stack->wake = entry.wake;
                    std::make_heap(mWaiting.begin(), mWaiting.end(), later);
                    return true;
                }
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    std::uint32_t Runtime::cancelWaiting(openeden::script::InstanceId instance) noexcept
    {
        try
        {
            if (mInNative)
            {
                return 0;
            }
            std::uint32_t dropped = 0;
            auto drops = [&](const WaitEntry& entry) {
                return entry.stack != nullptr && (instance == 0 || entry.stack->instance == instance);
            };
            // The stacks go back to the pool (releaseStack never allocates).
            for (const WaitEntry& entry : mWaiting)
            {
                if (drops(entry))
                {
                    releaseStack(entry.stack);
                    ++dropped;
                }
            }
            mWaiting.erase(std::remove_if(mWaiting.begin(), mWaiting.end(), drops), mWaiting.end());
            std::make_heap(mWaiting.begin(), mWaiting.end(), later);
            // The part of a tick's due list that has not run (empty unless a tick was left half-way by an exception).
            for (std::size_t i = mDueHead; i < mDue.size(); ++i)
            {
                if (drops(mDue[i]))
                {
                    releaseStack(mDue[i].stack);
                    mDue[i].stack = nullptr;
                    ++dropped;
                }
            }
            mSuspendedCount -= dropped;
            return dropped;
        }
        catch (...)
        {
            return 0;
        }
    }

    // ---- tick
    // ------------------------------------------------------------------------------------------------------------

    openeden::script::TickResult Runtime::tickImpl(double scriptTime, const openeden::script::Budget& budget)
    {
        using namespace openeden::script;
        TickResult result;
        if (mInNative)
        {
            result.errors.push_back(apiError(ErrorKind::Internal, "tick() cannot be used from inside a native"));
            return result;
        }
        mNow = acceptTime(scriptTime);
        const std::uint64_t maxInstructions = effectiveInstructions(budget);
        const std::uint32_t maxDepth = effectiveDepth(budget);

        // The stacks that are due, in (wake, seq) order, taken out of the heap. What does not run in this tick goes
        // back, whatever way the tick ends.
        mDue.clear();
        mDueHead = 0;
        if (mDue.capacity() < mWaiting.size())
        {
            ++mStats.schedulerGrowths;
        }
        mDue.reserve(mWaiting.size());
        while (!mWaiting.empty() && mWaiting.front().wake <= mNow)
        {
            std::pop_heap(mWaiting.begin(), mWaiting.end(), later);
            mDue.push_back(mWaiting.back());
            mWaiting.pop_back();
        }
        struct GiveBack
        {
            Runtime& mRuntime;
            ~GiveBack()
            {
                for (std::size_t i = mRuntime.mDueHead; i < mRuntime.mDue.size(); ++i)
                {
                    if (mRuntime.mDue[i].stack != nullptr)
                    {
                        // Room was kept by reserveForPark(): this cannot allocate.
                        mRuntime.mWaiting.push_back(mRuntime.mDue[i]);
                        std::push_heap(mRuntime.mWaiting.begin(), mRuntime.mWaiting.end(), later);
                    }
                }
                mRuntime.mDue.clear();
                mRuntime.mDueHead = 0;
            }
        } giveBack{ *this };

        std::size_t processed = 0;
        auto room = [&] {
            return processed < kMaxStacksPerTick && result.instructionsExecuted < mOptions.maxInstructionsPerTick;
        };
        // One stack, run (or resumed) and then finished, failed or parked again.
        auto runOne = [&](Stack* stack, bool isEvent) {
            StackLease lease(*this, stack);
            ++processed;
            std::uint64_t executed = 0;
            const int outcome = runStack(*stack, isEvent, maxInstructions, maxDepth, executed);
            ++result.stacksRun;
            ++mStats.stacksRun;
            result.instructionsExecuted += executed;
            if (outcome == 3)
            {
                parkStack(*stack);
                lease.keep();
                return;
            }
            ++result.stacksFinished;
            ++mStats.stacksFinished;
            if (outcome == 2)
            {
                result.errors.push_back(takeError());
            }
        };

        while (mDueHead < mDue.size() && room())
        {
            Stack* stack = mDue[mDueHead++].stack;
            if (stack == nullptr)
            {
                continue; // cancelled
            }
            --mSuspendedCount;
            ++mStats.stacksWoken;
            runOne(stack, false);
        }
        while (mReadyHead < mReady.size() && room())
        {
            Stack* stack = mReady[mReadyHead++];
            runOne(stack, true);
        }

        std::uint32_t dueLeft = 0;
        for (std::size_t i = mDueHead; i < mDue.size(); ++i)
        {
            if (mDue[i].stack != nullptr)
            {
                ++dueLeft;
            }
        }
        if (mReadyHead >= mReady.size())
        {
            mReady.clear();
            mReadyHead = 0;
        }
        else if (mReadyHead >= 64 && mReadyHead * 2 >= mReady.size())
        {
            // Under sustained overload the queue never drains: give back the slots already consumed.
            mReady.erase(mReady.begin(), mReady.begin() + static_cast<std::ptrdiff_t>(mReadyHead));
            mReadyHead = 0;
        }
        result.stacksWaiting = static_cast<std::uint32_t>(mReady.size() - mReadyHead) + dueLeft;
        mStats.readyQueueSlots = mReady.size();
        return result;
    }
}
