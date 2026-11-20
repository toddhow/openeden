#include <apps/components_tests/havokpack/splinegen.hpp>
#include <apps/openmw/mwphysics/neutralpathfollower.hpp>
#include <apps/openmw/mwrender/skyrimlocomotion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <future>
#include <limits>
#include <memory>

// Task 0361: the idle variant laid over a standing actor's idle. The test clips are constants that differ only in the
// x translation of bone "a" (idle 1, variant 9, walk 3, run 5), so the value shows the weights directly.
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
        void until(Controller& c, float t)
        {
            while (mTime < t)
                step(c, 0.05f);
        }
    };

    Controller make(Rig& rig)
    {
        return Controller(ready(makeBound(1)), ready(makeBound(3)), ready(makeBound(5)), rig.bones(), 0.f);
    }
}

TEST(SkyrimIdleFidget, without_a_variant_nothing_changes)
{
    Rig rig;
    Controller c = make(rig);
    rig.until(c, 12.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f);
    EXPECT_EQ(c.state().mFidgetWeight, 0.f);
}

TEST(SkyrimIdleFidget, a_variant_plays_in_its_slot_and_the_plain_idle_returns_after_it)
{
    Rig rig;
    Controller c = make(rig);
    const float duration = makeBound(9)->mClip.mDuration;
    ASSERT_GT(duration, 0.8f);
    c.setFidget(ready(makeBound(9)), { 4.f, 30.f });
    rig.until(c, 3.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f); // before the first start
    rig.until(c, 4.f + 0.5f * duration);
    EXPECT_NEAR(c.state().mFidgetWeight, 1.f, 1e-4f);
    EXPECT_NEAR(rig.mA->translation[0], 9.f, 1e-3f); // the variant alone
    rig.until(c, 4.f + duration + 1.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f); // after it
    rig.until(c, 4.f + 30.f + 0.5f * duration);
    EXPECT_NEAR(rig.mA->translation[0], 9.f, 1e-3f); // the next one a period later
}

TEST(SkyrimIdleFidget, a_variant_that_does_not_exist_is_the_plain_idle)
{
    Rig rig;
    Controller c = make(rig);
    c.setFidget(ClipFuture(), { 1.f, 10.f });
    rig.until(c, 5.f);
    EXPECT_NEAR(rig.mA->translation[0], 1.f, 1e-4f);
}

TEST(SkyrimIdleFidget, a_walking_actor_does_not_play_it)
{
    Rig rig;
    Controller c = make(rig);
    c.setFidget(ready(makeBound(9)), { 0.f, 20.f });
    c.setGroundSpeed(82.45f);
    rig.until(c, 6.f);
    EXPECT_NEAR(rig.mA->translation[0], 3.f, 1e-3f);
    EXPECT_EQ(c.state().mFidgetWeight, 0.f);
}

TEST(SkyrimIdleFidget, two_actors_with_different_schedules_do_not_pose_alike)
{
    Rig r1, r2;
    Controller a = make(r1), b = make(r2);
    const float duration = makeBound(9)->mClip.mDuration;
    a.setFidget(ready(makeBound(9)), { 3.f, 30.f });
    b.setFidget(ready(makeBound(9)), { 13.f, 30.f });
    r1.until(a, 3.f + 0.5f * duration);
    r2.until(b, 3.f + 0.5f * duration);
    EXPECT_NEAR(r1.mA->translation[0], 9.f, 1e-3f);
    EXPECT_NEAR(r2.mA->translation[0], 1.f, 1e-4f);
}
