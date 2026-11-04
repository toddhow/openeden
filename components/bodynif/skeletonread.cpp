#include "skeletonread.hpp"

#include <components/nif/data.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace BodyNif
{
    namespace
    {
        /// NIF rotations are stored as 32-bit floats: rows of an exported rotation are unit and orthogonal to about
        /// 1e-6; 1e-3 is far above that and far below any scale or shear an exporter would leave in the matrix.
        constexpr double sOrthonormalTolerance = 1e-3;
        /// A scale that differs from 1 by less than this is 1 (float round-off of an authored 1.0).
        constexpr double sScaleTolerance = 1e-6;
        /// A vertex's weights must sum to 1 within this. Packed weights are half floats (11 significant bits, a step
        /// of about 5e-4 near 1), so four of them sum to 1 within about 2e-3; 1e-2 still catches a real error.
        constexpr double sWeightSumTolerance = 1e-2;
        /// The packed vertex format of BSTriShape holds four influences per vertex (BSVertexData::mBoneWeights).
        constexpr std::size_t sPackedInfluences = 4;

        std::array<double, 4> quaternionOf(const Nif::Matrix3& matrix, bool& orthonormal)
        {
            double r[3][3];
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    r[i][j] = static_cast<double>(matrix.mValues[i][j]);
                }
            }
            orthonormal = true;
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    const double dot = r[i][0] * r[j][0] + r[i][1] * r[j][1] + r[i][2] * r[j][2];
                    const double expected = i == j ? 1.0 : 0.0;
                    if (!(std::abs(dot - expected) <= sOrthonormalTolerance))
                    {
                        orthonormal = false;
                    }
                }
            }
            const double determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
                - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
            if (!(determinant > 0))
            {
                orthonormal = false;
            }
            // Shepperd's method for a column-vector rotation matrix (v' = R v).
            double x = 0;
            double y = 0;
            double z = 0;
            double w = 1;
            const double trace = r[0][0] + r[1][1] + r[2][2];
            if (trace > 0)
            {
                const double s = std::sqrt(trace + 1.0) * 2.0;
                w = 0.25 * s;
                x = (r[2][1] - r[1][2]) / s;
                y = (r[0][2] - r[2][0]) / s;
                z = (r[1][0] - r[0][1]) / s;
            }
            else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
            {
                const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
                w = (r[2][1] - r[1][2]) / s;
                x = 0.25 * s;
                y = (r[0][1] + r[1][0]) / s;
                z = (r[0][2] + r[2][0]) / s;
            }
            else if (r[1][1] > r[2][2])
            {
                const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
                w = (r[0][2] - r[2][0]) / s;
                x = (r[0][1] + r[1][0]) / s;
                y = 0.25 * s;
                z = (r[1][2] + r[2][1]) / s;
            }
            else
            {
                const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
                w = (r[1][0] - r[0][1]) / s;
                x = (r[0][2] + r[2][0]) / s;
                y = (r[1][2] + r[2][1]) / s;
                z = 0.25 * s;
            }
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!std::isfinite(length) || length <= 0)
            {
                orthonormal = false;
                return { 0, 0, 0, 1 };
            }
            // q and -q are the same rotation: the sign with w >= 0 is the one written, so equal input gives equal
            // output.
            const double sign = w < 0 ? -1.0 : 1.0;
            return { sign * x / length, sign * y / length, sign * z / length, sign * w / length };
        }

        bool isGeometry(const Nif::NiAVObject& object)
        {
            return dynamic_cast<const Nif::NiGeometry*>(&object) != nullptr
                || dynamic_cast<const Nif::BSTriShape*>(&object) != nullptr;
        }

        std::string shown(const std::string& name)
        {
            return "'" + name + "'";
        }

        // ---- weights -----------------------------------------------------------------------------------------------

        struct RouteSums
        {
            bool mPresent = false; // the route exists in the file (even if it holds no weight)
            bool mAny = false; // it holds at least one positive weight
            std::vector<double> mMass; // per skin bone
            std::vector<double> mVertexSum; // per shape vertex
            std::size_t mOutOfRange = 0;
            std::size_t mNonFinite = 0;

            RouteSums(std::size_t bones, std::size_t vertices)
                : mMass(bones, 0.0)
                , mVertexSum(vertices, 0.0)
            {
            }

            // Adds one influence; a weight that is not finite or negative, or an index out of range, is counted and
            // left out; a zero weight is left out silently.
            void add(std::size_t vertex, std::size_t bone, double weight)
            {
                if (!std::isfinite(weight) || weight < 0)
                {
                    ++mNonFinite;
                    return;
                }
                if (weight == 0)
                {
                    return;
                }
                if (vertex >= mVertexSum.size() || bone >= mMass.size())
                {
                    ++mOutOfRange;
                    return;
                }
                mMass[bone] += weight;
                mVertexSum[vertex] += weight;
                mAny = true;
            }

            double total() const
            {
                double sum = 0;
                for (const double m : mMass)
                {
                    sum += m;
                }
                return sum;
            }
        };

        double largestShareDifference(const RouteSums& a, const RouteSums& b)
        {
            const double ta = a.total();
            const double tb = b.total();
            if (!(ta > 0) || !(tb > 0))
            {
                return -1;
            }
            double largest = 0;
            for (std::size_t i = 0; i < a.mMass.size() && i < b.mMass.size(); ++i)
            {
                largest = std::max(largest, std::abs(a.mMass[i] / ta - b.mMass[i] / tb));
            }
            return largest;
        }

        RouteSums skinDataRoute(const Nif::NiSkinInstance& skin, std::size_t bones, std::size_t vertices)
        {
            RouteSums sums(bones, vertices);
            if (skin.mData.empty())
            {
                return sums;
            }
            const Nif::NiSkinData& data = *skin.mData.getPtr();
            sums.mPresent = true;
            for (std::size_t bone = 0; bone < data.mBones.size() && bone < bones; ++bone)
            {
                for (const auto& [vertex, weight] : data.mBones[bone].mWeights)
                {
                    sums.add(vertex, bone, weight);
                }
            }
            return sums;
        }

        RouteSums partitionRoute(const Nif::NiSkinPartition* partitions, std::size_t bones, std::size_t vertices,
            std::vector<std::string>& errors, const std::string& shapeName)
        {
            RouteSums sums(bones, vertices);
            if (partitions == nullptr)
            {
                return sums;
            }
            std::vector<bool> seen(vertices, false);
            bool malformed = false;
            for (const Nif::NiSkinPartition::Partition& part : partitions->mPartitions)
            {
                const std::size_t count = part.mVertexMap.size();
                if (count == 0 || part.mWeights.empty())
                {
                    continue;
                }
                const std::size_t perVertex = part.mWeights.size() / count;
                const bool hasIndices = !part.mBoneIndices.empty();
                if (part.mWeights.size() % count != 0
                    || (hasIndices && part.mBoneIndices.size() != part.mWeights.size())
                    || (!hasIndices && perVertex != part.mBones.size()))
                {
                    malformed = true;
                    continue;
                }
                sums.mPresent = true;
                for (std::size_t j = 0; j < count; ++j)
                {
                    const std::size_t vertex = part.mVertexMap[j];
                    if (vertex >= vertices)
                    {
                        ++sums.mOutOfRange;
                        continue;
                    }
                    if (seen[vertex])
                    {
                        continue;
                    }
                    seen[vertex] = true;
                    for (std::size_t k = 0; k < perVertex; ++k)
                    {
                        const std::size_t slot = j * perVertex + k;
                        const std::size_t local = hasIndices ? static_cast<unsigned char>(part.mBoneIndices[slot]) : k;
                        const double weight = part.mWeights[slot];
                        if (std::isfinite(weight) && weight > 0 && local >= part.mBones.size())
                        {
                            ++sums.mOutOfRange;
                            continue;
                        }
                        sums.add(vertex, local < part.mBones.size() ? part.mBones[local] : bones, weight);
                    }
                }
            }
            if (malformed)
            {
                errors.push_back("shape " + shown(shapeName)
                    + ": a skin partition's weight or index lists do not match "
                    + "its vertex map: that partition is not read");
            }
            return sums;
        }

        // The packed vertex data with the indices as skin bone indices.
        RouteSums vertexDataGlobal(const std::vector<Nif::BSVertexData>& data, std::size_t bones, std::size_t vertices)
        {
            RouteSums sums(bones, vertices);
            sums.mPresent = true;
            for (std::size_t vertex = 0; vertex < data.size(); ++vertex)
            {
                for (std::size_t k = 0; k < sPackedInfluences; ++k)
                {
                    const double weight = halfToFloat(data[vertex].mBoneWeights[k]);
                    sums.add(vertex, static_cast<unsigned char>(data[vertex].mBoneIndices[k]), weight);
                }
            }
            return sums;
        }

        // The packed vertex data with the indices into each partition's bone list (a shape vertex counts once).
        RouteSums vertexDataLocal(const std::vector<Nif::BSVertexData>& data, const Nif::NiSkinPartition& partitions,
            std::size_t bones, std::size_t vertices)
        {
            RouteSums sums(bones, vertices);
            sums.mPresent = true;
            std::vector<bool> seen(vertices, false);
            for (const Nif::NiSkinPartition::Partition& part : partitions.mPartitions)
            {
                for (const unsigned short mapped : part.mVertexMap)
                {
                    const std::size_t vertex = mapped;
                    if (vertex >= vertices || vertex >= data.size())
                    {
                        ++sums.mOutOfRange;
                        continue;
                    }
                    if (seen[vertex])
                    {
                        continue;
                    }
                    seen[vertex] = true;
                    for (std::size_t k = 0; k < sPackedInfluences; ++k)
                    {
                        const double weight = halfToFloat(data[vertex].mBoneWeights[k]);
                        const std::size_t local = static_cast<unsigned char>(data[vertex].mBoneIndices[k]);
                        if (std::isfinite(weight) && weight > 0 && local >= part.mBones.size())
                        {
                            ++sums.mOutOfRange;
                            continue;
                        }
                        sums.add(vertex, local < part.mBones.size() ? part.mBones[local] : bones, weight);
                    }
                }
            }
            return sums;
        }

        // Fills a ShapeWeights from the routes available.
        void readWeights(ShapeWeights& shape, std::size_t bones, const Nif::NiSkinInstance* niSkin,
            const std::vector<Nif::BSVertexData>* packed, std::vector<std::string>& errors)
        {
            const std::size_t vertices = shape.mVertices;
            const Nif::NiSkinPartition* partitions = niSkin != nullptr ? niSkin->getPartitions() : nullptr;
            RouteSums skinData(bones, vertices);
            if (niSkin != nullptr)
            {
                skinData = skinDataRoute(*niSkin, bones, vertices);
            }
            RouteSums parts = partitionRoute(partitions, bones, vertices, errors, shape.mName);
            RouteSums global(bones, vertices);
            RouteSums local(bones, vertices);
            if (packed != nullptr)
            {
                global = vertexDataGlobal(*packed, bones, vertices);
                if (partitions != nullptr && !partitions->mPartitions.empty())
                {
                    local = vertexDataLocal(*packed, *partitions, bones, vertices);
                }
            }
            shape.mHasSkinDataWeights = skinData.mAny;
            shape.mHasPartitionWeights = parts.mAny;
            shape.mHasVertexData = packed != nullptr;

            const RouteSums* chosen = nullptr;
            if (skinData.mAny)
            {
                shape.mRoute = WeightRoute::SkinData;
                chosen = &skinData;
            }
            else if (parts.mAny)
            {
                shape.mRoute = WeightRoute::Partitions;
                chosen = &parts;
            }
            else if (global.mAny)
            {
                // Skin bone indices: what stage a measured in the Skyrim SE files (research note 0036, section 2).
                shape.mRoute = WeightRoute::VertexData;
                shape.mGlobalIndices = true;
                chosen = &global;
            }
            else if (local.mAny)
            {
                shape.mRoute = WeightRoute::VertexData;
                shape.mGlobalIndices = false;
                chosen = &local;
            }
            if (chosen == nullptr)
            {
                shape.mMass.assign(bones, 0.0);
                shape.mUnweightedVertices = vertices;
                return;
            }
            shape.mMass = chosen->mMass;
            shape.mTotal = chosen->total();
            shape.mOutOfRange = chosen->mOutOfRange;
            shape.mNonFinite = chosen->mNonFinite;
            for (const double sum : chosen->mVertexSum)
            {
                if (sum > 0)
                {
                    ++shape.mWeightedVertices;
                    if (!(std::abs(sum - 1.0) <= sWeightSumTolerance))
                    {
                        ++shape.mBadSums;
                    }
                }
                else
                {
                    ++shape.mUnweightedVertices;
                }
            }
            if (parts.mAny && chosen != &parts)
            {
                shape.mPartitionAgreement = largestShareDifference(*chosen, parts);
            }
            if (global.mAny && chosen != &global)
            {
                shape.mVertexDataGlobalAgreement = largestShareDifference(*chosen, global);
            }
            if (local.mAny && chosen != &local)
            {
                shape.mVertexDataLocalAgreement = largestShareDifference(*chosen, local);
            }
        }

        std::vector<std::string> boneNames(const Nif::NiAVObjectList& bones)
        {
            std::vector<std::string> names;
            names.reserve(bones.size());
            for (const auto& bone : bones)
            {
                names.push_back(bone.empty() ? std::string() : bone.getPtr()->mName);
            }
            return names;
        }

        bool skinned(std::uint16_t flags)
        {
            return (flags & Nif::BSVertexDesc::VertexAttribute::Skinned) != 0;
        }

        // A shape whose file says it has no vertices (Skyrim SE keeps a skinned BSTriShape's vertices in its skin
        // partition): the partition's packed vertex data, or the largest vertex the partitions map, plus one.
        void deriveVertexCount(ShapeWeights& shape, const Nif::NiSkinPartition* partitions)
        {
            if (shape.mVertices != 0 || partitions == nullptr)
            {
                return;
            }
            std::size_t count = partitions->mVertexData.size();
            if (count == 0)
            {
                for (const Nif::NiSkinPartition::Partition& part : partitions->mPartitions)
                {
                    for (const unsigned short vertex : part.mVertexMap)
                    {
                        count = std::max(count, static_cast<std::size_t>(vertex) + 1);
                    }
                }
            }
            if (count != 0)
            {
                shape.mVertices = count;
                shape.mVertexCountFromFile = false;
            }
        }

        std::string tooManyVertices(const ShapeWeights& shape, std::size_t cap)
        {
            return "shape " + shown(shape.mName) + " has more than " + std::to_string(cap) + " vertices: not read";
        }
    }

    float halfToFloat(std::uint16_t bits)
    {
        const bool negative = (bits & 0x8000u) != 0;
        const unsigned exponent = (bits >> 10) & 0x1Fu;
        const unsigned mantissa = bits & 0x3FFu;
        float magnitude = 0;
        if (exponent == 0)
        {
            magnitude = std::ldexp(static_cast<float>(mantissa), -24);
        }
        else if (exponent == 31)
        {
            if (mantissa != 0)
            {
                return std::numeric_limits<float>::quiet_NaN();
            }
            magnitude = std::numeric_limits<float>::infinity();
        }
        else
        {
            magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
        }
        return negative ? -magnitude : magnitude;
    }

    const char* toString(WeightRoute route)
    {
        switch (route)
        {
            case WeightRoute::None:
                return "none";
            case WeightRoute::SkinData:
                return "skindata";
            case WeightRoute::Partitions:
                return "partitions";
            case WeightRoute::VertexData:
                return "vertexdata";
        }
        return "none";
    }

    SkeletonRead readSkeleton(const Nif::NiAVObject& root, const SkeletonLimits& limits)
    {
        SkeletonRead result;
        struct Frame
        {
            const Nif::NiAVObject* mObject;
            int mParent;
            std::size_t mDepth;
            double mAncestorScale;
        };
        std::vector<Frame> stack{ { &root, -1, 0, 1.0 } };
        std::set<const Nif::NiAVObject*> seen;
        bool depthReported = false;
        while (!stack.empty())
        {
            const Frame frame = stack.back();
            stack.pop_back();
            if (!seen.insert(frame.mObject).second)
            {
                ++result.mRevisits;
                continue;
            }
            const auto* node = dynamic_cast<const Nif::NiNode*>(frame.mObject);
            if (node == nullptr)
            {
                if (isGeometry(*frame.mObject))
                {
                    ++result.mGeometry;
                }
                else
                {
                    ++result.mOtherObjects;
                }
                continue;
            }
            if (frame.mDepth > limits.mMaxDepth)
            {
                if (!depthReported)
                {
                    result.mErrors.push_back("node " + shown(node->mName) + " is at depth "
                        + std::to_string(frame.mDepth) + ", deeper than the cap of " + std::to_string(limits.mMaxDepth)
                        + ": it is left out with its subtree");
                    depthReported = true;
                }
                continue;
            }
            if (result.mNodes.size() >= limits.mMaxNodes)
            {
                result.mErrors.push_back("more than " + std::to_string(limits.mMaxNodes) + " nodes: reading stopped");
                break;
            }
            const Nif::NiTransform& transform = node->mTransform;
            SkeletonNode bone;
            bone.mName = node->mName;
            bone.mParent = frame.mParent;
            bone.mDepth = frame.mDepth;
            bone.mTranslation = { frame.mAncestorScale * transform.mTranslation.x,
                frame.mAncestorScale * transform.mTranslation.y, frame.mAncestorScale * transform.mTranslation.z };
            bone.mRotation = quaternionOf(transform.mRotation, bone.mOrthonormal);
            bone.mScale = transform.mScale;
            if (bone.mName.empty())
            {
                ++result.mUnnamed;
            }
            if (!(std::abs(bone.mScale - 1.0) <= sScaleTolerance))
            {
                ++result.mScaled;
            }
            if (!bone.mOrthonormal)
            {
                ++result.mNonOrthonormal;
            }
            const int index = static_cast<int>(result.mNodes.size());
            result.mMaxDepth = std::max(result.mMaxDepth, frame.mDepth);
            result.mNodes.push_back(std::move(bone));
            const double childScale = frame.mAncestorScale * transform.mScale;
            for (auto child = node->mChildren.rbegin(); child != node->mChildren.rend(); ++child)
            {
                if (!child->empty())
                {
                    stack.push_back({ child->getPtr(), index, frame.mDepth + 1, childScale });
                }
            }
        }
        return result;
    }

    SkinWeightsRead readSkinWeights(const Nif::NiAVObject& root, const SkeletonLimits& limits)
    {
        SkinWeightsRead result;
        struct Frame
        {
            const Nif::NiAVObject* mObject;
            std::size_t mDepth;
        };
        std::vector<Frame> stack{ { &root, 0 } };
        std::set<const Nif::NiAVObject*> seen;
        bool depthReported = false;
        bool shapesReported = false;
        while (!stack.empty())
        {
            const Frame frame = stack.back();
            stack.pop_back();
            if (!seen.insert(frame.mObject).second)
            {
                continue;
            }
            if (frame.mDepth > limits.mMaxDepth)
            {
                if (!depthReported)
                {
                    result.mErrors.push_back("an object deeper than " + std::to_string(limits.mMaxDepth)
                        + " levels is left out with its subtree");
                    depthReported = true;
                }
                continue;
            }
            const Nif::NiAVObject& object = *frame.mObject;
            if (const auto* node = dynamic_cast<const Nif::NiNode*>(&object))
            {
                for (auto child = node->mChildren.rbegin(); child != node->mChildren.rend(); ++child)
                {
                    if (!child->empty())
                    {
                        stack.push_back({ child->getPtr(), frame.mDepth + 1 });
                    }
                }
                continue;
            }
            const auto* geometry = dynamic_cast<const Nif::NiGeometry*>(&object);
            const auto* packedShape = dynamic_cast<const Nif::BSTriShape*>(&object);
            if (geometry == nullptr && packedShape == nullptr)
            {
                continue;
            }
            const bool hasSkin = geometry != nullptr ? !geometry->mSkin.empty() : !packedShape->mSkin.empty();
            if (!hasSkin)
            {
                ++result.mUnskinnedShapes;
                continue;
            }
            if (result.mShapes.size() >= limits.mMaxShapes)
            {
                if (!shapesReported)
                {
                    result.mErrors.push_back(
                        "more than " + std::to_string(limits.mMaxShapes) + " skinned shapes: the rest are not read");
                    shapesReported = true;
                }
                continue;
            }
            ShapeWeights shape;
            shape.mName = object.mName;
            if (geometry != nullptr)
            {
                const Nif::NiSkinInstance& skin = *geometry->mSkin.getPtr();
                shape.mKind = "NiGeometry+NiSkinInstance";
                shape.mVertices = geometry->mData.empty() ? 0 : geometry->mData.getPtr()->mVertices.size();
                shape.mBones = boneNames(skin.mBones);
                deriveVertexCount(shape, skin.getPartitions());
                if (shape.mVertices > limits.mMaxVertices)
                {
                    result.mErrors.push_back(tooManyVertices(shape, limits.mMaxVertices));
                    continue;
                }
                readWeights(shape, shape.mBones.size(), &skin, nullptr, result.mErrors);
            }
            else
            {
                const Nif::Record* skinRecord = packedShape->mSkin.getPtr();
                shape.mVertices = packedShape->mNumVertices;
                if (const auto* niSkin = dynamic_cast<const Nif::NiSkinInstance*>(skinRecord))
                {
                    deriveVertexCount(shape, niSkin->getPartitions());
                }
                if (shape.mVertices > limits.mMaxVertices)
                {
                    result.mErrors.push_back(tooManyVertices(shape, limits.mMaxVertices));
                    continue;
                }
                if (const auto* niSkin = dynamic_cast<const Nif::NiSkinInstance*>(skinRecord))
                {
                    shape.mKind = "BSTriShape+NiSkinInstance";
                    shape.mBones = boneNames(niSkin->mBones);
                    // Skyrim SE keeps a skinned shape's packed vertices in its skin partition; other files in the
                    // shape itself.
                    const std::vector<Nif::BSVertexData>* packed = nullptr;
                    if (!packedShape->mVertData.empty() && skinned(packedShape->mVertDesc.mFlags))
                    {
                        packed = &packedShape->mVertData;
                    }
                    else if (const Nif::NiSkinPartition* parts = niSkin->getPartitions();
                             parts != nullptr && !parts->mVertexData.empty() && skinned(parts->mVertexDesc.mFlags))
                    {
                        packed = &parts->mVertexData;
                    }
                    readWeights(shape, shape.mBones.size(), niSkin, packed, result.mErrors);
                }
                else if (const auto* bsSkin = dynamic_cast<const Nif::BSSkinInstance*>(skinRecord))
                {
                    shape.mKind = "BSTriShape+BSSkin::Instance";
                    shape.mBones = boneNames(bsSkin->mBones);
                    const std::vector<Nif::BSVertexData>* packed = nullptr;
                    if (!packedShape->mVertData.empty() && skinned(packedShape->mVertDesc.mFlags))
                    {
                        packed = &packedShape->mVertData;
                    }
                    readWeights(shape, shape.mBones.size(), nullptr, packed, result.mErrors);
                }
                else
                {
                    result.mErrors.push_back("shape " + shown(shape.mName) + ": a skin record of type "
                        + skinRecord->mRecordName + " is not read");
                    continue;
                }
            }
            result.mShapes.push_back(std::move(shape));
        }
        return result;
    }
}
