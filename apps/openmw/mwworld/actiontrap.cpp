#include "actiontrap.hpp"
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include "../mwmechanics/spellcasting.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

namespace MWWorld
{
    void ActionTrap::executeImp(const Ptr& actor)
    {
        vsg::vec3 actorPosition(actor.getRefData().getPosition().asVec3());
        vsg::vec3 trapPosition(mTrapSource.getRefData().getPosition().asVec3());
        float trapRange = MWBase::Environment::get().getWorld()->getMaxActivationDistance();

        // Note: can't just detonate the trap at the trapped object's location and use the blast
        // radius, because for most trap spells this is 1 foot, much less than the activation distance.
        // Using activation distance as the trap range.

        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr()
            && MWBase::Environment::get().getWorld()->getDistanceToFocusObject()
                > trapRange) // player activated object outside range of trap
        {
            MWMechanics::CastSpell cast(mTrapSource, mTrapSource);
            cast.mHitPosition = trapPosition;
            cast.cast(mSpellId);
        }
        else // player activated object within range of trap, or NPC activated trap
        {
            MWMechanics::CastSpell cast(mTrapSource, actor);
            cast.mHitPosition = actorPosition;
            cast.cast(mSpellId);
        }
        mTrapSource.getCellRef().setTrap(ESM::RefId());
    }
}
