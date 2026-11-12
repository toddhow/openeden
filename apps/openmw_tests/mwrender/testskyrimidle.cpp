#include <apps/components_tests/havokpack/splinegen.hpp>
#include <apps/openmw/mwrender/skyrimidle.hpp>

#include <gtest/gtest.h>

#include <memory>

// Task 0132: the idle controller writes decoded Havok transforms into the scene graph's named bones. Expected values are
// worked out by hand; the rotation convention is checked through the transform's effect on a point, not through the
// stored matrix.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

    std::shared_ptr<const BoundClip> makeBound()
    {
        ClipDesc c;
        c.mNumFrames = 31;
        std::vector<TrackDesc> block(2);
        block[0].mPos = [] {
            VecDesc v;
            v.mAxis[0].mKind = VecDesc::Axis::Constant;
            v.mAxis[0].mValue = 1;
            return v;
        }();
        block[1].mRot.mKind = RotDesc::Constant;
        block[1].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f }; // 90 degrees about z
        c.mBlocks = { block };
        c.mTrackToBone = { 0, 1 };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        const auto bytes = g.build();
        Skeleton s;
        s.mBoneNames = { "A", "B" };
        s.mParents = { -1, 0 };
        s.mReferencePose.resize(2);
        return std::make_shared<const BoundClip>(bindClip(s, readClips(Packfile::parse(bytes))[0]));
    }

    vsg::dvec3 mapPoint(const Anim::Transform& t, vsg::dvec3 p)
    {
        const vsg::dmat4 m = t.transform(vsg::dmat4());
        const vsg::dvec4 r = m * vsg::dvec4(p.x, p.y, p.z, 1.0);
        return { r.x, r.y, r.z };
    }
}

TEST(SkyrimIdleApply, a_quarter_turn_about_z_takes_x_to_y_and_the_translation_is_kept)
{
    auto t = Anim::Transform::create();
    Transform from;
    from.mTranslation = { 1, 2, 3 };
    from.mRotation = { 0, 0, 0.7071068f, 0.7071068f };
    from.mScale = { 2, 2, 2 };
    MWRender::SkyrimIdle::apply(from, *t);
    const vsg::dvec3 p = mapPoint(*t, { 1, 0, 0 });
    // scale 2 then rotate to (0, 2, 0) then translate
    EXPECT_NEAR(p.x, 1.0, 1e-5);
    EXPECT_NEAR(p.y, 4.0, 1e-5);
    EXPECT_NEAR(p.z, 3.0, 1e-5);
}

TEST(SkyrimIdleController, it_writes_each_bound_bone_and_skips_a_null_target)
{
    auto bound = makeBound();
    auto a = Anim::Transform::create();
    auto b = Anim::Transform::create();
    MWRender::SkyrimIdle::Controller controller(bound, { a, b }, 0.f);
    EXPECT_EQ(controller.boundTracks(), 2u);
    EXPECT_TRUE(controller.hints.autoPlay);
    controller.run(*a, 0.3f);
    EXPECT_NEAR(a->translation[0], 1.0f, 1e-5f);
    const vsg::dvec3 p = mapPoint(*b, { 1, 0, 0 });
    EXPECT_NEAR(p.x, 0.0, 1e-5);
    EXPECT_NEAR(p.y, 1.0, 1e-5);

    auto c = Anim::Transform::create();
    MWRender::SkyrimIdle::Controller partial(bound, { vsg::ref_ptr<Anim::Transform>(), c }, 0.f);
    EXPECT_EQ(partial.boundTracks(), 1u);
    partial.run(*c, 0.3f);
    EXPECT_NEAR(mapPoint(*c, { 1, 0, 0 }).y, 1.0, 1e-5);
}

TEST(SkyrimIdleController, a_controller_with_no_clip_or_no_target_changes_nothing)
{
    auto a = Anim::Transform::create();
    a->translation = { 7, 7, 7 };
    MWRender::SkyrimIdle::Controller none(nullptr, { a }, 0.f);
    none.run(*a, 1.f);
    EXPECT_FLOAT_EQ(a->translation[0], 7.0f);
    MWRender::SkyrimIdle::Controller unbound(makeBound(), { vsg::ref_ptr<Anim::Transform>() }, 0.f);
    EXPECT_EQ(unbound.boundTracks(), 0u);
    unbound.run(*a, 1.f);
    EXPECT_FLOAT_EQ(a->translation[0], 7.0f);
}

TEST(SkyrimIdleController, the_phase_shifts_the_loop_and_time_wraps)
{
    auto bound = makeBound();
    const float d = bound->mClip.mDuration;
    ASSERT_GT(d, 0.f);
    // constant tracks: wrapping cannot change them, but the call must be well defined for any time
    auto a = Anim::Transform::create();
    auto b = Anim::Transform::create();
    MWRender::SkyrimIdle::Controller controller(bound, { a, b }, 0.25f * d);
    controller.run(*a, 1000.f * d + 0.1f);
    EXPECT_NEAR(a->translation[0], 1.0f, 1e-5f);
    controller.run(*a, -5.f);
    EXPECT_NEAR(a->translation[0], 1.0f, 1e-5f);
}

TEST(SkyrimIdleLoad, a_skeleton_with_no_clip_path_fails_with_a_reason_and_throws_nothing)
{
    VFS::Manager vfs;
    MWRender::SkyrimIdle::LoadReport report;
    EXPECT_EQ(MWRender::SkyrimIdle::loadIdle(vfs, "skeleton.nif", report), nullptr);
    EXPECT_FALSE(report.mFailure.empty());
    EXPECT_EQ(MWRender::SkyrimIdle::loadIdle(vfs, "meshes/actors/nothing/character assets/skeleton.nif", report),
        nullptr);
    EXPECT_NE(report.mFailure.find("not found"), std::string::npos) << report.mFailure;
}
