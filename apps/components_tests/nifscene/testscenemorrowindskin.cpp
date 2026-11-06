#include <components/nif/exception.hpp>
#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../nif/nifbuilder.hpp"

// Task 0082, item (c): a skinned NiTriShape of the Morrowind and Oblivion era in the same Scene type as the Skyrim
// shapes. Generated 4.0.0.2 bytes only.
namespace
{
    using namespace NifScene;
    using Nif::Testing::ByteWriter;

    constexpr VFS::Path::NormalizedView path("mw-skin-test.nif");

    struct Weight
    {
        std::uint16_t mVertex;
        float mWeight;
    };

    void avObject(ByteWriter& out, const std::string& name, float x)
    {
        out.sized(name);
        out.put<std::int32_t>(-1); // extra data
        out.put<std::int32_t>(-1); // controller
        out.put<std::uint16_t>(0); // flags
        out.floats({ x, 0.f, 0.f });
        out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
        out.put(1.f);
        out.floats({ 0, 0, 0 }); // velocity
        out.put<std::int32_t>(0); // properties
        out.put<std::int32_t>(0); // no bounds
    }

    void nodeRecord(ByteWriter& out, const std::string& name, float x, const std::vector<std::int32_t>& children)
    {
        out.sized("NiNode");
        avObject(out, name, x);
        out.put(static_cast<std::int32_t>(children.size()));
        for (std::int32_t child : children)
            out.put(child);
        out.put<std::int32_t>(0); // effects
    }

    // Records: 0 root, 1 the shape (data 2, skin 3), 2 its data (three vertices), 3 the skin instance (data 6, root 0,
    // bones 4 and 5), 4 and 5 the bones, 6 the skin data.
    std::string skinnedFile(const std::vector<std::vector<Weight>>& weights, bool withSkin = true)
    {
        ByteWriter out;
        out.raw("NetImmerse File Format, Version 4.0.0.2\n");
        out.put<std::uint32_t>(0x04000002);
        out.put<std::uint32_t>(withSkin ? 7 : 3);

        nodeRecord(out, "Root", 0.f, withSkin ? std::vector<std::int32_t>{ 1, 4, 5 } : std::vector<std::int32_t>{ 1 });

        out.sized("NiTriShape");
        avObject(out, "Body", 1.f);
        out.put<std::int32_t>(2);
        out.put<std::int32_t>(withSkin ? 3 : -1);

        out.sized("NiTriShapeData");
        out.put<std::uint16_t>(3);
        out.put<std::int32_t>(1);
        out.floats({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        out.put<std::int32_t>(0); // no normals
        out.floats({ 0, 0, 0, 1 });
        out.put<std::int32_t>(0); // no colours
        out.put<std::uint16_t>(0); // no UV sets
        out.put<std::int32_t>(0);
        out.put<std::uint16_t>(1);
        out.put<std::uint32_t>(3);
        for (const std::uint16_t index : std::array<std::uint16_t, 3>{ 0, 1, 2 })
            out.put(index);
        out.put<std::uint16_t>(0);

        if (withSkin)
        {
            out.sized("NiSkinInstance");
            out.put<std::int32_t>(6);
            out.put<std::int32_t>(0);
            out.put<std::uint32_t>(2);
            out.put<std::int32_t>(4);
            out.put<std::int32_t>(5);

            nodeRecord(out, "BoneA", 10.f, {});
            nodeRecord(out, "BoneB", 20.f, {});

            out.sized("NiSkinData");
            out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 }); // skin transform rotation
            out.floats({ 0.f, 0.f, 0.5f });
            out.put(2.f); // scale
            out.put<std::uint32_t>(static_cast<std::uint32_t>(weights.size()));
            out.put<std::int32_t>(-1); // partitions
            for (std::size_t bone = 0; bone < weights.size(); ++bone)
            {
                out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
                out.floats({ static_cast<float>(bone + 1), 0.f, 0.f });
                out.put(1.f);
                out.floats({ 0, 0, 0, 1 }); // bounding sphere
                out.put<std::uint16_t>(static_cast<std::uint16_t>(weights[bone].size()));
                for (const Weight& w : weights[bone])
                {
                    out.put(w.mVertex);
                    out.put(w.mWeight);
                }
            }
        }

        out.put<std::uint32_t>(1);
        out.put<std::int32_t>(0);
        return out.bytes();
    }

    Scene read(const std::string& bytes)
    {
        auto input = std::make_unique<std::istringstream>(bytes);
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return readScene(std::move(input), path, SceneOptions{});
    }

    std::string errorOf(const std::string& bytes)
    {
        try
        {
            read(bytes);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    const std::vector<std::vector<Weight>> twoBones{ { { 0, 1.f }, { 1, 0.25f } }, { { 1, 0.75f }, { 2, 1.f } } };

    TEST(NifSceneMorrowindSkin, a_skinned_ni_tri_shape_has_a_skin_with_bone_names_and_bind_transforms)
    {
        const Scene scene = read(skinnedFile(twoBones));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        ASSERT_EQ(scene.mGeometries[0].mSkin, 0u);
        ASSERT_EQ(scene.mSkins.size(), 1u);
        const Skin& skin = scene.mSkins[0];
        EXPECT_EQ(skin.mBoneNames, (std::vector<std::string>{ "BoneA", "BoneB" }));
        // Nodes in record order: Root 0, Body 1, BoneA 2, BoneB 3.
        EXPECT_EQ(skin.mBoneNodes, (std::vector<std::uint32_t>{ 2, 3 }));
        EXPECT_EQ(skin.mSkinTransform.mTranslation, (Vec3{ 0.f, 0.f, 0.5f }));
        EXPECT_EQ(skin.mSkinTransform.mScale, 2.f);
        ASSERT_EQ(skin.mBindTransforms.size(), 2u);
        EXPECT_EQ(skin.mBindTransforms[0].mTranslation, (Vec3{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(skin.mBindTransforms[1].mTranslation, (Vec3{ 2.f, 0.f, 0.f }));
        EXPECT_TRUE(skin.mPartitions.empty());
    }

    TEST(NifSceneMorrowindSkin, per_bone_weights_become_per_vertex_influences_in_bone_order)
    {
        const Scene scene = read(skinnedFile(twoBones));
        const Geometry& g = scene.mGeometries.at(0);
        EXPECT_EQ(g.mInfluenceStart, (std::vector<std::uint32_t>{ 0, 1, 3, 4 }));
        ASSERT_EQ(g.mInfluences.size(), 4u);
        EXPECT_EQ(g.mInfluences[0].mBone, 0u);
        EXPECT_EQ(g.mInfluences[0].mWeight, 1.f);
        EXPECT_EQ(g.mInfluences[1].mBone, 0u);
        EXPECT_EQ(g.mInfluences[1].mWeight, 0.25f);
        EXPECT_EQ(g.mInfluences[2].mBone, 1u);
        EXPECT_EQ(g.mInfluences[2].mWeight, 0.75f);
        EXPECT_EQ(g.mInfluences[3].mBone, 1u);
        EXPECT_EQ(g.mInfluences[3].mWeight, 1.f);
        EXPECT_FALSE(g.mVerticesFromSkinPartition);
    }

    TEST(NifSceneMorrowindSkin, a_vertex_no_bone_weights_gets_an_empty_range)
    {
        const std::vector<std::vector<Weight>> partial{ { { 0, 1.f } }, { { 2, 0.5f } } };
        const Scene scene = read(skinnedFile(partial));
        const Geometry& g = scene.mGeometries.at(0);
        EXPECT_EQ(g.mInfluenceStart, (std::vector<std::uint32_t>{ 0, 1, 1, 2 }));
    }

    TEST(NifSceneMorrowindSkin, a_shape_without_a_skin_has_no_influences_and_no_skin)
    {
        const Scene scene = read(skinnedFile({}, false));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        EXPECT_EQ(scene.mGeometries[0].mSkin, sNone);
        EXPECT_TRUE(scene.mGeometries[0].mInfluences.empty());
        EXPECT_TRUE(scene.mGeometries[0].mInfluenceStart.empty());
        EXPECT_TRUE(scene.mSkins.empty());
    }

    TEST(NifSceneMorrowindSkin, a_weight_on_a_vertex_outside_the_shape_is_a_located_error)
    {
        const std::string error = errorOf(skinnedFile({ { { 7, 1.f } }, {} }));
        EXPECT_NE(error.find("mw-skin-test.nif"), std::string::npos) << error;
        EXPECT_NE(error.find("7"), std::string::npos) << error;
        EXPECT_NE(error.find("Body"), std::string::npos) << error;
    }

    TEST(NifSceneMorrowindSkin, the_positions_and_triangles_are_the_same_with_and_without_the_skin)
    {
        const Scene skinnedScene = read(skinnedFile(twoBones));
        const Scene plainScene = read(skinnedFile({}, false));
        const Geometry& skinned = skinnedScene.mGeometries.at(0);
        const Geometry& plain = plainScene.mGeometries.at(0);
        EXPECT_EQ(skinned.mPositions, plain.mPositions);
        EXPECT_EQ(skinned.mTriangles, plain.mTriangles);
    }

    TEST(NifSceneMorrowindSkin, the_dump_shows_the_skin_and_is_repeatable)
    {
        const std::string a = dump(read(skinnedFile(twoBones)));
        EXPECT_EQ(a, dump(read(skinnedFile(twoBones))));
        EXPECT_NE(a.find("BoneA"), std::string::npos) << a;
        EXPECT_NE(a, dump(read(skinnedFile({ { { 0, 0.5f } }, {} }))));
    }
}
