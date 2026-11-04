#include "skinnedshape.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

#include <components/nif/data.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>

namespace BodyNif
{
    namespace
    {
        // Row-major 3 x 4, [rotation and scale | translation]: v' = M3 * v + t (as components/lod/nifflatten.cpp).
        using Matrix = Matrix34;

        Matrix identityMatrix()
        {
            return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        }

        // outer * inner: the point goes through `inner` first.
        Matrix multiply(const Matrix& outer, const Matrix& inner)
        {
            Matrix result{};
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    double sum = 0;
                    for (int k = 0; k < 3; ++k)
                        sum += outer[row * 4 + k] * inner[k * 4 + col];
                    result[row * 4 + col] = sum;
                }
                double translation = outer[row * 4 + 3];
                for (int k = 0; k < 3; ++k)
                    translation += outer[row * 4 + k] * inner[k * 4 + 3];
                result[row * 4 + 3] = translation;
            }
            return result;
        }

        // NiTransform::toMatrix: entry (row i, column j) is mRotation.mValues[i][j] * mScale.
        Matrix fromTransform(const Nif::NiTransform& transform)
        {
            Matrix result{};
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                    result[i * 4 + j] = static_cast<double>(transform.mRotation.mValues[i][j]) * transform.mScale;
            }
            result[3] = transform.mTranslation.x;
            result[7] = transform.mTranslation.y;
            result[11] = transform.mTranslation.z;
            return result;
        }

        double determinant(const Matrix& m)
        {
            return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8])
                + m[2] * (m[4] * m[9] - m[5] * m[8]);
        }

        // Within 1e-6 of the identity (rotation, scale and translation).
        bool isIdentityMatrix(const Matrix& m)
        {
            const Matrix identity = identityMatrix();
            for (std::size_t i = 0; i < m.size(); ++i)
            {
                if (std::abs(m[i] - identity[i]) > 1e-6)
                    return false;
            }
            return true;
        }

        Vec3 transformPoint(const Matrix& m, const vsg::vec3& v)
        {
            Vec3 out{};
            for (int row = 0; row < 3; ++row)
            {
                out[static_cast<std::size_t>(row)] = static_cast<float>(
                    m[row * 4] * v.x + m[row * 4 + 1] * v.y + m[row * 4 + 2] * v.z + m[row * 4 + 3]);
            }
            return out;
        }

        // The rotation and scale part applied to a direction, then normalised (zero stays zero).
        Vec3 transformNormal(const Matrix& m, const vsg::vec3& n)
        {
            double x = m[0] * n.x + m[1] * n.y + m[2] * n.z;
            double y = m[4] * n.x + m[5] * n.y + m[6] * n.z;
            double z = m[8] * n.x + m[9] * n.y + m[10] * n.z;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 1e-12)
            {
                x /= length;
                y /= length;
                z /= length;
            }
            return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
        }

        // What a node passes down to its children (components/lod/nifflatten.cpp has the same idea).
        struct Inherited
        {
            std::string mTexture;
            bool mAlphaTest = false;
            bool mAlphaBlend = false;
        };

        char lower(char c)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // Whether `text` starts with `prefix`, ignoring ASCII case.
        bool startsWithIgnoringCase(std::string_view text, std::string_view prefix)
        {
            if (text.size() < prefix.size())
                return false;
            for (std::size_t i = 0; i < prefix.size(); ++i)
            {
                if (lower(text[i]) != lower(prefix[i]))
                    return false;
            }
            return true;
        }

        std::string lowerCase(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = lower(c);
            return out;
        }

        void collectRest(
            const Nif::NiAVObject& node, const Matrix& parent, std::size_t depth, std::size_t maxDepth, RestPose& rest)
        {
            if (depth > maxDepth)
                return;
            const Matrix world = multiply(parent, fromTransform(node.mTransform));
            if (!node.mName.empty())
                rest.emplace(lowerCase(node.mName), world);
            if (const auto* group = dynamic_cast<const Nif::NiNode*>(&node))
            {
                for (const auto& child : group->mChildren)
                {
                    if (!child.empty())
                        collectRest(*child.getPtr(), world, depth + 1, maxDepth, rest);
                }
            }
        }

        bool hasSkin(const Nif::NiAVObject& node, std::size_t depth, std::size_t maxDepth)
        {
            if (depth > maxDepth)
                return false;
            if (const auto* geometry = dynamic_cast<const Nif::NiGeometry*>(&node))
                return !geometry->mSkin.empty();
            if (const auto* group = dynamic_cast<const Nif::NiNode*>(&node))
            {
                for (const auto& child : group->mChildren)
                {
                    if (!child.empty() && hasSkin(*child.getPtr(), depth + 1, maxDepth))
                        return true;
                }
            }
            return false;
        }

        class Reader
        {
        public:
            Reader(std::string_view filter, const ReadLimits& limits, bool skinning, const Placement& placement)
                : mFilter(filter)
                , mLimits(limits)
                , mSkinning(skinning)
                , mPlacement(placement)
            {
                mResult.mSkinning = skinning;
            }

            ReadResult run(const Nif::NiAVObject& root)
            {
                visit(root, identityMatrix(), false, Inherited{}, 0);
                return std::move(mResult);
            }

        private:
            void error(const std::string& text) { mResult.mErrors.push_back(text); }

            void applyProperty(const Nif::NiProperty& property, Inherited& inherited)
            {
                if (property.mRecordType == Nif::RC_NiTexturingProperty)
                {
                    const auto& texturing = static_cast<const Nif::NiTexturingProperty&>(property);
                    inherited.mTexture.clear();
                    if (!texturing.mTextures.empty())
                    {
                        const auto& base = texturing.mTextures[Nif::NiTexturingProperty::BaseTexture];
                        if (base.mEnabled && !base.mSourceTexture.empty() && base.mSourceTexture->mExternal)
                            inherited.mTexture = base.mSourceTexture->mFile;
                    }
                }
                else if (property.mRecordType == Nif::RC_NiAlphaProperty)
                {
                    const auto& alpha = static_cast<const Nif::NiAlphaProperty&>(property);
                    inherited.mAlphaBlend = alpha.useAlphaBlending();
                    inherited.mAlphaTest = alpha.useAlphaTesting();
                }
            }

            void visit(
                const Nif::NiAVObject& node, const Matrix& parent, bool matched, Inherited inherited, std::size_t depth)
            {
                if (mDepthReported)
                    return;
                if (depth > mLimits.mMaxDepth)
                {
                    error("node tree deeper than the depth limit of " + std::to_string(mLimits.mMaxDepth) + " at node '"
                        + node.mName + "'");
                    mDepthReported = true;
                    return;
                }
                if (node.mRecordType == Nif::RC_RootCollisionNode || node.mRecordType == Nif::RC_AvoidNode)
                    return;

                // The engine's skin filter (nif.cpp, handleNode): on a skinned file, nodes before the match are
                // passed through without their transforms or properties.
                if (mSkinning && !mFilter.empty() && !matched)
                {
                    matched = filterMatches(node.mName, mFilter);
                    if (!matched)
                    {
                        if (const auto* group = dynamic_cast<const Nif::NiNode*>(&node))
                        {
                            for (const auto& child : group->mChildren)
                            {
                                if (!child.empty())
                                    visit(*child.getPtr(), identityMatrix(), false, Inherited{}, depth + 1);
                            }
                        }
                        return;
                    }
                }

                const Matrix world = multiply(parent, fromTransform(node.mTransform));
                for (const auto& property : node.mProperties)
                {
                    if (!property.empty())
                        applyProperty(*property.getPtr(), inherited);
                }

                switch (node.mRecordType)
                {
                    case Nif::RC_NiTriShape:
                    case Nif::RC_NiTriStrips:
                    case Nif::RC_BSLODTriShape:
                        addShape(static_cast<const Nif::NiGeometry&>(node), world, inherited);
                        return;
                    default:
                        break;
                }
                if (const auto* group = dynamic_cast<const Nif::NiNode*>(&node))
                {
                    for (const auto& child : group->mChildren)
                    {
                        if (!child.empty())
                            visit(*child.getPtr(), world, matched, inherited, depth + 1);
                    }
                }
            }

            // Fills the triangle list; false (with an error) when the data cannot be used.
            bool readTriangles(const Nif::NiGeometry& geometry, std::size_t vertexCount, Shape& shape)
            {
                const Nif::NiGeometryData& data = *geometry.mData.getPtr();
                if (data.mRecordType == Nif::RC_NiTriShapeData)
                {
                    for (const unsigned short index : static_cast<const Nif::NiTriShapeData&>(data).mTriangles)
                        shape.mTriangles.push_back(index);
                    if (shape.mTriangles.size() % 3 != 0)
                    {
                        error("shape '" + geometry.mName + "': the triangle list has "
                            + std::to_string(shape.mTriangles.size()) + " indices, not a multiple of 3");
                        return false;
                    }
                }
                else if (data.mRecordType == Nif::RC_NiTriStripsData)
                {
                    for (const auto& strip : static_cast<const Nif::NiTriStripsData&>(data).mStrips)
                    {
                        for (std::size_t i = 0; i + 2 < strip.size(); ++i)
                        {
                            const std::uint32_t a = strip[i], b = strip[i + 1], c = strip[i + 2];
                            if (a == b || b == c || a == c)
                                continue;
                            shape.mTriangles.push_back(a);
                            shape.mTriangles.push_back(i % 2 == 0 ? b : c);
                            shape.mTriangles.push_back(i % 2 == 0 ? c : b);
                        }
                    }
                }
                else
                {
                    return false; // not a triangle shape: silently not ours
                }
                if (shape.mTriangles.size() / 3 > mLimits.mMaxTriangles)
                {
                    error("shape '" + geometry.mName + "': " + std::to_string(shape.mTriangles.size() / 3)
                        + " triangles are over the limit of " + std::to_string(mLimits.mMaxTriangles));
                    return false;
                }
                for (const std::uint32_t index : shape.mTriangles)
                {
                    if (index >= vertexCount)
                    {
                        error("shape '" + geometry.mName + "': triangle index " + std::to_string(index)
                            + " is out of range (" + std::to_string(vertexCount) + " vertices)");
                        return false;
                    }
                }
                return true;
            }

            // Fills the bone table and the per-vertex influences; false (with an error) when the skin is unusable.
            bool readSkin(
                const Nif::NiGeometry& geometry, std::size_t vertexCount, Shape& shape, std::vector<Matrix>& boneBind)
            {
                const Nif::NiSkinInstance& skin = *geometry.mSkin.getPtr();
                const std::string where = "shape '" + geometry.mName + "': ";
                if (skin.mData.empty())
                {
                    error(where + "the skin has no data");
                    return false;
                }
                const Nif::NiSkinData& data = *skin.mData.getPtr();
                if (skin.mBones.empty())
                {
                    error(where + "the skin has no bones");
                    return false;
                }
                if (skin.mBones.size() > mLimits.mMaxBones)
                {
                    error(where + std::to_string(skin.mBones.size()) + " bones are over the limit of "
                        + std::to_string(mLimits.mMaxBones));
                    return false;
                }
                if (skin.mBones.size() != data.mBones.size())
                {
                    error(where + "the skin lists " + std::to_string(skin.mBones.size())
                        + " bones but its data describes " + std::to_string(data.mBones.size()));
                    return false;
                }
                if (!data.mTransform.isIdentity())
                    shape.mIdentityTransform = false;
                const Matrix skinTransform = fromTransform(data.mTransform);

                std::vector<std::vector<std::pair<std::uint16_t, float>>> weights(vertexCount);
                for (std::size_t bone = 0; bone < skin.mBones.size(); ++bone)
                {
                    if (skin.mBones[bone].empty())
                    {
                        error(where + "bone " + std::to_string(bone) + " is missing");
                        return false;
                    }
                    const std::string& boneName = skin.mBones[bone].getPtr()->mName;
                    shape.mBones.push_back(boneName);
                    // The bone's skin transform after the skin's own: skin space to the bone's space at bind.
                    boneBind.push_back(multiply(fromTransform(data.mBones[bone].mTransform), skinTransform));
                    for (const auto& [vertex, weight] : data.mBones[bone].mWeights)
                    {
                        if (vertex >= vertexCount)
                        {
                            error(where + "weight for vertex " + std::to_string(vertex) + " of bone '" + boneName
                                + "' is out of range (" + std::to_string(vertexCount) + " vertices)");
                            return false;
                        }
                        if (!std::isfinite(weight))
                        {
                            error(where + "weight for vertex " + std::to_string(vertex) + " of bone '" + boneName
                                + "' is not a finite number");
                            return false;
                        }
                        if (weight > 0.f)
                            weights[vertex].emplace_back(static_cast<std::uint16_t>(bone), weight);
                    }
                }

                shape.mSkin.resize(vertexCount);
                for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
                {
                    auto& list = weights[vertex];
                    // The engine sorts on the weight alone; equal weights go by bone index here (a total order).
                    std::stable_sort(list.begin(), list.end(), [](const auto& left, const auto& right) {
                        if (left.second != right.second)
                            return left.second > right.second;
                        return left.first < right.first;
                    });
                    VertexSkin& out = shape.mSkin[vertex];
                    out.mBeforeCut = static_cast<std::uint8_t>(std::min<std::size_t>(list.size(), 255));
                    out.mCount = static_cast<std::uint8_t>(std::min(list.size(), maxInfluences));
                    for (std::size_t i = 0; i < out.mCount; ++i)
                        out.mInfluences[i] = { list[i].first, list[i].second };
                }
                return true;
            }

            // Brings a skinned shape to its bind pose in the skeleton's space (see Placement): each vertex is the
            // weighted sum, over its kept influences, of (the bone's world matrix at rest times its skin transform
            // times the skin's own transform) applied to the stored vertex. A bone the skeleton lacks leaves the shape
            // where it is, with one error.
            void placeSkinned(const Nif::NiGeometry& geometry, const std::vector<Matrix>& boneBind,
                const Nif::NiGeometryData& data, Shape& shape)
            {
                std::vector<Matrix> full;
                full.reserve(shape.mBones.size());
                for (std::size_t bone = 0; bone < shape.mBones.size() && bone < boneBind.size(); ++bone)
                {
                    const auto found = mPlacement.mRest->find(lowerCase(shape.mBones[bone]));
                    if (found == mPlacement.mRest->end())
                    {
                        error(
                            "shape '" + geometry.mName + "': bone '" + shape.mBones[bone] + "' is not in the skeleton");
                        return;
                    }
                    full.push_back(multiply(found->second, boneBind[bone]));
                }
                for (std::size_t i = 0; i < shape.mPositions.size() && i < shape.mSkin.size(); ++i)
                {
                    const VertexSkin& skin = shape.mSkin[i];
                    double sum = 0;
                    for (std::size_t k = 0; k < skin.mCount; ++k)
                        sum += skin.mInfluences[k].mWeight;
                    if (sum <= 0)
                        continue; // no influence: stays in the skin's space
                    double position[3] = { 0, 0, 0 };
                    double normal[3] = { 0, 0, 0 };
                    for (std::size_t k = 0; k < skin.mCount; ++k)
                    {
                        const double weight = skin.mInfluences[k].mWeight / sum;
                        const Matrix& matrix = full[skin.mInfluences[k].mBone];
                        const Vec3 p = transformPoint(matrix, data.mVertices[i]);
                        for (std::size_t axis = 0; axis < 3; ++axis)
                            position[axis] += weight * p[axis];
                        if (!shape.mNormals.empty())
                        {
                            const Vec3 n = transformNormal(matrix, data.mNormals[i]);
                            for (std::size_t axis = 0; axis < 3; ++axis)
                                normal[axis] += weight * n[axis];
                        }
                    }
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        shape.mPositions[i][axis] = static_cast<float>(position[axis]);
                    if (!shape.mNormals.empty())
                    {
                        const double length
                            = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                        for (std::size_t axis = 0; axis < 3; ++axis)
                            shape.mNormals[i][axis] = static_cast<float>(length > 1e-12 ? normal[axis] / length : 0.0);
                    }
                }
                shape.mPlaced = true;
            }

            void addShape(const Nif::NiGeometry& geometry, const Matrix& world, const Inherited& inherited)
            {
                if (geometry.mData.empty())
                    return;
                if (mResult.mShapes.size() >= mLimits.mMaxShapes)
                {
                    if (!mShapeLimitReported)
                        error("more shapes than the limit of " + std::to_string(mLimits.mMaxShapes) + " at shape '"
                            + geometry.mName + "'");
                    mShapeLimitReported = true;
                    return;
                }
                const Nif::NiGeometryData& data = *geometry.mData.getPtr();
                const std::size_t vertexCount = data.mVertices.size();
                if (vertexCount > mLimits.mMaxVertices)
                {
                    error("shape '" + geometry.mName + "': " + std::to_string(vertexCount)
                        + " vertices are over the limit of " + std::to_string(mLimits.mMaxVertices));
                    return;
                }

                Shape shape;
                shape.mName = geometry.mName;
                shape.mTexture = inherited.mTexture;
                shape.mAlphaTest = inherited.mAlphaTest;
                shape.mAlphaBlend = inherited.mAlphaBlend;
                shape.mSkinned = !geometry.mSkin.empty();
                shape.mIdentityTransform = isIdentityMatrix(world);
                // A rigid shape hangs under its attach bone: its vertices are then in the skeleton's space.
                Matrix placed = world;
                if (!shape.mSkinned && mPlacement.mRest != nullptr)
                {
                    const auto bone = mPlacement.mRest->find(lowerCase(mPlacement.mBone));
                    if (bone != mPlacement.mRest->end())
                    {
                        // The engine mirrors a part on a "Left" bone in the bone's space (decorate, scale (-1, 1, 1)).
                        const Matrix mirrorX = { -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                        const bool mirror = mPlacement.mBone.find("Left") != std::string::npos;
                        placed = multiply(bone->second, mirror ? multiply(mirrorX, world) : world);
                        shape.mPlaced = true;
                    }
                    else if (!mPlacementReported)
                    {
                        error("attach bone '" + mPlacement.mBone + "' is not in the skeleton");
                        mPlacementReported = true;
                    }
                }
                shape.mMirrored = determinant(placed) < 0;

                if (!readTriangles(geometry, vertexCount, shape))
                    return;
                if (shape.mTriangles.empty() || vertexCount == 0)
                    return;
                std::vector<Matrix> boneBind;
                if (shape.mSkinned && !readSkin(geometry, vertexCount, shape, boneBind))
                    return;

                shape.mPositions.reserve(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    const vsg::vec3& v = data.mVertices[i];
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                    {
                        error(
                            "shape '" + geometry.mName + "': vertex " + std::to_string(i) + " is not a finite number");
                        return;
                    }
                    shape.mPositions.push_back(transformPoint(placed, v));
                }
                // A normal or UV list of another size than the vertex list is dropped, never read past its end.
                if (data.mNormals.size() == vertexCount)
                {
                    shape.mNormals.reserve(vertexCount);
                    for (const vsg::vec3& n : data.mNormals)
                        shape.mNormals.push_back(transformNormal(placed, n));
                }
                if (shape.mSkinned && mPlacement.mRest != nullptr)
                    placeSkinned(geometry, boneBind, data, shape);
                if (!data.mUVList.empty() && data.mUVList[0].size() == vertexCount)
                {
                    shape.mUvs.reserve(vertexCount);
                    for (const vsg::vec2& uv : data.mUVList[0])
                        shape.mUvs.push_back({ uv.x, uv.y });
                }
                mResult.mShapes.push_back(std::move(shape));
            }

            std::string_view mFilter;
            ReadLimits mLimits;
            bool mSkinning;
            Placement mPlacement;
            bool mPlacementReported = false;
            ReadResult mResult;
            bool mDepthReported = false;
            bool mShapeLimitReported = false;
        };
    }

    bool filterMatches(std::string_view nodeName, std::string_view filter)
    {
        if (filter.empty())
            return true;
        if (startsWithIgnoringCase(nodeName, filter))
            return true;
        constexpr std::string_view triPrefix = "tri ";
        return startsWithIgnoringCase(nodeName, triPrefix)
            && startsWithIgnoringCase(nodeName.substr(triPrefix.size()), filter);
    }

    RestPose readRestPose(const Nif::NiAVObject& root, const ReadLimits& limits)
    {
        RestPose rest;
        collectRest(root, identityMatrix(), 0, limits.mMaxDepth, rest);
        return rest;
    }

    ReadResult readShapes(
        const Nif::NiAVObject& root, std::string_view skinFilter, const ReadLimits& limits, const Placement& placement)
    {
        const bool skinning = hasSkin(root, 0, limits.mMaxDepth);
        return Reader(skinFilter, limits, skinning, placement).run(root);
    }
}
