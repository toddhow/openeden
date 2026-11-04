#include "player.hpp"

#include <vsg/maths/transform.h>

#include <components/animation/rotate.hpp>
#include <components/mwanimation/play.hpp>
#include <components/pipeline/override.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "env.hpp"

namespace MWRender
{
    Player::Player(const MWAnim::Context& mwctx, const MWWorld::Ptr& ptr, ViewMode viewMode)
        : Npc(mwctx, ptr, viewMode)
    {
        assignBones();

        mFirstPersonContext = mContext;
        auto override_ = vsg::ref_ptr{ new Pipeline::Override };
        override_->pipelineOptions = [](Pipeline::Options& o) { o.depthBias = true; };
        auto replaceOptions = [override_](vsg::ref_ptr<const vsg::Options>& options) {
            auto newOptions = vsg::Options::create(*options);
            override_->composite(*newOptions);
            options = newOptions;
        };
        for (auto& options : mFirstPersonContext.partBoneOptions)
            replaceOptions(options);
        replaceOptions(mFirstPersonContext.nodeOptions);
    }

    Player::Player(const MWAnim::Context& mwctx, TestSetup setup)
        : Npc(mwctx, setup)
    {
        mFirstPersonContext = mContext;
    }

    Player::~Player() {}

    void Player::update(float dt)
    {
        Npc::update(dt);
        updatePendingQuiver();
    }

    void Player::manualAnimation(float pitchSpine, float dt)
    {
        Actor::manualAnimation(pitchSpine, dt);
        if (mNeck && animation->isRotationControlled(*mNeck))
        {
            if (accurateAiming)
                mAimingFactor = 1.f;
            else
                mAimingFactor = std::max(0.f, mAimingFactor - dt * 0.5f);
            float rotateFactor = 0.75f + 0.25f * mAimingFactor;
            Anim::rotateBone(*mNeck,
                vsg::rotate(getPtr().getRefData().getPosition().rot[0] * rotateFactor, vsg::vec3(-1, 0, 0)),
                firstPersonOffset);
        }
    }

    void Player::setViewMode(Npc::ViewMode m)
    {
        if (mViewMode == m)
            return;
        // Task 0074f (finding 2 of the review of 0074e): the view mode and the part context are the player's only once
        // the rebuild's files are read (applyBasePlan); set here first, a read that threw left the player in the new
        // view with the old Play, and a repeated setViewMode(m) returned at once.
        // mAmmunition.reset();
        rebuild(m, m == ViewMode::FirstPerson ? &mFirstPersonContext : &mContext);
        assignBones();
    }

    void Player::assignBones()
    {
        if (mViewMode == ViewMode::FirstPerson)
            mNeck = animation->bones.search("bip01 neck");
        else
            mNeck = {};
        Npc::assignBones();
    }
}
