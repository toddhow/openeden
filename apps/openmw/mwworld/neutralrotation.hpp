#ifndef OPENMW_MWWORLD_NEUTRALROTATION_H
#define OPENMW_MWWORLD_NEUTRALROTATION_H

// Task 0148: the rotation the renderer gets for a neutral actor that the physics side turned (World::doPhysics).
// The three angles are the ones ESM::Position stores for a neutral object (task 0109, R3), so the quaternion is
// Misc::Convert::makeOsgQuat's, the same one the object was inserted with. (Misc::Convert::makeQuaternion, which the
// first version used, builds a quaternion from the angle as a COMPONENT and is not a unit quaternion: it sheared and bent
// every actor that turned.) Pure; any thread.

#include <components/esm/position.hpp>
#include <components/misc/convert.hpp>

namespace MWWorld
{
    inline vsg::dquat neutralActorQuaternion(const ESM::Position& position)
    {
        return Misc::Convert::makeOsgQuat(position);
    }
}

#endif
