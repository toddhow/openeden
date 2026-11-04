#include "bodymesh.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <type_traits>

namespace Body
{
    namespace
    {
        static_assert(std::endian::native == std::endian::little, "the bodymesh payload is little-endian");
        static_assert(sizeof(float) == 4, "the bodymesh payload stores binary32 floats");

        constexpr std::uint32_t flagNormals = 1;
        constexpr std::uint32_t flagUvs = 2;
        constexpr std::size_t headerBytes = 8 + 4 + 4 + 8 * 4;

        std::string num(std::uint64_t value)
        {
            return std::to_string(value);
        }

        bool finite(const std::array<float, 3>& v)
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }

        bool validName(const std::string& name)
        {
            if (name.empty() || name.size() > BodyMeshLimits::sMaxNameBytes)
            {
                return false;
            }
            return std::none_of(name.begin(), name.end(), [](char c) {
                const unsigned char u = static_cast<unsigned char>(c);
                return u < 0x20 || u == 0x7F;
            });
        }

        // The one rule set, used by the writer (before writing) and by the reader (after reading). Returns an empty
        // string when the body is valid.
        std::string check(const MergedBody& body)
        {
            namespace L = BodyMeshLimits;
            const std::size_t vertices = body.mPositions.size();
            const std::size_t triangleIndices = body.mTriangles.size();
            if (vertices == 0 || vertices > L::sMaxVertices)
            {
                return "vertices: " + num(vertices) + " is not from 1 to " + num(L::sMaxVertices);
            }
            if (triangleIndices == 0 || triangleIndices % 3 != 0 || triangleIndices / 3 > L::sMaxTriangles)
            {
                return "triangles: " + num(triangleIndices) + " indices is not a whole number of triangles from 1 to "
                    + num(L::sMaxTriangles);
            }
            const std::uint64_t triangles = triangleIndices / 3;
            if (!body.mNormals.empty() && body.mNormals.size() != vertices)
            {
                return "normals: " + num(body.mNormals.size()) + " normals for " + num(vertices) + " vertices";
            }
            if (!body.mUvs.empty() && body.mUvs.size() != vertices)
            {
                return "uvs: " + num(body.mUvs.size()) + " UVs for " + num(vertices) + " vertices";
            }
            if (body.mBoneIndices.size() != vertices || body.mBoneWeights.size() != vertices)
            {
                return "skin: " + num(body.mBoneIndices.size()) + " index sets and " + num(body.mBoneWeights.size())
                    + " weight sets for " + num(vertices) + " vertices";
            }
            if (body.mBones.empty() || body.mBones.size() > L::sMaxBones)
            {
                return "bones: " + num(body.mBones.size()) + " is not from 1 to " + num(L::sMaxBones);
            }
            if (body.mMaterials.empty() || body.mMaterials.size() > L::sMaxMaterials)
            {
                return "materials: " + num(body.mMaterials.size()) + " is not from 1 to " + num(L::sMaxMaterials);
            }
            if (body.mRegions.empty() || body.mRegions.size() > L::sMaxRegions)
            {
                return "regions: " + num(body.mRegions.size()) + " is not from 1 to " + num(L::sMaxRegions);
            }
            if (body.mSubmeshes.empty() || body.mSubmeshes.size() > L::sMaxSubmeshes)
            {
                return "submeshes: " + num(body.mSubmeshes.size()) + " is not from 1 to " + num(L::sMaxSubmeshes);
            }
            if (body.mWeldGroups.size() > L::sMaxWeldGroups)
            {
                return "weld groups: " + num(body.mWeldGroups.size()) + " is over " + num(L::sMaxWeldGroups);
            }

            std::set<std::string> names;
            for (std::size_t i = 0; i < body.mBones.size(); ++i)
            {
                if (!validName(body.mBones[i]))
                {
                    return "bones: bone " + num(i) + " has an empty, over-long or control-character name";
                }
                if (!names.insert(body.mBones[i]).second)
                {
                    return "bones: bone " + num(i) + " repeats an earlier name";
                }
            }
            for (std::size_t i = 0; i < body.mMaterials.size(); ++i)
            {
                if (!validName(body.mMaterials[i]))
                {
                    return "materials: material " + num(i) + " has an empty, over-long or control-character name";
                }
                if (i > 0 && !(body.mMaterials[i - 1] < body.mMaterials[i]))
                {
                    return "materials: material " + num(i) + " is not after the one before it by byte value";
                }
            }

            for (std::size_t i = 0; i < vertices; ++i)
            {
                if (!finite(body.mPositions[i]))
                {
                    return "positions: vertex " + num(i) + ": a coordinate is not a finite number";
                }
                if (!body.mNormals.empty() && !finite(body.mNormals[i]))
                {
                    return "normals: vertex " + num(i) + ": a component is not a finite number";
                }
                if (!body.mUvs.empty() && !(std::isfinite(body.mUvs[i][0]) && std::isfinite(body.mUvs[i][1])))
                {
                    return "uvs: vertex " + num(i) + ": a coordinate is not a finite number";
                }
                double sum = 0;
                for (std::size_t k = 0; k < maxMergedInfluences; ++k)
                {
                    const float weight = body.mBoneWeights[i][k];
                    if (!std::isfinite(weight) || weight < 0.f)
                    {
                        return "skin: vertex " + num(i) + ": weight " + num(k) + " is negative or not finite";
                    }
                    if (body.mBoneIndices[i][k] >= body.mBones.size())
                    {
                        return "skin: vertex " + num(i) + ": bone index " + num(body.mBoneIndices[i][k])
                            + " is not in the bone list of " + num(body.mBones.size());
                    }
                    sum += weight;
                }
                if (std::fabs(sum - 1.0) > L::sWeightSumTolerance)
                {
                    return "skin: vertex " + num(i) + ": the weights add up to " + std::to_string(sum) + ", not 1";
                }
            }
            for (std::size_t i = 0; i < triangleIndices; ++i)
            {
                if (body.mTriangles[i] >= vertices)
                {
                    return "triangles: index " + num(i) + " is " + num(body.mTriangles[i]) + ", the mesh has "
                        + num(vertices) + " vertices";
                }
            }

            std::uint64_t next = 0;
            for (std::size_t i = 0; i < body.mRegions.size(); ++i)
            {
                const RegionRange& region = body.mRegions[i];
                if (i > 0 && region.mSlot <= body.mRegions[i - 1].mSlot)
                {
                    return "regions: region " + num(i) + " is not after the one before it by slot";
                }
                if (region.mFirstTriangle != next || region.mTriangleCount == 0
                    || next + region.mTriangleCount > triangles)
                {
                    return "regions: region " + num(i) + " does not follow the one before it inside the "
                        + num(triangles) + " triangles";
                }
                next += region.mTriangleCount;
            }
            if (next != triangles)
            {
                return "regions: the regions cover " + num(next) + " of " + num(triangles) + " triangles";
            }

            next = 0;
            for (std::size_t i = 0; i < body.mSubmeshes.size(); ++i)
            {
                const Submesh& sub = body.mSubmeshes[i];
                if (sub.mMaterial >= body.mMaterials.size())
                {
                    return "submeshes: submesh " + num(i) + " names material " + num(sub.mMaterial) + " of "
                        + num(body.mMaterials.size());
                }
                if (i > 0)
                {
                    const Submesh& before = body.mSubmeshes[i - 1];
                    if (!(before.mSlot < sub.mSlot || (before.mSlot == sub.mSlot && before.mMaterial < sub.mMaterial)))
                    {
                        return "submeshes: submesh " + num(i) + " is not after the one before it by (slot, material)";
                    }
                }
                if (sub.mFirstTriangle != next || sub.mTriangleCount == 0 || next + sub.mTriangleCount > triangles)
                {
                    return "submeshes: submesh " + num(i) + " does not follow the one before it inside the "
                        + num(triangles) + " triangles";
                }
                const auto region = std::find_if(body.mRegions.begin(), body.mRegions.end(),
                    [&sub](const RegionRange& r) { return r.mSlot == sub.mSlot; });
                if (region == body.mRegions.end() || sub.mFirstTriangle < region->mFirstTriangle
                    || sub.mFirstTriangle + std::uint64_t{ sub.mTriangleCount }
                        > std::uint64_t{ region->mFirstTriangle } + region->mTriangleCount)
                {
                    return "submeshes: submesh " + num(i) + " is not inside the region of its slot";
                }
                next += sub.mTriangleCount;
            }
            if (next != triangles)
            {
                return "submeshes: the submeshes cover " + num(next) + " of " + num(triangles) + " triangles";
            }

            std::uint64_t members = 0;
            std::set<std::uint32_t> inGroup;
            for (std::size_t i = 0; i < body.mWeldGroups.size(); ++i)
            {
                const std::vector<std::uint32_t>& group = body.mWeldGroups[i];
                if (group.size() < 2)
                {
                    return "weld groups: group " + num(i) + " has " + num(group.size()) + " members, needs two";
                }
                members += group.size();
                if (members > L::sMaxWeldMembers)
                {
                    return "weld groups: more than " + num(L::sMaxWeldMembers) + " members in all";
                }
                if (i > 0 && !(body.mWeldGroups[i - 1].front() < group.front()))
                {
                    return "weld groups: group " + num(i) + " does not start after the group before it";
                }
                for (std::size_t j = 0; j < group.size(); ++j)
                {
                    if (j > 0 && group[j] <= group[j - 1])
                    {
                        return "weld groups: group " + num(i) + " is not strictly increasing";
                    }
                    if (group[j] >= vertices)
                    {
                        return "weld groups: group " + num(i) + " names vertex " + num(group[j]) + " of "
                            + num(vertices);
                    }
                    if (!inGroup.insert(group[j]).second)
                    {
                        return "weld groups: vertex " + num(group[j]) + " is in two groups";
                    }
                }
            }
            return {};
        }

        class Writer
        {
        public:
            explicit Writer(std::vector<std::uint8_t>& out)
                : mOut(out)
            {
            }

            template <class T>
            void put(T value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                const std::size_t at = mOut.size();
                mOut.resize(at + sizeof(T));
                std::memcpy(mOut.data() + at, &value, sizeof(T));
            }

            void putName(const std::string& name)
            {
                put<std::uint16_t>(static_cast<std::uint16_t>(name.size()));
                mOut.insert(mOut.end(), name.begin(), name.end());
            }

        private:
            std::vector<std::uint8_t>& mOut;
        };

        class Reader
        {
        public:
            Reader(const std::uint8_t* data, std::size_t size)
                : mData(data)
                , mSize(size)
            {
            }

            std::size_t remaining() const { return mSize - mPos; }

            template <class T>
            bool get(T& value)
            {
                if (remaining() < sizeof(T))
                {
                    return false;
                }
                std::memcpy(&value, mData + mPos, sizeof(T));
                mPos += sizeof(T);
                return true;
            }

            bool getName(std::string& name)
            {
                std::uint16_t length = 0;
                if (!get(length) || length > BodyMeshLimits::sMaxNameBytes || remaining() < length)
                {
                    return false;
                }
                name.assign(reinterpret_cast<const char*>(mData + mPos), length);
                mPos += length;
                return true;
            }

        private:
            const std::uint8_t* mData;
            std::size_t mSize;
            std::size_t mPos = 0;
        };
    }

    bool writeBodyMesh(const MergedBody& body, std::vector<std::uint8_t>& out, std::string& error)
    {
        out.clear();
        error = check(body);
        if (!error.empty())
        {
            return false;
        }
        std::uint64_t members = 0;
        for (const std::vector<std::uint32_t>& group : body.mWeldGroups)
        {
            members += group.size();
        }
        Writer w(out);
        for (char c : bodyMeshMagic)
        {
            w.put<std::uint8_t>(static_cast<std::uint8_t>(c));
        }
        w.put<std::uint32_t>(bodyMeshLayoutVersion);
        w.put<std::uint32_t>((body.mNormals.empty() ? 0u : flagNormals) | (body.mUvs.empty() ? 0u : flagUvs));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mPositions.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mTriangles.size() / 3));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mBones.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mMaterials.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mRegions.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mSubmeshes.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(body.mWeldGroups.size()));
        w.put<std::uint32_t>(static_cast<std::uint32_t>(members));
        for (const std::string& bone : body.mBones)
        {
            w.putName(bone);
        }
        for (const std::string& material : body.mMaterials)
        {
            w.putName(material);
        }
        for (const Vec3& p : body.mPositions)
        {
            w.put(p[0]);
            w.put(p[1]);
            w.put(p[2]);
        }
        for (const Vec3& n : body.mNormals)
        {
            w.put(n[0]);
            w.put(n[1]);
            w.put(n[2]);
        }
        for (const Vec2& uv : body.mUvs)
        {
            w.put(uv[0]);
            w.put(uv[1]);
        }
        for (const auto& indices : body.mBoneIndices)
        {
            for (std::uint16_t index : indices)
            {
                w.put(index);
            }
        }
        for (const auto& weights : body.mBoneWeights)
        {
            for (float weight : weights)
            {
                w.put(weight);
            }
        }
        for (std::uint32_t index : body.mTriangles)
        {
            w.put(index);
        }
        for (const RegionRange& region : body.mRegions)
        {
            w.put<std::int32_t>(region.mSlot);
            w.put<std::uint32_t>(region.mFirstTriangle);
            w.put<std::uint32_t>(region.mTriangleCount);
        }
        for (const Submesh& sub : body.mSubmeshes)
        {
            w.put<std::uint32_t>(sub.mMaterial);
            w.put<std::int32_t>(sub.mSlot);
            w.put<std::uint32_t>(sub.mFirstTriangle);
            w.put<std::uint32_t>(sub.mTriangleCount);
        }
        for (const std::vector<std::uint32_t>& group : body.mWeldGroups)
        {
            w.put<std::uint32_t>(static_cast<std::uint32_t>(group.size()));
        }
        for (const std::vector<std::uint32_t>& group : body.mWeldGroups)
        {
            for (std::uint32_t vertex : group)
            {
                w.put(vertex);
            }
        }
        return true;
    }

    BodyMeshReadResult readBodyMesh(const std::uint8_t* data, std::size_t size)
    {
        namespace L = BodyMeshLimits;
        BodyMeshReadResult result;
        const auto fail = [&result](std::string message) {
            result.mBody.reset();
            result.mError = std::move(message);
            return result;
        };
        if (data == nullptr || size < headerBytes)
        {
            return fail("header: " + num(size) + " bytes, a payload starts with " + num(headerBytes));
        }
        Reader r(data, size);
        char magic[8];
        for (char& c : magic)
        {
            std::uint8_t byte = 0;
            r.get(byte);
            c = static_cast<char>(byte);
        }
        if (std::memcmp(magic, bodyMeshMagic, 8) != 0)
        {
            return fail("header: not a bodymesh payload (the magic is wrong)");
        }
        std::uint32_t version = 0;
        std::uint32_t flags = 0;
        r.get(version);
        r.get(flags);
        if (version != bodyMeshLayoutVersion)
        {
            return fail("header: layout version " + num(version) + " is not supported (this reader reads version "
                + num(bodyMeshLayoutVersion) + ")");
        }
        if ((flags & ~(flagNormals | flagUvs)) != 0)
        {
            return fail("header: unknown flag bits " + num(flags));
        }
        std::uint32_t counts[8];
        for (std::uint32_t& count : counts)
        {
            r.get(count);
        }
        const std::uint32_t vertices = counts[0], triangles = counts[1], bones = counts[2], materials = counts[3],
                            regions = counts[4], submeshes = counts[5], groups = counts[6], members = counts[7];
        if (vertices == 0 || vertices > L::sMaxVertices || triangles == 0 || triangles > L::sMaxTriangles || bones == 0
            || bones > L::sMaxBones || materials == 0 || materials > L::sMaxMaterials || regions == 0
            || regions > L::sMaxRegions || submeshes == 0 || submeshes > L::sMaxSubmeshes || groups > L::sMaxWeldGroups
            || members > L::sMaxWeldMembers || members < 2ull * groups)
        {
            return fail("header: a count is outside its limit (vertices " + num(vertices) + ", triangles "
                + num(triangles) + ", bones " + num(bones) + ", materials " + num(materials) + ", regions "
                + num(regions) + ", submeshes " + num(submeshes) + ", weld groups " + num(groups) + ", members "
                + num(members) + ")");
        }
        // The bytes the counts promise must be there before anything is allocated.
        std::uint64_t needed = 2ull * (bones + materials);
        needed += std::uint64_t{ vertices }
            * (12 + ((flags & flagNormals) != 0 ? 12 : 0) + ((flags & flagUvs) != 0 ? 8 : 0) + 8 + 16);
        needed += std::uint64_t{ triangles } * 12 + std::uint64_t{ regions } * 12 + std::uint64_t{ submeshes } * 16
            + std::uint64_t{ groups } * 4 + std::uint64_t{ members } * 4;
        if (needed > r.remaining())
        {
            return fail(
                "payload: the counts need at least " + num(needed) + " bytes, " + num(r.remaining()) + " are left");
        }

        MergedBody body;
        body.mBones.resize(bones);
        for (std::uint32_t i = 0; i < bones; ++i)
        {
            if (!r.getName(body.mBones[i]))
            {
                return fail(
                    "bones: bone " + num(i) + ": the name is cut off or over " + num(L::sMaxNameBytes) + " bytes");
            }
        }
        body.mMaterials.resize(materials);
        for (std::uint32_t i = 0; i < materials; ++i)
        {
            if (!r.getName(body.mMaterials[i]))
            {
                return fail("materials: material " + num(i) + ": the name is cut off or over " + num(L::sMaxNameBytes)
                    + " bytes");
            }
        }
        // Re-check the fixed part: names consumed more than the minimum counted above.
        std::uint64_t fixed = needed - 2ull * (bones + materials);
        if (fixed > r.remaining())
        {
            return fail(
                "payload: " + num(fixed) + " bytes of arrays are expected, " + num(r.remaining()) + " are left");
        }
        body.mPositions.resize(vertices);
        for (Vec3& p : body.mPositions)
        {
            r.get(p[0]);
            r.get(p[1]);
            r.get(p[2]);
        }
        if ((flags & flagNormals) != 0)
        {
            body.mNormals.resize(vertices);
            for (Vec3& n : body.mNormals)
            {
                r.get(n[0]);
                r.get(n[1]);
                r.get(n[2]);
            }
        }
        if ((flags & flagUvs) != 0)
        {
            body.mUvs.resize(vertices);
            for (Vec2& uv : body.mUvs)
            {
                r.get(uv[0]);
                r.get(uv[1]);
            }
        }
        body.mBoneIndices.resize(vertices);
        for (auto& indices : body.mBoneIndices)
        {
            for (std::uint16_t& index : indices)
            {
                r.get(index);
            }
        }
        body.mBoneWeights.resize(vertices);
        for (auto& weights : body.mBoneWeights)
        {
            for (float& weight : weights)
            {
                r.get(weight);
            }
        }
        body.mTriangles.resize(std::size_t{ triangles } * 3);
        for (std::uint32_t& index : body.mTriangles)
        {
            r.get(index);
        }
        body.mRegions.resize(regions);
        for (RegionRange& region : body.mRegions)
        {
            std::int32_t slot = 0;
            r.get(slot);
            region.mSlot = slot;
            r.get(region.mFirstTriangle);
            r.get(region.mTriangleCount);
        }
        body.mSubmeshes.resize(submeshes);
        for (Submesh& sub : body.mSubmeshes)
        {
            std::int32_t slot = 0;
            r.get(sub.mMaterial);
            r.get(slot);
            sub.mSlot = slot;
            r.get(sub.mFirstTriangle);
            r.get(sub.mTriangleCount);
        }
        std::vector<std::uint32_t> sizes(groups);
        std::uint64_t total = 0;
        for (std::uint32_t& groupSize : sizes)
        {
            r.get(groupSize);
            total += groupSize;
        }
        if (total != members)
        {
            return fail("weld groups: the group sizes add up to " + num(total) + ", the header says " + num(members));
        }
        body.mWeldGroups.resize(groups);
        for (std::uint32_t g = 0; g < groups; ++g)
        {
            if (sizes[g] > r.remaining() / 4)
            {
                return fail("weld groups: group " + num(g) + " is cut off");
            }
            body.mWeldGroups[g].resize(sizes[g]);
            for (std::uint32_t& vertex : body.mWeldGroups[g])
            {
                r.get(vertex);
            }
        }
        if (r.remaining() != 0)
        {
            return fail("payload: " + num(r.remaining()) + " bytes follow the end of the data");
        }
        const std::string problem = check(body);
        if (!problem.empty())
        {
            return fail(problem);
        }
        result.mBody = std::move(body);
        return result;
    }
}
