#include "../resource/colladadocuments.hpp"

#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/dae/daeanim.hpp>

#include <vsg/io/Options.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace testing;
    using namespace ColladaTesting;

    constexpr float tolerance = 1e-4f;

    constexpr std::string_view modelPath = "meshes/anim.dae";
    constexpr std::string_view textKeysPath = "meshes/anim.txt";

    // 90 degrees about Z, row-major: x -> y.
    std::string makeRotationZ90Matrix(float x, float y, float z)
    {
        std::ostringstream stream;
        stream << "0 -1 0 " << x << " 1 0 0 " << y << " 0 0 1 " << z << " 0 0 0 1";
        return stream.str();
    }

    void expectVec3(const vsg::vec3& actual, float x, float y, float z)
    {
        EXPECT_NEAR(actual.x, x, tolerance);
        EXPECT_NEAR(actual.y, y, tolerance);
        EXPECT_NEAR(actual.z, z, tolerance);
    }

    // q and -q are the same rotation.
    void expectSameRotation(const vsg::quat& actual, const vsg::quat& expected)
    {
        const double dot
            = actual.x * expected.x + actual.y * expected.y + actual.z * expected.z + actual.w * expected.w;
        EXPECT_NEAR(std::abs(dot), 1.0, tolerance)
            << "actual (" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")";
    }

    // A static box under a non-animated parent "Root", with the given child nodes next to the box.
    std::string makeStaticNodes(std::string_view children)
    {
        return makeNode(
            "Root", "Root", makeMatrix(identityMatrix) + makeInstanceGeometry("box") + std::string(children));
    }

    struct VsgAdaptersDaeAnimationTest : Test
    {
        std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> mFiles;
        std::unique_ptr<VFS::Manager> mVfs;

        static vsg::ref_ptr<vsg::Options> makeOptions(std::string_view extensionHint = ".dae")
        {
            auto options = vsg::Options::create();
            options->extensionHint = std::string(extensionHint);
            options->setValue("filename", std::string(modelPath));
            return options;
        }

        // Reads `document` as the model at modelPath; extra VFS files (such as the text keys) are added next to it.
        vsg::ref_ptr<Anim::ControllerMap> read(const std::string& document,
            std::initializer_list<std::pair<std::string_view, std::string>> extraFiles = {},
            std::string_view extensionHint = ".dae")
        {
            std::vector<std::pair<std::string_view, std::string>> files{ { modelPath, document } };
            files.insert(files.end(), extraFiles.begin(), extraFiles.end());
            VFS::FileMap fileMap;
            for (const auto& [path, content] : files)
            {
                mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(content));
                fileMap.emplace(VFS::Path::Normalized(path), mFiles.back().get());
            }
            mVfs = TestingOpenMW::createTestVFS(std::move(fileMap));

            const vsg::ref_ptr<vsgAdapters::daeAnimation> reader{ new vsgAdapters::daeAnimation(*mVfs) };
            std::istringstream stream(document);
            const vsg::ref_ptr<vsg::Object> object = reader->read(stream, makeOptions(extensionHint));
            if (object == nullptr)
                return {};
            vsg::ref_ptr<Anim::ControllerMap> map(dynamic_cast<Anim::ControllerMap*>(object.get()));
            EXPECT_NE(map, nullptr) << "read returned an object that is not an Anim::ControllerMap";
            return map;
        }

        static const Anim::TransformController* findController(const Anim::ControllerMap& map, const std::string& name)
        {
            const auto found = map.map.find(name);
            if (found == map.map.end())
                return nullptr;
            return found->second.get();
        }
    };

    // An unskinned model whose node "Bone" (id "Bone-id") is animated from translation (1, 2, 3) at 0 to (4, 5, 6)
    // at `endTime`.
    std::string makeSingleAnimationDocument(float endTime = 1)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation(
            "bone-anim", "Bone-id", { 0, endTime }, { makeTranslationMatrix(1, 2, 3), makeTranslationMatrix(4, 5, 6) });
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 2, 3))));
        return makeColladaDocument(parts);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_return_null_for_other_extension_hint)
    {
        EXPECT_EQ(read(makeSingleAnimationDocument(), {}, ".kf"), nullptr);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_return_null_for_model_without_animations)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 2, 3))));

        EXPECT_EQ(read(makeColladaDocument(parts)), nullptr);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_return_one_controller_per_animated_node_keyed_by_node_name)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation("first-anim", "First-id", { 0, 1 },
                                { makeTranslationMatrix(0, 0, 0), makeTranslationMatrix(1, 0, 0) })
            + makeMatrixAnimation("second-anim", "Second-id", { 0, 1 },
                { makeTranslationMatrix(0, 0, 0), makeTranslationMatrix(0, 1, 0) });
        parts.mNodes = makeStaticNodes(makeNode("First-id", "First", makeMatrix(identityMatrix))
            + makeNode("Second-id", "Second", makeMatrix(identityMatrix))
            + makeNode("Still-id", "Still", makeMatrix(identityMatrix)));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        EXPECT_EQ(map->map.size(), 2u);
        EXPECT_NE(findController(*map, "First"), nullptr);
        EXPECT_NE(findController(*map, "Second"), nullptr);
        EXPECT_EQ(findController(*map, "First-id"), nullptr);
        EXPECT_EQ(findController(*map, "Still"), nullptr);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_take_translation_from_matrix_keys)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument());

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(0), 1, 2, 3);
        expectVec3(controller->translate->value(1), 4, 5, 6);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_take_rotation_from_matrix_keys)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation(
            "bone-anim", "Bone-id", { 0, 1 }, { std::string(identityMatrix), makeRotationZ90Matrix(0, 0, 0) });
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(identityMatrix)));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->rotate, nullptr);
        const float halfSqrt2 = std::sqrt(0.5f);
        expectSameRotation(controller->rotate->value(0), vsg::quat(0, 0, 0, 1));
        expectSameRotation(controller->rotate->value(1), vsg::quat(0, 0, halfSqrt2, halfSqrt2));
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_use_seconds_for_animation_times)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument(0.5f));

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(0), 1, 2, 3);
        expectVec3(controller->translate->value(0.25f), 2.5f, 3.5f, 4.5f);
        expectVec3(controller->translate->value(0.5f), 4, 5, 6);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_keep_underscores_in_node_names_without_skin)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation(
            "spine-anim", "Bip01_Spine", { 0, 1 }, { makeTranslationMatrix(0, 0, 0), makeTranslationMatrix(0, 0, 1) });
        parts.mNodes = makeStaticNodes(makeNode("Bip01_Spine", "Bip01_Spine", makeMatrix(identityMatrix)));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        EXPECT_NE(findController(*map, "Bip01_Spine"), nullptr);
        EXPECT_EQ(findController(*map, "Bip01 Spine"), nullptr);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_replace_underscores_with_spaces_in_node_names_with_skin)
    {
        const std::vector<std::string> joints = { "Bip01", "Bip01_Spine" };
        const std::vector<SkinnedVertex> vertices = {
            { { 0, 0, 0 }, { { "Bip01", 1.0f } } },
            { { 1, 0, 0 }, { { "Bip01", 0.5f }, { "Bip01_Spine", 0.5f } } },
            { { 0, 1, 0 }, { { "Bip01_Spine", 1.0f } } },
        };
        std::vector<Position> positions;
        for (const SkinnedVertex& vertex : vertices)
            positions.push_back(vertex.mPosition);

        const std::string spine
            = makeNode("Armature_Bip01_Spine", "Bip01_Spine", makeMatrix(makeTranslationMatrix(0, 0, 1)), true);
        const std::string bip01 = makeNode("Armature_Bip01", "Bip01", makeMatrix(identityMatrix) + spine, true);
        const std::string mesh
            = makeNode("Mesh", "Mesh", makeMatrix(identityMatrix) + makeInstanceController("skin", "Armature_Bip01"));

        DocumentParts parts;
        parts.mGeometries = makeGeometry("mesh", positions, { 0, 1, 2 });
        parts.mControllers = makeSkinController("skin", "mesh", joints, vertices, {});
        parts.mAnimations = makeMatrixAnimation("spine-anim", "Armature_Bip01_Spine", { 0, 1 },
            { makeTranslationMatrix(0, 0, 1), makeTranslationMatrix(0, 0, 2) });
        parts.mNodes = makeNode("Armature", "Armature", makeMatrix(identityMatrix) + bip01) + mesh;

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bip01 Spine");
        ASSERT_NE(controller, nullptr);
        EXPECT_EQ(findController(*map, "Bip01_Spine"), nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(0), 0, 0, 1);
        expectVec3(controller->translate->value(1), 0, 0, 2);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_place_clip_animation_at_clip_start)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation(
            "walk-anim", "Bone-id", { 0, 1 }, { makeTranslationMatrix(1, 0, 0), makeTranslationMatrix(2, 0, 0) });
        parts.mAnimationClips = makeAnimationClip("walk", 10, 11, { "walk-anim" });
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 0, 0))));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(10), 1, 0, 0);
        expectVec3(controller->translate->value(10.5f), 1.5f, 0, 0);
        expectVec3(controller->translate->value(11), 2, 0, 0);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_merge_clips_of_same_node_into_one_timeline)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation("walk-anim", "Bone-id", { 0, 1 },
                                { makeTranslationMatrix(1, 0, 0), makeTranslationMatrix(2, 0, 0) })
            + makeMatrixAnimation(
                "run-anim", "Bone-id", { 0, 1 }, { makeTranslationMatrix(5, 0, 0), makeTranslationMatrix(6, 0, 0) });
        parts.mAnimationClips
            = makeAnimationClip("walk", 10, 11, { "walk-anim" }) + makeAnimationClip("run", 20, 21, { "run-anim" });
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 0, 0))));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        EXPECT_EQ(map->map.size(), 1u);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(10), 1, 0, 0);
        expectVec3(controller->translate->value(11), 2, 0, 0);
        expectVec3(controller->translate->value(20), 5, 0, 0);
        expectVec3(controller->translate->value(21), 6, 0, 0);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_read_text_keys_from_txt_next_to_model)
    {
        const std::string textKeys
            = "Idle: Start 0.5\r\n"
              "  Idle: Stop 2  \r\n"
              "WalkForward: Loop Start 1\r\n";

        const vsg::ref_ptr<Anim::ControllerMap> map
            = read(makeSingleAnimationDocument(), { { textKeysPath, textKeys } });

        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        std::vector<std::pair<float, std::string>> tags(map->tags->begin(), map->tags->end());
        EXPECT_THAT(tags,
            ElementsAre(Pair(FloatNear(0.5f, tolerance), "idle: start"),
                Pair(FloatNear(1, tolerance), "walkforward: loop start"), Pair(FloatNear(2, tolerance), "idle: stop")));
        EXPECT_TRUE(map->tags->hasGroup("idle"));
        EXPECT_TRUE(map->tags->hasGroup("walkforward"));
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_read_text_keys_with_lf_line_endings)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map
            = read(makeSingleAnimationDocument(), { { textKeysPath, "idle: start 0\nidle: stop 1\n" } });

        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        std::vector<std::pair<float, std::string>> tags(map->tags->begin(), map->tags->end());
        EXPECT_THAT(tags,
            ElementsAre(Pair(FloatNear(0, tolerance), "idle: start"), Pair(FloatNear(1, tolerance), "idle: stop")));
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_place_nested_clip_animations_at_clip_start)
    {
        // Blender 2.8+: an action container holds one nested animation per node; the clip instances the nested ids.
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeAnimationContainer("action_container-Armature",
            makeMatrixAnimation("First_pose_matrix", "First-id", { 0, 1 },
                { makeTranslationMatrix(1, 0, 0), makeTranslationMatrix(2, 0, 0) })
                + makeMatrixAnimation("Second_pose_matrix", "Second-id", { 0, 1 },
                    { makeTranslationMatrix(0, 3, 0), makeTranslationMatrix(0, 4, 0) }));
        parts.mAnimationClips = makeAnimationClip("walk", 10, 11, { "First_pose_matrix", "Second_pose_matrix" });
        parts.mNodes = makeStaticNodes(makeNode("First-id", "First", makeMatrix(makeTranslationMatrix(1, 0, 0)))
            + makeNode("Second-id", "Second", makeMatrix(makeTranslationMatrix(0, 3, 0))));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        EXPECT_EQ(map->map.size(), 2u);
        const Anim::TransformController* first = findController(*map, "First");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(first->translate, nullptr);
        expectVec3(first->translate->value(10), 1, 0, 0);
        expectVec3(first->translate->value(10.5f), 1.5f, 0, 0);
        expectVec3(first->translate->value(11), 2, 0, 0);
        const Anim::TransformController* second = findController(*map, "Second");
        ASSERT_NE(second, nullptr);
        ASSERT_NE(second->translate, nullptr);
        expectVec3(second->translate->value(10), 0, 3, 0);
        expectVec3(second->translate->value(10.5f), 0, 3.5f, 0);
        expectVec3(second->translate->value(11), 0, 4, 0);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_should_shift_time_source_shared_by_samplers_once)
    {
        // One animation, one time source, two samplers and channels for two nodes.
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeSharedTimeMatrixAnimation("pair-anim", { "First-id", "Second-id" }, { 0, 1 },
            {
                { makeTranslationMatrix(1, 0, 0), makeTranslationMatrix(2, 0, 0) },
                { makeTranslationMatrix(0, 3, 0), makeTranslationMatrix(0, 4, 0) },
            });
        parts.mAnimationClips = makeAnimationClip("walk", 10, 11, { "pair-anim" });
        parts.mNodes = makeStaticNodes(makeNode("First-id", "First", makeMatrix(makeTranslationMatrix(1, 0, 0)))
            + makeNode("Second-id", "Second", makeMatrix(makeTranslationMatrix(0, 3, 0))));

        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeColladaDocument(parts));

        ASSERT_NE(map, nullptr);
        EXPECT_EQ(map->map.size(), 2u);
        const Anim::TransformController* first = findController(*map, "First");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(first->translate, nullptr);
        expectVec3(first->translate->value(10), 1, 0, 0);
        expectVec3(first->translate->value(10.5f), 1.5f, 0, 0);
        expectVec3(first->translate->value(11), 2, 0, 0);
        const Anim::TransformController* second = findController(*map, "Second");
        ASSERT_NE(second, nullptr);
        ASSERT_NE(second->translate, nullptr);
        expectVec3(second->translate->value(10), 0, 3, 0);
        expectVec3(second->translate->value(10.5f), 0, 3.5f, 0);
        expectVec3(second->translate->value(11), 0, 4, 0);
    }

    TEST_F(VsgAdaptersDaeAnimationTest, read_without_txt_should_have_empty_tags)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument());

        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        EXPECT_TRUE(map->tags->empty());
    }
}
