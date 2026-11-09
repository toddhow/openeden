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

        // The heap of parked stacks is a binary min-heap on (wake, seq). It is written by hand, and not with
        // std::push_heap and friends, because every entry's position is kept in its stack (Stack::heapPos): a
        // completion then finds its entry from the token in one step and moves it up in O(log n).
        bool earlier(const WaitEntry& a, const WaitEntry& b)
        {
            if (a.wake != b.wake)
            {
                return a.wake < b.wake;
            }
            return a.seq < b.seq;
        }

        void place(std::vector<WaitEntry>& heap, std::size_t index)
        {
            heap[index].stack->heapPos = static_cast<std::uint32_t>(index);
        }

        void siftUp(std::vector<WaitEntry>& heap, std::size_t index)
        {
            while (index > 0)
            {
                const std::size_t parent = (index - 1) / 2;
                if (!earlier(heap[index], heap[parent]))
                {
                    break;
                }
                std::swap(heap[index], heap[parent]);
                place(heap, index);
                index = parent;
            }
            place(heap, index);
        }

        void siftDown(std::vector<WaitEntry>& heap, std::size_t index)
        {
            const std::size_t size = heap.size();
            for (;;)
            {
                std::size_t best = index;
                const std::size_t left = 2 * index + 1;
                const std::size_t right = left + 1;
                if (left < size && earlier(heap[left], heap[best]))
                {
                    best = left;
                }
                if (right < size && earlier(heap[right], heap[best]))
                {
                    best = right;
                }
                if (best == index)
                {
                    break;
                }
                std::swap(heap[index], heap[best]);
                place(heap, index);
                index = best;
            }
            place(heap, index);
        }

        /// The new last entry takes its place. Never allocates (the room was made before).
        void pushHeap(std::vector<WaitEntry>& heap)
        {
            siftUp(heap, heap.size() - 1);
        }

        /// Takes the earliest entry out of the heap and returns it; its stack is no longer in the heap.
        WaitEntry popHeap(std::vector<WaitEntry>& heap)
        {
            const WaitEntry first = heap.front();
            first.stack->heapPos = Stack::kNotInHeap;
            heap.front() = heap.back();
            heap.pop_back();
            if (!heap.empty())
            {
                siftDown(heap, 0);
            }
            return first;
        }

        void rebuildHeap(std::vector<WaitEntry>& heap)
        {
            for (std::size_t i = 0; i < heap.size(); ++i)
            {
                place(heap, i);
            }
            for (std::size_t i = heap.size() / 2; i-- > 0;)
            {
                siftDown(heap, i);
            }
        }

        /// Room for `need` entries by doubling, so that a long series of parks copies the list O(log n) times.
        /// Returns true when it had to grow.
        bool growTo(std::vector<WaitEntry>& list, std::size_t need)
        {
            if (list.capacity() >= need)
            {
                return false;
            }
            list.reserve(std::max(need, 2 * list.capacity()));
            return true;
        }
    }

    void Runtime::reserveForPark()
    {
        // The due entries a tick took out go back if the tick stops early, so the room is for them as well.
        if (growTo(mWaiting, mWaiting.size() + (mDue.size() - mDueHead) + 1))
        {
            ++mStats.schedulerGrowths;
        }
    }

    void Runtime::parkStack(Stack& stack)
    {
        // The room was made by reserveForPark() before the latent handler ran: nothing here can throw.
        stack.wake = mSuspendWake;
        stack.hostWait = mSuspendHost;
        mWaiting.push_back(WaitEntry{ mSuspendWake, stack.seq, &stack });
        pushHeap(mWaiting);
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
        if (mInNative || token == 0)
        {
            return false;
        }
        // The token names the stack (pool index in the high half) and the park (the low half): a token from an
        // earlier wait of the same stack does not match, and neither does one of a stack that is not in the heap.
        const std::uint64_t slot = (token >> 32);
        if (slot == 0 || slot > mStackPool.size())
        {
            return false;
        }
        Stack& stack = *mStackPool[static_cast<std::size_t>(slot - 1)];
        if (stack.parkToken != token || stack.heapPos == Stack::kNotInHeap || stack.heapPos >= mWaiting.size()
            || mWaiting[stack.heapPos].stack != &stack)
        {
            return false;
        }
        // Before every timer: the host's completion is the oldest news. Among completed stacks the creation order
        // decides. The key only gets smaller, so the entry only moves up.
        stack.wake = -std::numeric_limits<double>::infinity();
        stack.hostWait = false;
        mWaiting[stack.heapPos].wake = stack.wake;
        siftUp(mWaiting, stack.heapPos);
        return true;
    }

    std::uint32_t Runtime::cancelWaiting(openeden::script::InstanceId instance) noexcept
    {
        try
        {
            if (mInNative)
            {
                return 0;
            }
            auto drops = [&](const WaitEntry& entry) {
                return entry.stack != nullptr && (instance == 0 || entry.stack->instance == instance);
            };
            // The dropped entries are sorted out first; only then do their stacks go back to the pool (releaseStack
            // never allocates), so nothing reads a stack that is already in the free list.
            const auto keptEnd = std::stable_partition(
                mWaiting.begin(), mWaiting.end(), [&](const WaitEntry& entry) { return !drops(entry); });
            std::uint32_t dropped = 0;
            for (auto it = keptEnd; it != mWaiting.end(); ++it)
            {
                it->stack->heapPos = Stack::kNotInHeap;
                releaseStack(it->stack);
                ++dropped;
            }
            mWaiting.erase(keptEnd, mWaiting.end());
            rebuildHeap(mWaiting);
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
        // With latent natives off nothing uses the time: it is not looked at, so no warning and no counter.
        if (mOptions.runLatentNatives)
        {
            mNow = acceptTime(scriptTime);
        }
        const std::uint64_t maxInstructions = effectiveInstructions(budget);
        const std::uint32_t maxDepth = effectiveDepth(budget);

        // The stacks that are due, in (wake, seq) order, taken out of the heap. What does not run in this tick goes
        // back, whatever way the tick ends.
        mDue.clear();
        mDueHead = 0;
        if (growTo(mDue, mWaiting.size()))
        {
            ++mStats.schedulerGrowths;
        }
        while (!mWaiting.empty() && mWaiting.front().wake <= mNow)
        {
            mDue.push_back(popHeap(mWaiting));
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
                        pushHeap(mRuntime.mWaiting);
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
            if (stack->hostWait && stack->wake != -std::numeric_limits<double>::infinity())
            {
                ++mStats.hostWaitsExpired; // the host never completed it: it woke by timeout
            }
            stack->hostWait = false;
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
