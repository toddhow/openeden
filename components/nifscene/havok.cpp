#include "havok.hpp"

#include <cmath>
#include <unordered_map>

#include <components/nif/node.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/physics.hpp>

namespace NifScene::Havok
{
    namespace
    {
        Vec4 quatFromFile(const vsg::dquat& q, bool xyzw)
        {
            // The reader put file element 0 in w, 1 in x, 2 in y, 3 in z.
            const float f0 = static_cast<float>(q.w), f1 = static_cast<float>(q.x), f2 = static_cast<float>(q.y),
                        f3 = static_cast<float>(q.z);
            if (xyzw)
                return { f0, f1, f2, f3 };
            return { f1, f2, f3, f0 };
        }

        bool finite(const Vec3& v)
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }

        Vec3 v3(const vsg::vec4& v)
        {
            return { v.x, v.y, v.z };
        }

        Vec3 v3(const vsg::vec3& v)
        {
            return { v.x, v.y, v.z };
        }

        class Reader
        {
        public:
            Reader(const Limits& limits, Collision& out)
                : mLimits(limits)
                , mOut(out)
            {
            }

            // Returns the index of the shape for a record, or sNone when it is absent or a limit was hit.
            std::uint32_t shape(const Nif::Record* record, std::size_t depth)
            {
                if (record == nullptr)
                    return sNone;
                if (const auto found = mMemo.find(record); found != mMemo.end())
                    return found->second;
                if (depth > mLimits.mMaxDepth || mOut.mShapes.size() >= mLimits.mMaxShapes)
                {
                    mOut.mReport.mOverCap = true;
                    return sNone;
                }
                const auto index = static_cast<std::uint32_t>(mOut.mShapes.size());
                mOut.mShapes.emplace_back();
                mMemo.emplace(record, index);
                Shape built;
                built.mTypeName = record->mRecordName;
                fill(built, *record, depth);
                mOut.mShapes[index] = std::move(built);
                return index;
            }

        private:
            const Limits& mLimits;
            Collision& mOut;
            std::unordered_map<const Nif::Record*, std::uint32_t> mMemo;
            std::size_t mVertices = 0;
            std::size_t mTriangles = 0;

            void unsupported(Shape& s)
            {
                s.mKind = ShapeKind::Unsupported;
                ++mOut.mReport.mShapesUnsupported;
            }

            void notFinite(Shape& s)
            {
                Shape unsupported;
                unsupported.mTypeName = s.mTypeName;
                s = std::move(unsupported);
                ++mOut.mReport.mShapesNotFinite;
            }

            void fill(Shape& s, const Nif::Record& record, std::size_t depth)
            {
                switch (record.mRecordType)
                {
                    case Nif::RC_bhkMoppBvTreeShape:
                    {
                        // The MOPP code is a query accelerator for the same shape: the mesh is built from the shape.
                        const auto& mopp = static_cast<const Nif::bhkMoppBvTreeShape&>(record);
                        const std::uint32_t child = shape(mopp.mShape.empty() ? nullptr : mopp.mShape.getPtr(), depth + 1);
                        if (child == sNone)
                            return unsupported(s);
                        s.mKind = ShapeKind::List;
                        s.mChildren.push_back(child);
                        return;
                    }
                    case Nif::RC_bhkBoxShape:
                    {
                        const auto& box = static_cast<const Nif::bhkBoxShape&>(record);
                        s.mKind = ShapeKind::Box;
                        s.mMaterial = box.mHavokMaterial.mMaterial;
                        s.mRadius = box.mRadius;
                        s.mHalfExtents = v3(box.mExtents);
                        if (!finite(s.mHalfExtents) || !std::isfinite(s.mRadius))
                            notFinite(s);
                        return;
                    }
                    case Nif::RC_bhkSphereShape:
                    {
                        const auto& sphere = static_cast<const Nif::bhkConvexShape&>(record);
                        s.mKind = ShapeKind::Sphere;
                        s.mMaterial = sphere.mHavokMaterial.mMaterial;
                        s.mRadius = sphere.mRadius;
                        if (!std::isfinite(s.mRadius))
                            notFinite(s);
                        return;
                    }
                    case Nif::RC_bhkCapsuleShape:
                    {
                        const auto& capsule = static_cast<const Nif::bhkCapsuleShape&>(record);
                        s.mKind = ShapeKind::Capsule;
                        s.mMaterial = capsule.mHavokMaterial.mMaterial;
                        s.mRadius = capsule.mRadius;
                        s.mPoint1 = v3(capsule.mPoint1);
                        s.mPoint2 = v3(capsule.mPoint2);
                        s.mRadius1 = capsule.mRadius1;
                        s.mRadius2 = capsule.mRadius2;
                        if (!finite(s.mPoint1) || !finite(s.mPoint2) || !std::isfinite(s.mRadius1)
                            || !std::isfinite(s.mRadius2))
                            notFinite(s);
                        return;
                    }
                    case Nif::RC_bhkCylinderShape:
                    {
                        const auto& cylinder = static_cast<const Nif::bhkCylinderShape&>(record);
                        s.mKind = ShapeKind::Cylinder;
                        s.mMaterial = cylinder.mHavokMaterial.mMaterial;
                        s.mRadius = cylinder.mRadius;
                        s.mPoint1 = v3(cylinder.mVertexA);
                        s.mPoint2 = v3(cylinder.mVertexB);
                        s.mRadius1 = cylinder.mCylinderRadius;
                        if (!finite(s.mPoint1) || !finite(s.mPoint2) || !std::isfinite(s.mRadius1))
                            notFinite(s);
                        return;
                    }
                    case Nif::RC_bhkConvexVerticesShape:
                    {
                        const auto& hull = static_cast<const Nif::bhkConvexVerticesShape&>(record);
                        s.mKind = ShapeKind::ConvexHull;
                        s.mMaterial = hull.mHavokMaterial.mMaterial;
                        s.mRadius = hull.mRadius;
                        if (!reserveVertices(hull.mVertices.size()))
                            return unsupported(s);
                        for (const auto& v : hull.mVertices)
                            s.mPoints.push_back(v3(v));
                        for (const auto& p : s.mPoints)
                        {
                            if (!finite(p))
                                return notFinite(s);
                        }
                        return;
                    }
                    case Nif::RC_bhkListShape:
                    {
                        const auto& list = static_cast<const Nif::bhkListShape&>(record);
                        s.mKind = ShapeKind::List;
                        s.mMaterial = list.mHavokMaterial.mMaterial;
                        for (const auto& child : list.mSubshapes)
                        {
                            if (child.empty())
                                continue;
                            const std::uint32_t index = shape(child.getPtr(), depth + 1);
                            if (index != sNone)
                                s.mChildren.push_back(index);
                        }
                        return;
                    }
                    case Nif::RC_bhkConvexTransformShape:
                    {
                        const auto& transform = static_cast<const Nif::bhkConvexTransformShape&>(record);
                        s.mKind = ShapeKind::Transform;
                        s.mMaterial = transform.mHavokMaterial.mMaterial;
                        s.mRadius = transform.mRadius;
                        for (int i = 0; i < 16; ++i)
                            s.mTransform[i] = transform.mTransform[i / 4][i % 4];
                        for (float value : s.mTransform)
                        {
                            if (!std::isfinite(value))
                                return notFinite(s);
                        }
                        const std::uint32_t child
                            = shape(transform.mShape.empty() ? nullptr : transform.mShape.getPtr(), depth + 1);
                        if (child == sNone)
                            return unsupported(s);
                        s.mChildren.push_back(child);
                        return;
                    }
                    case Nif::RC_bhkCompressedMeshShape:
                    {
                        const auto& mesh = static_cast<const Nif::bhkCompressedMeshShape&>(record);
                        if (mesh.mData.empty())
                            return unsupported(s);
                        fillCompressed(s, mesh);
                        return;
                    }
                    case Nif::RC_bhkPackedNiTriStripsShape:
                    {
                        const auto& packed = static_cast<const Nif::bhkPackedNiTriStripsShape&>(record);
                        if (packed.mData.empty())
                            return unsupported(s);
                        const auto& data = *packed.mData.getPtr();
                        s.mKind = ShapeKind::TriMesh;
                        if (!reserveVertices(data.mVertices.size()) || !reserveTriangles(data.mTriangles.size()))
                            return unsupported(s);
                        const Vec3 scale = v3(packed.mScale);
                        for (const auto& v : data.mVertices)
                            s.mVertices.push_back({ v.x * scale[0], v.y * scale[1], v.z * scale[2] });
                        for (const auto& t : data.mTriangles)
                        {
                            std::array<std::uint32_t, 3> tri{ t.mTriangle[0], t.mTriangle[1], t.mTriangle[2] };
                            if (tri[0] >= s.mVertices.size() || tri[1] >= s.mVertices.size()
                                || tri[2] >= s.mVertices.size())
                                ++mOut.mReport.mTrianglesDropped;
                            else
                                s.mTriangles.push_back(tri);
                        }
                        return;
                    }
                    default:
                        return unsupported(s);
                }
            }

            bool reserveVertices(std::size_t count)
            {
                mVertices += count;
                if (mVertices > mLimits.mMaxVertices)
                {
                    mOut.mReport.mOverCap = true;
                    return false;
                }
                return true;
            }

            bool reserveTriangles(std::size_t count)
            {
                mTriangles += count;
                if (mTriangles > mLimits.mMaxTriangles)
                {
                    mOut.mReport.mOverCap = true;
                    return false;
                }
                return true;
            }

            void fillCompressed(Shape& s, const Nif::bhkCompressedMeshShape& mesh)
            {
                const Nif::bhkCompressedMeshShapeData& data = *mesh.mData.getPtr();
                s.mKind = ShapeKind::TriMesh;
                s.mRadius = mesh.mRadius;
                if (!reserveVertices(data.mBigVerts.size()))
                    return unsupported(s);
                // Big triangles use full precision vertices.
                for (const auto& v : data.mBigVerts)
                    s.mVertices.push_back(v3(v));
                if (!reserveTriangles(data.mBigTris.size()))
                    return unsupported(s);
                for (const auto& t : data.mBigTris)
                {
                    std::array<std::uint32_t, 3> tri{ t.mTriangle[0], t.mTriangle[1], t.mTriangle[2] };
                    if (tri[0] >= s.mVertices.size() || tri[1] >= s.mVertices.size() || tri[2] >= s.mVertices.size())
                        ++mOut.mReport.mTrianglesDropped;
                    else
                        s.mTriangles.push_back(tri);
                }
                for (const Nif::bhkCMSChunk& stored : data.mChunks)
                {
                    if (!reserveVertices(stored.mVertices.size() / 3)
                        || !reserveTriangles(stored.mIndices.size() / 3 + stored.mStrips.size()))
                        return unsupported(s);
                    Chunk chunk;
                    chunk.mTranslation = { stored.mTranslation.x, stored.mTranslation.y, stored.mTranslation.z,
                        stored.mTranslation.w };
                    chunk.mTransformIndex = stored.mTransformIndex;
                    chunk.mVertices = stored.mVertices;
                    chunk.mIndices = stored.mIndices;
                    chunk.mStrips = stored.mStrips;
                    ChunkTransform transform;
                    const ChunkTransform* use = nullptr;
                    if (stored.mTransformIndex < data.mChunkTransforms.size())
                    {
                        const Nif::bhkQsTransform& q = data.mChunkTransforms[stored.mTransformIndex];
                        transform.mTranslation = { q.mTranslation.x, q.mTranslation.y, q.mTranslation.z, q.mTranslation.w };
                        transform.mRotation = quatFromFile(q.mRotation, mLimits.mChunkQuatXyzw);
                        use = &transform;
                    }
                    mOut.mReport.mTrianglesDropped += decodeChunk(chunk, use, s.mVertices, s.mTriangles);
                }
                // The shape's own scale.
                const Vec3 scale = v3(mesh.mScale);
                for (auto& v : s.mVertices)
                {
                    v[0] *= scale[0];
                    v[1] *= scale[1];
                    v[2] *= scale[2];
                }
                for (const auto& v : s.mVertices)
                {
                    if (!finite(v))
                        return notFinite(s);
                }
            }
        };
    }

    bool bodyBlocksActors(const Body& body)
    {
        // Layer numbers by the usual Skyrim collision layer list (static 1, anim static 2, ... trigger 12, non
        // collidable 15). RECALLED-VERIFY.
        constexpr std::uint8_t sTrigger = 12, sNonCollidable = 15, sCloudTrap = 16, sPortal = 18;
        constexpr std::uint8_t sResponseNone = 3; // HkResponseType::Response_None
        if (body.mResponseType == sResponseNone)
            return false;
        return body.mLayer != sTrigger && body.mLayer != sNonCollidable && body.mLayer != sCloudTrap
            && body.mLayer != sPortal;
    }

    Vec3 rotate(const Vec4& q, const Vec3& v)
    {
        // v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z)
        const float ux = q[0], uy = q[1], uz = q[2], w = q[3];
        const float cx = uy * v[2] - uz * v[1];
        const float cy = uz * v[0] - ux * v[2];
        const float cz = ux * v[1] - uy * v[0];
        const float dx = uy * cz - uz * cy;
        const float dy = uz * cx - ux * cz;
        const float dz = ux * cy - uy * cx;
        return { v[0] + 2.f * (w * cx + dx), v[1] + 2.f * (w * cy + dy), v[2] + 2.f * (w * cz + dz) };
    }

    std::uint32_t decodeChunk(const Chunk& chunk, const ChunkTransform* transform, std::vector<Vec3>& vertices,
        std::vector<std::array<std::uint32_t, 3>>& triangles)
    {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        const std::size_t count = chunk.mVertices.size() / 3;
        for (std::size_t i = 0; i < count; ++i)
        {
            Vec3 v{ chunk.mTranslation[0] + kChunkVertexStep * chunk.mVertices[3 * i],
                chunk.mTranslation[1] + kChunkVertexStep * chunk.mVertices[3 * i + 1],
                chunk.mTranslation[2] + kChunkVertexStep * chunk.mVertices[3 * i + 2] };
            if (transform != nullptr)
            {
                v = rotate(transform->mRotation, v);
                v[0] += transform->mTranslation[0];
                v[1] += transform->mTranslation[1];
                v[2] += transform->mTranslation[2];
            }
            vertices.push_back(v);
        }

        std::uint32_t dropped = 0;
        auto add = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            if (a >= count || b >= count || c >= count)
            {
                ++dropped;
                return;
            }
            if (a == b || b == c || a == c)
                return; // a strip's degenerate join, not an error
            triangles.push_back({ base + a, base + b, base + c });
        };

        std::size_t at = 0;
        for (std::uint16_t length : chunk.mStrips)
        {
            if (at + length > chunk.mIndices.size())
            {
                ++dropped;
                at = chunk.mIndices.size();
                break;
            }
            for (std::size_t k = 2; k < length; ++k)
            {
                const std::uint32_t a = chunk.mIndices[at + k - 2], b = chunk.mIndices[at + k - 1],
                                    c = chunk.mIndices[at + k];
                if (k % 2 == 0)
                    add(a, b, c);
                else
                    add(b, a, c);
            }
            at += length;
        }
        for (; at + 2 < chunk.mIndices.size(); at += 3)
            add(chunk.mIndices[at], chunk.mIndices[at + 1], chunk.mIndices[at + 2]);
        return dropped;
    }

    Collision readCollision(const Nif::NIFFile& file, const Limits& limits)
    {
        Collision out;
        Reader reader(limits, out);
        for (const auto& record : file.mRecords)
        {
            const auto* object = dynamic_cast<const Nif::NiAVObject*>(record.get());
            if (object == nullptr || object->mCollision.empty())
                continue;
            ++out.mReport.mCollisionObjects;
            const auto* collision = dynamic_cast<const Nif::bhkCollisionObject*>(object->mCollision.getPtr());
            if (collision == nullptr || collision->mBody.empty())
            {
                ++out.mReport.mObjectsOfOtherKinds;
                continue;
            }
            const auto* body = dynamic_cast<const Nif::bhkRigidBody*>(collision->mBody.getPtr());
            if (body == nullptr)
            {
                ++out.mReport.mObjectsOfOtherKinds;
                continue;
            }
            if (out.mBodies.size() >= limits.mMaxBodies)
            {
                out.mReport.mOverCap = true;
                break;
            }
            Body b;
            b.mNodeRecord = object->mRecordIndex;
            b.mShape = reader.shape(body->mShape.empty() ? nullptr : body->mShape.getPtr(), 0);
            b.mHasTransform = body->mRecordType == Nif::RC_bhkRigidBodyT;
            b.mTranslation = v3(body->mInfo.mTranslation);
            b.mRotation = quatFromFile(body->mInfo.mRotation, limits.mRigidBodyQuatXyzw);
            b.mLayer = body->mInfo.mHavokFilter.mLayer;
            b.mFilterFlags = body->mInfo.mHavokFilter.mFlags;
            b.mGroup = body->mInfo.mHavokFilter.mGroup;
            b.mWorldObjectLayer = body->mHavokFilter.mLayer;
            b.mMotionType = static_cast<std::uint8_t>(body->mInfo.mMotionType);
            b.mQuality = static_cast<std::uint8_t>(body->mInfo.mQualityType);
            b.mResponseType = static_cast<std::uint8_t>(body->mInfo.mResponseType);
            b.mMass = body->mInfo.mMass;
            b.mFriction = body->mInfo.mFriction;
            b.mRestitution = body->mInfo.mRestitution;
            b.mCollisionFlags = collision->mFlags;
            out.mBodies.push_back(b);
            ++out.mReport.mBodies;
        }
        return out;
    }
}
