#ifndef VSGOPENMW_MWRENDER_PLAYER_H
#define VSGOPENMW_MWRENDER_PLAYER_H

#include "npc.hpp"

#include <components/mwanimation/context.hpp>

namespace MWRender
{
    /*
     * Supports first person meshes.
     */
    class Player : public Npc
    {
        Anim::Bone* mNeck{};
        float mAimingFactor = 1.f;
        void assignBones();
        MWAnim::Context mFirstPersonContext;

    public:
        Player(const MWAnim::Context& mwctx, const MWWorld::Ptr& ptr, ViewMode viewMode = ViewMode::Normal);
        ~Player();
        // The player is updated on the main thread (RenderManager::update), so its pending quiver change is done here.
        void update(float dt) override;
        void manualAnimation(float pitchSpine, float dt) override;
        void setViewMode(Npc::ViewMode m);

        vsg::vec3 firstPersonOffset;
        bool accurateAiming = false;
        // Externally set true when important animations are currently playing on the upper body.
        bool busy = false;
    };
}

#endif
