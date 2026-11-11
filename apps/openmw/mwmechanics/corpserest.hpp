#ifndef OPENEDEN_MWMECHANICS_CORPSEREST_H
#define OPENEDEN_MWMECHANICS_CORPSEREST_H

#include <cmath>

namespace MWMechanics
{
    /// Task 0106, second slice (3). A dead NPC's ragdoll lies where it came to rest; the actor itself (its capsule,
    /// its saved position, the bounds its model is culled with) still stands where it died. Once every body is asleep
    /// the actor is moved to the ragdoll's resting place, once.
    constexpr double cCorpseMinShiftMetres = 0.35; // closer than this the actor stays where it is
    constexpr double cCorpseMaxShiftMetres = 40.0; // farther than this the ragdoll is taken for a glitch

    struct CorpseRestInput
    {
        bool mSettled = false; // every ragdoll body is asleep
        bool mAlreadyDone = false; // this corpse was handled before
        double mActor[3] = { 0.0, 0.0, 0.0 }; // the actor's position (its feet), game units
        double mPelvis[3] = { 0.0, 0.0, 0.0 }; // the ragdoll pelvis bone's position, game units
        bool mHasGround = false; // a ray down from the pelvis hit the world
        double mGroundZ = 0.0; // where it hit
        double mUnitsPerMeter = 69.99125109;
    };

    struct CorpseRestDecision
    {
        bool mDone = false; // the corpse needs no further look
        bool mMove = false; // move the actor to mPosition
        double mPosition[3] = { 0.0, 0.0, 0.0 };
    };

    /// Pure. While the ragdoll still moves nothing is decided; once settled the corpse is done, and the actor moves to
    /// the pelvis' x and y at the ground below it unless that is under cCorpseMinShiftMetres in both directions, more
    /// than cCorpseMaxShiftMetres in either, or no ground was found.
    inline CorpseRestDecision decideCorpseRest(const CorpseRestInput& in)
    {
        CorpseRestDecision out;
        if (in.mAlreadyDone)
        {
            out.mDone = true;
            return out;
        }
        if (!in.mSettled)
            return out;
        out.mDone = true;
        if (!in.mHasGround)
            return out;
        const double dx = in.mPelvis[0] - in.mActor[0];
        const double dy = in.mPelvis[1] - in.mActor[1];
        const double horizontal = std::sqrt(dx * dx + dy * dy);
        const double vertical = std::abs(in.mActor[2] - in.mGroundZ);
        const double minShift = cCorpseMinShiftMetres * in.mUnitsPerMeter;
        const double maxShift = cCorpseMaxShiftMetres * in.mUnitsPerMeter;
        if (horizontal > maxShift || vertical > maxShift)
            return out;
        if (horizontal < minShift && vertical < minShift)
            return out;
        out.mMove = true;
        out.mPosition[0] = in.mPelvis[0];
        out.mPosition[1] = in.mPelvis[1];
        out.mPosition[2] = in.mGroundZ;
        return out;
    }
}

#endif
