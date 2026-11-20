#ifndef OPENMW_MWWORLD_NEUTRALPLAYERSPEED_H
#define OPENMW_MWWORLD_NEUTRALPLAYERSPEED_H

// Task 0361e follow-up (toddhow, 2026-11-20): the player of a Skyrim (neutral world) session walks and runs at the speeds
// of the Skyrim walk and run clips, the same numbers the neutral actors' mover uses, so the clips' cycles play at their
// own rate and the feet do not slide. Morrowind's attribute based speeds (fMoveBase and the Speed attribute: the walk
// about 149 and the run about 272 units a second for the stand-in player) stay for every other session.
//
// Pure: the caller says whether the rule applies. Main thread only (it is called by Class::getMaxSpeed).

#include <apps/openmw/mwphysics/neutralpathfollower.hpp>

namespace MWWorld
{
    /// The ground speed of `isSkyrimPlayer` (a neutral world session, the setting on, the actor is the player): the clip
    /// speed of the walk or the run, but never more than 0 for a paralysed, encumbered or dead actor (`original` 0).
    inline float neutralPlayerSpeed(bool neutralWorld, bool settingOn, bool running, float original)
    {
        if (!neutralWorld || !settingOn || !(original > 0.f))
            return original;
        return running ? MWPhysics::NeutralPath::kRunSpeed : MWPhysics::NeutralPath::kWalkSpeed;
    }
}

#endif
