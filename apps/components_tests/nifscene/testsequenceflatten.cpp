#include <components/nifscene/flatten.hpp>
#include <components/nifscene/scene.hpp>

#include <gtest/gtest.h>

#include <string>

// Task 0232: shapes under an animated node are kept apart and say which node moves them.
namespace
{
    using namespace NifScene;

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

    // Root (0) > Hinge (1, at x = 10) > Leaf shape (2); Root > Frame shape (3, at y = 5).
    Scene doorScene()
    {
        Scene scene;
        scene.mNodes.push_back(makeNode("Root", sNone, { 0, 0, 0 }));
        scene.mNodes.push_back(makeNode("Hinge", 0, { 10, 0, 0 }));
        scene.mNodes.push_back(makeNode("Leaf", 1, { 0, 0, 0 }, 0));
        scene.mNodes.push_back(makeNode("Frame", 0, { 0, 5, 0 }, 1));
        scene.mGeometries = { quad(), quad() };
        scene.mMaterials.emplace_back();
        scene.mRoots = { 0 };
        return scene;
    }

    TEST(NifSequenceFlatten, without_animated_nodes_the_shapes_merge_as_before)
    {
        const Flat::FlatModel model = Flat::flatten(doorScene());
        ASSERT_EQ(model.mShapes.size(), 1u);
        EXPECT_EQ(model.mShapes[0].mMergedFrom, 2u);
        EXPECT_EQ(model.mShapes[0].mAnimNode, 0xFFFFFFFFu);
    }

    TEST(NifSequenceFlatten, a_shape_under_an_animated_node_is_kept_apart_and_names_it)
    {
        Flat::Limits limits;
        limits.mAnimatedNodes = { 1 };
        const Flat::FlatModel model = Flat::flatten(doorScene(), limits);
        ASSERT_EQ(model.mShapes.size(), 2u);
        std::size_t moving = 0;
        for (const Flat::Shape& shape : model.mShapes)
        {
            if (shape.mAnimNode == 1)
            {
                ++moving;
                // baked at the rest pose: the hinge's offset is in the vertices
                EXPECT_FLOAT_EQ(shape.mPositions[0][0], 10.f);
            }
            else
            {
                EXPECT_EQ(shape.mAnimNode, 0xFFFFFFFFu);
                EXPECT_FLOAT_EQ(shape.mPositions[0][1], 5.f);
            }
        }
        EXPECT_EQ(moving, 1u);
    }

    TEST(NifSequenceFlatten, the_nearest_animated_ancestor_wins)
    {
        Flat::Limits limits;
        limits.mAnimatedNodes = { 1, 2 };
        const Flat::FlatModel model = Flat::flatten(doorScene(), limits);
        bool found = false;
        for (const Flat::Shape& shape : model.mShapes)
            found = found || shape.mAnimNode == 2;
        EXPECT_TRUE(found);
    }
}
