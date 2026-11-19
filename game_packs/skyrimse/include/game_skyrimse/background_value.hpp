#ifndef OPENEDEN_GAME_SKYRIMSE_BACKGROUND_VALUE_H
#define OPENEDEN_GAME_SKYRIMSE_BACKGROUND_VALUE_H

// Task 0355 stage c follow-up: one value built ONCE on a worker thread and picked up without ever blocking the caller.
// The weather records of the plugins are read this way (the first cell image space lookup used to read them on the main
// thread: about 1.8 s). Owned by one thread (the main thread): start, ready, tryGet and take are not for two threads.

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace game_skyrimse
{
    template <class T>
    class BackgroundValue
    {
    public:
        BackgroundValue() = default;
        BackgroundValue(const BackgroundValue&) = delete;
        BackgroundValue& operator=(const BackgroundValue&) = delete;
        ~BackgroundValue()
        {
            if (mThread.joinable())
                mThread.join();
        }

        /// Starts the build unless it was started before. Returns at once.
        void start(std::function<T()> build)
        {
            if (mStarted)
                return;
            mStarted = true;
            mThread = std::thread([this, build = std::move(build)]() {
                mValue.emplace(build());
                mReady.store(true, std::memory_order_release);
            });
        }

        [[nodiscard]] bool started() const { return mStarted; }

        /// True when the value is built and nothing needs to wait for it. Never blocks.
        [[nodiscard]] bool ready() const { return mReady.load(std::memory_order_acquire); }

        /// The value when it is ready, else null. Never blocks.
        [[nodiscard]] T* tryGet() { return ready() ? &*mValue : nullptr; }

        /// The value; waits for the build when it is still running (the old, blocking behaviour). Null if never started.
        [[nodiscard]] T* take()
        {
            if (!mStarted)
                return nullptr;
            if (mThread.joinable())
                mThread.join();
            return &*mValue;
        }

    private:
        bool mStarted = false;
        std::thread mThread;
        std::atomic<bool> mReady{ false };
        std::optional<T> mValue;
    };
}

#endif
