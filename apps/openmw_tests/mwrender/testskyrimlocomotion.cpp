#include <apps/components_tests/havokpack/splinegen.hpp>
#include <apps/openmw/mwrender/skyrimlocomotion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <future>
#include <limits>
#include <memory>

// Task 0148: the locomotion controller blends the idle, walk and run clips by the actor's ground speed. The three test
// clips are constants that differ only in the x translation of bone "a" (idle 1, walk 3, run 5), so the expected
// value at any speed is the weighted mean of those numbers, worked out by hand from the gait maths (the mover's walk
// speed 120 is pure walk, 280 pure run, 200 half way; 17.5 is half way through the idle's fade).
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;
    using MWRender::SkyrimIdle::ClipFuture;
    using MWRender::SkyrimLocomotion::Controller;

    std::shared_ptr<const BoundClip> makeBound(float x)
    {
        ClipDesc c;
        c.mNumFrames = 31;
        std::vector<TrackDesc> block(2);
        block[0].mPos = [x] {
            VecDesc v;
            v.mAxis[0].mKind = VecDesc::Axis::Constant;
            v.mAxis[0].mValue = x;
            return v;
        }();
        block[1].mRot.mKind = RotDesc::Constant;
        block[1].mRot.mConstant = { 0, 0, 0, 1 };
        c.mBlocks = { block };
        c.mTrackToBone = { 0, 1 };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        const auto bytes = g.build();
        Skeleton s;
        s.mBoneNames = { "a", "b" };
        s.mParents = { -1, 0 };
        s.mReferencePose.resize(2);
        return std::make_shared<const BoundClip>(bindClip(s, readClips(Packfile::parse(bytes))[0]));
    }

    ClipFuture ready(std::shared_ptr<const BoundClip> clip)
    {
        std::promise<std::shared_ptr<const BoundClip>> p;
        p.set_value(std::move(clip));
        return p.get_future().share();
    }

    struct Rig
    {
        vsg::ref_ptr<Anim::Transform> mA = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mB = Anim::Transform::create();
        float mTime = 0.f;

        Controller::BoneMap bones() const { return { { "a", mA }, { "b", mB } }; }
        void step(Controller& c, float dt)
        {
            c.run(*mA, mTime);
            mTime += dt;
        }
        // Runs 6 seconds at 20 Hz: far longer than the speed smoothing needs.
        void settle(Controller& c, float speed)
        {
            c.setGroundSpeed(speed);
            for (int i = 0; i < 121; ++i)
                step(c, 0.05f);
        }
    };

    Controller make(Rig& rig, ClipFuture idle, ClipFuture walk, ClipFuture run, HavokPack::Gait::Params p = {})
    {
        return Controller(std::move(idle), std::move(walk), std::move(run), rig.bones(), 0.f, p);
    }
}

TEST(SkyrimLocomotion, a_standing_actor_plays_the_idle_only)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    EXPECT_TRUE(c.hints.autoPlay);
    rig.settle(c, 0.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f);
    EXPECT_NEAR(c.state().mWeights.mIdle, 1.f, 1e-6f);
    EXPECT_FLOAT_EQ(c.state().mPhase, 0.f);
}

TEST(SkyrimLocomotion, the_mover_s_walk_speed_plays_the_walk_and_its_run_speed_the_run)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 120.f);
    EXPECT_NEAR(rig.mA->translation[0], 3.f, 1e-3f);
    EXPECT_NEAR(c.state().mWeights.mWalk, 1.f, 1e-3f);
    rig.settle(c, 280.f);
    EXPECT_NEAR(rig.mA->translation[0], 5.f, 1e-3f);
}

TEST(SkyrimLocomotion, between_walk_and_run_the_clips_are_mixed)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 200.f);
    EXPECT_NEAR(rig.mA->translation[0], 4.f, 1e-3f);
    rig.settle(c, 17.5f); // the idle has faded half way: (1 + 3) / 2
    EXPECT_NEAR(rig.mA->translation[0], 2.f, 1e-3f);
}

TEST(SkyrimLocomotion, stopping_goes_back_to_the_idle_and_the_phase_restarts)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 120.f);
    EXPECT_GT(c.state().mPhase, 0.f);
    rig.settle(c, 0.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-3f);
    EXPECT_FLOAT_EQ(c.state().mPhase, 0.f);
}

TEST(SkyrimLocomotion, a_missing_gait_hands_its_weight_to_the_next_slower_one)
{
    Rig rig;
    Controller noWalk = make(rig, ready(makeBound(1)), ready(nullptr), ready(makeBound(5)));
    rig.settle(noWalk, 120.f); // walk weight 1 goes to the idle
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-3f);

    Rig rig2;
    Controller noRun = make(rig2, ready(makeBound(1)), ready(makeBound(3)), ready(nullptr));
    rig2.settle(noRun, 280.f); // run weight 1 goes to the walk
    EXPECT_NEAR(rig2.mA->translation[0], 3.f, 1e-3f);
}

TEST(SkyrimLocomotion, nothing_is_written_until_a_clip_is_decoded_and_then_it_is_picked_up)
{
    Rig rig;
    rig.mA->translation = { 7, 7, 7 };
    std::promise<std::shared_ptr<const BoundClip>> idle;
    Controller c = make(rig, idle.get_future().share(), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 0.f);
    EXPECT_FLOAT_EQ(rig.mA->translation[0], 7.f); // still pending: the bones stay as they are
    EXPECT_FALSE(c.state().mIdleReady);
    idle.set_value(makeBound(1));
    rig.step(c, 0.05f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f);
    EXPECT_TRUE(c.state().mIdleReady);
}

TEST(SkyrimLocomotion, the_speed_is_smoothed_not_jumped_to)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 0.f);
    c.setGroundSpeed(120.f);
    rig.step(c, 0.05f); // dt is measured from the previous run, 0.05 s earlier
    // one step of 0.05 s with tau 0.12: 120 * (1 - exp(-0.05 / 0.12)) = 40.9
    EXPECT_NEAR(c.state().mSmoothedSpeed, 120.f * (1.f - std::exp(-0.05f / 0.12f)), 0.5f);
}

TEST(SkyrimLocomotion, the_shared_phase_advances_by_the_cycle_rate)
{
    Rig rig;
    HavokPack::Gait::Params p;
    p.mWalkSpeed = 93.448f; // pure walk at this speed, and a stride of 93.448: one cycle a second
    p.mSmoothTau = 0.f;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)), p);
    c.setGroundSpeed(93.448f);
    rig.step(c, 0.1f); // t = 0: the first run takes the speed as it is and advances nothing
    rig.step(c, 0.1f); // t = 0.1: one step of 0.1 s at one cycle a second
    EXPECT_NEAR(c.state().mPhase, 0.1f, 1e-4f);
    for (int i = 0; i < 4; ++i)
        rig.step(c, 0.1f); // t = 0.5
    EXPECT_NEAR(c.state().mPhase, 0.5f, 1e-3f);
    for (int i = 0; i < 5; ++i)
        rig.step(c, 0.1f); // t = 1.0: wrapped round to the start
    const float wrapped = c.state().mPhase;
    EXPECT_TRUE(wrapped < 1e-3f || wrapped > 1.f - 1e-3f) << wrapped;
}

TEST(SkyrimLocomotion, a_bad_speed_counts_as_standing)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, std::numeric_limits<float>::quiet_NaN());
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f);
    EXPECT_TRUE(std::isfinite(c.state().mSmoothedSpeed));
    rig.settle(c, 120.f);
    EXPECT_NEAR(rig.mA->translation[0], 3.f, 1e-3f); // the NaN did not poison the smoothing
}

TEST(SkyrimLocomotion, time_going_backwards_or_a_long_pause_does_not_jump_the_speed)
{
    Rig rig;
    Controller c = make(rig, ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)));
    rig.settle(c, 0.f);
    c.setGroundSpeed(120.f);
    c.run(*rig.mA, rig.mTime - 100.f);
    EXPECT_FLOAT_EQ(c.state().mSmoothedSpeed, 0.f);
    c.run(*rig.mA, rig.mTime + 1000.f); // dt is capped at a tenth of a second
    EXPECT_LT(c.state().mSmoothedSpeed, 120.f * (1.f - std::exp(-0.1f / 0.12f)) + 0.5f);
}
