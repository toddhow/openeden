#ifndef OPENEDEN_COMPONENTS_FACE_FACESAMPLE_H
#define OPENEDEN_COMPONENTS_FACE_FACESAMPLE_H

// OpenEden facial speech animation, task 0071 stage e (the part that needs no engine): sampling curves at a voice
// stream's position and reducing them to what a stock Morrowind head can show (a talk amount and a blink amount, the
// two channels MWAnim::Face has). Allocation-free after construction; main thread; no engine type.
//
// OFF BY DEFAULT is the caller's job: nothing here is reached unless the engine's setting is on (the engine side is
// not wired yet; see the task's stage log). With no curves the driver answers "nothing" and the caller keeps today's
// loudness drive, byte for byte.

#include "facecurves.hpp"

#include <cstddef>
#include <optional>

namespace Face
{
    /// Linear interpolation between frames. Returns false when `seconds` is outside [0, frameCount / frameRate) or
    /// `outCount` is smaller than the channel count. `out` receives one value per channel: weights 0..1, signed
    /// channels -1..1.
    bool sampleAt(const Curves& curves, double seconds, float* out, std::size_t outCount);

    struct Reduction
    {
        float mTalk = 0.f; // 0..1, replaces the loudness drive's input of Face::update
        float mBlink = 0.f; // 0..1
    };

    /// Tuning of the stock-head reduction. INITIAL VALUES, chosen by me, not measured on a real head: they await
    /// toddhow's look in a frame (task 0071, stage e).
    struct StockHeadTuning
    {
        float mJawOpen = 1.0f;
        float mMouthFunnel = 0.5f;
        float mMouthPucker = 0.25f;
    };

    class Driver
    {
    public:
        /// The curves must outlive the driver. Resolves the channel indices once (no allocation in update).
        explicit Driver(const Curves& curves, StockHeadTuning tuning = {});
        /// Talk and blink at the stream position, or nothing outside the curves.
        std::optional<Reduction> update(double streamSeconds) const;

    private:
        const Curves* mCurves;
        StockHeadTuning mTuning;
        int mJaw = -1, mFunnel = -1, mPucker = -1, mBlinkLeft = -1, mBlinkRight = -1;
    };
}

#endif
