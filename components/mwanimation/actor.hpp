#ifndef VSGOPENMW_MWANIMATION_ACTOR_H
#define VSGOPENMW_MWANIMATION_ACTOR_H

#include <array>

#include "objectanim.hpp"

namespace MWAnim
{
    /*
     * Manually animates bones.
     */
    class Actor : public ObjectAnimation
    {
    public:
        Actor(const Context& ctx);
        ~Actor();
        float headYawRadians = 0;
        float headPitchRadians = 0;
        float upperBodyYawRadians = 0;
        float legsYawRadians = 0;
        float bodyPitchRadians = 0;

        /// A relative factor (0-1) that decides if and how much the skeleton should be pitched
        /// to indicate the facing orientation of the character, for ranged weapon aiming.
        float spinePitchFactor = 0;

        // void manualAnimation(float headYaw...
        virtual void manualAnimation(float pitchSpine, float dt);
        const Anim::Bone* getHead() { return mHead; }

        // Finds the bones this class turns (head, spine, root) in the current `animation`. Virtual (task 0074g):
        // Npc::applyBasePlan calls it through MWAnim::Actor after every rebuild, and Player::assignBones must run then
        // (its first-person neck bone points into the Play). Called by a constructor, it reaches only the version of
        // the class being constructed, so a derived constructor that needs its own version calls it again.
        virtual void assignBones();

    protected:
        Anim::Bone* mHead{};
        std::array<Anim::Bone*, 2> mSpine;
        Anim::Bone* mRoot{};
    };
}

#endif
