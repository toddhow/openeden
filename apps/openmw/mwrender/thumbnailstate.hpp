#ifndef OPENEDEN_MWRENDER_THUMBNAILSTATE_H
#define OPENEDEN_MWRENDER_THUMBNAILSTATE_H

#include <cstdint>
#include <optional>
#include <utility>

namespace MWRender
{
    /*
     * The state of the save thumbnail's request, shared between RenderManager's request and retrieve (called by
     * MWState::SaveGame::run on successive frames) and the callbacks of Render::FrameCapture (OpenEden, 2026-11-04,
     * round four, findings 5 and 6). Pure, so it can be tested without a device. Main thread only: the capture calls
     * its callbacks from Engine::loop, and SaveGame::run runs in the loop callback.
     *
     * Each request has a generation. A callback carries its request's generation and is ignored when it is no longer
     * the current one: a save that is abandoned between request and retrieve leaves a callback in the capture, which
     * would otherwise write an older frame over the next save's state.
     *
     * Copy is whatever the caller reads the picture from (RenderManager: a function that reads it).
     */
    template <class Copy>
    class ThumbnailState
    {
    public:
        struct Result
        {
            bool ready = false;
            std::optional<Copy> copy; // empty: no picture (failed, or nothing was requested)
        };

        // A new request: whatever was waiting or unread is abandoned. Returns the generation for its callbacks.
        std::uint64_t begin()
        {
            mPrevious = mCurrent;
            mCurrent = Request{ ++mLastGeneration, Status::Waiting, {} };
            return mCurrent.generation;
        }

        // FrameCapture::request answered that a request is already waiting for its frame, so it merged this one into
        // it and dropped this one's callbacks: the waiting request's callbacks, of the generation before, answer. Call
        // right after the begin() whose request was refused for that reason.
        void merged() { mCurrent = mPrevious; }

        void delivered(std::uint64_t generation, Copy copy)
        {
            if (generation != mCurrent.generation || mCurrent.status != Status::Waiting)
                return;
            mCurrent.status = Status::Ready;
            mCurrent.copy = std::move(copy);
        }

        void failed(std::uint64_t generation)
        {
            if (generation != mCurrent.generation || mCurrent.status != Status::Waiting)
                return;
            mCurrent.status = Status::Ready;
            mCurrent.copy.reset();
        }

        bool waiting() const { return mCurrent.status == Status::Waiting; }

        // Not ready while the frame is awaited. Otherwise the picture, if there is one, and nothing is requested any
        // more: the next call is ready with no picture (what the code before answered when nothing had been asked).
        Result retrieve()
        {
            if (mCurrent.status == Status::Waiting)
                return {};
            Result result{ true, std::move(mCurrent.copy) };
            mCurrent = Request{ mCurrent.generation, Status::Idle, {} };
            return result;
        }

    private:
        enum class Status
        {
            Idle,
            Waiting,
            Ready,
        };
        struct Request
        {
            std::uint64_t generation = 0;
            Status status = Status::Idle;
            std::optional<Copy> copy;
        };

        Request mCurrent;
        Request mPrevious;
        std::uint64_t mLastGeneration = 0;
    };
}

#endif
