// Task 0070 (stage a), synthetic check 1: what the renderer does with a skin bone that the actor's skeleton lacks.
//
// The engine's NIF reader copies every skin bone NAME into the skin (vsgAdapters nif.cpp, handleSkin:
// `bones.push_back({ ..., skin.mBones[i].getPtr()->mName })`) and never checks it against a skeleton; the names are
// resolved only when the skin is linked to an actor (components/animation/skin.cpp, Anim::Skin::link:
// `context.bones->search(bone.name)`). A name that is not found prints "<name> bone.path.empty()" to stderr and leaves
// that bone's path empty; Anim::Skin::apply then writes `skinMatrix * computeTransform(empty path) * invBindMatrix`
// for it, which no animation changes.
//
// This test builds a skinned shape as Nif records in memory (a mesh skinned to "Known" and "Missing"), builds the
// Anim::Skin from them exactly as handleSkin does (each bone's NiSkinData transform converted by
// vsgAdapters::convertTrafo, the names copied), links it with Anim::Skin::link to a skeleton that has only "Known", and
// skins the vertices with Anim::SoftwareSkin (the CPU copy of the shader's skinning the engine uses for intersections).
// No device, no files, no game data.
#include <components/animation/bones.hpp>
#include <components/animation/context.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/softwareskin.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/data.hpp>
#include <components/nif/node.hpp>
#include <components/vsgadapters/nif/anim.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/core/Array.h>

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Captures what is written to std::cerr while it lives.
    class CaptureErrors
    {
    public:
        CaptureErrors()
            : mOld(std::cerr.rdbuf(mText.rdbuf()))
        {
        }
        ~CaptureErrors() { std::cerr.rdbuf(mOld); }
        CaptureErrors(const CaptureErrors&) = delete;
        CaptureErrors& operator=(const CaptureErrors&) = delete;
        std::string text() const { return mText.str(); }

    private:
        std::ostringstream mText;
        std::streambuf* mOld;
    };

    Nif::NiTransform translation(float x, float y, float z)
    {
        Nif::NiTransform result = Nif::NiTransform::getIdentity();
        result.mTranslation = vsg::vec3(x, y, z);
        return result;
    }

    vsg::mat4 engineMatrix(const Nif::NiTransform& transform)
    {
        Anim::Transform converted;
        vsgAdapters::convertTrafo(converted, transform);
        return converted.t_transform(vsg::mat4());
    }

    // The shape: vertex 0 on "Known" only, vertex 1 on "Missing" only, vertex 2 half and half. The mesh was skinned
    // to a skeleton with "Known" at height 1 and "Missing" at height 1.5 (the inverse bind matrices say so).
    struct SkinnedModel
    {
        std::vector<std::unique_ptr<Nif::Record>> mRecords;
        Nif::NiSkinInstance* mInstance = nullptr;
        Nif::NiSkinData* mData = nullptr;
        std::vector<vsg::vec3> mVertices
            = { vsg::vec3(0.1f, 0, 1.0f), vsg::vec3(0.1f, 0, 1.6f), vsg::vec3(0.1f, 0, 1.25f) };

        SkinnedModel()
        {
            auto instance = std::make_unique<Nif::NiSkinInstance>();
            auto data = std::make_unique<Nif::NiSkinData>();
            data->mTransform = Nif::NiTransform::getIdentity();
            data->mPartitions = Nif::NiSkinPartitionPtr(nullptr);
            const std::vector<std::pair<std::string, float>> bones = { { "Known", 1.0f }, { "Missing", 1.5f } };
            const std::vector<std::vector<std::pair<unsigned short, float>>> weights
                = { { { 0, 1.f }, { 2, 0.5f } }, { { 1, 1.f }, { 2, 0.5f } } };
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                auto node = std::make_unique<Nif::NiNode>();
                node->mName = bones[i].first;
                instance->mBones.push_back(Nif::NiAVObjectPtr(node.get()));
                mRecords.push_back(std::move(node));
                Nif::NiSkinData::BoneInfo info;
                info.mTransform = translation(0, 0, -bones[i].second); // the inverse bind: bind height 1 and 1.5
                info.mWeights = weights[i];
                data->mBones.push_back(std::move(info));
            }
            instance->mData = Nif::NiSkinDataPtr(data.get());
            mInstance = instance.get();
            mData = data.get();
            mRecords.push_back(std::move(instance));
            mRecords.push_back(std::move(data));
        }
    };

    // An actor skeleton as the game has one: the model's root node (where the skin is attached, the reference frame),
    // an animated "Bip01" below it, "Known" at height 1 under that and, when asked, "Missing" at height 1.5.
    struct Actor
    {
        vsg::ref_ptr<Anim::Transform> mRoot = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mBip01 = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mKnown = Anim::Transform::create();
        std::unique_ptr<Anim::Bones> mBones;

        explicit Actor(bool withMissing)
        {
            vsgUtil::setName(*mRoot, "Root");
            vsgUtil::setName(*mBip01, "Bip01");
            vsgUtil::setName(*mKnown, "Known");
            mKnown->translation = { 0.f, 0.f, 1.f };
            mRoot->addChild(mBip01);
            mBip01->addChild(mKnown);
            if (withMissing)
            {
                auto missing = Anim::Transform::create();
                vsgUtil::setName(*missing, "Missing");
                missing->translation = { 0.f, 0.f, 1.5f };
                mBip01->addChild(missing);
            }
            // The engine's case-insensitive lookup (MWAnim::Bones).
            mBones = std::make_unique<Anim::Bones>(
                [](const std::string& name) { return Misc::StringUtils::lowerCase(name); }, *mRoot);
        }
    };

    // handleSkin's construction of the skin from the records (components/vsgadapters/nif/nif.cpp).
    vsg::ref_ptr<Anim::Skin> skinAsHandleSkinBuildsIt(const SkinnedModel& model)
    {
        auto skin = Anim::Skin::create();
        const auto count = static_cast<std::uint32_t>(model.mVertices.size());
        auto indices = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
        auto weights = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
        std::vector<std::vector<std::pair<std::uint32_t, float>>> perVertex(count);
        for (std::size_t i = 0; i < model.mInstance->mBones.size(); ++i)
        {
            const Nif::NiSkinData::BoneInfo& info = model.mData->mBones[i];
            skin->bones.push_back({ engineMatrix(info.mTransform), {}, model.mInstance->mBones[i].getPtr()->mName });
            for (const auto& [vertex, weight] : info.mWeights)
                perVertex[vertex].emplace_back(static_cast<std::uint32_t>(i), weight);
        }
        for (std::uint32_t v = 0; v < count; ++v)
        {
            for (std::size_t k = 0; k < perVertex[v].size() && k < 4; ++k)
            {
                indices->at(v)[k] = static_cast<float>(perVertex[v][k].first);
                weights->at(v)[k] = perVertex[v][k].second;
            }
        }
        skin->boneIndices = indices;
        skin->boneWeights = weights;
        return skin;
    }

    struct Linked
    {
        vsg::ref_ptr<Anim::Skin> mSkin;
        vsg::ref_ptr<vsg::mat4Array> mMatrices;
        std::string mErrors;
    };

    Linked link(const SkinnedModel& model, Actor& actor)
    {
        Linked linked;
        linked.mSkin = skinAsHandleSkinBuildsIt(model);
        linked.mMatrices = vsg::mat4Array::create(static_cast<std::uint32_t>(linked.mSkin->bones.size()));
        CaptureErrors capture;
        Anim::Context context{ {}, { actor.mRoot.get() }, actor.mBones.get(), nullptr };
        linked.mSkin->link(context, *linked.mMatrices);
        linked.mSkin->apply(*linked.mMatrices, 0.f);
        linked.mErrors = capture.text();
        return linked;
    }

    std::vector<vsg::vec3> skinned(const SkinnedModel& model, const Linked& linked)
    {
        auto software = Anim::SoftwareSkin::create(vsg::ref_ptr<const vsg::mat4Array>(linked.mMatrices), linked.mSkin);
        auto in = vsg::vec3Array::create(static_cast<std::uint32_t>(model.mVertices.size()));
        for (std::uint32_t v = 0; v < in->size(); ++v)
            in->at(v) = model.mVertices[v];
        software->apply(*in);
        std::vector<vsg::vec3> out;
        for (std::uint32_t v = 0; v < in->size(); ++v)
            out.push_back(software->vertices->at(v));
        return out;
    }

    void expectNear(const vsg::vec3& actual, const vsg::vec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 1e-5f);
        EXPECT_NEAR(actual.y, expected.y, 1e-5f);
        EXPECT_NEAR(actual.z, expected.z, 1e-5f);
    }

    TEST(MissingSkinBone, linking_a_skin_to_a_skeleton_without_one_of_its_bones_does_not_crash_and_says_so)
    {
        const SkinnedModel model;
        Actor actor(false);
        const Linked linked = link(model, actor);
        EXPECT_NE(linked.mErrors.find("Missing bone.path.empty()"), std::string::npos) << linked.mErrors;
        EXPECT_EQ(linked.mErrors.find("Known bone.path.empty()"), std::string::npos) << linked.mErrors;
    }

    TEST(MissingSkinBone, the_missing_bone_keeps_only_its_inverse_bind_matrix)
    {
        const SkinnedModel model;
        Actor actor(false);
        const Linked linked = link(model, actor);
        // Known: world (height 1) times inverse bind (height -1) is the identity at rest.
        expectNear(vsg::vec3((*linked.mMatrices)[0][3][0], (*linked.mMatrices)[0][3][1], (*linked.mMatrices)[0][3][2]),
            vsg::vec3(0, 0, 0));
        // Missing: the identity in place of the bone's world matrix, so only the inverse bind remains.
        expectNear(vsg::vec3((*linked.mMatrices)[1][3][0], (*linked.mMatrices)[1][3][1], (*linked.mMatrices)[1][3][2]),
            vsg::vec3(0, 0, -1.5f));
    }

    TEST(MissingSkinBone, a_vertex_on_the_missing_bone_is_pulled_towards_the_skeleton_root)
    {
        const SkinnedModel model;
        Actor actor(false);
        const std::vector<vsg::vec3> out = skinned(model, link(model, actor));
        ASSERT_EQ(out.size(), 3u);
        expectNear(out[0], vsg::vec3(0.1f, 0, 1.0f)); // on Known: where it was bound
        expectNear(out[1], vsg::vec3(0.1f, 0, 0.1f)); // on Missing: moved down by the bone's bind height
        expectNear(out[2], vsg::vec3(0.1f, 0, 0.5f)); // half and half: halfway
    }

    TEST(MissingSkinBone, with_the_bone_present_the_same_vertices_stay_where_they_were_bound)
    {
        const SkinnedModel model;
        Actor actor(true);
        const Linked linked = link(model, actor);
        EXPECT_EQ(linked.mErrors.find("bone.path.empty()"), std::string::npos) << linked.mErrors;
        const std::vector<vsg::vec3> out = skinned(model, linked);
        ASSERT_EQ(out.size(), 3u);
        for (std::size_t v = 0; v < out.size(); ++v)
            expectNear(out[v], model.mVertices[v]);
    }

    // The vertex stays where the inverse bind put it in the frame the skin is attached to (in the game: the actor's
    // model root, which moves with the actor's placement); no bone below it moves it, Bip01's motion included.
    TEST(MissingSkinBone, the_missing_bone_does_not_follow_the_animation)
    {
        const SkinnedModel model;
        Actor actor(false);
        Linked linked = link(model, actor);
        const vsg::mat4 before = (*linked.mMatrices)[1];
        // The animation moves the skeleton: Bip01 is lifted by 2 and Known moves sideways.
        actor.mBip01->translation = { 0.f, 0.f, 2.f };
        actor.mKnown->translation = { 0.f, 0.3f, 1.f };
        linked.mSkin->apply(*linked.mMatrices, 0.f);
        const vsg::mat4 after = (*linked.mMatrices)[1];
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
                EXPECT_EQ(after[c][r], before[c][r]) << c << ", " << r;
        }
        // The known bone did follow.
        EXPECT_NEAR((*linked.mMatrices)[0][3][1], 0.3f, 1e-5f);
    }
}
