#include "../nif/node.hpp"

#include <components/misc/vsgmath.hpp>
#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/node.hpp>
#include <components/nifjolt/joltnifloader.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

// This file was testbulletnifloader.cpp, written against Bullet's shapes and last compiled before the Jolt port. Every
// test's input and expected value is kept. What changed is how a result is compared, through the Expected* types below,
// for what Jolt does differently (task 0014):
//  - Each child is compared by its triangles in model space, so child transform, scale and vertices are checked
//    together. Jolt keeps a child's position relative to the compound's centre of mass, so they cannot be compared apart.
//  - A mesh's triangles compare without their order and first vertex, because JPH::MeshShape reorders them into its
//    tree; winding is still checked. A static compound also reorders its children, so those compare without order,
//    while a mutable one keeps them, and mAnimatedShapes indexes them, so those compare in order.
//  - Bullet's usesQuantizedAabbCompression and getOwnsBvh are gone: Jolt has no such choice.
namespace
{
    using Triangle = std::array<vsg::vec3, 3>;

    bool isNear(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 1e-5;
    }

    bool isNear(const vsg::vec3& lhs, const vsg::vec3& rhs)
    {
        return isNear(lhs.x, rhs.x) && isNear(lhs.y, rhs.y) && isNear(lhs.z, rhs.z);
    }

    bool isNear(const Triangle& lhs, const Triangle& rhs)
    {
        return isNear(lhs[0], rhs[0]) && isNear(lhs[1], rhs[1]) && isNear(lhs[2], rhs[2]);
    }

    // Rounded, so that values within the tolerance sort the same way.
    std::tuple<long, long, long> sortKey(const vsg::vec3& value)
    {
        return { std::lround(value.x * 1000.f), std::lround(value.y * 1000.f), std::lround(value.z * 1000.f) };
    }

    bool lessTriangle(const Triangle& lhs, const Triangle& rhs)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (sortKey(lhs[i]) != sortKey(rhs[i]))
                return sortKey(lhs[i]) < sortKey(rhs[i]);
        }
        return false;
    }

    // Starts the triangle at its smallest vertex, which keeps the winding.
    Triangle normalizedTriangle(const Triangle& value)
    {
        std::size_t first = 0;
        for (std::size_t i = 1; i < 3; ++i)
            if (sortKey(value[i]) < sortKey(value[first]))
                first = i;
        return { value[first], value[(first + 1) % 3], value[(first + 2) % 3] };
    }

    using TriangleList = std::vector<Triangle>;

    void normalize(TriangleList& triangles)
    {
        for (Triangle& triangle : triangles)
            triangle = normalizedTriangle(triangle);
        std::sort(triangles.begin(), triangles.end(), lessTriangle);
    }

    bool isNear(const TriangleList& lhs, const TriangleList& rhs)
    {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
            [](const Triangle& l, const Triangle& r) { return isNear(l, r); });
    }

    using Basis = std::array<std::array<float, 3>, 3>;

    Basis identityBasis()
    {
        return { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    }

    Basis rotationAboutX(float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { { { 1, 0, 0 }, { 0, c, -s }, { 0, s, c } } };
    }

    struct ExpectedTransform
    {
        Basis mBasis = identityBasis();
        vsg::vec3 mOrigin;

        static ExpectedTransform identity() { return ExpectedTransform{}; }

        vsg::vec3 apply(const vsg::vec3& value) const
        {
            return vsg::vec3(mBasis[0][0] * value.x + mBasis[0][1] * value.y + mBasis[0][2] * value.z + mOrigin.x,
                mBasis[1][0] * value.x + mBasis[1][1] * value.y + mBasis[1][2] * value.z + mOrigin.y,
                mBasis[2][0] * value.x + mBasis[2][1] * value.y + mBasis[2][2] * value.z + mOrigin.z);
        }
    };

    struct ExpectedTriangles
    {
        explicit ExpectedTriangles(bool /*use32BitIndices*/) {}

        void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c) { mValue.push_back({ a, b, c }); }

        TriangleList mValue;
    };

    struct ExpectedMesh
    {
        ExpectedMesh(ExpectedTriangles* triangles, bool /*useQuantizedAabbCompression*/)
            : mTriangles(triangles)
        {
        }

        std::unique_ptr<ExpectedTriangles> mTriangles;
    };

    struct ExpectedScaledMesh
    {
        ExpectedScaledMesh(ExpectedMesh* mesh, const vsg::vec3& scale)
            : mMesh(mesh)
            , mScale(scale)
        {
        }

        std::unique_ptr<ExpectedMesh> mMesh;
        vsg::vec3 mScale;
    };

    struct ExpectedCompound
    {
        void addChildShape(const ExpectedTransform& transform, ExpectedScaledMesh* child)
        {
            TriangleList triangles;
            for (const Triangle& triangle : child->mMesh->mTriangles->mValue)
            {
                Triangle moved;
                for (std::size_t i = 0; i < 3; ++i)
                    moved[i] = transform.apply(Misc::VsgMath::componentMultiply(triangle[i], child->mScale));
                triangles.push_back(moved);
            }
            normalize(triangles);
            mChildren.push_back(std::move(triangles));
            delete child;
        }

        std::vector<TriangleList> mChildren;
    };

    struct ExpectedPhysicsShape
    {
        std::unique_ptr<ExpectedCompound> mCollisionShape;
        std::unique_ptr<ExpectedCompound> mAvoidCollisionShape;
        Resource::CollisionBox mCollisionBox;
        std::map<int, int> mAnimatedShapes;
        Resource::VisualCollisionType mVisualCollisionType = Resource::VisualCollisionType::None;
    };

    void collectTriangles(
        const JPH::Shape& shape, JPH::Vec3 position, JPH::Quat rotation, JPH::Vec3 scale, TriangleList& triangles)
    {
        // Only leaf shapes give triangles (JPH::DecoratedShape::GetTrianglesStart asserts), so this unwraps the others.
        if (const auto* scaled = dynamic_cast<const JPH::ScaledShape*>(&shape))
            return collectTriangles(
                *scaled->GetInnerShape(), position, rotation, scale * scaled->GetScale(), triangles);
        if (const auto* rotated = dynamic_cast<const JPH::RotatedTranslatedShape*>(&shape))
            return collectTriangles(*rotated->GetInnerShape(), position + rotation * (scale * rotated->GetPosition()),
                rotation * rotated->GetRotation(), scale, triangles);

        // A box of JPH::AABox::sBiggest overflows when the mesh transforms it, and finds nothing.
        const JPH::AABox everything(JPH::Vec3::sReplicate(-1e6f), JPH::Vec3::sReplicate(1e6f));
        JPH::Shape::GetTrianglesContext context;
        shape.GetTrianglesStart(context, everything, position, rotation, scale);
        constexpr int maxTriangles = 256;
        std::array<JPH::Float3, 3 * maxTriangles> vertices;
        for (int count = shape.GetTrianglesNext(context, maxTriangles, vertices.data()); count > 0;
             count = shape.GetTrianglesNext(context, maxTriangles, vertices.data()))
        {
            for (int i = 0; i < count; ++i)
            {
                Triangle triangle;
                for (int j = 0; j < 3; ++j)
                {
                    const JPH::Float3& v = vertices[static_cast<std::size_t>(i * 3 + j)];
                    triangle[static_cast<std::size_t>(j)] = vsg::vec3(v.x, v.y, v.z);
                }
                triangles.push_back(triangle);
            }
        }
    }

    // The model space triangles of each child of a compound, in its child order. JPH::StaticCompoundShapeSettings makes
    // a JPH::RotatedTranslatedShape for one child and a JPH::EmptyShape for none (StaticCompoundShapeSettings::Create).
    std::vector<TriangleList> getChildren(const JPH::Shape& shape)
    {
        std::vector<TriangleList> result;
        if (const auto* compound = dynamic_cast<const JPH::CompoundShape*>(&shape))
        {
            const JPH::Vec3 centerOfMass = compound->GetCenterOfMass();
            for (const JPH::CompoundShape::SubShape& child : compound->GetSubShapes())
            {
                TriangleList triangles;
                collectTriangles(*child.mShape.GetPtr(), centerOfMass + child.GetPositionCOM(), child.GetRotation(),
                    JPH::Vec3::sReplicate(1.0f), triangles);
                normalize(triangles);
                result.push_back(std::move(triangles));
            }
            return result;
        }
        if (shape.GetSubType() == JPH::EShapeSubType::Empty)
            return result;
        TriangleList triangles;
        collectTriangles(shape, JPH::Vec3::sZero(), JPH::Quat::sIdentity(), JPH::Vec3::sReplicate(1.0f), triangles);
        normalize(triangles);
        result.push_back(std::move(triangles));
        return result;
    }

    bool sameChildren(std::vector<TriangleList> lhs, std::vector<TriangleList> rhs, bool ordered)
    {
        if (lhs.size() != rhs.size())
            return false;
        if (!ordered)
        {
            const auto less = [](const TriangleList& l, const TriangleList& r) {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end(), lessTriangle);
            };
            std::sort(lhs.begin(), lhs.end(), less);
            std::sort(rhs.begin(), rhs.end(), less);
        }
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(),
            [](const TriangleList& l, const TriangleList& r) { return isNear(l, r); });
    }

    bool sameShape(const JPH::Shape* actual, const ExpectedCompound* expected, bool ordered)
    {
        if (actual == nullptr || expected == nullptr)
            return actual == nullptr && expected == nullptr;
        return sameChildren(getChildren(*actual), expected->mChildren, ordered);
    }

    std::ostream& operator<<(std::ostream& stream, const vsg::vec3& value)
    {
        return stream << "{" << std::setprecision(std::numeric_limits<float>::max_digits10) << value.x << ", "
                      << value.y << ", " << value.z << "}";
    }

    void write(std::ostream& stream, const std::vector<TriangleList>& children)
    {
        stream << "{";
        for (const TriangleList& child : children)
        {
            stream << "child {";
            for (const Triangle& triangle : child)
                stream << "(" << triangle[0] << ", " << triangle[1] << ", " << triangle[2] << "), ";
            stream << "}, ";
        }
        stream << "}";
    }

    void write(std::ostream& stream, const std::map<int, int>& value)
    {
        stream << "{";
        for (const auto& [key, index] : value)
            stream << "{" << key << ", " << index << "},";
        stream << "}";
    }

    bool operator==(const Resource::PhysicsShape& actual, const ExpectedPhysicsShape& expected)
    {
        const bool ordered = !expected.mAnimatedShapes.empty();
        return sameShape(actual.mCollisionShape.GetPtr(), expected.mCollisionShape.get(), ordered)
            && sameShape(actual.mAvoidCollisionShape.GetPtr(), expected.mAvoidCollisionShape.get(), ordered)
            && actual.mCollisionBox.mExtents == expected.mCollisionBox.mExtents
            && actual.mCollisionBox.mCenter == expected.mCollisionBox.mCenter
            && actual.mVisualCollisionType == expected.mVisualCollisionType
            && actual.mAnimatedShapes == expected.mAnimatedShapes;
    }

    std::ostream& operator<<(std::ostream& stream, const ExpectedPhysicsShape& value)
    {
        stream << "expected {";
        write(stream, value.mCollisionShape ? value.mCollisionShape->mChildren : std::vector<TriangleList>{});
        stream << ", avoid ";
        write(stream, value.mAvoidCollisionShape ? value.mAvoidCollisionShape->mChildren : std::vector<TriangleList>{});
        stream << ", box " << value.mCollisionBox.mExtents << " " << value.mCollisionBox.mCenter << ", animated ";
        write(stream, value.mAnimatedShapes);
        return stream << ", visual " << static_cast<int>(value.mVisualCollisionType) << "}";
    }
}

namespace Resource
{
    static void PrintTo(const PhysicsShape& value, std::ostream* stream)
    {
        *stream << "actual {";
        write(*stream,
            value.mCollisionShape == nullptr ? std::vector<TriangleList>{} : getChildren(*value.mCollisionShape));
        *stream << ", avoid ";
        write(*stream,
            value.mAvoidCollisionShape == nullptr ? std::vector<TriangleList>{}
                                                  : getChildren(*value.mAvoidCollisionShape));
        *stream << ", box " << value.mCollisionBox.mExtents << " " << value.mCollisionBox.mCenter << ", animated ";
        write(*stream, value.mAnimatedShapes);
        *stream << ", visual " << static_cast<int>(value.mVisualCollisionType) << "}";
    }
}

namespace
{
    using namespace testing;
    using namespace Nif::Testing;
    using NifJolt::JoltNifLoader;

    constexpr VFS::Path::NormalizedView testNif("test.nif");
    constexpr VFS::Path::NormalizedView xtestNif("xtest.nif");

    void copy(const ExpectedTransform& src, Nif::NiTransform& dst)
    {
        dst.mTranslation = src.mOrigin;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                dst.mRotation.mValues[row][column] = src.mBasis[row][column];
    }

    struct TestJoltNifLoader : Test
    {
        JoltNifLoader mLoader;
        Nif::NiAVObject mNode;
        Nif::NiAVObject mNode2;
        Nif::NiNode mNiNode;
        Nif::NiNode mNiNode2;
        Nif::NiNode mNiNode3;
        Nif::NiTriShapeData mNiTriShapeData;
        Nif::NiTriShape mNiTriShape;
        Nif::NiTriShapeData mNiTriShapeData2;
        Nif::NiTriShape mNiTriShape2;
        Nif::NiTriStripsData mNiTriStripsData;
        Nif::NiTriStrips mNiTriStrips;
        Nif::NiSkinInstance mNiSkinInstance;
        Nif::NiStringExtraData mNiStringExtraData;
        Nif::NiStringExtraData mNiStringExtraData2;
        Nif::NiIntegerExtraData mNiIntegerExtraData;
        Nif::NiTimeController mController;
        ExpectedTransform mTransform{ rotationAboutX(0.5f), vsg::vec3(1, 2, 3) };
        ExpectedTransform mTransformScale2{ rotationAboutX(0.5f), vsg::vec3(2, 4, 6) };
        ExpectedTransform mTransformScale3{ rotationAboutX(0.5f), vsg::vec3(3, 6, 9) };
        ExpectedTransform mTransformScale4{ rotationAboutX(0.5f), vsg::vec3(4, 8, 12) };
        const std::string mHash = "hash";

        TestJoltNifLoader()
        {
            init(mNode);
            init(mNode2);
            init(mNiNode);
            init(mNiNode2);
            init(mNiNode3);
            init(mNiTriShape);
            init(mNiTriShape2);
            init(mNiTriStrips);
            init(mNiSkinInstance);
            init(mNiStringExtraData);
            init(mNiStringExtraData2);
            init(mController);

            mNiTriShapeData.mRecordType = Nif::RC_NiTriShapeData;
            mNiTriShapeData.mVertices = { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0) };
            mNiTriShapeData.mNumTriangles = 1;
            mNiTriShapeData.mTriangles = { 0, 1, 2 };
            mNiTriShape.mData = Nif::NiGeometryDataPtr(&mNiTriShapeData);

            mNiTriShapeData2.mRecordType = Nif::RC_NiTriShapeData;
            mNiTriShapeData2.mVertices = { vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 1), vsg::vec3(1, 1, 1) };
            mNiTriShapeData2.mNumTriangles = 1;
            mNiTriShapeData2.mTriangles = { 0, 1, 2 };
            mNiTriShape2.mData = Nif::NiGeometryDataPtr(&mNiTriShapeData2);

            mNiTriStripsData.mRecordType = Nif::RC_NiTriStripsData;
            mNiTriStripsData.mVertices
                = { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0), vsg::vec3(0, 1, 0) };
            mNiTriStripsData.mNumTriangles = 2;
            mNiTriStripsData.mStrips = { { 0, 1, 2, 3 } };
            mNiTriStrips.mData = Nif::NiGeometryDataPtr(&mNiTriStripsData);
        }
    };

    TEST_F(TestJoltNifLoader, for_zero_num_roots_should_return_default)
    {
        Nif::NIFFile file(testNif);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
        EXPECT_EQ(result->mFileName, "test.nif");
        EXPECT_EQ(result->mFileHash, mHash);
    }

    TEST_F(TestJoltNifLoader, should_ignore_nullptr_root)
    {
        Nif::NIFFile file(testNif);
        file.mRoots.push_back(nullptr);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_default_root_nif_node_should_return_default)
    {
        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_default_root_collision_node_nif_node_should_return_default)
    {
        mNode.mRecordType = Nif::RC_RootCollisionNode;

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_default_root_nif_node_and_filename_starting_with_x_should_return_default)
    {
        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_root_bounding_box_should_return_shape_with_bounding_box_data)
    {
        mNode.mName = "Bounding Box";
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;
        expected.mCollisionBox.mExtents = vsg::vec3(1, 2, 3);
        expected.mCollisionBox.mCenter = vsg::vec3(-1, -2, -3);

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_child_bounding_box_should_return_shape_with_bounding_box_data)
    {
        mNode.mName = "Bounding Box";
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);
        mNode.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNode) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;
        expected.mCollisionBox.mExtents = vsg::vec3(1, 2, 3);
        expected.mCollisionBox.mCenter = vsg::vec3(-1, -2, -3);

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_root_with_bounds_and_child_bounding_box_should_use_bounding_box)
    {
        mNode.mName = "Bounding Box";
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);
        mNode.mParents.push_back(&mNiNode);

        mNiNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNiNode.mBounds.mBox.mExtents = vsg::vec3(4, 5, 6);
        mNiNode.mBounds.mBox.mCenter = vsg::vec3(-4, -5, -6);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNode) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;
        expected.mCollisionBox.mExtents = vsg::vec3(1, 2, 3);
        expected.mCollisionBox.mCenter = vsg::vec3(-1, -2, -3);

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_root_and_two_children_where_both_with_bounds_but_one_is_bounding_box_use_bounding_box)
    {
        mNode.mName = "Bounding Box";
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);
        mNode.mParents.push_back(&mNiNode);

        mNode2.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode2.mBounds.mBox.mExtents = vsg::vec3(4, 5, 6);
        mNode2.mBounds.mBox.mCenter = vsg::vec3(-4, -5, -6);
        mNode2.mParents.push_back(&mNiNode);

        mNiNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNiNode.mBounds.mBox.mExtents = vsg::vec3(7, 8, 9);
        mNiNode.mBounds.mBox.mCenter = vsg::vec3(-7, -8, -9);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNode), Nif::NiAVObjectPtr(&mNode2) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;
        expected.mCollisionBox.mExtents = vsg::vec3(1, 2, 3);
        expected.mCollisionBox.mCenter = vsg::vec3(-1, -2, -3);

        EXPECT_EQ(*result, expected);
    }

    TEST_F(
        TestJoltNifLoader, for_root_and_two_children_where_both_with_bounds_but_second_is_bounding_box_use_bounding_box)
    {
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);
        mNode.mParents.push_back(&mNiNode);

        mNode2.mName = "Bounding Box";
        mNode2.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode2.mBounds.mBox.mExtents = vsg::vec3(4, 5, 6);
        mNode2.mBounds.mBox.mCenter = vsg::vec3(-4, -5, -6);
        mNode2.mParents.push_back(&mNiNode);

        mNiNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNiNode.mBounds.mBox.mExtents = vsg::vec3(7, 8, 9);
        mNiNode.mBounds.mBox.mCenter = vsg::vec3(-7, -8, -9);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNode), Nif::NiAVObjectPtr(&mNode2) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;
        expected.mCollisionBox.mExtents = vsg::vec3(4, 5, 6);
        expected.mCollisionBox.mCenter = vsg::vec3(-4, -5, -6);

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_root_nif_node_with_bounds_should_return_shape_with_null_collision_shape)
    {
        mNode.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNode.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNode.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_root_node_should_return_static_shape)
    {
        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriShape);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_root_node_with_bounds_should_return_static_shape)
    {
        mNiTriShape.mBounds.mType = Nif::BoundingVolume::Type::BOX_BV;
        mNiTriShape.mBounds.mBox.mExtents = vsg::vec3(1, 2, 3);
        mNiTriShape.mBounds.mBox.mCenter = vsg::vec3(-1, -2, -3);

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriShape);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_child_node_should_return_static_shape)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_nested_tri_shape_child_should_return_static_shape)
    {
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiNode2) };
        mNiNode2.mParents.push_back(&mNiNode);
        mNiNode2.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiTriShape.mParents.push_back(&mNiNode2);

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_two_tri_shape_children_should_return_static_shape_with_all_meshes)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiTriShape2.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape), Nif::NiAVObjectPtr(&mNiTriShape2) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedTriangles> triangles2(new ExpectedTriangles(false));
        triangles2->addTriangle(vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 1), vsg::vec3(1, 1, 1));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        auto triShape2 = std::make_unique<ExpectedMesh>(triangles2.release(), true);

        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape2.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader,
        for_tri_shape_child_node_and_filename_starting_with_x_and_not_empty_skin_should_return_static_shape)
    {
        mNiTriShape.mSkin = Nif::NiSkinInstancePtr(&mNiSkinInstance);
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        auto triShape = std::make_unique<ExpectedMesh>(triangles.release(), true);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(triShape.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_root_node_and_filename_starting_with_x_should_return_animated_shape)
    {
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 3;

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiTriShape);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(mTransform, new ExpectedScaledMesh(mesh.release(), vsg::vec3(3, 3, 3)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_child_node_and_filename_starting_with_x_should_return_animated_shape)
    {
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 3;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode.mTransform.mScale = 4;

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(
            mTransformScale4, new ExpectedScaledMesh(mesh.release(), vsg::vec3(12, 12, 12)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(
        TestJoltNifLoader, for_two_tri_shape_children_nodes_and_filename_starting_with_x_should_return_animated_shape)
    {
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 3;
        mNiTriShape.mParents.push_back(&mNiNode);

        copy(mTransform, mNiTriShape2.mTransform);
        mNiTriShape2.mTransform.mScale = 3;
        mNiTriShape2.mParents.push_back(&mNiNode);

        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape), Nif::NiAVObjectPtr(&mNiTriShape2) };

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));

        std::unique_ptr<ExpectedTriangles> triangles2(new ExpectedTriangles(false));
        triangles2->addTriangle(vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 1), vsg::vec3(1, 1, 1));
        std::unique_ptr<ExpectedMesh> mesh2(new ExpectedMesh(triangles2.release(), true));

        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(mTransform, new ExpectedScaledMesh(mesh.release(), vsg::vec3(3, 3, 3)));
        shape->addChildShape(mTransform, new ExpectedScaledMesh(mesh2.release(), vsg::vec3(3, 3, 3)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_child_node_with_controller_should_return_animated_shape)
    {
        mController.mRecordType = Nif::RC_NiKeyframeController;
        mController.mFlags |= Nif::NiTimeController::Flag_Active;
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 3;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiTriShape.mController = Nif::NiTimeControllerPtr(&mController);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode.mTransform.mScale = 4;

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(
            mTransformScale4, new ExpectedScaledMesh(mesh.release(), vsg::vec3(12, 12, 12)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_two_tri_shape_children_nodes_where_one_with_controller_should_return_animated_shape)
    {
        mController.mRecordType = Nif::RC_NiKeyframeController;
        mController.mFlags |= Nif::NiTimeController::Flag_Active;
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 3;
        mNiTriShape.mParents.push_back(&mNiNode);
        copy(mTransform, mNiTriShape2.mTransform);
        mNiTriShape2.mTransform.mScale = 3;
        mNiTriShape2.mParents.push_back(&mNiNode);
        mNiTriShape2.mController = Nif::NiTimeControllerPtr(&mController);
        mNiNode.mChildren = Nif::NiAVObjectList{
            Nif::NiAVObjectPtr(&mNiTriShape),
            Nif::NiAVObjectPtr(&mNiTriShape2),
        };
        mNiNode.mTransform.mScale = 4;

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));

        std::unique_ptr<ExpectedTriangles> triangles2(new ExpectedTriangles(false));
        triangles2->addTriangle(vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 1), vsg::vec3(1, 1, 1));
        std::unique_ptr<ExpectedMesh> mesh2(new ExpectedMesh(triangles2.release(), true));

        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(
            mTransformScale4, new ExpectedScaledMesh(mesh.release(), vsg::vec3(12, 12, 12)));
        shape->addChildShape(
            mTransformScale4, new ExpectedScaledMesh(mesh2.release(), vsg::vec3(12, 12, 12)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 1 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, should_add_static_mesh_to_existing_compound_mesh)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mRoots.push_back(&mNiTriShape2);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));

        std::unique_ptr<ExpectedTriangles> triangles2(new ExpectedTriangles(false));
        triangles2->addTriangle(vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 1), vsg::vec3(1, 1, 1));
        std::unique_ptr<ExpectedMesh> mesh2(new ExpectedMesh(triangles2.release(), true));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh2.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(
        TestJoltNifLoader, for_root_avoid_node_and_tri_shape_child_node_should_return_shape_with_null_collision_shape)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode.mRecordType = Nif::RC_AvoidNode;

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));

        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));
        ExpectedPhysicsShape expected;
        expected.mAvoidCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_child_node_with_empty_data_should_return_shape_with_null_collision_shape)
    {
        mNiTriShape.mData = Nif::NiGeometryDataPtr(nullptr);
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader,
        for_tri_shape_child_node_with_empty_data_triangles_should_return_shape_with_null_collision_shape)
    {
        auto data = static_cast<Nif::NiTriShapeData*>(mNiTriShape.mData.getPtr());
        data->mTriangles.clear();
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(
        TestJoltNifLoader, for_root_node_with_extra_data_string_equal_ncc_should_return_shape_with_cameraonly_collision)
    {
        mNiStringExtraData.mData = "NCC__";
        mNiStringExtraData.mRecordType = Nif::RC_NiStringExtraData;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        expected.mVisualCollisionType = Resource::VisualCollisionType::Camera;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader,
        for_root_node_with_not_first_extra_data_string_equal_ncc_should_return_shape_with_cameraonly_collision)
    {
        mNiStringExtraData.mNext = Nif::ExtraPtr(&mNiStringExtraData2);
        mNiStringExtraData2.mData = "NCC__";
        mNiStringExtraData2.mRecordType = Nif::RC_NiStringExtraData;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mVisualCollisionType = Resource::VisualCollisionType::Camera;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(
        TestJoltNifLoader, for_root_node_with_extra_data_string_starting_with_nc_should_return_shape_with_nocollision)
    {
        mNiStringExtraData.mData = "NC___";
        mNiStringExtraData.mRecordType = Nif::RC_NiStringExtraData;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mVisualCollisionType = Resource::VisualCollisionType::Default;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader,
        for_root_node_with_not_first_extra_data_string_starting_with_nc_should_return_shape_with_nocollision)
    {
        mNiStringExtraData.mNext = Nif::ExtraPtr(&mNiStringExtraData2);
        mNiStringExtraData2.mData = "NC___";
        mNiStringExtraData2.mRecordType = Nif::RC_NiStringExtraData;
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mVisualCollisionType = Resource::VisualCollisionType::Default;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_shape_child_node_with_extra_data_string_should_ignore_extra_data)
    {
        mNiStringExtraData.mData = "NC___";
        mNiStringExtraData.mRecordType = Nif::RC_NiStringExtraData;
        mNiTriShape.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_empty_root_collision_node_without_nc_should_return_shape_with_cameraonly_collision)
    {
        Nif::NiTriShape niTriShape;
        Nif::NiNode emptyCollisionNode;
        init(niTriShape);
        init(emptyCollisionNode);

        niTriShape.mData = Nif::NiGeometryDataPtr(&mNiTriShapeData);
        niTriShape.mParents.push_back(&mNiNode);

        emptyCollisionNode.mRecordType = Nif::RC_RootCollisionNode;
        emptyCollisionNode.mParents.push_back(&mNiNode);

        mNiNode.mChildren
            = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&niTriShape), Nif::NiAVObjectPtr(&emptyCollisionNode) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mVisualCollisionType = Resource::VisualCollisionType::Camera;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, bsx_editor_marker_flag_disables_collision_for_markers)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiTriShape.mName = "EditorMarker";
        mNiIntegerExtraData.mData = 34; // BSXFlags "has collision" | "editor marker"
        mNiIntegerExtraData.mRecordType = Nif::RC_BSXFlags;
        mNiNode.mExtraList.push_back(Nif::ExtraPtr(&mNiIntegerExtraData));
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;
        file.mVersion = Nif::NIFStream::generateVersion(10, 0, 1, 0);

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, mrk_editor_marker_flag_disables_collision_for_markers)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiTriShape.mName = "Tri EditorMarker";
        mNiStringExtraData.mData = "MRK";
        mNiStringExtraData.mRecordType = Nif::RC_NiStringExtraData;
        mNiNode.mExtra = Nif::ExtraPtr(&mNiStringExtraData);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_tri_strips_root_node_should_return_static_shape)
    {
        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriStrips);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        triangles->addTriangle(vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, should_ignore_tri_strips_data_with_empty_strips)
    {
        mNiTriStripsData.mStrips.clear();

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriStrips);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        const ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_static_mesh_should_ignore_tri_strips_data_with_less_than_3_strips)
    {
        mNiTriStripsData.mStrips.front() = { 0, 1 };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriStrips);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        const ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_avoid_collision_mesh_should_ignore_tri_strips_data_with_less_than_3_strips)
    {
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode.mRecordType = Nif::RC_AvoidNode;
        mNiTriStripsData.mStrips.front() = { 0, 1 };

        Nif::NIFFile file(testNif);
        file.mRoots.push_back(&mNiTriStrips);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        const ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, for_animated_mesh_should_ignore_tri_strips_data_with_less_than_3_strips)
    {
        mNiTriStripsData.mStrips.front() = { 0, 1 };
        mNiTriStrips.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriStrips) };

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        const ExpectedPhysicsShape expected;

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, should_not_add_static_mesh_with_no_triangles_to_compound_shape)
    {
        mNiTriStripsData.mStrips.front() = { 0, 1 };
        mNiTriShape.mParents.push_back(&mNiNode);
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mRoots.push_back(&mNiTriStrips);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles(new ExpectedTriangles(false));
        triangles->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh(new ExpectedMesh(triangles.release(), true));
        std::unique_ptr<ExpectedCompound> compound(new ExpectedCompound);
        compound->addChildShape(
            ExpectedTransform::identity(), new ExpectedScaledMesh(mesh.release(), vsg::vec3(1, 1, 1)));

        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(compound.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }

    TEST_F(TestJoltNifLoader, should_handle_node_with_multiple_parents)
    {
        copy(mTransform, mNiTriShape.mTransform);
        mNiTriShape.mTransform.mScale = 4;
        mNiTriShape.mParents = { &mNiNode, &mNiNode2 };
        mNiNode.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode.mTransform.mScale = 2;
        mNiNode2.mChildren = Nif::NiAVObjectList{ Nif::NiAVObjectPtr(&mNiTriShape) };
        mNiNode2.mTransform.mScale = 3;

        Nif::NIFFile file(xtestNif);
        file.mRoots.push_back(&mNiNode);
        file.mRoots.push_back(&mNiNode2);
        file.mHash = mHash;

        const auto result = mLoader.load(file);

        std::unique_ptr<ExpectedTriangles> triangles1(new ExpectedTriangles(false));
        triangles1->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh1(new ExpectedMesh(triangles1.release(), true));
        std::unique_ptr<ExpectedTriangles> triangles2(new ExpectedTriangles(false));
        triangles2->addTriangle(vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(1, 1, 0));
        std::unique_ptr<ExpectedMesh> mesh2(new ExpectedMesh(triangles2.release(), true));
        std::unique_ptr<ExpectedCompound> shape(new ExpectedCompound);
        shape->addChildShape(
            mTransformScale2, new ExpectedScaledMesh(mesh1.release(), vsg::vec3(8, 8, 8)));
        shape->addChildShape(
            mTransformScale3, new ExpectedScaledMesh(mesh2.release(), vsg::vec3(12, 12, 12)));
        ExpectedPhysicsShape expected;
        expected.mCollisionShape.reset(shape.release());
        expected.mAnimatedShapes = { { -1, 0 } };

        EXPECT_EQ(*result, expected);
    }
}
