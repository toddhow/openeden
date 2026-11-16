// Task 0254 (dead stays dead across a load): the combat side. A saved game said an actor was dead; the actor is built
// again from the pack alive, so this puts it back the way its death left it.

#include <game_skyrimse/combat.hpp>

#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
        /// The death clip is played this many times faster than its own speed, so that a body lies in the clip's last
        /// pose a moment after the load instead of falling again in front of the player. 10 is the fastest rate the
        /// core accepts (kMaxClipSpeed of NeutralCombatBridge).
        constexpr float kRestoredDeathClipSpeed = 10.f;
    }

    bool SkyrimCombat::readView(ActorHandle handle, FighterView& out)
    {
        out = {};
        Fighter* fighter = find(CombatSubject::of(handle));
        // ready() reads the actor's record once and remembers a miss for good: not before the records are read
        if (fighter == nullptr || fighter->mSubject.player || !dataReady() || !ready(*fighter))
            return false;
        return fighterView(fighter->mSubject, out);
    }

    bool SkyrimCombat::restoreDead(CombatRequestWriter& writer, ActorHandle handle)
    {
        Fighter* fighter = find(CombatSubject::of(handle));
        if (fighter == nullptr || fighter->mSubject.player || !dataReady() || !ready(*fighter))
            return false;
        fighter->mHealth = 0.f;
        fighter->mDead = true;
        fighter->mStaggered = false;
        fighter->mSwinging = false;
        // The same clip the death of this fighter plays (the stagger-back clip lays a person down; a creature has a
        // death clip of its own); the core holds its last pose.
        std::string_view clip = clipOf(*fighter, ClipSlot::Death);
        if (clip.empty())
            clip = meleeClip(ClipSlot::Stagger, *fighter, false);
        if (!clip.empty())
        {
            ClipRequest request;
            request.slot = ClipSlot::Death;
            request.clip = clip;
            request.blendSeconds = 0.f;
            request.token = mNextToken++;
            if (writer.playClip(fighter->mSubject, request))
                (void)writer.setClipSpeed(fighter->mSubject, kRestoredDeathClipSpeed);
        }
        (void)writer.reportDeath(handle);
        if (mTrace)
            trace("COMBAT restored dead actor " + std::to_string(handle.reference));
        return true;
    }
}
