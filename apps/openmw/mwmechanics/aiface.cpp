#include "aiface.hpp"

#include "../mwworld/ptr.hpp"

#include "steering.hpp"

MWMechanics::AiFace::AiFace(float targetX, float targetY)
    : mTargetX(targetX)
    , mTargetY(targetY)
{
}

bool MWMechanics::AiFace::execute(const MWWorld::Ptr& actor, MWMechanics::CharacterController& /*characterController*/,
    MWMechanics::AiState& /*state*/, float duration)
{
    vsg::vec3 dir = vsg::vec3(mTargetX, mTargetY, 0) - actor.getRefData().getPosition().asVec3();
    return zTurn(actor, duration, std::atan2(dir.x, dir.y), Misc::VsgMath::inDegrees(3.f));
}
