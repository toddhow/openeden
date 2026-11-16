// Task 0232: the sequence model and driver of a Skyrim door or chest (apps/openmw/mwrender/skyrimsequence.*). Scenes are
// built by hand; no game asset is read and no GPU object is made (the parts are plain Anim::Transform nodes).

#include <apps/openmw/mwrender/skyrimsequence.hpp>
#include <components/animation/transform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace
{
    using namespace NifScene;
    namespace Host = MWRender::SkyrimSequence;

    constexpr float s45 = 0.70710678f;

    Node makeNode(const std::string& name, std::uint32_t parent, Vec3 translation, std::uint32_t geometry = sNone)
    {
        Node n;
        n.mName = name;
        n.mParent = parent;
        n.mTransform.mTranslation = translation;
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

    Track hingeTrack()
    {
        Track track;
        track.mNodeName = "Hinge";
        track.mRotations = { { 0.f, { 0, 0, 0, 1 } }, { 1.f, { 0, 0, s45, s45 } } };
        track.mHasDefault = true;
        track.mTranslations = { { 0.f, { 10, 0, 0 } }, { 1.f, { 10, 0, 0 } } };
        return track;
    }

    // Root (0) > Hinge (1, at x = 10) > Leaf shape (2); Root > Frame shape (3).
    Scene doorScene(bool withOpen = true, bool withClose = false)
    {
        Scene scene;
        scene.mNodes.push_back(makeNode("Root", sNone, { 0, 0, 0 }));
        scene.mNodes.push_back(makeNode("Hinge", 0, { 10, 0, 0 }));
        scene.mNodes.push_back(makeNode("Leaf", 1, { 0, 0, 0 }, 0));
        scene.mNodes.push_back(makeNode("Frame", 0, { 0, 5, 0 }, 1));
        scene.mGeometries = { quad(), quad() };
        scene.mMaterials.emplace_back();
        scene.mRoots = { 0 };
        if (withOpen)
        {
            Sequence open;
            open.mName = "Open";
            open.mStart = 0.f;
            open.mStop = 0.5f;
            open.mTracks.push_back(hingeTrack());
            open.mTracks[0].mRotations[1].mTime = 0.5f;
            open.mTracks[0].mTranslations[1].mTime = 0.5f;
            scene.mSequences.push_back(open);
        }
        if (withClose)
        {
            Sequence close;
            close.mName = "Close";
            close.mStart = 2.f;
            close.mStop = 2.5f;
            Track track = hingeTrack();
            track.mRotations = { { 2.f, { 0, 0, s45, s45 } }, { 2.5f, { 0, 0, 0, 1 } } };
            track.mTranslations = { { 2.f, { 10, 0, 0 } }, { 2.5f, { 10, 0, 0 } } };
            close.mTracks.push_back(track);
            scene.mSequences.push_back(close);
        }
        return scene;
    }

    // The moving part's place for a point 1 unit along x from the hinge, from the transform's own fields.
    vsg::vec3 place(const Anim::Transform& t, vsg::vec3 p)
    {
        return vsg::vec3(t.rotation[0][0] * p.x + t.rotation[1][0] * p.y + t.rotation[2][0] * p.z,
                   t.rotation[0][1] * p.x + t.rotation[1][1] * p.y + t.rotation[2][1] * p.z,
                   t.rotation[0][2] * p.x + t.rotation[1][2] * p.y + t.rotation[2][2] * p.z)
            * t.scale.x
            + t.translation;
    }

    struct Rig
    {
        std::shared_ptr<const Host::Model> mModel;
        vsg::ref_ptr<Anim::Transform> mPart = Anim::Transform::create();
        vsg::ref_ptr<Host::Instance> mInstance;

        explicit Rig(Scene scene)
        {
            mModel = Host::makeModel(std::move(scene), NifScene::Flat::Limits{});
            if (!mModel)
                return;
            Host::Instance::PartList parts;
            for (const Host::Model::Part& part : mModel->mParts)
                parts.emplace_back(part.mNode, mPart);
            mInstance = vsg::ref_ptr<Host::Instance>(new Host::Instance(mModel, std::move(parts)));
        }
    };

    TEST(SkyrimSequenceModel, a_model_without_an_open_sequence_is_not_animated)
    {
        EXPECT_EQ(Host::makeModel(doorScene(false), NifScene::Flat::Limits{}), nullptr);
    }

    TEST(SkyrimSequenceModel, a_sequence_that_moves_no_node_of_the_file_is_not_animated)
    {
        Scene scene = doorScene();
        scene.mSequences[0].mTracks[0].mNodeName = "Nothing";
        EXPECT_EQ(Host::makeModel(std::move(scene), NifScene::Flat::Limits{}), nullptr);
    }

    TEST(SkyrimSequenceModel, the_moving_shapes_are_split_from_the_still_ones)
    {
        Rig rig(doorScene());
        ASSERT_NE(rig.mModel, nullptr);
        ASSERT_EQ(rig.mModel->mParts.size(), 1u);
        EXPECT_EQ(rig.mModel->mParts[0].mNode, 1u);
        EXPECT_EQ(rig.mModel->mParts[0].mFlat.mShapes.size(), 1u);
        EXPECT_EQ(rig.mModel->mStatic.mShapes.size(), 1u);
        EXPECT_EQ(rig.mModel->mOpen, 0u);
        EXPECT_EQ(rig.mModel->mClose, NifScene::Sequences::sNoSequence);
    }

    TEST(SkyrimSequenceModel, the_opening_time_comes_from_the_sequence_and_is_kept_sane)
    {
        Rig rig(doorScene());
        EXPECT_FLOAT_EQ(rig.mModel->mSeconds, 0.5f);
        Scene slow = doorScene();
        slow.mSequences[0].mStop = 90.f;
        Rig tooLong(std::move(slow));
        EXPECT_FLOAT_EQ(tooLong.mModel->mSeconds, Host::kMaxSeconds);
    }

    TEST(SkyrimSequenceModel, the_model_keeps_no_geometry)
    {
        Rig rig(doorScene());
        EXPECT_TRUE(rig.mModel->mScene.mGeometries.empty());
        EXPECT_EQ(rig.mModel->mScene.mNodes.size(), 4u);
    }

    TEST(SkyrimSequenceInstance, a_closed_door_leaves_its_part_where_the_file_puts_it)
    {
        Rig rig(doorScene());
        rig.mInstance->apply(0.f, false);
        const vsg::vec3 p = place(*rig.mPart, { 11, 0, 0 });
        EXPECT_NEAR(p.x, 11.f, 1e-4);
        EXPECT_NEAR(p.y, 0.f, 1e-4);
    }

    TEST(SkyrimSequenceInstance, an_open_door_swings_its_part_a_quarter_turn_about_the_hinge)
    {
        Rig rig(doorScene());
        rig.mInstance->apply(1.f, false);
        const vsg::vec3 p = place(*rig.mPart, { 11, 0, 0 });
        EXPECT_NEAR(p.x, 10.f, 1e-4);
        EXPECT_NEAR(p.y, 1.f, 1e-4);
        EXPECT_NEAR(p.z, 0.f, 1e-4);
        EXPECT_NEAR(rig.mPart->scale.x, 1.f, 1e-4);
    }

    TEST(SkyrimSequenceInstance, a_half_open_door_is_half_turned)
    {
        Rig rig(doorScene());
        rig.mInstance->apply(0.5f, false);
        const vsg::vec3 p = place(*rig.mPart, { 11, 0, 0 });
        EXPECT_NEAR(p.x, 10.f + s45, 1e-3);
        EXPECT_NEAR(p.y, s45, 1e-3);
    }

    TEST(SkyrimSequenceInstance, closing_with_a_close_sequence_shows_the_same_pose_as_opening_at_that_progress)
    {
        Rig rig(doorScene(true, true));
        ASSERT_NE(rig.mModel->mClose, NifScene::Sequences::sNoSequence);
        rig.mInstance->apply(0.5f, true);
        const vsg::vec3 closing = place(*rig.mPart, { 11, 0, 0 });
        rig.mInstance->apply(0.5f, false);
        const vsg::vec3 opening = place(*rig.mPart, { 11, 0, 0 });
        EXPECT_NEAR(closing.x, opening.x, 1e-3);
        EXPECT_NEAR(closing.y, opening.y, 1e-3);
    }

    TEST(SkyrimSequenceInstance, set_progress_is_applied_by_the_next_run)
    {
        Rig rig(doorScene());
        rig.mInstance->setProgress(1.f, false);
        rig.mInstance->run(*rig.mPart, 0.f);
        EXPECT_NEAR(place(*rig.mPart, { 11, 0, 0 }).y, 1.f, 1e-4);
        EXPECT_FLOAT_EQ(rig.mInstance->progress(), 1.f);
    }

    TEST(SkyrimSequenceInstance, a_lid_opens_over_the_sequence_time_and_closes_again)
    {
        Rig rig(doorScene()); // 0.5 s
        rig.mInstance->setTarget(true);
        float time = 0.f;
        rig.mInstance->run(*rig.mPart, time); // the first call has no step
        for (int i = 0; i < 3; ++i)
        {
            time += 0.1f;
            rig.mInstance->run(*rig.mPart, time);
        }
        EXPECT_NEAR(rig.mInstance->progress(), 0.6f, 1e-4);
        for (int i = 0; i < 10; ++i)
        {
            time += 0.1f;
            rig.mInstance->run(*rig.mPart, time);
        }
        EXPECT_FLOAT_EQ(rig.mInstance->progress(), 1.f);
        rig.mInstance->setTarget(false);
        for (int i = 0; i < 10; ++i)
        {
            time += 0.1f;
            rig.mInstance->run(*rig.mPart, time);
        }
        EXPECT_FLOAT_EQ(rig.mInstance->progress(), 0.f);
    }

    TEST(SkyrimSequenceInstance, a_part_with_no_transform_is_skipped_without_a_crash)
    {
        Rig rig(doorScene());
        Host::Instance::PartList parts;
        parts.emplace_back(1u, vsg::ref_ptr<Anim::Transform>());
        Host::Instance instance(rig.mModel, std::move(parts));
        instance.apply(1.f, false);
        SUCCEED();
    }
}
