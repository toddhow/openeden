// Task 0121 (neutral world stage N5-2): a game pack's actor as a scene graph (apps/openmw/mwrender/skyrimactor.*): the
// skeleton as named Anim::Transform nodes in the rest pose, skinned shapes as skinned draws, and the two linked by the
// engine's own animation code (Anim::Bones, Anim::Skin) so that the rest pose gives identity bone matrices. Models are
// built by hand; no game asset is read.

#include <apps/openmw/mwrender/skyrimactor.hpp>
#include <apps/openmw/mwrender/skyrimmodel.hpp>
#include <components/animation/bones.hpp>
#include <components/animation/context.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/pipeline/builder.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/name.hpp>

#include <gtest/gtest.h>

#include <vsg/core/Visitor.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/Sampler.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    using namespace NifScene;

    // A skeleton: root, pelvis (turned 90 degrees about Z, 10 up), spine (5 up from the pelvis).
    Scene skeletonScene()
    {
        Scene scene;
        Node root;
        root.mName = "NPC Root [Root]";
        Node pelvis;
        pelvis.mName = "NPC Pelvis [Pelv]";
        pelvis.mParent = 0;
        pelvis.mTransform.mTranslation = { 0.f, 0.f, 10.f };
        pelvis.mTransform.mRotation = { 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f }; // row-major, +90 about Z
        Node spine;
        spine.mName = "NPC Spine [Spn0]";
        spine.mParent = 1;
        spine.mTransform.mTranslation = { 0.f, 0.f, 5.f };
        scene.mNodes = { root, pelvis, spine };
        scene.mRoots = { 0 };
        return scene;
    }

    struct Found
    {
        const Anim::Skin* skin = nullptr;
        vsg::mat4Array* matrices = nullptr;
    };

    struct SkyrimActorTest : ::testing::Test
    {
        vsg::ref_ptr<const vsg::Options> mShaderOptions
            = Resource::createShaderOptions(
                (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "shaders").string(), "");
        Pipeline::Builder mBuilder{ mShaderOptions, vsg::Sampler::create() };

        MWRender::SkyrimModel::BuildContext context() const
        {
            return { mBuilder, vsg::Options::create(), nullptr, 0, "test", {} };
        }

        // One skinned quad whose vertices all follow the pelvis, with the pelvis' rest pose as its bind (the inverse of
        // the pelvis' world transform: translate -10 in Z, then turn -90 about Z).
        Skinned::SkinnedModel pelvisQuad() const
        {
            Skinned::SkinnedModel model;
            Skinned::Shape shape;
            shape.mName = "body";
            shape.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            shape.mNormals.assign(4, { 0.f, 0.f, 1.f });
            shape.mIndices = { 0, 1, 2, 0, 2, 3 };
            shape.mBones.resize(2);
            shape.mBones[0].mName = "NPC Pelvis [Pelv]";
            // Skin space to bone space: the bone sits at (0, 0, 10) turned +90 about Z, so p_bone = R^-1 (p - t).
            shape.mBones[0].mInverseBind.mRotation = { 0.f, 1.f, 0.f, -1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
            shape.mBones[0].mInverseBind.mTranslation = { 0.f, 0.f, -10.f };
            shape.mBones[1].mName = "NPC Spine [Spn0]";
            shape.mBoneIndices.assign(4, { 0, 0, 0, 0 });
            shape.mBoneWeights.assign(4, { 1.f, 0.f, 0.f, 0.f });
            model.mShapes.push_back(shape);
            return model;
        }

        // Builds skeleton + skin, links them as MWAnim::create does and returns the skin and its matrix array.
        Found link(vsg::ref_ptr<vsg::Group>& root, Skinned::SkinnedModel model, std::unique_ptr<Anim::Bones>& bones)
        {
            MWRender::SkyrimModel::BuildResult built = MWRender::SkyrimModel::buildSkinnedNode(model, context());
            EXPECT_TRUE(built.mNode);
            root = MWRender::SkyrimActor::buildSkeletonNodes(Skeleton::plan(skeletonScene()));
            root->addChild(built.mNode);
            Anim::Contents contents;
            contents.add(Anim::Contents::Skins | Anim::Contents::Controllers | Anim::Contents::DynamicBounds);
            auto meta = vsg::ref_ptr{ new Anim::Meta(contents) };
            meta->attachTo(*root);
            vsg::ref_ptr<vsg::Node> node = root;
            MWAnim::CloneResult result = MWAnim::cloneIfRequired(node);
            root = node.cast<vsg::Group>();
            bones = std::make_unique<Anim::Bones>(MWAnim::Bones(*node));
            Anim::Transform worldRoot;
            Anim::Context ctx{ { &worldRoot }, { node.get() }, bones.get(), nullptr };
            Found found;
            result.link(ctx, [&found](const Anim::Controller* controller, vsg::Object* target) {
                found.skin = dynamic_cast<const Anim::Skin*>(controller);
                found.matrices = dynamic_cast<vsg::mat4Array*>(target);
            });
            return found;
        }
    };

    bool near(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) <= eps;
    }
}

TEST_F(SkyrimActorTest, the_skeleton_nodes_are_named_and_nested_as_the_plan_says)
{
    auto root = MWRender::SkyrimActor::buildSkeletonNodes(Skeleton::plan(skeletonScene()));
    ASSERT_EQ(root->children.size(), 1u);
    Anim::Bones bones = MWAnim::Bones(*root);
    Anim::Bone* spine = bones.search("npc spine [spn0]");
    ASSERT_NE(spine, nullptr);
    EXPECT_EQ(spine->path.size(), 3u); // root, pelvis, spine
    EXPECT_EQ(vsgUtil::getName(*spine->node()), "NPC Spine [Spn0]");
    EXPECT_FLOAT_EQ(spine->node()->translation.z, 5.f);
    Anim::Bone* pelvis = bones.search("NPC Pelvis [Pelv]");
    ASSERT_NE(pelvis, nullptr);
    EXPECT_FLOAT_EQ(pelvis->node()->translation.z, 10.f);
}

TEST_F(SkyrimActorTest, the_files_row_major_rotation_becomes_the_transforms_transposed_rotation)
{
    auto root = MWRender::SkyrimActor::buildSkeletonNodes(Skeleton::plan(skeletonScene()));
    Anim::Bones bones = MWAnim::Bones(*root);
    const Anim::Transform* pelvis = bones.search("npc pelvis [pelv]")->node();
    // convertTrafo: rotation(j, i) = file[i][j]. The file's first row is (0, -1, 0).
    EXPECT_FLOAT_EQ(pelvis->rotation(0, 0), 0.f);
    EXPECT_FLOAT_EQ(pelvis->rotation(1, 0), -1.f);
    EXPECT_FLOAT_EQ(pelvis->rotation(0, 1), 1.f);
}

TEST_F(SkyrimActorTest, a_skinned_shape_is_one_skinned_draw_with_a_bone_matrix_per_bone)
{
    auto built = MWRender::SkyrimModel::buildSkinnedNode(pelvisQuad(), context());
    ASSERT_TRUE(built.mNode);
    EXPECT_EQ(built.mDraws, 1u);
    struct Census : vsg::Visitor
    {
        int draws = 0;
        std::uint32_t arrays = 0;
        void apply(vsg::Object& o) override { o.traverse(*this); }
        void apply(vsg::VertexIndexDraw& d) override
        {
            ++draws;
            arrays = static_cast<std::uint32_t>(d.arrays.size());
        }
    } census;
    built.mNode->accept(census);
    EXPECT_EQ(census.draws, 1);
    EXPECT_EQ(census.arrays, 4u); // position, normal, bone indices, bone weights (no uv in this shape)
}

TEST_F(SkyrimActorTest, at_the_rest_pose_every_bone_matrix_is_the_identity)
{
    vsg::ref_ptr<vsg::Group> root;
    std::unique_ptr<Anim::Bones> bones;
    const Found found = link(root, pelvisQuad(), bones);
    ASSERT_NE(found.skin, nullptr);
    ASSERT_NE(found.matrices, nullptr);
    ASSERT_GE(found.matrices->size(), 2u);
    const_cast<Anim::Skin*>(found.skin)->apply(*found.matrices, 0.f);
    const vsg::mat4& m = found.matrices->at(0);
    EXPECT_TRUE(near(m[0][0], 1.f) && near(m[1][1], 1.f) && near(m[2][2], 1.f)) << "diagonal";
    EXPECT_TRUE(near(m[0][1], 0.f) && near(m[1][0], 0.f)) << "no turn left over";
    EXPECT_TRUE(near(m[3][0], 0.f) && near(m[3][1], 0.f) && near(m[3][2], 0.f)) << "no offset left over";
}

TEST_F(SkyrimActorTest, the_skin_datas_own_transform_is_not_applied_on_top_of_the_bone_bind)
{
    // Skyrim SE skin data carries a transform of its own (in the real files a lift of 120 units: the shape's node
    // transform) next to the per-bone binds, which already include it. Applying it again (Anim::Skin::transform, the
    // Morrowind rule) drops every such body, dress and glove 120 units below the actor, leaving hair, eyes and brows
    // (no skin transform) floating as a head with no body. At the rest pose the bone matrices must stay the identity.
    Skinned::SkinnedModel model = pelvisQuad();
    model.mShapes[0].mHasSkinTransform = true;
    model.mShapes[0].mSkinTransform.mTranslation = { 0.f, 1.5f, -120.f };
    vsg::ref_ptr<vsg::Group> root;
    std::unique_ptr<Anim::Bones> bones;
    const Found found = link(root, model, bones);
    ASSERT_NE(found.skin, nullptr);
    ASSERT_NE(found.matrices, nullptr);
    const_cast<Anim::Skin*>(found.skin)->apply(*found.matrices, 0.f);
    const vsg::mat4& m = found.matrices->at(0);
    EXPECT_TRUE(near(m[3][0], 0.f) && near(m[3][1], 0.f) && near(m[3][2], 0.f))
        << "offset " << m[3][0] << "," << m[3][1] << "," << m[3][2];
}

TEST_F(SkyrimActorTest, moving_the_bone_moves_the_skin_by_the_same_amount)
{
    vsg::ref_ptr<vsg::Group> root;
    std::unique_ptr<Anim::Bones> bones;
    const Found found = link(root, pelvisQuad(), bones);
    ASSERT_NE(found.skin, nullptr);
    ASSERT_NE(found.matrices, nullptr);
    Anim::Bone* pelvis = bones->search("npc pelvis [pelv]");
    ASSERT_NE(pelvis, nullptr);
    pelvis->node()->translation.x += 3.f;
    const_cast<Anim::Skin*>(found.skin)->apply(*found.matrices, 0.f);
    const vsg::mat4& m = found.matrices->at(0);
    EXPECT_TRUE(near(m[3][0], 3.f)) << m[3][0];
    EXPECT_TRUE(near(m[3][1], 0.f));
    EXPECT_TRUE(near(m[3][2], 0.f));
}

TEST_F(SkyrimActorTest, an_actor_whose_skeleton_cannot_be_read_gives_no_node_and_a_reason)
{
    // A VFS with no files: the skeleton is not found.
    VFS::Manager vfs;
    openeden::sdk::game::ActorAppearance appearance;
    appearance.skeletonPath = "actors\\character\\character assets\\skeleton.nif";
    MWRender::SkyrimActor::Report report;
    EXPECT_FALSE(MWRender::SkyrimActor::buildActorNode(appearance, vfs, context(), report));
    EXPECT_FALSE(report.mFailure.empty());
}
