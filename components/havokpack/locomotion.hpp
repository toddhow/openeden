#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_LOCOMOTION_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_LOCOMOTION_H

// Task 0148 (M-S3, locomotion): the maths that turns a measured ground speed into a blend of the standing idle, the
// walk cycle and the run cycle, and the playback time of the cycles. Pure; standard library only. Formats layer.
//
// The cycles are driven by ONE shared phase p in [0, 1) that advances at (ground speed / stride) cycles a second, where
// the stride is the distance one cycle of the clip covers according to its own root motion (anims_default*.txt, task
// 0119); the walk and the run are a hard switch (wantRun, hysteresis) with a short linear cross-fade (advanceRunMix),
// so only the cycle being faded stride-mixes. A clip is sampled at fract(p + leftFoot) of its
// duration, so the left foot lands at p = 0 in both clips and a mix of the two keeps the feet together. The clips'
// root motion is only used to get the stride; it is never applied to the body.
//
// Every function allocates nothing and has no state; callers may use them from any thread.

#include "skeleton.hpp" // Transform

namespace HavokPack::Gait
{
    struct Params
    {
        // The ground speeds (units a second) the walk and run clips cover by their own root motion, which are also the
        // speeds of the mover's walk and run (NeutralPath::kWalkSpeed / kRunSpeed, task 0148): measured on the user's
        // own files, MT_WalkForward 93.448 units in 1.13333 s and MT_RunForward 222.219 units in 0.633333 s.
        float mWalkSpeed = 82.45f;
        float mRunSpeed = 350.87f;
        // Units one cycle covers (the same measurement; male and female agree to 0.02 percent).
        float mWalkStride = 93.448f;
        float mRunStride = 222.219f;
        // Where in the clip (fraction of its duration) the left foot lands: FootLeft 0.2 s of 1.13333 s, 0 of 0.633333
        // s.
        float mWalkLeftFoot = 0.176471f;
        float mRunLeftFoot = 0.f;
        // The idle fades out between these speeds (below mStopSpeed the actor only idles).
        float mStopSpeed = 5.f;
        float mFullSpeed = 30.f;
        // Time constant of the smoothing of the measured speed (seconds).
        float mSmoothTau = 0.12f;
        // One gait at a time: the run starts above the speed half way between the clips' speeds plus mSwitchBand (a
        // share of it), the walk returns below it minus the band; in between the gait stays as it was (no flicker). The
        // change is a linear cross-fade of mCrossFade seconds (12 frames at 60 a second).
        float mSwitchBand = 0.1f;
        float mCrossFade = 0.2f;
    };

    struct Weights
    {
        float mIdle = 1.f;
        float mWalk = 0.f;
        float mRun = 0.f;
        float mCyclesPerSecond = 0.f;
    };

    // The blend for a ground speed (a non-finite or negative speed counts as 0) and the run mix, 0 for the walk cycle
    // and 1 for the run cycle (clamped; the cross-fade moves it, see advanceRunMix). The three weights add up to 1. The
    // speed never mixes the gaits by itself.
    Weights weights(float speed, float runMix, const Params& params = {});

    // Whether the actor should be running at `speed`, given whether it was: a hysteresis band round the half way speed.
    bool wantRun(float speed, bool wasRunning, const Params& params = {});

    // The run mix moved towards 1 (run) or 0 (walk) by dt / crossFade, clamped. dt <= 0 or not finite keeps it;
    // crossFade <= 0 gives the target at once.
    float advanceRunMix(float runMix, bool run, float dt, float crossFade);

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
