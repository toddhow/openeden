// Task 0289 (dungeon mechanics): Havok clips of placed objects as named sequences (apps/openmw/mwrender/skyrimhavok.*)
// and their playback by SkyrimSequence::Instance::play. Scenes and clips are made by hand; no game asset is read.

#include <apps/openmw/mwrender/skyrimhavok.hpp>
#include <apps/openmw/mwrender/skyrimsequence.hpp>
#include <components/animation/transform.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <string>

namespace
{
    using namespace NifScene;
    namespace Host = MWRender::SkyrimSequence;
    namespace Havok = MWRender::SkyrimHavok;

    constexpr float s45 = 0.70710678f;

    // A bone that turns about Z from 0 to a quarter turn over `duration` seconds, as a pillar's clip does.
    void quarterTurn(float duration, float time, std::span<HavokPack::Transform> out)
    {
        const float f = std::clamp(time / duration, 0.f, 1.f);
        const float angle = f * 1.5707963f;
        for (HavokPack::Transform& t : out)
        {
            t = {};
            t.mRotation = { 0.f, 0.f, std::sin(angle / 2.f), std::cos(angle / 2.f) };
        }
    }

    TEST(SkyrimHavokSequence, a_sampled_clip_becomes_linear_keys_every_thirtieth_of_a_second_and_one_at_the_end)
    {
        const Sequence sequence = Havok::sequenceFromSampler("trigger01", 1.0f, { "Pillar01" },
            [](float t, std::span<HavokPack::Transform> out) { quarterTurn(1.0f, t, out); });
        EXPECT_EQ(sequence.mName, "trigger01");
        EXPECT_FLOAT_EQ(sequence.mStop, 1.0f);
        ASSERT_EQ(sequence.mTracks.size(), 1u);
        const Track& track = sequence.mTracks[0];
        EXPECT_EQ(track.mNodeName, "Pillar01");
        ASSERT_GE(track.mRotations.size(), 31u);
        EXPECT_FLOAT_EQ(track.mRotations.front().mTime, 0.f);
        EXPECT_FLOAT_EQ(track.mRotations.back().mTime, 1.0f);
        EXPECT_NEAR(track.mRotations.back().mValue[2], s45, 1e-4f);
        EXPECT_EQ(track.mRotations.size(), track.mTranslations.size());
    }

    TEST(SkyrimHavokSequence, a_track_with_no_node_is_skipped_and_a_bad_duration_gives_no_tracks)
    {
        const Havok::Sampler sampler = [](float t, std::span<HavokPack::Transform> out) { quarterTurn(1.f, t, out); };
        const Sequence two = Havok::sequenceFromSampler("a", 1.f, { "", "Bone" }, sampler);
        ASSERT_EQ(two.mTracks.size(), 1u);
        EXPECT_EQ(two.mTracks[0].mNodeName, "Bone");
        EXPECT_TRUE(Havok::sequenceFromSampler("a", 0.f, { "Bone" }, sampler).mTracks.empty());
        EXPECT_TRUE(Havok::sequenceFromSampler("a", -1.f, { "Bone" }, sampler).mTracks.empty());
        EXPECT_TRUE(Havok::sequenceFromSampler("a", 1e9f, { "Bone" }, sampler).mTracks.empty());
        EXPECT_TRUE(Havok::sequenceFromSampler("a", std::nanf(""), { "Bone" }, sampler).mTracks.empty());
        EXPECT_TRUE(Havok::sequenceFromSampler("a", 1.f, { "Bone" }, Havok::Sampler{}).mTracks.empty());
    }

    Node makeNode(const std::string& name, std::uint32_t parent, std::uint32_t geometry = sNone)
    {
        Node n;
        n.mName = name;
        n.mParent = parent;
        n.mGeometry = geometry;
        return n;
    }

    Geometry quad()
    {
        Geometry g;
        g.mName = "Quad";
        g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
        g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        g.mTriangles = { 0, 1, 2, 0, 2, 3 };
        g.mMaterial = 0;
        return g;
    }

    // Root (0) > Pillar01 (1) > shape (2). Sequences are the named ones of the pillar: trigger01, trigger02.
    Scene pillarScene()
    {
        Scene scene;
        scene.mNodes.push_back(makeNode("Root", sNone));
        scene.mNodes.push_back(makeNode("Pillar01", 0));
        scene.mNodes.push_back(makeNode("Shape", 1, 0));
        scene.mGeometries = { quad() };
        scene.mMaterials.emplace_back();
        scene.mRoots = { 0 };
        for (const char* name : { "trigger01", "trigger02" })
        {
            const bool second = std::string(name) == "trigger02";
            scene.mSequences.push_back(Havok::sequenceFromSampler(
                name, 1.0f, { "Pillar01" }, [second](float t, std::span<HavokPack::Transform> out) {
                    // trigger01 turns 0 to 90 degrees; trigger02 turns 90 to 180
                    quarterTurn(1.0f, t, out);
                    for (HavokPack::Transform& tr : out)
                    {
                        const float angle = (second ? 1.5707963f : 0.f) + std::asin(tr.mRotation[2]) * 2.f;
                        tr.mRotation = { 0.f, 0.f, std::sin(angle / 2.f), std::cos(angle / 2.f) };
                    }
                }));
        }
        return scene;
    }

    // The angle of the part's turn about Z in radians.
    float zAngle(const Anim::Transform& t)
    {
        return std::atan2(t.rotation[0][1], t.rotation[0][0]);
    }

    struct Rig
    {
        std::shared_ptr<const Host::Model> mModel;
        vsg::ref_ptr<Anim::Transform> mPart = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mTarget = Anim::Transform::create();
        vsg::ref_ptr<Host::Instance> mInstance;

        explicit Rig(Scene scene, bool havok = false)
        {
            mModel = Host::makeModel(std::move(scene), NifScene::Flat::Limits{}, havok);
            if (!mModel)
                return;
            Host::Instance::PartList parts;
            for (const Host::Model::Part& part : mModel->mParts)
                parts.emplace_back(part.mNode, mPart);
            mInstance = vsg::ref_ptr<Host::Instance>(new Host::Instance(mModel, std::move(parts)));
        }
    };

    TEST(SkyrimHavokInstance, a_model_with_only_named_sequences_is_animated_and_has_no_open_sequence)
    {
        Rig rig(pillarScene());
        ASSERT_NE(rig.mModel, nullptr);
        EXPECT_EQ(rig.mModel->mOpen, Sequences::sNoSequence);
        EXPECT_EQ(rig.mModel->mParts.size(), 1u);
    }

    TEST(SkyrimHavokInstance, a_sequence_that_moves_no_node_leaves_the_model_unanimated)
    {
        Scene scene = pillarScene();
        for (Sequence& sequence : scene.mSequences)
            sequence.mTracks[0].mNodeName = "Nothing";
        EXPECT_EQ(Host::makeModel(std::move(scene), NifScene::Flat::Limits{}), nullptr);
    }

    TEST(SkyrimHavokInstance, play_turns_the_part_over_the_clip_and_holds_the_end)
    {
        Rig rig(pillarScene());
        ASSERT_NE(rig.mInstance, nullptr);
        EXPECT_FLOAT_EQ(rig.mInstance->secondsOf("Trigger01"), 1.0f);
        ASSERT_TRUE(rig.mInstance->play("Trigger01"));
        rig.mInstance->run(*rig.mTarget, 10.0f); // the frame that sees the request starts the clock
        EXPECT_NEAR(zAngle(*rig.mPart), 0.f, 1e-3f);
        rig.mInstance->run(*rig.mTarget, 10.5f);
        EXPECT_NEAR(zAngle(*rig.mPart), 0.7853982f, 0.02f);
        rig.mInstance->run(*rig.mTarget, 12.0f);
        EXPECT_NEAR(zAngle(*rig.mPart), 1.5707963f, 1e-3f);
        rig.mInstance->run(*rig.mTarget, 20.0f); // held
        EXPECT_NEAR(zAngle(*rig.mPart), 1.5707963f, 1e-3f);
    }

    TEST(SkyrimHavokInstance, a_start_state_shows_the_first_pose_of_its_clip_at_once)
    {
        Rig rig(pillarScene());
        ASSERT_TRUE(rig.mInstance->play("StartState02"));
        rig.mInstance->run(*rig.mTarget, 1.0f);
        EXPECT_NEAR(zAngle(*rig.mPart), 1.5707963f, 1e-3f); // trigger02 begins at a quarter turn
        EXPECT_FLOAT_EQ(rig.mInstance->secondsOf("StartState02"), 0.f);
        rig.mInstance->run(*rig.mTarget, 5.0f);
        EXPECT_NEAR(zAngle(*rig.mPart), 1.5707963f, 1e-3f);
    }

    TEST(SkyrimHavokInstance, an_event_the_model_does_not_know_is_refused_and_changes_nothing)
    {
        Rig rig(pillarScene());
        EXPECT_FALSE(rig.mInstance->play("Trigger09"));
        EXPECT_FALSE(rig.mInstance->play("StartState09"));
        EXPECT_FALSE(rig.mInstance->play(""));
        EXPECT_FLOAT_EQ(rig.mInstance->secondsOf("Nothing"), 0.f);
    }

    // The pillar scene with its first sequence named Open, as a portcullis has.
    Scene gateScene()
    {
        Scene scene = pillarScene();
        scene.mSequences[0].mName = "open";
        return scene;
    }

    TEST(SkyrimHavokInstance, a_havok_model_stays_in_its_rest_pose_until_an_animation_plays)
    {
        Rig havok(gateScene(), true);
        ASSERT_NE(havok.mInstance, nullptr);
        EXPECT_TRUE(havok.mModel->mHavok);
        havok.mInstance->setProgress(1.f, false); // the door machinery asks for fully open
        havok.mInstance->run(*havok.mTarget, 1.0f);
        EXPECT_NEAR(zAngle(*havok.mPart), 0.f, 1e-4f) << "a Havok model ignores the door progress";
        ASSERT_TRUE(havok.mInstance->play("Open"));
        havok.mInstance->run(*havok.mTarget, 2.0f);
        havok.mInstance->run(*havok.mTarget, 4.0f);
        EXPECT_NEAR(zAngle(*havok.mPart), 1.5707963f, 1e-3f);
    }

    TEST(SkyrimHavokInstance, a_model_with_an_open_sequence_of_its_own_still_follows_the_door_progress)
    {
        Rig door(gateScene(), false);
        ASSERT_NE(door.mInstance, nullptr);
        EXPECT_FALSE(door.mModel->mHavok);
        door.mInstance->setProgress(1.f, false);
        door.mInstance->run(*door.mTarget, 1.0f);
        EXPECT_NEAR(zAngle(*door.mPart), 1.5707963f, 1e-3f);
    }

    TEST(SkyrimHavokInstance, a_second_animation_replaces_the_first_from_its_own_start)
    {
        Rig rig(pillarScene());
        ASSERT_TRUE(rig.mInstance->play("Trigger01"));
        rig.mInstance->run(*rig.mTarget, 0.0f);
        rig.mInstance->run(*rig.mTarget, 2.0f);
        ASSERT_TRUE(rig.mInstance->play("Trigger02"));
        rig.mInstance->run(*rig.mTarget, 3.0f);
        EXPECT_NEAR(zAngle(*rig.mPart), 1.5707963f, 1e-3f);
        rig.mInstance->run(*rig.mTarget, 4.5f);
        EXPECT_NEAR(std::abs(zAngle(*rig.mPart)), 3.1415926f, 0.01f);
    }
}
