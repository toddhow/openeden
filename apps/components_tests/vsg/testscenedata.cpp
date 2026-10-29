#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <vsg/all.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // A test visitor to verify traversal invariants over VSG node hierarchies.
    class NodeCountingVisitor : public vsg::Visitor
    {
    public:
        unsigned int mGroupCount = 0;
        unsigned int mNodeCount = 0;
        unsigned int mTransformCount = 0;

        void apply(vsg::Node& node) override
        {
            ++mNodeCount;
            node.traverse(*this);
        }

        void apply(vsg::Group& group) override
        {
            ++mGroupCount;
            ++mNodeCount;
            group.traverse(*this);
        }

        void apply(vsg::MatrixTransform& transform) override
        {
            ++mTransformCount;
            ++mNodeCount;
            transform.traverse(*this);
        }
    };

    // Calculate axis-aligned bounding box from a vertex array
    vsg::dbox computeBounds(const vsg::vec3Array& vertices)
    {
        vsg::dbox box;
        for (const auto& v : vertices)
        {
            box.add(vsg::dvec3(v.x, v.y, v.z));
        }
        return box;
    }

    // Calculate bounding sphere enclosing a bounding box
    vsg::dsphere computeBoundingSphere(const vsg::dbox& box)
    {
        if (!box.valid())
            return vsg::dsphere{};

        const vsg::dvec3 center = (box.min + box.max) * 0.5;
        const double radius = vsg::length(box.max - center);
        return vsg::dsphere{ center, radius };
    }
}

TEST(VsgSceneDataTest, should_create_node_hierarchy_and_maintain_invariants)
{
    auto root = vsg::Group::create();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->children.size(), 0u);

    auto childGroup1 = vsg::Group::create();
    auto childGroup2 = vsg::Group::create();
    auto leafNode = vsg::Node::create();

    childGroup1->addChild(leafNode);
    root->addChild(childGroup1);
    root->addChild(childGroup2);

    EXPECT_EQ(root->children.size(), 2u);
    EXPECT_EQ(childGroup1->children.size(), 1u);
    EXPECT_EQ(childGroup2->children.size(), 0u);

    // Switch node and mask invariants
    auto switchNode = vsg::Switch::create();
    switchNode->addChild(vsg::MASK_ALL, leafNode);
    EXPECT_EQ(switchNode->children.size(), 1u);
    EXPECT_EQ(switchNode->children[0].mask, vsg::MASK_ALL);
    switchNode->children[0].mask = vsg::MASK_OFF;
    EXPECT_EQ(switchNode->children[0].mask, vsg::MASK_OFF);

    // Traversal visitor invariant
    NodeCountingVisitor counter;
    root->accept(counter);

    EXPECT_EQ(counter.mGroupCount, 3u); // root + childGroup1 + childGroup2
    EXPECT_EQ(counter.mNodeCount, 4u); // 3 groups + 1 leafNode
}

TEST(VsgSceneDataTest, should_compose_transforms_and_maintain_coordinate_invariants)
{
    auto transform = vsg::MatrixTransform::create();
    ASSERT_NE(transform, nullptr);

    // Default transform must be the identity matrix
    const vsg::dmat4 identity;
    EXPECT_EQ(transform->matrix, identity);

    // Invariant: Translation of the origin (0,0,0) by (10, 20, 30) yields (10, 20, 30)
    const vsg::dvec3 translation(10.0, 20.0, 30.0);
    transform->matrix = vsg::translate(translation);

    const vsg::dvec4 origin(0.0, 0.0, 0.0, 1.0);
    const vsg::dvec4 transformedOrigin = transform->matrix * origin;

    EXPECT_DOUBLE_EQ(transformedOrigin.x, 10.0);
    EXPECT_DOUBLE_EQ(transformedOrigin.y, 20.0);
    EXPECT_DOUBLE_EQ(transformedOrigin.z, 30.0);
    EXPECT_DOUBLE_EQ(transformedOrigin.w, 1.0);

    // Invariant: Coordinate conversion adapter between OpenMW Z-up and Vulkan Y-down
    // OpenMW / Morrowind coordinate convention: X east, Y north, Z up
    // Vulkan clip coordinate convention: X right, Y down, Z depth [0, 1]
    const vsg::dmat4 zUpToVulkan = vsg::rotate(vsg::radians(-90.0), 1.0, 0.0, 0.0);
    const vsg::dvec3 openmwUp(0.0, 0.0, 1.0);
    const vsg::dvec4 convertedUp = zUpToVulkan * vsg::dvec4(openmwUp, 0.0);

    // Z-up rotated -90 degrees around X yields Y-forward (+Y in Vulkan view space)
    EXPECT_NEAR(convertedUp.x, 0.0, 1e-6);
    EXPECT_NEAR(convertedUp.y, 1.0, 1e-6);
    EXPECT_NEAR(convertedUp.z, 0.0, 1e-6);
}

TEST(VsgSceneDataTest, should_create_geometry_and_verify_data_invariants)
{
    // Quad vertices (4 points)
    auto vertices = vsg::vec3Array::create(4);
    vertices->set(0, vsg::vec3(-1.0f, -1.0f, 0.0f));
    vertices->set(1, vsg::vec3(1.0f, -1.0f, 0.0f));
    vertices->set(2, vsg::vec3(1.0f, 1.0f, 0.0f));
    vertices->set(3, vsg::vec3(-1.0f, 1.0f, 0.0f));

    ASSERT_EQ(vertices->size(), 4u);
    EXPECT_EQ(vertices->valueSize(), sizeof(vsg::vec3));

    // Unit normals (all pointing along +Z)
    auto normals = vsg::vec3Array::create(4);
    for (size_t i = 0; i < normals->size(); ++i)
    {
        normals->set(i, vsg::vec3(0.0f, 0.0f, 1.0f));
        const float len = vsg::length(normals->at(i));
        EXPECT_NEAR(len, 1.0f, 1e-6f); // Normal vector must be unit length
    }

    // Texture coordinates in [0, 1]
    auto texCoords = vsg::vec2Array::create(4);
    texCoords->set(0, vsg::vec2(0.0f, 0.0f));
    texCoords->set(1, vsg::vec2(1.0f, 0.0f));
    texCoords->set(2, vsg::vec2(1.0f, 1.0f));
    texCoords->set(3, vsg::vec2(0.0f, 1.0f));

    for (size_t i = 0; i < texCoords->size(); ++i)
    {
        EXPECT_GE(texCoords->at(i).x, 0.0f);
        EXPECT_LE(texCoords->at(i).x, 1.0f);
        EXPECT_GE(texCoords->at(i).y, 0.0f);
        EXPECT_LE(texCoords->at(i).y, 1.0f);
    }

    // Indices for two triangles (6 indices)
    auto indices = vsg::ushortArray::create(6);
    indices->set(0, 0);
    indices->set(1, 1);
    indices->set(2, 2);
    indices->set(3, 2);
    indices->set(4, 3);
    indices->set(5, 0);

    // Invariant: every index must be strictly less than the vertex count
    for (size_t i = 0; i < indices->size(); ++i)
    {
        EXPECT_LT(indices->at(i), static_cast<unsigned short>(vertices->size()));
    }

    // Construct VertexIndexDraw
    auto vid = vsg::VertexIndexDraw::create();
    ASSERT_NE(vid, nullptr);

    vid->assignArrays({ vertices, normals, texCoords });
    vid->assignIndices(indices);
    vid->indexCount = static_cast<uint32_t>(indices->size());
    vid->instanceCount = 1;

    EXPECT_EQ(vid->arrays.size(), 3u);
    ASSERT_NE(vid->indices, nullptr);
    EXPECT_EQ(vid->indices->data, indices);
    EXPECT_EQ(vid->indexCount, 6u);
    EXPECT_EQ(vid->instanceCount, 1u);
}

TEST(VsgSceneDataTest, should_compute_geometry_bounding_volumes)
{
    auto vertices = vsg::vec3Array::create(4);
    vertices->set(0, vsg::vec3(-2.0f, -3.0f, -1.0f));
    vertices->set(1, vsg::vec3(4.0f, -3.0f, -1.0f));
    vertices->set(2, vsg::vec3(4.0f, 5.0f, 3.0f));
    vertices->set(3, vsg::vec3(-2.0f, 5.0f, 3.0f));

    const vsg::dbox bbox = computeBounds(*vertices);
    ASSERT_TRUE(bbox.valid());

    EXPECT_DOUBLE_EQ(bbox.min.x, -2.0);
    EXPECT_DOUBLE_EQ(bbox.min.y, -3.0);
    EXPECT_DOUBLE_EQ(bbox.min.z, -1.0);

    EXPECT_DOUBLE_EQ(bbox.max.x, 4.0);
    EXPECT_DOUBLE_EQ(bbox.max.y, 5.0);
    EXPECT_DOUBLE_EQ(bbox.max.z, 3.0);

    // Invariant: every vertex must be within the bounding box
    for (const auto& v : *vertices)
    {
        EXPECT_GE(v.x, bbox.min.x);
        EXPECT_LE(v.x, bbox.max.x);
        EXPECT_GE(v.y, bbox.min.y);
        EXPECT_LE(v.y, bbox.max.y);
        EXPECT_GE(v.z, bbox.min.z);
        EXPECT_LE(v.z, bbox.max.z);
    }

    // Invariant: bounding sphere enclosing all vertices
    const vsg::dsphere sphere = computeBoundingSphere(bbox);
    ASSERT_TRUE(sphere.valid());

    // Center should be midpoint of bbox
    EXPECT_DOUBLE_EQ(sphere.center.x, 1.0);
    EXPECT_DOUBLE_EQ(sphere.center.y, 1.0);
    EXPECT_DOUBLE_EQ(sphere.center.z, 1.0);

    for (const auto& v : *vertices)
    {
        const double dist = vsg::length(vsg::dvec3(v.x, v.y, v.z) - sphere.center);
        EXPECT_LE(dist, sphere.radius + 1e-6);
    }
}

TEST(VsgSceneDataTest, should_create_image_data_and_verify_layout_and_mipmap_invariants)
{
    const uint32_t width = 64;
    const uint32_t height = 32;

    auto image = vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
    ASSERT_NE(image, nullptr);

    EXPECT_EQ(image->width(), width);
    EXPECT_EQ(image->height(), height);
    EXPECT_EQ(image->valueSize(), sizeof(vsg::ubvec4));
    EXPECT_EQ(image->dataSize(), width * height * sizeof(vsg::ubvec4));

    // Fill test pattern and verify value retrieval
    const vsg::ubvec4 red(255, 0, 0, 255);
    const vsg::ubvec4 blue(0, 0, 255, 255);

    image->set(0, red);
    image->set(width * height - 1, blue);

    EXPECT_EQ(image->at(0), red);
    EXPECT_EQ(image->at(width * height - 1), blue);

    // Invariant: Mipmap chain dimension calculation
    // Each successive mip level is max(1, floor(dim / 2)) until reaching 1x1
    std::vector<std::pair<uint32_t, uint32_t>> mipDimensions;
    uint32_t mw = width;
    uint32_t mh = height;
    while (true)
    {
        mipDimensions.emplace_back(mw, mh);
        if (mw == 1 && mh == 1)
            break;
        mw = std::max(1u, mw / 2);
        mh = std::max(1u, mh / 2);
    }

    // Expected mip dimensions for 64x32:
    // 64x32, 32x16, 16x8, 8x4, 4x2, 2x1, 1x1 (7 levels total)
    EXPECT_EQ(mipDimensions.size(), 7u);
    EXPECT_EQ(mipDimensions.front(), (std::pair<uint32_t, uint32_t>{ 64, 32 }));
    EXPECT_EQ(mipDimensions.back(), (std::pair<uint32_t, uint32_t>{ 1, 1 }));
}

#endif // OPENEDEN_BUILD_VSG
