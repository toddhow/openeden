#pragma once

#include <openeden/game/bindings.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace openeden::extension
{
    struct BoundFunction;

    /**
     * @brief The Mutate calls scripts made and the main thread has not run yet (task 0097, stage B).
     *
     * A call is validated by the caller, copied here (strings and array elements into a fixed arena) and run later by
     * drain(), on the main thread, in the order the calls were pushed. Nothing here allocates after reserve(): when
     * the call slots or the arena are full the call is dropped and counted against its function, and the next drain
     * logs it once per function.
     *
     * push() may be called from any thread (the script thread); drain() from the main thread. The two use a swap of
     * two batches under a mutex, so a handler never runs under the lock and a push during a drain waits for the next
     * one. Owned by BindingsStore; the pointers to BoundFunction stay valid because the store never moves them.
     */
    class MutateQueue
    {
    public:
        enum class PushResult
        {
            Queued,
            Dropped,
        };

        struct DrainResult
        {
            std::size_t ran = 0; ///< Handlers called (Ok or not)
            std::size_t failed = 0; ///< Handlers that answered with a status other than Ok, or leaked an exception
            std::size_t retired = 0; ///< Calls dropped because the pack that declared them is gone
            std::size_t functionsWithDrops = 0; ///< Functions that lost calls to a full queue since the last drain
        };

        MutateQueue(std::size_t maxCalls = openeden::sdk::kMaxBindingQueuedCalls, std::size_t arenaBytes = 1u << 20,
            std::size_t arenaValues = 8192);
        ~MutateQueue();
        MutateQueue(const MutateQueue&) = delete;
        MutateQueue& operator=(const MutateQueue&) = delete;

        /// Allocates both batches. Idempotent. A queue that was never reserved drops everything.
        void reserve();

        /// `args` were checked against the function's declaration by the caller.
        [[nodiscard]] PushResult push(BoundFunction& function, std::span<const openeden::sdk::BindingValue> args);

        /// Runs every waiting call in the order pushed. Main thread only.
        DrainResult drain();

        [[nodiscard]] std::size_t pending() const;
        [[nodiscard]] std::uint64_t droppedTotal() const;

    private:
        struct Entry
        {
            BoundFunction* function = nullptr;
            std::uint8_t argc = 0;
            openeden::sdk::BindingValue args[openeden::sdk::kMaxBindingParameters];
        };

        struct Batch
        {
            std::unique_ptr<Entry[]> entries;
            std::size_t entryCount = 0;
            std::unique_ptr<char[]> bytes;
            std::size_t byteCount = 0;
            std::unique_ptr<openeden::sdk::BindingValue[]> values;
            std::size_t valueCount = 0;
            void clear() noexcept
            {
                entryCount = 0;
                byteCount = 0;
                valueCount = 0;
            }
        };

        std::size_t mMaxCalls;
        std::size_t mArenaBytes;
        std::size_t mArenaValues;
        mutable std::mutex mMutex;
        Batch mBatches[2];
        int mActive = 0;
        std::vector<BoundFunction*> mDropped; ///< Functions that lost calls since the last drain (reserved)
        bool mReserved = false;
        std::uint64_t mDroppedTotal = 0;
    };
}
