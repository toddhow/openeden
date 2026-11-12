#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_LOCOMOTION_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_LOCOMOTION_H

// Task 0148 (M-S3, locomotion): the maths that turns a measured ground speed into a blend of the standing idle, the
// walk cycle and the run cycle, and the playback time of the cycles. Pure; standard library only. Formats layer.
//
// The cycles are driven by ONE shared phase p in [0, 1) that advances at (ground speed / stride) cycles a second, where
// the stride is the distance one cycle of the clip covers according to its own root motion (anims_default*.txt, task
// 0119) and is mixed between the walk's and the run's by the gait mix. A clip is sampled at fract(p + leftFoot) of its
// duration, so the left foot lands at p = 0 in both clips and a mix of the two keeps the feet together. The clips'
// root motion is only used to get the stride; it is never applied to the body.
//
// Every function allocates nothing and has no state; callers may use them from any thread.

#include "skeleton.hpp" // Transform

namespace HavokPack::Gait
{
    struct Params
    {
        // The ground speeds (units a second) at which the mix is pure walk and pure run: the mover's own speeds
        // (NeutralPath::kWalkSpeed / kRunSpeed, estimates of task 0135).
        float mWalkSpeed = 120.f;
        float mRunSpeed = 280.f;
        // Units one cycle covers, from the clips' root motion (male and female agree to 0.02 percent): MT_WalkForward
        // 93.448 units in 1.13333 s (82.45 u/s), MT_RunForward 222.219 units in 0.633333 s (350.87 u/s).
        float mWalkStride = 93.448f;
        float mRunStride = 222.219f;
        // Where in the clip (fraction of its duration) the left foot lands: FootLeft 0.2 s of 1.13333 s, 0 of 0.633333 s.
        float mWalkLeftFoot = 0.176471f;
        float mRunLeftFoot = 0.f;
        // The idle fades out between these speeds (below mStopSpeed the actor only idles).
        float mStopSpeed = 5.f;
        float mFullSpeed = 30.f;
        // Time constant of the smoothing of the measured speed (seconds).
        float mSmoothTau = 0.12f;
    };

    struct Weights
    {
        float mIdle = 1.f;
        float mWalk = 0.f;
        float mRun = 0.f;
        float mCyclesPerSecond = 0.f;
    };

    // The blend for a ground speed (a non-finite or negative speed counts as 0). The three weights add up to 1.
    Weights weights(float speed, const Params& params = {});

    // `current` moved towards `target` by an exponential of time constant `tau` over `dt` seconds. dt <= 0 or not
    // finite keeps `current`; tau <= 0 gives `target`.
    float smoothSpeed(float current, float target, float dt, float tau);

    // phase + cyclesPerSecond * dt wrapped into [0, 1). A negative or non-finite rate or dt leaves the phase alone.
    float advancePhase(float phase, float cyclesPerSecond, float dt);

    // The clip time in seconds for the shared phase: fract(phase + leftFoot) * duration.
    float clipTime(float phase, float leftFoot, float duration);

    // Linear mix of two bone transforms (translation and scale linearly, rotation by normalised linear interpolation
    // along the shorter arc). t is clamped to [0, 1].
    Transform blend(const Transform& a, const Transform& b, float t);
}

#endif
