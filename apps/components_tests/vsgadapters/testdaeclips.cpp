#include "../resource/colladadocuments.hpp"

#include <components/animation/animset.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/testing/util.hpp>
#include <components/mwanimation/context.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/dae/daeclips.hpp>
#include <components/vsgadapters/vfs.hpp>

#include <vsg/io/Options.h>
#include <vsg/utils/SharedObjects.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
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

    struct VsgAdaptersDaeClipsTest : Test
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

            const vsg::ref_ptr<vsgAdapters::daeClips> reader{ new vsgAdapters::daeClips(*mVfs) };
            std::istringstream stream(document);
            const vsg::ref_ptr<vsg::Object> object = reader->read(stream, makeOptions(extensionHint));
            if (object == nullptr)
                return {};
            mLastObject = object;
            mLastSet = vsg::ref_ptr<const Anim::AnimSet>(dynamic_cast<const Anim::AnimSet*>(object.get()));
            EXPECT_NE(mLastSet, nullptr) << "read returned an object that is not an Anim::AnimSet";
            if (mLastSet == nullptr)
                return {};
            // The keyframe controllers inside the set: what the old reader returned directly.
            return vsg::ref_ptr<Anim::ControllerMap>(const_cast<Anim::ControllerMap*>(mLastSet->controllers.get()));
        }

        /// The object and the set the last read returned.
        vsg::ref_ptr<vsg::Object> mLastObject;
        vsg::ref_ptr<const Anim::AnimSet> mLastSet;

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

    // Port of VsgAdaptersDaeAnimationTest.read_should_return_null_for_other_extension_hint (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_return_null_for_other_extension_hint)
    {
        EXPECT_EQ(read(makeSingleAnimationDocument(), {}, ".kf"), nullptr);
    }

    // Port of VsgAdaptersDaeAnimationTest.read_should_return_null_for_model_without_animations (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_return_null_for_model_without_animations)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 2, 3))));

        EXPECT_EQ(read(makeColladaDocument(parts)), nullptr);
    }

    // Port of VsgAdaptersDaeAnimationTest.read_should_return_one_controller_per_animated_node_keyed_by_node_name (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_return_one_controller_per_animated_node_keyed_by_node_name)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_take_translation_from_matrix_keys (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_take_translation_from_matrix_keys)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument());

        ASSERT_NE(map, nullptr);
        const Anim::TransformController* controller = findController(*map, "Bone");
        ASSERT_NE(controller, nullptr);
        ASSERT_NE(controller->translate, nullptr);
        expectVec3(controller->translate->value(0), 1, 2, 3);
        expectVec3(controller->translate->value(1), 4, 5, 6);
    }

    // Port of VsgAdaptersDaeAnimationTest.read_should_take_rotation_from_matrix_keys (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_take_rotation_from_matrix_keys)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_use_seconds_for_animation_times (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_use_seconds_for_animation_times)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_keep_underscores_in_node_names_without_skin (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_keep_underscores_in_node_names_without_skin)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_replace_underscores_with_spaces_in_node_names_with_skin (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_replace_underscores_with_spaces_in_node_names_with_skin)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_place_clip_animation_at_clip_start (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_place_clip_animation_at_clip_start)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_merge_clips_of_same_node_into_one_timeline (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_merge_clips_of_same_node_into_one_timeline)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_read_text_keys_from_txt_next_to_model (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_read_text_keys_from_txt_next_to_model)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_read_text_keys_with_lf_line_endings (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_read_text_keys_with_lf_line_endings)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map
            = read(makeSingleAnimationDocument(), { { textKeysPath, "idle: start 0\nidle: stop 1\n" } });

        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        std::vector<std::pair<float, std::string>> tags(map->tags->begin(), map->tags->end());
        EXPECT_THAT(tags,
            ElementsAre(Pair(FloatNear(0, tolerance), "idle: start"), Pair(FloatNear(1, tolerance), "idle: stop")));
    }

    // Port of VsgAdaptersDaeAnimationTest.read_should_place_nested_clip_animations_at_clip_start (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_place_nested_clip_animations_at_clip_start)
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

    // Port of VsgAdaptersDaeAnimationTest.read_should_shift_time_source_shared_by_samplers_once (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_should_shift_time_source_shared_by_samplers_once)
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

    // Port of VsgAdaptersDaeAnimationTest.read_without_txt_should_have_empty_tags (task 0124, stage C3).
    TEST_F(VsgAdaptersDaeClipsTest, read_without_txt_should_have_empty_tags)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument());

        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        EXPECT_TRUE(map->tags->empty());
    }

    // A digest of everything a controller map says: the node names, every controller sampled at 60 Hz over the span of
    // the text keys (float bit patterns, so exact) and the text keys themselves. FNV-1a, 64 bit.
    struct Digest
    {
        std::uint64_t mValue = 1469598103934665603ull;
        void add(const void* data, std::size_t size)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i)
                mValue = (mValue ^ bytes[i]) * 1099511628211ull;
        }
        void add(float value) { add(&value, sizeof(value)); }
        void add(const std::string& text) { add(text.data(), text.size()); }
    };

    std::uint64_t digestOf(const Anim::ControllerMap& map)
    {
        Digest digest;
        float last = 0;
        if (map.tags != nullptr)
            for (const auto& [time, text] : *map.tags)
            {
                digest.add(time);
                digest.add(text);
                last = std::max(last, time);
            }
        std::vector<std::string> names;
        for (const auto& entry : map.map)
            names.push_back(entry.first);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names)
        {
            digest.add(name);
            const Anim::TransformController& controller = *map.map.at(name);
            for (float time = 0; time <= last + 1.f; time += 1.f / 60.f)
            {
                if (controller.translate != nullptr)
                {
                    const vsg::vec3 value = controller.translate->value(time);
                    digest.add(value.x), digest.add(value.y), digest.add(value.z);
                }
                if (controller.rotate != nullptr)
                {
                    const vsg::quat value = controller.rotate->value(time);
                    digest.add(value.x), digest.add(value.y), digest.add(value.z), digest.add(value.w);
                }
                if (controller.scale != nullptr)
                    digest.add(controller.scale->value(time));
            }
        }
        return digest.mValue;
    }

    // The two animated models of the OpenMW example suite (the Lua integration tests' player and creature), read
    // through the real reader: OPENEDEN_ANIMSET_SUITE_DIR is the suite's folder. The numbers were taken from the
    // reader this one replaced (vsgAdapters::daeAnimation, at 9547cf0e86) and must not change.
    TEST(VsgAdaptersDaeClipsSuite, the_suite_models_give_the_group_tables_and_keyframes_the_old_reader_gave)
    {
        const char* dir = std::getenv("OPENEDEN_ANIMSET_SUITE_DIR");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_ANIMSET_SUITE_DIR is not set";
        struct Expected
        {
            const char* mModel;
            const char* mText;
            std::size_t mGroups;
            std::size_t mNodes;
            std::uint64_t mDigest;
        };
        const Expected expected[] = {
            { "example_animated_creature/data/meshes/land_racer", "land_racer", 10, 38, 10444860057075963985ull },
            { "game_template/data/meshes/BasicPlayer", "BasicPlayer", 112, 78, 10132760807064208851ull },
        };
        for (const Expected& model : expected)
        {
            SCOPED_TRACE(model.mModel);
            const std::filesystem::path base = std::filesystem::path(dir) / model.mModel;
            auto slurp = [](const std::filesystem::path& path) {
                std::ifstream file(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(file), {});
            };
            const std::string dae = slurp(base.string() + ".dae");
            const std::string txt = slurp(base.string() + ".txt");
            ASSERT_FALSE(dae.empty());
            ASSERT_FALSE(txt.empty());
            const std::string name = std::string("meshes/") + model.mText;
            std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> files;
            files.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(dae));
            files.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(txt));
            VFS::FileMap fileMap;
            fileMap.emplace(VFS::Path::Normalized(name + ".dae"), files[0].get());
            fileMap.emplace(VFS::Path::Normalized(name + ".txt"), files[1].get());
            const auto vfs = TestingOpenMW::createTestVFS(std::move(fileMap));

            auto options = vsg::Options::create();
            options->extensionHint = ".dae";
            options->setValue("filename", name + ".dae");
            const vsg::ref_ptr<vsgAdapters::daeClips> reader{ new vsgAdapters::daeClips(*vfs) };
            std::istringstream stream(dae);
            const vsg::ref_ptr<vsg::Object> object = reader->read(stream, options);
            const auto* set = dynamic_cast<const Anim::AnimSet*>(object.get());
            ASSERT_NE(set, nullptr);
            const std::uint64_t digest = digestOf(*set->controllers);
            std::cout << "[daeclips] " << model.mModel << ": " << set->groups().size() << " groups, "
                      << set->controllers->map.size() << " nodes, digest " << digest << "\n";
            EXPECT_EQ(set->groups().size(), model.mGroups);
            EXPECT_EQ(set->controllers->map.size(), model.mNodes);
            EXPECT_EQ(digest, model.mDigest);
        }
    }

    // --- Task 0124, stage C3: the reader hands the engine an AnimSet, shared by everything that asks for the file.

    TEST_F(VsgAdaptersDaeClipsTest, read_returns_an_anim_set_named_after_the_file_and_not_a_bare_controller_map)
    {
        ASSERT_NE(read(makeSingleAnimationDocument()), nullptr);

        EXPECT_EQ(dynamic_cast<Anim::ControllerMap*>(mLastObject.get()), nullptr);
        ASSERT_NE(mLastSet, nullptr);
        EXPECT_EQ(mLastSet->name, std::string(modelPath));
        ASSERT_NE(mLastSet->controllers, nullptr);
        EXPECT_EQ(mLastSet->controllers->map.size(), 1u);
    }

    TEST_F(VsgAdaptersDaeClipsTest, the_group_table_comes_from_the_text_key_file_and_not_from_the_clip_names)
    {
        // The clip in the file is called "walk"; the groups are what the .txt says (BasicPlayer.dae works this way).
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation(
            "walk-anim", "Bone-id", { 0, 1 }, { makeTranslationMatrix(1, 0, 0), makeTranslationMatrix(2, 0, 0) });
        parts.mAnimationClips = makeAnimationClip("walk", 10, 11, { "walk-anim" });
        parts.mNodes = makeStaticNodes(makeNode("Bone-id", "Bone", makeMatrix(makeTranslationMatrix(1, 0, 0))));
        const std::string textKeys = "run: start 10\nrun: loop start 10.25\nrun: loop stop 10.75\nrun: stop 11\n"
                                     "hit1 stop 11.5\nbroken: start 10\n";

        ASSERT_NE(read(makeColladaDocument(parts), { { textKeysPath, textKeys } }), nullptr);

        const Anim::AnimSet& set = *mLastSet;
        EXPECT_FALSE(set.hasGroup("walk"));
        ASSERT_TRUE(set.hasGroup("run"));
        const Anim::AnimSet::Group& run = *set.find("run");
        EXPECT_NEAR(run.start, 10.f, tolerance);
        EXPECT_NEAR(run.stop, 11.f, tolerance);
        EXPECT_NEAR(run.loopStart, 10.25f, tolerance);
        EXPECT_NEAR(run.loopStop, 10.75f, tolerance);
        // A key line without a colon names no group (BasicPlayer.txt's "hit1 stop"), and a group with only a start
        // key is listed but has no stop: neither can be played.
        EXPECT_FALSE(set.hasGroup("hit1"));
        ASSERT_TRUE(set.hasGroup("broken"));
        EXPECT_EQ(set.find("broken")->stop, Anim::AnimSet::sAbsent);
    }

    TEST_F(VsgAdaptersDaeClipsTest, the_set_is_the_one_the_animation_system_shares_for_its_controllers)
    {
        const vsg::ref_ptr<Anim::ControllerMap> map = read(makeSingleAnimationDocument());

        ASSERT_NE(map, nullptr);
        EXPECT_EQ(Anim::AnimSet::forSource(map).get(), mLastSet.get());
    }

    TEST_F(VsgAdaptersDaeClipsTest, a_model_without_text_keys_gives_a_set_with_no_groups)
    {
        ASSERT_NE(read(makeSingleAnimationDocument()), nullptr);

        EXPECT_TRUE(mLastSet->groups().empty());
        EXPECT_NE(mLastSet->tags, nullptr);
    }

    TEST_F(VsgAdaptersDaeClipsTest, the_context_reads_the_controllers_of_a_cached_set_once_per_file)
    {
        mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(makeSingleAnimationDocument()));
        mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>("idle: start 0\nidle: stop 1\n"));
        VFS::FileMap fileMap;
        fileMap.emplace(VFS::Path::Normalized(modelPath), mFiles[0].get());
        fileMap.emplace(VFS::Path::Normalized(textKeysPath), mFiles[1].get());
        mVfs = TestingOpenMW::createTestVFS(std::move(fileMap));

        auto options = vsg::Options::create();
        options->readerWriters
            = { vsg::ref_ptr{ new vsgAdapters::vfs(*mVfs, { vsg::ref_ptr{ new vsgAdapters::daeClips(*mVfs) } }) } };
        options->sharedObjects = vsg::SharedObjects::create();
        MWAnim::Context context;
        context.animOptions = options;

        const vsg::ref_ptr<Anim::ControllerMap> first = context.readAnimation(std::string(modelPath));
        const vsg::ref_ptr<Anim::ControllerMap> second = context.readAnimation(std::string(modelPath));

        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first.get(), second.get());
        EXPECT_EQ(first->map.size(), 1u);
        const vsg::ref_ptr<const Anim::AnimSet> set = Anim::AnimSet::forSource(first);
        EXPECT_TRUE(set->hasGroup("idle"));
        EXPECT_EQ(context.readAnimations({ std::string(modelPath), "meshes/missing.dae" }).size(), 1u);
    }
}
