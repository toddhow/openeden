#include <apps/components_tests/havokpack/splinegen.hpp>
#include <apps/components_tests/vfs/vfsfakes.hpp>
#include <apps/openmw/mwrender/skyrimidle.hpp>
#include <apps/openmw/mwrender/skyrimpose.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <numbers>
#include <string>
#include <thread>

// Task 0267: a paired clip is read through the VFS on a worker, bound to one side's bone names, and described for the
// game pack. The clip is generated here (the values are worked out by hand in the havokpack tests, which use the same
// tracks): two actors' tracks, the first actor's "NPC" a quarter turn about z and ten units along x, its root rising
// from z 0 to 130 over one second at (1, 2).
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;
    using openeden::sdk::game::PairedClipInfo;
    using openeden::sdk::game::PairedStatus;

    constexpr double Pi = std::numbers::pi;

    VecDesc constantVec(float x, float y, float z)
    {
        VecDesc v;
        const float val[3] = { x, y, z };
        for (int i = 0; i < 3; ++i)
        {
            v.mAxis[i].mKind = VecDesc::Axis::Constant;
            v.mAxis[i].mValue = val[i];
        }
        return v;
    }

    std::string pairedFile(const std::vector<std::string>& names)
    {
        std::vector<TrackDesc> tracks(4);
        tracks[2].mPos = constantVec(10, 0, 0);
        tracks[2].mRot.mKind = RotDesc::Constant;
        tracks[2].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f };
        tracks[3].mPos = constantVec(1, 2, 0);
        tracks[3].mPos.mAxis[2].mKind = VecDesc::Axis::Spline;
        tracks[3].mPos.mAxis[2].mMin = 0;
        tracks[3].mPos.mAxis[2].mMax = 130;
        tracks[3].mPos.mAxis[2].mControl = { 0, 65535 };
        tracks[3].mPos.mKnots = { 0, 0, 30, 30 };
        ClipDesc c;
        c.mNumFrames = 31;
        c.mBlocks = { tracks };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        const std::uint32_t anim = addSplineClip(g, c);
        if (!names.empty())
        {
            std::vector<std::uint8_t> entries(24 * names.size(), 0);
            const std::uint32_t at = g.addBytes(entries);
            for (std::size_t i = 0; i < names.size(); ++i)
                g.pointer(at + 24 * static_cast<std::uint32_t>(i), g.addCString(names[i]));
            g.array(anim + 40, at, static_cast<std::uint32_t>(names.size()));
        }
        const auto bytes = g.build();
        return std::string(bytes.begin(), bytes.end());
    }

    PairedStatus waitFor(const VFS::Manager& vfs, const std::string& name, PairedClipInfo& info)
    {
        PairedStatus status = PairedStatus::NotReady;
        for (int i = 0; i < 500 && status == PairedStatus::NotReady; ++i)
        {
            status = MWRender::SkyrimIdle::pairedClipInfo(vfs, name, info);
            if (status == PairedStatus::NotReady)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return status;
    }

    struct PairedVfs
    {
        VFS::Manager mVfs;
        VFSTest::FakeArchive* mArchive = nullptr;

        PairedVfs()
        {
            mArchive = &VFSTest::addFakeArchive(mVfs, "paired test");
            mArchive->add("meshes/actors/test/paired_mount.hkx",
                pairedFile({ "PairedRoot", "2_Horse", "NPC", "NPC Root [Root]" }));
            mArchive->add("meshes/actors/test/not_paired.hkx", pairedFile({}));
            mVfs.buildIndex();
        }
    };
}

TEST(SkyrimPairedLoad, the_first_ask_reads_the_clip_and_answers_ready_with_the_roots_poses)
{
    PairedVfs rig;
    PairedClipInfo info;
    EXPECT_EQ(waitFor(rig.mVfs, "~1/actors/test/paired_mount", info), PairedStatus::Ready);
    EXPECT_NEAR(info.duration, 1.0f, 1e-4f);
    EXPECT_NEAR(info.start.x, 8.0, 1e-3);
    EXPECT_NEAR(info.start.y, 1.0, 1e-3);
    EXPECT_NEAR(info.start.z, 0.0, 1e-2);
    EXPECT_NEAR(info.start.yaw, Pi / 2, 1e-3);
    EXPECT_NEAR(info.end.x, 8.0, 1e-3);
    EXPECT_NEAR(info.end.z, 130.0, 0.1);
}

TEST(SkyrimPairedLoad, the_second_side_of_the_same_file_is_described_the_same)
{
    PairedVfs rig;
    PairedClipInfo first, second;
    EXPECT_EQ(waitFor(rig.mVfs, "~1/actors/test/paired_mount", first), PairedStatus::Ready);
    EXPECT_EQ(waitFor(rig.mVfs, "~2/actors/test/paired_mount", second), PairedStatus::Ready);
    EXPECT_EQ(first.duration, second.duration);
    EXPECT_EQ(first.end.z, second.end.z);
}

TEST(SkyrimPairedLoad, a_missing_file_a_bad_name_and_a_clip_with_no_track_names_fail)
{
    PairedVfs rig;
    PairedClipInfo info;
    EXPECT_EQ(waitFor(rig.mVfs, "~1/actors/test/nothing_here", info), PairedStatus::Failed);
    EXPECT_EQ(MWRender::SkyrimIdle::pairedClipInfo(rig.mVfs, "mt_idle", info), PairedStatus::Failed);
    EXPECT_EQ(MWRender::SkyrimIdle::pairedClipInfo(rig.mVfs, "~1/../x", info), PairedStatus::Failed);
    EXPECT_EQ(waitFor(rig.mVfs, "~1/actors/test/not_paired", info), PairedStatus::Failed);
    EXPECT_EQ(info.duration, 0.f);
}

TEST(SkyrimPairedLoad, the_clip_loader_binds_a_side_to_its_own_bone_names)
{
    PairedVfs rig;
    auto first = MWRender::SkyrimIdle::loadIdleAsync(
        rig.mVfs, "meshes/actors/character/character assets/skeleton.nif", "", "~1/actors/test/paired_mount");
    auto second = MWRender::SkyrimIdle::loadIdleAsync(
        rig.mVfs, "meshes/actors/horse/character assets/skeleton.nif", "", "~2/actors/test/paired_mount");
    ASSERT_EQ(first.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    ASSERT_EQ(second.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    const auto a = first.get();
    const auto b = second.get();
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_EQ(a->mTrackBones.size(), 4u);
    EXPECT_EQ(a->mTrackBones[2], "npc");
    EXPECT_EQ(a->mTrackBones[3], "npc root [root]");
    EXPECT_EQ(a->mTrackBones[1], "");
    EXPECT_EQ(b->mTrackBones[1], "horse");
    EXPECT_EQ(b->mTrackBones[3], "");
}

TEST(SkyrimPairedLoad, a_plain_clip_name_still_goes_the_way_of_the_actors_own_folder)
{
    PairedVfs rig;
    auto plain = MWRender::SkyrimIdle::loadIdleAsync(
        rig.mVfs, "meshes/actors/character/character assets/skeleton.nif", "male", "mt_idle");
    ASSERT_EQ(plain.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_FALSE(plain.get()); // the fake archive has no such clip
    auto bad = MWRender::SkyrimIdle::loadIdleAsync(rig.mVfs, "skeleton.nif", "", "~9/a");
    ASSERT_EQ(bad.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_FALSE(bad.get());
}

namespace
{
    // The pose controller of a drawn actor over the paired file, with two bones the clip's first side names.
    struct PairedPoseRig
    {
        PairedVfs mFiles;
        vsg::ref_ptr<Anim::Transform> mNpc = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mRoot = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mOther = Anim::Transform::create();
        std::unique_ptr<MWRender::SkyrimPose::Controller> mController;

        PairedPoseRig()
        {
            mNpc->translation = vsg::vec3(5.f, 6.f, 7.f); // what the node had before any clip
            mOther->translation = vsg::vec3(1.f, 2.f, 3.f);
            MWRender::SkyrimPose::Controller::BoneMap bones;
            bones["npc"] = mNpc;
            bones["npc root [root]"] = mRoot;
            bones["other"] = mOther;
            mController = std::make_unique<MWRender::SkyrimPose::Controller>(
                mFiles.mVfs, "meshes/actors/character/character assets/skeleton.nif", false, std::move(bones));
        }

        void runUntilDrawing(float time)
        {
            for (int i = 0; i < 500 && !mController->drawing(); ++i)
            {
                mController->run(*mNpc, time);
                if (!mController->drawing())
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            mController->run(*mNpc, time);
        }
    };
}

TEST(SkyrimPairedPose, a_paired_clip_drives_the_bones_its_side_names_and_leaves_the_others)
{
    PairedPoseRig rig;
    rig.mController->playOnce("~1/actors/test/paired_mount", false);
    rig.runUntilDrawing(0.f);
    ASSERT_TRUE(rig.mController->drawing());
    EXPECT_NEAR(rig.mNpc->translation.x, 10.f, 1e-4f); // the clip's NPC track, not the 5 the node had
    EXPECT_NEAR(rig.mNpc->translation.y, 0.f, 1e-4f);
    EXPECT_EQ(rig.mOther->translation.x, 1.f); // no track names it
    EXPECT_EQ(rig.mController->drivenBones(), 2u);
}

TEST(SkyrimPairedPose, the_bones_it_drove_get_their_old_values_back_when_the_clip_ends)
{
    PairedPoseRig rig;
    rig.mController->playOnce("~1/actors/test/paired_mount", false);
    rig.runUntilDrawing(0.f);
    ASSERT_NE(rig.mNpc->translation.x, 5.f);
    rig.mController->setClip({}, false);
    rig.mController->run(*rig.mNpc, 1.f);
    EXPECT_EQ(rig.mNpc->translation.x, 5.f);
    EXPECT_EQ(rig.mNpc->translation.y, 6.f);
    EXPECT_EQ(rig.mNpc->translation.z, 7.f);
    EXPECT_EQ(rig.mRoot->translation.x, 0.f); // the node's own value before the clip
    EXPECT_EQ(rig.mController->drivenBones(), 0u);
}

TEST(SkyrimPairedPose, the_bones_are_given_back_when_another_clip_replaces_it_too)
{
    PairedPoseRig rig;
    rig.mController->playOnce("~1/actors/test/paired_mount", false);
    rig.runUntilDrawing(0.f);
    ASSERT_NE(rig.mNpc->translation.x, 5.f);
    // the loop that follows (a clip the fake archive does not have: it never resolves to a clip, but the old one is
    // dropped when its future is ready and holds nothing)
    rig.mController->setClip("horse_rider/idle", false);
    for (int i = 0; i < 500 && rig.mController->drivenBones() != 0; ++i)
    {
        rig.mController->run(*rig.mNpc, 1.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(rig.mNpc->translation.x, 5.f);
    EXPECT_EQ(rig.mController->drivenBones(), 0u);
}

TEST(SkyrimPairedPose, an_ordinary_clip_never_records_a_bone)
{
    PairedPoseRig rig;
    rig.mController->setClip("horse_rider/idle", false); // not in the fake archive: nothing is ever driven
    for (int i = 0; i < 20; ++i)
        rig.mController->run(*rig.mNpc, 0.f);
    EXPECT_EQ(rig.mController->drivenBones(), 0u);
    EXPECT_EQ(rig.mNpc->translation.x, 5.f);
}
