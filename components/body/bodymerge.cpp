#include "bodymerge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace Body
{
    namespace
    {
        constexpr std::size_t maxLabelNameBytes = 64;
        /// A placement whose determinant is below this fraction of its largest entry cubed is singular.
        constexpr double singularRelative = 1.0e-12;

        // ------------------------------------------------------------------------------------------------------
        // Messages
        // ------------------------------------------------------------------------------------------------------

        /// A part's or bone's name made safe to put in a message: printable ASCII only, cut at maxLabelNameBytes.
        std::string safeName(const std::string& name)
        {
            std::string out;
            for (const char c : name)
            {
                if (out.size() >= maxLabelNameBytes)
                    break;
                out += (c >= 0x20 && c < 0x7f) ? c : '?';
            }
            return out;
        }

        std::string label(const PartMesh& part)
        {
            return "part '" + safeName(part.mName) + "' (slot " + std::to_string(part.mSlot) + ")";
        }

        /// Collects located messages up to a cap; at least one is always kept so that a failure is never silent.
        class ErrorSink
        {
        public:
            ErrorSink(std::vector<std::string>& errors, std::size_t cap)
                : mErrors(errors)
                , mCap(std::max<std::size_t>(cap, 1))
            {
            }
            void add(std::string message)
            {
                mFailed = true;
                if (mErrors.size() < mCap)
                    mErrors.push_back(std::move(message));
            }
            bool failed() const { return mFailed; }
            bool full() const { return mErrors.size() >= mCap; }

        private:
            std::vector<std::string>& mErrors;
            std::size_t mCap;
            bool mFailed = false;
        };

        // ------------------------------------------------------------------------------------------------------
        // Placement
        // ------------------------------------------------------------------------------------------------------

        using Mat3 = std::array<double, 9>;

        double det3(const Mat3& a)
        {
            return a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6])
                + a[2] * (a[3] * a[7] - a[4] * a[6]);
        }

        /// The cofactor matrix: det * inverse-transpose, so that it carries normals without dividing.
        Mat3 cofactor3(const Mat3& a)
        {
            return { a[4] * a[8] - a[5] * a[7], -(a[3] * a[8] - a[5] * a[6]), a[3] * a[7] - a[4] * a[6],
                -(a[1] * a[8] - a[2] * a[7]), a[0] * a[8] - a[2] * a[6], -(a[0] * a[7] - a[1] * a[6]),
                a[1] * a[5] - a[2] * a[4], -(a[0] * a[5] - a[2] * a[3]), a[0] * a[4] - a[1] * a[3] };
        }

        /// A part's mirror and placement as one transform: position' = E * position + T, normal' = N * normal.
        struct Effective
        {
            Mat3 mE{};
            std::array<double, 3> mT{};
            Mat3 mN{};
            bool mFlipWinding = false;
            bool mSingular = false;
        };

        Effective effectiveTransform(const PartMesh& part)
        {
            Effective e;
            const Mat34& m = part.mPlacement;
            const double mirror = part.mMirror ? -1.0 : 1.0;
            double largest = 0;
            for (std::size_t r = 0; r < 3; ++r)
            {
                e.mE[r * 3 + 0] = m[r * 4 + 0] * mirror;
                e.mE[r * 3 + 1] = m[r * 4 + 1];
                e.mE[r * 3 + 2] = m[r * 4 + 2];
                e.mT[r] = m[r * 4 + 3];
                for (std::size_t c = 0; c < 3; ++c)
                    largest = std::max(largest, std::abs(e.mE[r * 3 + c]));
            }
            const double det = det3(e.mE);
            e.mSingular = !(std::abs(det) > singularRelative * largest * largest * largest);
            const double sign = det < 0 ? -1.0 : 1.0;
            e.mN = cofactor3(e.mE);
            for (double& v : e.mN)
                v *= sign;
            e.mFlipWinding = det < 0;
            return e;
        }

        // ------------------------------------------------------------------------------------------------------
        // Validation
        // ------------------------------------------------------------------------------------------------------

        bool finite3(const Vec3& v)
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }

        void validatePart(const PartMesh& part, const MergeLimits& limits, ErrorSink& sink)
        {
            const std::string where = label(part);
            const std::size_t vertices = part.mPositions.size();
            if (part.mSlot < 0)
                sink.add(where + ": slot is negative");
            if (part.mName.size() > limits.mMaxNameBytes)
                sink.add(where + ": name is longer than " + std::to_string(limits.mMaxNameBytes) + " bytes");
            if (part.mMaterial.size() > limits.mMaxNameBytes)
                sink.add(where + ": material key is longer than " + std::to_string(limits.mMaxNameBytes) + " bytes");
            if (part.mRigidBone.size() > limits.mMaxNameBytes)
            {
                sink.add(where + ": rigid bone name is longer than " + std::to_string(limits.mMaxNameBytes) + " bytes");
            }
            if (vertices == 0)
            {
                sink.add(where + ": has no vertices");
                return;
            }
            if (vertices > limits.mMaxVerticesPerPart)
            {
                sink.add(where + ": too many vertices (" + std::to_string(vertices) + ", limit "
                    + std::to_string(limits.mMaxVerticesPerPart) + ")");
                return;
            }
            const std::size_t triangleIndices = part.mTriangles.size();
            if (triangleIndices == 0)
                sink.add(where + ": has no triangles");
            else if (triangleIndices % 3 != 0)
            {
                sink.add(where + ": triangle list size " + std::to_string(triangleIndices) + " is not a multiple of 3");
            }
            else if (triangleIndices / 3 > limits.mMaxTrianglesPerPart)
            {
                sink.add(where + ": too many triangles (" + std::to_string(triangleIndices / 3) + ", limit "
                    + std::to_string(limits.mMaxTrianglesPerPart) + ")");
                return;
            }
            if (!part.mNormals.empty() && part.mNormals.size() != vertices)
            {
                sink.add(where + ": normals are given for " + std::to_string(part.mNormals.size())
                    + " vertices, expected " + std::to_string(vertices));
            }
            if (!part.mUvs.empty() && part.mUvs.size() != vertices)
            {
                sink.add(where + ": uvs are given for " + std::to_string(part.mUvs.size()) + " vertices, expected "
                    + std::to_string(vertices));
            }

            bool placementFinite = true;
            for (const double v : part.mPlacement)
                placementFinite = placementFinite && std::isfinite(v);
            if (!placementFinite)
                sink.add(where + ": placement is not finite");
            else if (effectiveTransform(part).mSingular)
                sink.add(where + ": placement is singular");

            for (std::size_t v = 0; v < vertices && !sink.full(); ++v)
            {
                const Vec3& p = part.mPositions[v];
                if (!finite3(p))
                {
                    sink.add(where + ": vertex " + std::to_string(v) + ": position is not finite");
                    continue;
                }
                const double largest = std::max({ std::abs(p[0]), std::abs(p[1]), std::abs(p[2]) });
                if (largest > maxCoordinate)
                {
                    sink.add(where + ": vertex " + std::to_string(v) + ": coordinate " + std::to_string(largest)
                        + " exceeds the limit " + std::to_string(maxCoordinate));
                }
                if (part.mNormals.size() == vertices && !finite3(part.mNormals[v]))
                    sink.add(where + ": vertex " + std::to_string(v) + ": normal is not finite");
                if (part.mUvs.size() == vertices && !(std::isfinite(part.mUvs[v][0]) && std::isfinite(part.mUvs[v][1])))
                    sink.add(where + ": vertex " + std::to_string(v) + ": uv is not finite");
            }

            if (triangleIndices % 3 == 0)
            {
                for (std::size_t t = 0; t < triangleIndices / 3 && !sink.full(); ++t)
                {
                    const std::uint32_t a = part.mTriangles[t * 3];
                    const std::uint32_t b = part.mTriangles[t * 3 + 1];
                    const std::uint32_t c = part.mTriangles[t * 3 + 2];
                    bool inRange = true;
                    for (const std::uint32_t index : { a, b, c })
                    {
                        if (index >= vertices)
                        {
                            sink.add(where + ": triangle " + std::to_string(t) + ": index " + std::to_string(index)
                                + " out of range (" + std::to_string(vertices) + " vertices)");
                            inRange = false;
                            break;
                        }
                    }
                    if (inRange && (a == b || b == c || a == c))
                        sink.add(where + ": triangle " + std::to_string(t) + ": repeated vertex index");
                }
            }

            const bool skinned = !part.mSkin.empty() || !part.mBones.empty();
            if (skinned && !part.mRigidBone.empty())
            {
                sink.add(where + ": has both a skin and a rigid bone");
                return;
            }
            if (!skinned)
            {
                if (part.mRigidBone.empty())
                    sink.add(where + ": needs either a skin or a rigid bone");
                return;
            }
            if (part.mBones.size() > limits.mMaxBonesPerPart)
            {
                sink.add(where + ": too many bones (" + std::to_string(part.mBones.size()) + ", limit "
                    + std::to_string(limits.mMaxBonesPerPart) + ")");
                return;
            }
            for (const std::string& bone : part.mBones)
            {
                if (bone.empty() || bone.size() > limits.mMaxNameBytes)
                {
                    sink.add(where + ": a bone name is empty or longer than " + std::to_string(limits.mMaxNameBytes)
                        + " bytes");
                    break;
                }
            }
            if (part.mSkin.size() != vertices)
            {
                sink.add(where + ": skin has " + std::to_string(part.mSkin.size()) + " entries, expected "
                    + std::to_string(vertices) + " vertices");
                return;
            }
            for (std::size_t v = 0; v < vertices && !sink.full(); ++v)
            {
                const PartVertexSkin& skin = part.mSkin[v];
                if (skin.mCount > maxInputInfluences)
                {
                    sink.add(where + ": vertex " + std::to_string(v) + ": more than "
                        + std::to_string(maxInputInfluences) + " influences");
                    continue;
                }
                double positive = 0;
                bool bad = false;
                for (std::size_t i = 0; i < skin.mCount; ++i)
                {
                    const PartInfluence& influence = skin.mInfluences[i];
                    if (!std::isfinite(influence.mWeight) || influence.mWeight < 0.f)
                    {
                        sink.add(where + ": vertex " + std::to_string(v) + ": influence " + std::to_string(i)
                            + ": weight is negative or not finite");
                        bad = true;
                        break;
                    }
                    if (influence.mBone >= part.mBones.size())
                    {
                        sink.add(where + ": vertex " + std::to_string(v) + ": influence " + std::to_string(i)
                            + ": bone index " + std::to_string(influence.mBone) + " out of range ("
                            + std::to_string(part.mBones.size()) + " bones)");
                        bad = true;
                        break;
                    }
                    positive += influence.mWeight;
                }
                if (!bad && !(positive > 0))
                    sink.add(where + ": vertex " + std::to_string(v) + ": no influence with a positive weight");
            }
        }

        bool validateInput(const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams,
            const MergeLimits& limits, ErrorSink& sink)
        {
            if (parts.empty())
            {
                sink.add("no parts");
                return false;
            }
            if (parts.size() > limits.mMaxParts)
            {
                sink.add("too many parts (" + std::to_string(parts.size()) + ", limit "
                    + std::to_string(limits.mMaxParts) + ")");
                return false;
            }
            if (seams.size() > limits.mMaxSeams)
            {
                sink.add("too many seams (" + std::to_string(seams.size()) + ", limit "
                    + std::to_string(limits.mMaxSeams) + ")");
                return false;
            }
            std::size_t totalVertices = 0;
            std::size_t totalTriangles = 0;
            for (const PartMesh& part : parts)
            {
                totalVertices += part.mPositions.size();
                totalTriangles += part.mTriangles.size() / 3;
            }
            if (totalVertices > limits.mMaxTotalVertices)
            {
                sink.add("too many vertices in all parts (" + std::to_string(totalVertices) + ", limit "
                    + std::to_string(limits.mMaxTotalVertices) + ")");
                return false;
            }
            if (totalTriangles > limits.mMaxTotalTriangles)
            {
                sink.add("too many triangles in all parts (" + std::to_string(totalTriangles) + ", limit "
                    + std::to_string(limits.mMaxTotalTriangles) + ")");
                return false;
            }
            for (const PartMesh& part : parts)
                validatePart(part, limits, sink);

            bool anyNormals = false;
            bool anyUvs = false;
            for (const PartMesh& part : parts)
            {
                anyNormals = anyNormals || !part.mNormals.empty();
                anyUvs = anyUvs || !part.mUvs.empty();
            }
            for (const PartMesh& part : parts)
            {
                if (anyNormals && part.mNormals.empty())
                    sink.add("part '" + safeName(part.mName) + "' has no normals while other parts do");
                if (anyUvs && part.mUvs.empty())
                    sink.add("part '" + safeName(part.mName) + "' has no uvs while other parts do");
            }

            std::set<int> slots;
            for (const PartMesh& part : parts)
                slots.insert(part.mSlot);
            for (std::size_t k = 0; k < seams.size(); ++k)
            {
                const SeamDeclaration& seam = seams[k];
                const std::string where = "seam " + std::to_string(k);
                if (!std::isfinite(seam.mTolerance) || seam.mTolerance < 0 || seam.mTolerance > maxSeamTolerance)
                {
                    sink.add(
                        where + ": tolerance is negative, not finite or above " + std::to_string(maxSeamTolerance));
                }
                for (const int slot : { seam.mSlotA, seam.mSlotB })
                {
                    if (!slots.contains(slot))
                        sink.add(where + ": slot " + std::to_string(slot) + " has no part");
                }
            }
            return !sink.failed();
        }

        // ------------------------------------------------------------------------------------------------------
        // The canonical order of parts
        // ------------------------------------------------------------------------------------------------------

        template <typename T>
        int compareValue(const T& a, const T& b)
        {
            return a < b ? -1 : (b < a ? 1 : 0);
        }

        template <typename Array>
        int compareArrays(const Array& a, const Array& b)
        {
            for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            {
                if (const int c = compareValue(a[i], b[i]))
                    return c;
            }
            return compareValue(a.size(), b.size());
        }

        /// A total order on parts (slot, material, name, sizes, then content), so the result is independent of the
        /// order the caller passes them in. Parts equal in every field are interchangeable.
        int compareParts(const PartMesh& a, const PartMesh& b)
        {
            if (const int c = compareValue(a.mSlot, b.mSlot))
                return c;
            if (const int c = compareValue(a.mMaterial, b.mMaterial))
                return c;
            if (const int c = compareValue(a.mName, b.mName))
                return c;
            if (const int c = compareValue(a.mPositions.size(), b.mPositions.size()))
                return c;
            if (const int c = compareValue(a.mTriangles.size(), b.mTriangles.size()))
                return c;
            if (const int c = compareArrays(a.mPositions, b.mPositions))
                return c;
            if (const int c = compareArrays(a.mTriangles, b.mTriangles))
                return c;
            if (const int c = compareArrays(a.mNormals, b.mNormals))
                return c;
            if (const int c = compareArrays(a.mUvs, b.mUvs))
                return c;
            if (const int c = compareValue(a.mRigidBone, b.mRigidBone))
                return c;
            if (const int c = compareArrays(a.mBones, b.mBones))
                return c;
            if (const int c = compareValue(a.mMirror, b.mMirror))
                return c;
            return compareArrays(a.mPlacement, b.mPlacement);
        }

        // ------------------------------------------------------------------------------------------------------
        // Placed vertices, weld sets
        // ------------------------------------------------------------------------------------------------------

        class UnionFind
        {
        public:
            explicit UnionFind(std::size_t size)
                : mParent(size)
            {
                std::iota(mParent.begin(), mParent.end(), 0u);
            }
            std::uint32_t find(std::uint32_t x)
            {
                while (mParent[x] != x)
                {
                    mParent[x] = mParent[mParent[x]];
                    x = mParent[x];
                }
                return x;
            }
            /// The smaller id becomes the root, so the root of a set is its lowest member.
            void join(std::uint32_t a, std::uint32_t b)
            {
                a = find(a);
                b = find(b);
                if (a < b)
                    mParent[b] = a;
                else if (b < a)
                    mParent[a] = b;
            }

        private:
            std::vector<std::uint32_t> mParent;
        };

        struct GridEntry
        {
            std::int64_t mX = 0;
            std::int64_t mY = 0;
            std::int64_t mZ = 0;
            std::uint32_t mId = 0;
        };

        bool entryLess(const GridEntry& a, const GridEntry& b)
        {
            if (a.mX != b.mX)
                return a.mX < b.mX;
            if (a.mY != b.mY)
                return a.mY < b.mY;
            if (a.mZ != b.mZ)
                return a.mZ < b.mZ;
            return a.mId < b.mId;
        }

        std::int64_t cellOf(double value, double cell)
        {
            return static_cast<std::int64_t>(std::floor(value / cell));
        }

        /// Everything the weld and the merge share: the canonical order, the placed vertices (concatenated in
        /// canonical order: a vertex's id is its global index), and the union-find over the declared seams.
        struct Prepared
        {
            std::vector<std::size_t> mOrder; // rank -> input index
            std::vector<std::uint32_t> mOffset; // rank -> first global id; one extra entry: the total
            std::vector<Vec3> mPositions;
            std::vector<Vec3> mNormals; // empty when no part has normals
            std::vector<Vec2> mUvs;
            std::vector<bool> mFlip; // rank -> winding flipped
            std::vector<std::uint32_t> mRankOf; // global id -> rank
            std::vector<std::size_t> mPairsPerSeam;
            std::vector<std::uint32_t> mRoot; // global id -> lowest member of its set
            std::vector<std::vector<std::uint32_t>> mSets; // sets of two or more, members ascending, by first member
        };

        bool prepare(const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams,
            const MergeLimits& limits, ErrorSink& sink, Prepared& out)
        {
            if (!validateInput(parts, seams, limits, sink))
                return false;

            out.mOrder.resize(parts.size());
            std::iota(out.mOrder.begin(), out.mOrder.end(), std::size_t{ 0 });
            std::stable_sort(out.mOrder.begin(), out.mOrder.end(),
                [&parts](std::size_t a, std::size_t b) { return compareParts(parts[a], parts[b]) < 0; });

            const bool hasNormals = !parts.front().mNormals.empty();
            const bool hasUvs = !parts.front().mUvs.empty();
            out.mOffset.reserve(parts.size() + 1);
            out.mFlip.reserve(parts.size());
            for (std::size_t rank = 0; rank < out.mOrder.size(); ++rank)
            {
                const PartMesh& part = parts[out.mOrder[rank]];
                out.mOffset.push_back(static_cast<std::uint32_t>(out.mPositions.size()));
                const Effective e = effectiveTransform(part);
                out.mFlip.push_back(e.mFlipWinding);
                for (std::size_t v = 0; v < part.mPositions.size(); ++v)
                {
                    const Vec3& p = part.mPositions[v];
                    Vec3 placed;
                    for (std::size_t r = 0; r < 3; ++r)
                    {
                        placed[r] = static_cast<float>(
                            e.mE[r * 3 + 0] * p[0] + e.mE[r * 3 + 1] * p[1] + e.mE[r * 3 + 2] * p[2] + e.mT[r]);
                    }
                    out.mPositions.push_back(placed);
                    out.mRankOf.push_back(static_cast<std::uint32_t>(rank));
                    if (hasNormals)
                    {
                        const Vec3& n = part.mNormals[v];
                        std::array<double, 3> t{};
                        for (std::size_t r = 0; r < 3; ++r)
                            t[r] = e.mN[r * 3 + 0] * n[0] + e.mN[r * 3 + 1] * n[1] + e.mN[r * 3 + 2] * n[2];
                        const double length = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
                        const double scale = length > 0 ? 1.0 / length : 0.0;
                        out.mNormals.push_back({ static_cast<float>(t[0] * scale), static_cast<float>(t[1] * scale),
                            static_cast<float>(t[2] * scale) });
                    }
                    if (hasUvs)
                        out.mUvs.push_back(part.mUvs[v]);
                }
            }
            out.mOffset.push_back(static_cast<std::uint32_t>(out.mPositions.size()));

            UnionFind sets(out.mPositions.size());
            out.mPairsPerSeam.assign(seams.size(), 0);
            for (std::size_t k = 0; k < seams.size(); ++k)
            {
                const SeamDeclaration& seam = seams[k];
                const double cell = std::max(seam.mTolerance, minGridCell);
                const double limit2 = seam.mTolerance * seam.mTolerance;
                std::vector<std::uint32_t> ranksA;
                std::vector<std::uint32_t> ranksB;
                for (std::size_t rank = 0; rank < out.mOrder.size(); ++rank)
                {
                    const int slot = parts[out.mOrder[rank]].mSlot;
                    if (slot == seam.mSlotA)
                        ranksA.push_back(static_cast<std::uint32_t>(rank));
                    if (slot == seam.mSlotB && seam.mSlotB != seam.mSlotA)
                        ranksB.push_back(static_cast<std::uint32_t>(rank));
                }
                const bool sameSlot = seam.mSlotA == seam.mSlotB;
                const std::vector<std::uint32_t>& gridRanks = sameSlot ? ranksA : ranksB;

                std::vector<GridEntry> grid;
                for (const std::uint32_t rank : gridRanks)
                {
                    for (std::uint32_t id = out.mOffset[rank]; id < out.mOffset[rank + 1]; ++id)
                    {
                        const Vec3& p = out.mPositions[id];
                        grid.push_back({ cellOf(p[0], cell), cellOf(p[1], cell), cellOf(p[2], cell), id });
                    }
                }
                std::sort(grid.begin(), grid.end(), entryLess);

                std::size_t pairs = 0;
                for (const std::uint32_t rankA : ranksA)
                {
                    for (std::uint32_t idA = out.mOffset[rankA]; idA < out.mOffset[rankA + 1]; ++idA)
                    {
                        const Vec3& p = out.mPositions[idA];
                        const std::int64_t cx = cellOf(p[0], cell);
                        const std::int64_t cy = cellOf(p[1], cell);
                        const std::int64_t cz = cellOf(p[2], cell);
                        for (std::int64_t dx = -1; dx <= 1; ++dx)
                        {
                            for (std::int64_t dy = -1; dy <= 1; ++dy)
                            {
                                for (std::int64_t dz = -1; dz <= 1; ++dz)
                                {
                                    const GridEntry low{ cx + dx, cy + dy, cz + dz, 0 };
                                    auto it = std::lower_bound(grid.begin(), grid.end(), low, entryLess);
                                    for (; it != grid.end() && it->mX == low.mX && it->mY == low.mY && it->mZ == low.mZ;
                                         ++it)
                                    {
                                        const std::uint32_t idB = it->mId;
                                        if (out.mRankOf[idB] == rankA)
                                            continue; // two vertices of one part are never welded to each other
                                        if (sameSlot && idB <= idA)
                                            continue; // count each pair once
                                        const Vec3& q = out.mPositions[idB];
                                        const double ex = static_cast<double>(q[0]) - static_cast<double>(p[0]);
                                        const double ey = static_cast<double>(q[1]) - static_cast<double>(p[1]);
                                        const double ez = static_cast<double>(q[2]) - static_cast<double>(p[2]);
                                        if (ex * ex + ey * ey + ez * ez <= limit2)
                                        {
                                            sets.join(idA, idB);
                                            ++pairs;
                                        }
                                    }
                                }
                            }
                        }
                        if (pairs > limits.mMaxPairsPerSeam)
                        {
                            sink.add("seam " + std::to_string(k) + ": too many vertex pairs (limit "
                                + std::to_string(limits.mMaxPairsPerSeam) + ")");
                            return false;
                        }
                    }
                }
                out.mPairsPerSeam[k] = pairs;
            }

            const std::size_t total = out.mPositions.size();
            out.mRoot.resize(total);
            std::vector<std::int32_t> setIndexOfRoot(total, -1);
            for (std::uint32_t id = 0; id < total; ++id)
            {
                const std::uint32_t root = sets.find(id);
                out.mRoot[id] = root;
                if (root == id)
                    continue;
                if (setIndexOfRoot[root] < 0)
                {
                    setIndexOfRoot[root] = static_cast<std::int32_t>(out.mSets.size());
                    out.mSets.push_back({ root });
                }
            }
            // Roots in ascending order give sets in order of their first member; members are appended ascending.
            std::vector<std::vector<std::uint32_t>> ordered(out.mSets.size());
            std::vector<std::pair<std::uint32_t, std::size_t>> byRoot;
            for (std::size_t s = 0; s < out.mSets.size(); ++s)
                byRoot.emplace_back(out.mSets[s].front(), s);
            std::sort(byRoot.begin(), byRoot.end());
            std::vector<std::int32_t> position(out.mSets.size(), 0);
            for (std::size_t i = 0; i < byRoot.size(); ++i)
                position[byRoot[i].second] = static_cast<std::int32_t>(i);
            for (std::uint32_t id = 0; id < total; ++id)
            {
                const std::uint32_t root = out.mRoot[id];
                if (root == id)
                    continue;
                ordered[static_cast<std::size_t>(position[static_cast<std::size_t>(setIndexOfRoot[root])])].push_back(
                    id);
            }
            for (std::size_t i = 0; i < byRoot.size(); ++i)
            {
                std::vector<std::uint32_t>& members = ordered[i];
                members.insert(members.begin(), byRoot[i].first);
            }
            out.mSets = std::move(ordered);
            return true;
        }

        // ------------------------------------------------------------------------------------------------------
        // Influences
        // ------------------------------------------------------------------------------------------------------

        using Influences = std::vector<std::pair<std::uint32_t, double>>; // canonical bone index, weight

        struct FinalInfluence
        {
            std::array<std::uint32_t, maxMergedInfluences> mBone{};
            std::array<float, maxMergedInfluences> mWeight{};
        };

        /// Adds duplicates, drops zeros and normalises to sum 1. Sorted by bone index.
        Influences normalised(Influences raw)
        {
            std::sort(raw.begin(), raw.end());
            Influences merged;
            for (const auto& [bone, weight] : raw)
            {
                if (!merged.empty() && merged.back().first == bone)
                    merged.back().second += weight;
                else
                    merged.emplace_back(bone, weight);
            }
            double sum = 0;
            for (const auto& entry : merged)
                sum += entry.second;
            Influences result;
            for (const auto& [bone, weight] : merged)
            {
                if (weight > 0 && sum > 0)
                    result.emplace_back(bone, weight / sum);
            }
            return result;
        }

        /// The four largest (ties by lower bone index), renormalised to sum 1, largest first.
        FinalInfluence cutToFour(const Influences& weights)
        {
            Influences sorted = weights;
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                if (a.second != b.second)
                    return a.second > b.second;
                return a.first < b.first;
            });
            if (sorted.size() > maxMergedInfluences)
                sorted.resize(maxMergedInfluences);
            double sum = 0;
            for (const auto& entry : sorted)
                sum += entry.second;
            FinalInfluence result;
            for (std::size_t i = 0; i < sorted.size(); ++i)
            {
                result.mBone[i] = sorted[i].first;
                result.mWeight[i] = static_cast<float>(sum > 0 ? sorted[i].second / sum : 0.0);
            }
            return result;
        }

        double angleBetween(const Vec3& a, const Vec3& b)
        {
            const double la = std::sqrt(
                static_cast<double>(a[0]) * a[0] + static_cast<double>(a[1]) * a[1] + static_cast<double>(a[2]) * a[2]);
            const double lb = std::sqrt(
                static_cast<double>(b[0]) * b[0] + static_cast<double>(b[1]) * b[1] + static_cast<double>(b[2]) * b[2]);
            if (la == 0 || lb == 0)
                return (la == 0 && lb == 0) ? 0.0 : std::numeric_limits<double>::infinity();
            const double dot = static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1]
                + static_cast<double>(a[2]) * b[2];
            return std::acos(std::clamp(dot / (la * lb), -1.0, 1.0));
        }
    }

    // ----------------------------------------------------------------------------------------------------------
    // Public functions
    // ----------------------------------------------------------------------------------------------------------

    WeldMap computeWeldMap(
        const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams, const MergeLimits& limits)
    {
        WeldMap map;
        ErrorSink sink(map.mErrors, limits.mMaxErrors);
        Prepared prepared;
        if (!prepare(parts, seams, limits, sink, prepared))
            return map;
        map.mPairsPerSeam = prepared.mPairsPerSeam;
        for (const std::vector<std::uint32_t>& set : prepared.mSets)
        {
            std::vector<WeldMember> members;
            members.reserve(set.size());
            for (const std::uint32_t id : set)
            {
                const std::uint32_t rank = prepared.mRankOf[id];
                members.push_back({ static_cast<std::uint32_t>(prepared.mOrder[rank]), id - prepared.mOffset[rank] });
            }
            map.mSets.push_back(std::move(members));
            map.mPositions.push_back(prepared.mPositions[set.front()]);
        }
        return map;
    }

    MergeResult mergeParts(const std::vector<PartMesh>& parts, const std::vector<SeamDeclaration>& seams,
        const MergeOptions& options, const MergeLimits& limits)
    {
        MergeResult result;
        ErrorSink sink(result.mErrors, limits.mMaxErrors);
        Prepared prepared;
        if (!prepare(parts, seams, limits, sink, prepared))
            return result;

        const std::size_t total = prepared.mPositions.size();
        const bool hasNormals = !prepared.mNormals.empty();
        const bool hasUvs = !prepared.mUvs.empty();
        MergedBody& body = result.mBody;

        // --- Canonical bone indices: the known order first, then the other used names by byte value. ---
        std::set<std::string> used;
        for (const PartMesh& part : parts)
        {
            if (!part.mRigidBone.empty())
            {
                used.insert(part.mRigidBone);
                continue;
            }
            for (const PartVertexSkin& skin : part.mSkin)
            {
                for (std::size_t i = 0; i < skin.mCount; ++i)
                {
                    if (skin.mInfluences[i].mWeight > 0.f)
                        used.insert(part.mBones[skin.mInfluences[i].mBone]);
                }
            }
        }
        if (used.size() > std::numeric_limits<std::uint16_t>::max())
        {
            result.mErrors.push_back("too many distinct bones (" + std::to_string(used.size()) + ")");
            return result;
        }
        std::map<std::string, std::uint32_t> canonical;
        std::vector<std::string> canonicalNames;
        for (const std::string& name : options.mBoneOrder)
        {
            if (used.contains(name) && !canonical.contains(name))
            {
                canonical[name] = static_cast<std::uint32_t>(canonicalNames.size());
                canonicalNames.push_back(name);
            }
        }
        for (const std::string& name : used)
        {
            if (!canonical.contains(name))
            {
                canonical[name] = static_cast<std::uint32_t>(canonicalNames.size());
                canonicalNames.push_back(name);
            }
        }

        // --- The normalised influences of every input vertex, by canonical bone index. ---
        const auto vertexInfluences = [&](std::uint32_t id) {
            const std::uint32_t rank = prepared.mRankOf[id];
            const PartMesh& part = parts[prepared.mOrder[rank]];
            const std::uint32_t local = id - prepared.mOffset[rank];
            Influences raw;
            if (!part.mRigidBone.empty())
                raw.emplace_back(canonical.at(part.mRigidBone), 1.0);
            else
            {
                const PartVertexSkin& skin = part.mSkin[local];
                for (std::size_t i = 0; i < skin.mCount; ++i)
                {
                    if (skin.mInfluences[i].mWeight > 0.f)
                    {
                        raw.emplace_back(canonical.at(part.mBones[skin.mInfluences[i].mBone]),
                            static_cast<double>(skin.mInfluences[i].mWeight));
                    }
                }
            }
            return normalised(std::move(raw));
        };

        // --- Weld sets: the weights are the equal-weight mean of the members'. ---
        std::vector<std::int32_t> setOf(total, -1);
        for (std::size_t s = 0; s < prepared.mSets.size(); ++s)
        {
            for (const std::uint32_t id : prepared.mSets[s])
                setOf[id] = static_cast<std::int32_t>(s);
        }
        std::vector<FinalInfluence> setInfluence(prepared.mSets.size());
        double maxDisagreement = 0;
        for (std::size_t s = 0; s < prepared.mSets.size(); ++s)
        {
            const std::vector<std::uint32_t>& members = prepared.mSets[s];
            std::vector<Influences> memberWeights;
            for (const std::uint32_t id : members)
                memberWeights.push_back(vertexInfluences(id));
            std::set<std::uint32_t> bones;
            for (const Influences& weights : memberWeights)
            {
                for (const auto& entry : weights)
                    bones.insert(entry.first);
            }
            Influences mean;
            for (const std::uint32_t bone : bones)
            {
                double weightSum = 0;
                double lowest = std::numeric_limits<double>::infinity();
                double highest = 0;
                for (const Influences& weights : memberWeights)
                {
                    double w = 0;
                    for (const auto& entry : weights)
                    {
                        if (entry.first == bone)
                            w = entry.second;
                    }
                    weightSum += w;
                    lowest = std::min(lowest, w);
                    highest = std::max(highest, w);
                }
                maxDisagreement = std::max(maxDisagreement, highest - lowest);
                mean.emplace_back(bone, weightSum / static_cast<double>(members.size()));
            }
            setInfluence[s] = cutToFour(mean);
        }

        // --- Clusters inside each set: equal UV and a close normal merge into one output vertex. ---
        std::vector<std::uint32_t> clusterRep(total);
        std::iota(clusterRep.begin(), clusterRep.end(), 0u);
        std::vector<Vec3> clusterNormal;
        if (hasNormals)
            clusterNormal = prepared.mNormals;
        double maxMergedAngle = 0;
        for (const std::vector<std::uint32_t>& members : prepared.mSets)
        {
            std::vector<std::uint32_t> reps;
            std::map<std::uint32_t, std::array<double, 3>> normalSum;
            for (const std::uint32_t id : members)
            {
                bool placed = false;
                for (const std::uint32_t rep : reps)
                {
                    bool compatible = true;
                    if (hasUvs)
                    {
                        compatible = std::abs(static_cast<double>(prepared.mUvs[id][0]) - prepared.mUvs[rep][0])
                                <= options.mUvTolerance
                            && std::abs(static_cast<double>(prepared.mUvs[id][1]) - prepared.mUvs[rep][1])
                                <= options.mUvTolerance;
                    }
                    double angle = 0;
                    if (compatible && hasNormals)
                    {
                        angle = angleBetween(prepared.mNormals[id], prepared.mNormals[rep]);
                        compatible = angle <= options.mNormalAngle;
                    }
                    if (compatible)
                    {
                        clusterRep[id] = rep;
                        maxMergedAngle = std::max(maxMergedAngle, angle);
                        if (hasNormals)
                        {
                            auto& sum = normalSum[rep];
                            for (std::size_t r = 0; r < 3; ++r)
                                sum[r] += prepared.mNormals[id][r];
                        }
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                {
                    reps.push_back(id);
                    if (hasNormals)
                    {
                        auto& sum = normalSum[id];
                        for (std::size_t r = 0; r < 3; ++r)
                            sum[r] = prepared.mNormals[id][r];
                    }
                }
            }
            if (hasNormals)
            {
                for (const std::uint32_t rep : reps)
                {
                    const auto& sum = normalSum[rep];
                    const double length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                    if (length > 0)
                    {
                        clusterNormal[rep] = { static_cast<float>(sum[0] / length), static_cast<float>(sum[1] / length),
                            static_cast<float>(sum[2] / length) };
                    }
                }
            }
        }

        // --- Output vertices, in order of the lowest member of each cluster. ---
        std::vector<std::uint32_t> outIndex(total, 0);
        std::vector<FinalInfluence> vertexInfluence;
        for (std::uint32_t id = 0; id < total; ++id)
        {
            if (clusterRep[id] != id)
            {
                outIndex[id] = outIndex[clusterRep[id]];
                continue;
            }
            outIndex[id] = static_cast<std::uint32_t>(body.mPositions.size());
            const std::int32_t s = setOf[id];
            body.mPositions.push_back(s >= 0 ? prepared.mPositions[prepared.mSets[static_cast<std::size_t>(s)].front()]
                                             : prepared.mPositions[id]);
            if (hasNormals)
                body.mNormals.push_back(clusterNormal[id]);
            if (hasUvs)
                body.mUvs.push_back(prepared.mUvs[id]);
            vertexInfluence.push_back(
                s >= 0 ? setInfluence[static_cast<std::size_t>(s)] : cutToFour(vertexInfluences(id)));
        }
        // Members of a set that are not first in their cluster were mapped above; a set's first member is a root.
        // The bone list: only bones some vertex still uses, in canonical order.
        std::set<std::uint32_t> usedCanonical;
        for (const FinalInfluence& influence : vertexInfluence)
        {
            for (std::size_t i = 0; i < maxMergedInfluences; ++i)
            {
                if (influence.mWeight[i] > 0.f)
                    usedCanonical.insert(influence.mBone[i]);
            }
        }
        std::map<std::uint32_t, std::uint16_t> finalIndex;
        for (const std::uint32_t bone : usedCanonical)
        {
            finalIndex[bone] = static_cast<std::uint16_t>(body.mBones.size());
            body.mBones.push_back(canonicalNames[bone]);
        }
        for (const FinalInfluence& influence : vertexInfluence)
        {
            std::array<std::uint16_t, maxMergedInfluences> indices{};
            for (std::size_t i = 0; i < maxMergedInfluences; ++i)
                indices[i] = influence.mWeight[i] > 0.f ? finalIndex.at(influence.mBone[i]) : std::uint16_t{ 0 };
            body.mBoneIndices.push_back(indices);
            body.mBoneWeights.push_back(influence.mWeight);
        }

        // --- Materials, triangles, regions, submeshes. ---
        std::set<std::string> materialSet;
        for (const PartMesh& part : parts)
            materialSet.insert(part.mMaterial);
        body.mMaterials.assign(materialSet.begin(), materialSet.end());
        const auto materialIndex = [&body](const std::string& key) {
            return static_cast<std::uint32_t>(
                std::lower_bound(body.mMaterials.begin(), body.mMaterials.end(), key) - body.mMaterials.begin());
        };
        std::size_t inputTriangles = 0;
        std::size_t dropped = 0;
        for (std::size_t rank = 0; rank < prepared.mOrder.size(); ++rank)
        {
            const PartMesh& part = parts[prepared.mOrder[rank]];
            const std::uint32_t firstTriangle = static_cast<std::uint32_t>(body.mTriangles.size() / 3);
            for (std::size_t t = 0; t < part.mTriangles.size() / 3; ++t)
            {
                ++inputTriangles;
                std::uint32_t a = outIndex[prepared.mOffset[rank] + part.mTriangles[t * 3]];
                std::uint32_t b = outIndex[prepared.mOffset[rank] + part.mTriangles[t * 3 + 1]];
                std::uint32_t c = outIndex[prepared.mOffset[rank] + part.mTriangles[t * 3 + 2]];
                if (a == b || b == c || a == c)
                {
                    ++dropped;
                    continue;
                }
                if (prepared.mFlip[rank])
                    std::swap(b, c);
                body.mTriangles.insert(body.mTriangles.end(), { a, b, c });
            }
            const std::uint32_t count = static_cast<std::uint32_t>(body.mTriangles.size() / 3) - firstTriangle;
            if (count == 0)
                continue;
            if (!body.mRegions.empty() && body.mRegions.back().mSlot == part.mSlot)
                body.mRegions.back().mTriangleCount += count;
            else
                body.mRegions.push_back({ part.mSlot, firstTriangle, count });
            const std::uint32_t material = materialIndex(part.mMaterial);
            if (!body.mSubmeshes.empty() && body.mSubmeshes.back().mSlot == part.mSlot
                && body.mSubmeshes.back().mMaterial == material)
            {
                body.mSubmeshes.back().mTriangleCount += count;
            }
            else
                body.mSubmeshes.push_back({ material, part.mSlot, firstTriangle, count });
        }

        // --- Weld groups: sets that kept two or more output vertices. ---
        for (const std::vector<std::uint32_t>& members : prepared.mSets)
        {
            std::set<std::uint32_t> distinct;
            for (const std::uint32_t id : members)
                distinct.insert(outIndex[id]);
            if (distinct.size() >= 2)
                body.mWeldGroups.emplace_back(distinct.begin(), distinct.end());
        }

        // --- Maps back to the caller's numbering, and the report. ---
        body.mVertexMap.resize(parts.size());
        for (std::size_t rank = 0; rank < prepared.mOrder.size(); ++rank)
        {
            std::vector<std::uint32_t>& map = body.mVertexMap[prepared.mOrder[rank]];
            for (std::uint32_t id = prepared.mOffset[rank]; id < prepared.mOffset[rank + 1]; ++id)
                map.push_back(outIndex[id]);
        }
        body.mPartOrder = prepared.mOrder;
        MergeReport& report = body.mReport;
        report.mParts = parts.size();
        report.mInputVertices = total;
        report.mOutputVertices = body.mPositions.size();
        report.mInputTriangles = inputTriangles;
        report.mOutputTriangles = body.mTriangles.size() / 3;
        report.mCollapsedVertices = total - body.mPositions.size();
        report.mWeldSets = prepared.mSets.size();
        report.mWeldGroups = body.mWeldGroups.size();
        report.mDroppedTriangles = dropped;
        report.mMaxWeightDisagreement = maxDisagreement;
        report.mMaxMergedNormalAngle = maxMergedAngle;
        return result;
    }
}
