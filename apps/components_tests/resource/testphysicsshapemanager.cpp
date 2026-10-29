#include <components/resource/niffilemanager.hpp>
#include <components/resource/physicsshape.hpp>
#include <components/resource/physicsshapemanager.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace testing;

    // Tolerance for comparing shape bounds. Jolt stores mesh vertices as floats and may pad bounds slightly.
    constexpr float boundsTolerance = 1e-3f;

    struct Vec3
    {
        float mX;
        float mY;
        float mZ;
    };

    struct Bounds
    {
        Vec3 mMin;
        Vec3 mMax;
    };

    // Returns the axis-aligned bounds of the shape in model space (Jolt's local bounds are relative to the centre of
    // mass, so the centre of mass is added back).
    Bounds getModelSpaceBounds(const JPH::Shape& shape)
    {
        const JPH::AABox local = shape.GetLocalBounds();
        const JPH::Vec3 com = shape.GetCenterOfMass();
        const JPH::Vec3 min = local.mMin + com;
        const JPH::Vec3 max = local.mMax + com;
        return Bounds{ { min.GetX(), min.GetY(), min.GetZ() }, { max.GetX(), max.GetY(), max.GetZ() } };
    }

    void expectBounds(const JPH::Shape& shape, const Vec3& min, const Vec3& max)
    {
        const Bounds bounds = getModelSpaceBounds(shape);
        EXPECT_NEAR(bounds.mMin.mX, min.mX, boundsTolerance);
        EXPECT_NEAR(bounds.mMin.mY, min.mY, boundsTolerance);
        EXPECT_NEAR(bounds.mMin.mZ, min.mZ, boundsTolerance);
        EXPECT_NEAR(bounds.mMax.mX, max.mX, boundsTolerance);
        EXPECT_NEAR(bounds.mMax.mY, max.mY, boundsTolerance);
        EXPECT_NEAR(bounds.mMax.mZ, max.mZ, boundsTolerance);
    }

    // A COLLADA 1.4.1 <geometry> element holding a closed axis-aligned box made of 12 triangles.
    std::string makeBoxGeometry(std::string_view id, const Vec3& min, const Vec3& max)
    {
        std::ostringstream stream;
        stream << "<geometry id=\"" << id << "\" name=\"" << id << "\"><mesh>";
        stream << "<source id=\"" << id << "-positions\"><float_array id=\"" << id
               << "-positions-array\" count=\"24\">";
        for (int i = 0; i < 8; ++i)
        {
            const float x = (i & 1) ? max.mX : min.mX;
            const float y = (i & 2) ? max.mY : min.mY;
            const float z = (i & 4) ? max.mZ : min.mZ;
            stream << x << ' ' << y << ' ' << z << ' ';
        }
        stream << "</float_array><technique_common><accessor source=\"#" << id
               << "-positions-array\" count=\"8\" stride=\"3\">"
                  "<param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/>"
                  "<param name=\"Z\" type=\"float\"/></accessor></technique_common></source>";
        stream << "<vertices id=\"" << id << "-vertices\"><input semantic=\"POSITION\" source=\"#" << id
               << "-positions\"/></vertices>";
        stream << "<triangles count=\"12\"><input semantic=\"VERTEX\" source=\"#" << id
               << "-vertices\" offset=\"0\"/><p>"
                  "0 2 3 0 3 1 " // bottom (z = min)
                  "4 5 7 4 7 6 " // top (z = max)
                  "0 1 5 0 5 4 " // y = min
                  "2 6 7 2 7 3 " // y = max
                  "0 4 6 0 6 2 " // x = min
                  "1 3 7 1 7 5" // x = max
                  "</p></triangles>";
        stream << "</mesh></geometry>";
        return stream.str();
    }

    // A <node> instancing a geometry, with optional transform elements (e.g. "<translate>1 2 3</translate>") and
    // optional child nodes.
    std::string makeNode(std::string_view name, std::string_view transform, std::string_view geometryId,
        std::string_view children = {})
    {
        std::ostringstream stream;
        stream << "<node id=\"" << name << "\" name=\"" << name << "\" type=\"NODE\">" << transform;
        if (!geometryId.empty())
            stream << "<instance_geometry url=\"#" << geometryId << "\"/>";
        stream << children << "</node>";
        return stream.str();
    }

    // A <node> without transforms whose id and name attributes differ, instancing one geometry.
    std::string makeNodeWithId(std::string_view id, std::string_view name, std::string_view geometryId)
    {
        std::ostringstream stream;
        stream << "<node id=\"" << id << "\" name=\"" << name << "\" type=\"NODE\"><instance_geometry url=\"#"
               << geometryId << "\"/></node>";
        return stream.str();
    }

    // A complete COLLADA 1.4.1 document in meters with Z up.
    std::string makeColladaDocument(const std::vector<std::string>& geometries, std::string_view nodes)
    {
        std::ostringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">"
                  "<asset><contributor><authoring_tool>components-tests</authoring_tool></contributor>"
                  "<created>2026-10-29T00:00:00</created><modified>2026-10-29T00:00:00</modified>"
                  "<unit meter=\"1.0\" name=\"meter\"/><up_axis>Z_UP</up_axis></asset>";
        if (!geometries.empty())
        {
            stream << "<library_geometries>";
            for (const std::string& geometry : geometries)
                stream << geometry;
            stream << "</library_geometries>";
        }
        stream << "<library_visual_scenes><visual_scene id=\"Scene\" name=\"Scene\">" << nodes
               << "</visual_scene></library_visual_scenes>"
                  "<scene><instance_visual_scene url=\"#Scene\"/></scene></COLLADA>\n";
        return stream.str();
    }

    // A document with a single box at the scene root, without transforms.
    std::string makeBoxDocument(const Vec3& min, const Vec3& max)
    {
        return makeColladaDocument({ makeBoxGeometry("box", min, max) }, makeNode("Box", {}, "box"));
    }

    struct ResourcePhysicsShapeManagerTest : Test
    {
        const ToUTF8::Utf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
        std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> mFiles;
        std::unique_ptr<VFS::Manager> mVfs;
        std::unique_ptr<Resource::NifFileManager> mNifFileManager;
        std::unique_ptr<Resource::PhysicsShapeManager> mManager;

        Resource::PhysicsShapeManager& makeManager(
            std::initializer_list<std::pair<std::string_view, std::string>> files)
        {
            // A previous manager must go before the VFS it reads from is replaced.
            mManager.reset();
            mNifFileManager.reset();
            VFS::FileMap fileMap;
            for (const auto& [path, content] : files)
            {
                mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(content));
                fileMap.emplace(VFS::Path::Normalized(path), mFiles.back().get());
            }
            mVfs = TestingOpenMW::createTestVFS(std::move(fileMap));
            mNifFileManager
                = std::make_unique<Resource::NifFileManager>(mVfs.get(), &mEncoder.getStatelessEncoder());
            mManager = std::make_unique<Resource::PhysicsShapeManager>(mVfs.get(), mNifFileManager.get(), 1.0);
            return *mManager;
        }

        void TearDown() override
        {
            // The manager must go before the VFS and the files it reads from.
            mManager.reset();
            mNifFileManager.reset();
            mVfs.reset();
        }
    };

    constexpr VFS::Path::NormalizedView boxPath("meshes/box.dae");

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_with_triangles_should_return_mesh_shape)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ -1, -2, 0 }, { 3, 4, 10 }) } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        EXPECT_EQ(shape->mCollisionShape->GetSubType(), JPH::EShapeSubType::Mesh);
        expectBounds(*shape->mCollisionShape, { -1, -2, 0 }, { 3, 4, 10 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_set_collision_box_from_mesh_bounds)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ -1, -2, 0 }, { 3, 4, 10 }) } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.x(), 2.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.y(), 3.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.z(), 5.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.x(), 1.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.y(), 1.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.z(), 5.0f, boundsTolerance);
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_z_up_meter_dae_should_keep_file_coordinates)
    {
        // Every axis has a different, asymmetric range so that any axis swap, flip or scale changes the bounds.
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ 1, 2, 0 }, { 2, 5, 10 }) } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 1, 2, 0 }, { 2, 5, 10 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_apply_node_translate)
    {
        const std::string document = makeColladaDocument({ makeBoxGeometry("box", { 0, 0, 0 }, { 1, 1, 1 }) },
            makeNode("Box", "<translate sid=\"location\">5 6 7</translate>", "box"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 5, 6, 7 }, { 6, 7, 8 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_apply_node_matrix)
    {
        // COLLADA matrices are row-major: uniform scale 2, then translation (1, 2, 3).
        const std::string document = makeColladaDocument({ makeBoxGeometry("box", { 0, 0, 0 }, { 1, 1, 1 }) },
            makeNode("Box", "<matrix sid=\"transform\">2 0 0 1 0 2 0 2 0 0 2 3 0 0 0 1</matrix>", "box"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 1, 2, 3 }, { 3, 4, 5 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_apply_node_rotation_matrix)
    {
        // 90 degrees about Z: (x, y, z) -> (-y, x, z).
        const std::string document = makeColladaDocument({ makeBoxGeometry("box", { 0, 0, 0 }, { 1, 2, 3 }) },
            makeNode("Box", "<matrix sid=\"transform\">0 -1 0 0 1 0 0 0 0 0 1 0 0 0 0 1</matrix>", "box"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { -2, 0, 0 }, { 0, 1, 3 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_combine_nested_node_transforms)
    {
        const std::string child = makeNode("Child", "<translate>0 0 4</translate>", "box");
        const std::string document = makeColladaDocument({ makeBoxGeometry("box", { 0, 0, 0 }, { 1, 1, 1 }) },
            makeNode("Parent", "<translate>10 0 0</translate><scale>2 2 2</scale>", {}, child));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        // Parent: translate(10, 0, 0) * scale(2); child: translate(0, 0, 4). Box 0..1 -> parent space 0..1 + (0, 0,
        // 4) -> scaled 0..2, z 8..10 -> translated x 10..12.
        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 10, 0, 8 }, { 12, 2, 10 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_without_collision_node_should_use_all_geometry)
    {
        const std::string document = makeColladaDocument(
            {
                makeBoxGeometry("first", { 0, 0, 0 }, { 1, 1, 1 }),
                makeBoxGeometry("second", { 0, 0, 0 }, { 1, 1, 1 }),
            },
            makeNode("First", {}, "first") + makeNode("Second", "<translate>4 5 6</translate>", "second"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 0, 0, 0 }, { 5, 6, 7 });
        EXPECT_NEAR(shape->mCollisionBox.mExtents.x(), 2.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.y(), 3.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.z(), 3.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.x(), 2.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.y(), 3.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.z(), 3.5f, boundsTolerance);
    }

    std::string makeVisualAndCollisionDocument(std::string_view collisionNodeName)
    {
        // A large visual box at the root, and a small box under the collision node.
        return makeColladaDocument(
            {
                makeBoxGeometry("visual", { -10, -10, -10 }, { 10, 10, 10 }),
                makeBoxGeometry("collision", { 0, 0, 0 }, { 1, 2, 3 }),
            },
            makeNode("Visual", {}, "visual") + makeNode(collisionNodeName, {}, "collision"));
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_with_collision_node_should_use_only_its_geometry)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeVisualAndCollisionDocument("Collision") } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 0, 0, 0 }, { 1, 2, 3 });
        EXPECT_NEAR(shape->mCollisionBox.mExtents.x(), 0.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.y(), 1.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mExtents.z(), 1.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.x(), 0.5f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.y(), 1.0f, boundsTolerance);
        EXPECT_NEAR(shape->mCollisionBox.mCenter.z(), 1.5f, boundsTolerance);
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_match_collision_node_name_case_insensitively)
    {
        for (const std::string_view name : { "COLLISION", "collision", "CoLlIsIoN" })
        {
            SCOPED_TRACE(name);
            Resource::PhysicsShapeManager& manager
                = makeManager({ { boxPath.value(), makeVisualAndCollisionDocument(name) } });

            const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

            ASSERT_NE(shape.get(), nullptr);
            ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
            expectBounds(*shape->mCollisionShape, { 0, 0, 0 }, { 1, 2, 3 });
        }
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_use_geometry_nested_under_collision_node)
    {
        // The collision node has no geometry of its own; its geometry is in a child node with a transform, and the
        // collision node itself sits under a transformed parent.
        const std::string collisionChild = makeNode("CollisionPart", "<translate>0 0 1</translate>", "collision");
        const std::string collision = makeNode("Collision", "<translate>2 0 0</translate>", {}, collisionChild);
        const std::string document = makeColladaDocument(
            {
                makeBoxGeometry("visual", { -10, -10, -10 }, { 10, 10, 10 }),
                makeBoxGeometry("collision", { 0, 0, 0 }, { 1, 1, 1 }),
            },
            makeNode("Root", "<translate>0 3 0</translate>", "visual", collision));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 2, 3, 1 }, { 3, 4, 2 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_without_triangles_should_return_null)
    {
        const std::string document = makeColladaDocument({}, makeNode("Empty", "<translate>1 2 3</translate>", {}));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        EXPECT_EQ(manager.getShape(boxPath).get(), nullptr);
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getInstance_for_dae_without_triangles_should_return_null)
    {
        const std::string document = makeColladaDocument({}, makeNode("Empty", {}, {}));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        EXPECT_EQ(manager.getInstance(boxPath).get(), nullptr);
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getInstance_for_dae_with_triangles_should_return_instance)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ 0, 0, 0 }, { 1, 1, 1 }) } });

        const vsg::ref_ptr<Resource::PhysicsShapeInstance> instance = manager.getInstance(boxPath);

        ASSERT_NE(instance.get(), nullptr);
        EXPECT_NE(instance->mCollisionShape.GetPtr(), nullptr);
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_set_file_name_and_hash)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ 0, 0, 0 }, { 1, 1, 1 }) } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        EXPECT_EQ(shape->mFileName.value(), boxPath.value());
        EXPECT_FALSE(shape->mFileHash.empty());
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_same_dae_twice_should_return_cached_shape)
    {
        Resource::PhysicsShapeManager& manager
            = makeManager({ { boxPath.value(), makeBoxDocument({ 0, 0, 0 }, { 1, 1, 1 }) } });

        const vsg::ref_ptr<const Resource::PhysicsShape> first = manager.getShape(boxPath);
        const vsg::ref_ptr<const Resource::PhysicsShape> second = manager.getShape(boxPath);

        ASSERT_NE(first.get(), nullptr);
        EXPECT_EQ(first.get(), second.get());
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_match_collision_node_by_name_not_id)
    {
        const std::string document = makeColladaDocument(
            {
                makeBoxGeometry("visual", { -10, -10, -10 }, { 10, 10, 10 }),
                makeBoxGeometry("collision", { 0, 0, 0 }, { 1, 2, 3 }),
            },
            makeNodeWithId("Visual-node", "Visual", "visual")
                + makeNodeWithId("Collision-node", "Collision", "collision"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 0, 0, 0 }, { 1, 2, 3 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_not_treat_node_with_collision_id_as_collision_node)
    {
        // The node's id is "Collision" but its name is "Visual": no collision node, so all geometry counts.
        const std::string document = makeColladaDocument(
            {
                makeBoxGeometry("small", { 0, 0, 0 }, { 1, 2, 3 }),
                makeBoxGeometry("large", { -10, -10, -10 }, { 10, 10, 10 }),
            },
            makeNodeWithId("Collision", "Visual", "small") + makeNodeWithId("Large-node", "Large", "large"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { -10, -10, -10 }, { 10, 10, 10 });
    }

    TEST_F(ResourcePhysicsShapeManagerTest, getShape_for_dae_should_use_only_first_collision_node_in_document_order)
    {
        const std::string document = makeColladaDocument(
            {
                makeBoxGeometry("visual", { -10, -10, -10 }, { 10, 10, 10 }),
                makeBoxGeometry("first", { 0, 0, 0 }, { 1, 2, 3 }),
                makeBoxGeometry("second", { 20, 20, 20 }, { 21, 22, 23 }),
            },
            makeNodeWithId("Visual-node", "Visual", "visual")
                + makeNodeWithId("Collision-first", "Collision", "first")
                + makeNodeWithId("Collision-second", "Collision", "second"));
        Resource::PhysicsShapeManager& manager = makeManager({ { boxPath.value(), document } });

        const vsg::ref_ptr<const Resource::PhysicsShape> shape = manager.getShape(boxPath);

        ASSERT_NE(shape.get(), nullptr);
        ASSERT_NE(shape->mCollisionShape.GetPtr(), nullptr);
        expectBounds(*shape->mCollisionShape, { 0, 0, 0 }, { 1, 2, 3 });
    }
}
