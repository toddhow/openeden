#ifndef OPENCS_TESTS_MODEL_MODULE_ADVANCEDBODIES_ABTESTSUPPORT_H
#define OPENCS_TESTS_MODEL_MODULE_ADVANCEDBODIES_ABTESTSUPPORT_H

// Task 0069 stage f: shared helpers of the advancedbodies_editor tests, written from the design alone (the section
// "Stage f, design" of docs/tasks/0069-advancedbodies-merged-legacy-body.md) and the public headers of components/body.
//
// Everything here is SYNTHETIC: bodies are stacked quads or strips built in code, written with components/body into
// temp folders (packages, packs, merged caches) or built in memory (stage a report texts). No game data, no game file
// names. The expected values (summaries, region assignment, region lines, weld points, the palette) are computed here
// independently of the module, from the rules in the design.

#include "editor_modules/advancedbodies_editor/bodyview.hpp"

#include <components/body/bodycache.hpp>
#include <components/body/bodyjson.hpp>
#include <components/body/bodymerge.hpp>
#include <components/body/bodymesh.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodypackage.hpp>
#include <components/body/bodyvalidate.hpp>

#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace ABTest
{
    namespace edit = openeden::editor;
    namespace ab = advancedbodies_editor;

    using Point = std::array<double, 3>;

    // ---- text, bytes and files ---------------------------------------------------------------------------------

    inline bool contains(std::string_view text, std::string_view part)
    {
        return text.find(part) != std::string_view::npos;
    }

    inline std::vector<std::uint8_t> bytesOf(std::string_view text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    inline std::string textOf(const std::vector<std::uint8_t>& bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    inline void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    inline void writeText(const std::filesystem::path& path, std::string_view text)
    {
        writeBytes(path, bytesOf(text));
    }

    inline std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    /// Replaces the first occurrence of `needle`; a missing needle is a test failure (the fixture changed).
    inline std::string replaceFirst(std::string text, std::string_view needle, std::string_view replacement)
    {
        const std::size_t at = text.find(needle);
        if (at == std::string::npos)
        {
            ADD_FAILURE() << "the fixture text holds no '" << needle << "'";
            return text;
        }
        text.replace(at, needle.size(), replacement);
        return text;
    }

    /// Two paths name the same thing: equal after lexical normalisation, or the same existing file or folder.
    inline bool samePath(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        if (a.lexically_normal() == b.lexically_normal())
        {
            return true;
        }
        std::error_code code;
        return std::filesystem::exists(a, code) && std::filesystem::exists(b, code)
            && std::filesystem::equivalent(a, b, code);
    }

    /// Each test gets an empty temp folder of its own, removed afterwards.
    class AbTempTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
            mDir = std::filesystem::temp_directory_path()
                / (std::string("ab-") + test->test_suite_name() + "-" + test->name());
            std::error_code code;
            std::filesystem::remove_all(mDir, code);
            std::filesystem::create_directories(mDir);
        }

        void TearDown() override
        {
            std::error_code code;
            std::filesystem::remove_all(mDir, code);
        }

        std::filesystem::path mDir;
    };

    // ---- synthetic merged bodies ---------------------------------------------------------------------------------

    /// The bones of the synthetic bodies, in the payload's bone order (the skeleton profile maps the same names).
    inline const std::vector<std::string>& boneNames()
    {
        static const std::vector<std::string> names{ "Bip Pelvis", "Bip Spine", "Bip Neck", "Bip Head" };
        return names;
    }

    /// `slots` quads (1 to 4) stacked along z from 0 to `height`, x from -halfWidth to halfWidth, y = 0.
    /// Quad k owns vertices 4k..4k+3: (-w, z_k), (w, z_k), (w, z_k+1), (-w, z_k+1); triangles (4k, 4k+1, 4k+2) and
    /// (4k, 4k+2, 4k+3); slot number 2k + 1, so region k is triangles [2k, 2k + 2); one material "skin", one submesh
    /// per slot. The top corners of quad k and the bottom corners of quad k + 1 are the same points (bit for bit):
    /// weld groups [4k+2, 4k+5] and [4k+3, 4k+4]. Skin: the bottom corners of quad k weigh 1 on bone k; its top
    /// corners 0.75 on bone k + 1 then 0.25 on bone k (the last quad's top corners 1 on bone k). Normals and UVs set.
    inline Body::MergedBody makeStackedBody(float height, int slots, float halfWidth)
    {
        Body::MergedBody body;
        if (slots < 1 || slots > 4)
        {
            ADD_FAILURE() << "makeStackedBody takes 1 to 4 slots";
            return body;
        }
        const auto count = static_cast<std::uint32_t>(slots);
        body.mMaterials = { "skin" };
        for (std::uint32_t k = 0; k < count; ++k)
        {
            body.mBones.push_back(boneNames()[k]);
        }
        for (std::uint32_t k = 0; k < count; ++k)
        {
            const float z0 = height * static_cast<float>(k) / static_cast<float>(count);
            const float z1 = height * static_cast<float>(k + 1) / static_cast<float>(count);
            const float v0 = static_cast<float>(k) / static_cast<float>(count);
            const float v1 = static_cast<float>(k + 1) / static_cast<float>(count);
            body.mPositions.push_back({ -halfWidth, 0.f, z0 });
            body.mPositions.push_back({ halfWidth, 0.f, z0 });
            body.mPositions.push_back({ halfWidth, 0.f, z1 });
            body.mPositions.push_back({ -halfWidth, 0.f, z1 });
            for (int i = 0; i < 4; ++i)
            {
                body.mNormals.push_back({ 0.f, -1.f, 0.f });
            }
            body.mUvs.push_back({ 0.f, v0 });
            body.mUvs.push_back({ 1.f, v0 });
            body.mUvs.push_back({ 1.f, v1 });
            body.mUvs.push_back({ 0.f, v1 });

            const auto bone = static_cast<std::uint16_t>(k);
            const std::array<std::uint16_t, 4> bottomIndices{ bone, 0, 0, 0 };
            const std::array<float, 4> bottomWeights{ 1.f, 0.f, 0.f, 0.f };
            std::array<std::uint16_t, 4> topIndices{ bone, 0, 0, 0 };
            std::array<float, 4> topWeights{ 1.f, 0.f, 0.f, 0.f };
            if (k + 1 < count)
            {
                topIndices = { static_cast<std::uint16_t>(k + 1), bone, 0, 0 };
                topWeights = { 0.75f, 0.25f, 0.f, 0.f };
            }
            body.mBoneIndices.push_back(bottomIndices);
            body.mBoneIndices.push_back(bottomIndices);
            body.mBoneIndices.push_back(topIndices);
            body.mBoneIndices.push_back(topIndices);
            body.mBoneWeights.push_back(bottomWeights);
            body.mBoneWeights.push_back(bottomWeights);
            body.mBoneWeights.push_back(topWeights);
            body.mBoneWeights.push_back(topWeights);

            const std::uint32_t base = 4 * k;
            body.mTriangles.insert(body.mTriangles.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            const int slot = static_cast<int>(2 * k + 1);
            body.mRegions.push_back(Body::RegionRange{ slot, 2 * k, 2 });
            body.mSubmeshes.push_back(Body::Submesh{ 0, slot, 2 * k, 2 });
            if (k + 1 < count)
            {
                body.mWeldGroups.push_back({ base + 2, base + 5 });
                body.mWeldGroups.push_back({ base + 3, base + 4 });
            }
        }
        return body;
    }

    /// A strip of `quads` quads along z from 0 to `height` sharing their vertices: vertex 2i is (-w, z_i), 2i + 1 is
    /// (w, z_i); quad i is triangles (2i, 2i+1, 2i+3) and (2i, 2i+3, 2i+2). One slot (1), one region and submesh over
    /// all triangles, every vertex weight 1 on "Bip Pelvis", no normals, no UVs, no weld groups. Its distinct edges are
    /// 4 * quads + 1.
    inline Body::MergedBody makeStripBody(std::uint32_t quads, float height, float halfWidth)
    {
        Body::MergedBody body;
        body.mMaterials = { "skin" };
        body.mBones = { boneNames()[0] };
        for (std::uint32_t i = 0; i <= quads; ++i)
        {
            const float z = height * static_cast<float>(i) / static_cast<float>(quads);
            body.mPositions.push_back({ -halfWidth, 0.f, z });
            body.mPositions.push_back({ halfWidth, 0.f, z });
        }
        body.mBoneIndices.assign(body.mPositions.size(), std::array<std::uint16_t, 4>{ 0, 0, 0, 0 });
        body.mBoneWeights.assign(body.mPositions.size(), std::array<float, 4>{ 1.f, 0.f, 0.f, 0.f });
        for (std::uint32_t i = 0; i < quads; ++i)
        {
            const std::uint32_t a = 2 * i;
            body.mTriangles.insert(body.mTriangles.end(), { a, a + 1, a + 3, a, a + 3, a + 2 });
        }
        body.mRegions.push_back(Body::RegionRange{ 1, 0, 2 * quads });
        body.mSubmeshes.push_back(Body::Submesh{ 0, 1, 0, 2 * quads });
        return body;
    }

    /// The canonical payload bytes of a body (the fixture must be one the payload writer accepts).
    inline std::vector<std::uint8_t> payloadBytes(const Body::MergedBody& body)
    {
        std::vector<std::uint8_t> bytes;
        std::string error;
        const bool written = Body::writeBodyMesh(body, bytes, error);
        EXPECT_TRUE(written) << "the synthetic payload does not write: " << error;
        return bytes;
    }

    /// The body as the payload reader gives it back (the vertex map, part order and report empty).
    inline Body::MergedBody roundTrip(const Body::MergedBody& body)
    {
        const Body::BodyMeshReadResult read = Body::readBodyMesh(payloadBytes(body));
        if (!read.mBody)
        {
            ADD_FAILURE() << "the synthetic payload does not read back: " << read.mError;
            return body;
        }
        return *read.mBody;
    }

    /// Overwrites the little-endian u32 at `offset` of a payload (header: magic 0..7, version 8, flags 12, counts 16).
    inline void patchU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        if (bytes.size() < offset + 4)
        {
            ADD_FAILURE() << "the payload is too short to patch";
            return;
        }
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    }

    // ---- what the design says the model derives (computed independently) ------------------------------------

    /// The vertex positions as doubles, divided by `unitsPerMetre`.
    inline std::vector<Point> positionsInMetres(const Body::MergedBody& body, double unitsPerMetre)
    {
        std::vector<Point> out;
        out.reserve(body.mPositions.size());
        for (const Body::Vec3& p : body.mPositions)
        {
            out.push_back({ static_cast<double>(p[0]) / unitsPerMetre, static_cast<double>(p[1]) / unitsPerMetre,
                static_cast<double>(p[2]) / unitsPerMetre });
        }
        return out;
    }

    /// The summary the validator must see for a payload: counts, the bounds of the positions divided by
    /// `unitsPerMetre`, the bone names, per vertex the influences with a weight above 0 in the payload's slot order,
    /// the triangle indices, one UV set when the payload has UVs.
    inline Body::MeshSummary referenceSummary(const Body::MergedBody& body, double unitsPerMetre)
    {
        Body::MeshSummary summary;
        summary.mVertexCount = static_cast<std::uint32_t>(body.mPositions.size());
        summary.mTriangleCount = static_cast<std::uint32_t>(body.mTriangles.size() / 3);
        const std::vector<Point> positions = positionsInMetres(body, unitsPerMetre);
        if (!positions.empty())
        {
            summary.mBoundsMin = positions.front();
            summary.mBoundsMax = positions.front();
        }
        for (const Point& p : positions)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                summary.mBoundsMin[axis] = std::min(summary.mBoundsMin[axis], p[axis]);
                summary.mBoundsMax[axis] = std::max(summary.mBoundsMax[axis], p[axis]);
            }
        }
        summary.mBoneNames = body.mBones;
        summary.mInfluenceOffsets.push_back(0);
        for (std::size_t v = 0; v < body.mPositions.size(); ++v)
        {
            for (std::size_t slot = 0; slot < Body::maxMergedInfluences; ++slot)
            {
                const float weight = body.mBoneWeights[v][slot];
                if (weight > 0.f)
                {
                    summary.mInfluences.push_back(
                        Body::MeshSummary::Influence{ body.mBoneIndices[v][slot], static_cast<double>(weight) });
                }
            }
            summary.mInfluenceOffsets.push_back(static_cast<std::uint32_t>(summary.mInfluences.size()));
        }
        summary.mTriangleIndices = body.mTriangles;
        summary.mUvSetCount = body.mUvs.empty() ? 0u : 1u;
        return summary;
    }

    /// Per triangle: the index of the first region (map order, then range order) whose range holds it, else -1.
    inline std::vector<int> referenceTriangleRegions(const Body::RegionMap& map, std::size_t triangles)
    {
        std::vector<int> out(triangles, -1);
        for (std::size_t t = 0; t < triangles; ++t)
        {
            for (std::size_t r = 0; r < map.mRegions.size() && out[t] < 0; ++r)
            {
                for (const Body::TriangleRange& range : map.mRegions[r].mRanges)
                {
                    if (t >= range.mBegin && t < range.mEnd)
                    {
                        out[t] = static_cast<int>(r);
                        break;
                    }
                }
            }
        }
        return out;
    }

    /// The palette of the design (RGB, alpha 1), written out here so the module's own table is checked against it.
    inline const std::array<edit::Colour, 12>& designPalette()
    {
        static const std::array<edit::Colour, 12> palette{ {
            { 0.90f, 0.30f, 0.30f, 1.0f },
            { 0.30f, 0.75f, 0.35f, 1.0f },
            { 0.30f, 0.50f, 0.95f, 1.0f },
            { 0.95f, 0.80f, 0.25f, 1.0f },
            { 0.75f, 0.40f, 0.90f, 1.0f },
            { 0.25f, 0.85f, 0.85f, 1.0f },
            { 0.95f, 0.55f, 0.20f, 1.0f },
            { 0.60f, 0.85f, 0.30f, 1.0f },
            { 0.90f, 0.45f, 0.70f, 1.0f },
            { 0.55f, 0.60f, 0.95f, 1.0f },
            { 0.80f, 0.65f, 0.45f, 1.0f },
            { 0.45f, 0.70f, 0.60f, 1.0f },
        } };
        return palette;
    }

    inline const edit::Colour kDesignUnassigned{ 0.45f, 0.45f, 0.45f, 1.0f };
    inline const edit::Colour kDesignWeld{ 1.0f, 0.2f, 0.9f, 1.0f };

    /// The colour of a region index (or of -1, no region) by the design's rule.
    inline edit::Colour designColour(int region)
    {
        if (region < 0)
        {
            return kDesignUnassigned;
        }
        return designPalette()[static_cast<std::size_t>(region) % designPalette().size()];
    }

    inline bool sameColour(const edit::Colour& a, const edit::Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// Every distinct edge (vertex pair, smaller index first) once, in the order the triangles first use it (triangle
    /// t = 0.. with edges (a, b), (b, c), (c, a)), from the smaller vertex's position to the larger's, in the colour of
    /// the region of the first triangle that uses it.
    inline std::vector<ab::OverlayLine> referenceRegionLines(const std::vector<Point>& positions,
        const std::vector<std::uint32_t>& triangles, const std::vector<int>& triangleRegion)
    {
        std::vector<ab::OverlayLine> lines;
        std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        const std::size_t count = triangles.size() / 3;
        for (std::size_t t = 0; t < count; ++t)
        {
            const std::uint32_t a = triangles[3 * t];
            const std::uint32_t b = triangles[3 * t + 1];
            const std::uint32_t c = triangles[3 * t + 2];
            const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> edges{ { { a, b }, { b, c }, { c, a } } };
            for (const auto& edge : edges)
            {
                const std::pair<std::uint32_t, std::uint32_t> key{ std::min(edge.first, edge.second),
                    std::max(edge.first, edge.second) };
                if (!seen.insert(key).second)
                {
                    continue;
                }
                ab::OverlayLine line;
                line.mFrom = positions[key.first];
                line.mTo = positions[key.second];
                line.mColour = designColour(t < triangleRegion.size() ? triangleRegion[t] : -1);
                lines.push_back(line);
            }
        }
        return lines;
    }

    /// One weld point per group at its first member's position.
    inline std::vector<Point> referenceWeldPoints(
        const std::vector<Point>& positions, const std::vector<std::vector<std::uint32_t>>& groups)
    {
        std::vector<Point> points;
        for (const std::vector<std::uint32_t>& group : groups)
        {
            if (!group.empty() && group.front() < positions.size())
            {
                points.push_back(positions[group.front()]);
            }
        }
        return points;
    }

    // ---- comparisons -------------------------------------------------------------------------------------------

    /// Metres within 1e-6 relative (1e-6 m below 1 m).
    inline ::testing::AssertionResult nearPoint(const Point& actual, const Point& expected)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            const double tolerance = 1e-6 * std::max(1.0, std::abs(expected[i]));
            if (!(std::abs(actual[i] - expected[i]) <= tolerance))
            {
                return ::testing::AssertionFailure()
                    << "(" << actual[0] << ", " << actual[1] << ", " << actual[2] << ") is not (" << expected[0] << ", "
                    << expected[1] << ", " << expected[2] << ")";
            }
        }
        return ::testing::AssertionSuccess();
    }

    inline void expectNearPoints(const std::vector<Point>& actual, const std::vector<Point>& expected)
    {
        EXPECT_EQ(actual.size(), expected.size());
        int failures = 0;
        for (std::size_t i = 0; i < std::min(actual.size(), expected.size()) && failures < 10; ++i)
        {
            const ::testing::AssertionResult within = nearPoint(actual[i], expected[i]);
            if (!within)
            {
                ADD_FAILURE() << "point " << i << ": " << within.message();
                ++failures;
            }
        }
    }

    inline void expectSameLines(
        const std::vector<ab::OverlayLine>& actual, const std::vector<ab::OverlayLine>& expected)
    {
        EXPECT_EQ(actual.size(), expected.size());
        int failures = 0;
        for (std::size_t i = 0; i < std::min(actual.size(), expected.size()) && failures < 10; ++i)
        {
            const ::testing::AssertionResult from = nearPoint(actual[i].mFrom, expected[i].mFrom);
            const ::testing::AssertionResult to = nearPoint(actual[i].mTo, expected[i].mTo);
            const bool colour = sameColour(actual[i].mColour, expected[i].mColour);
            if (!from || !to || !colour)
            {
                ADD_FAILURE() << "line " << i << ": from " << from.message() << ", to " << to.message()
                              << (colour ? "" : ", another colour");
                ++failures;
            }
        }
    }

    inline void expectSameSummary(const Body::MeshSummary& actual, const Body::MeshSummary& expected)
    {
        EXPECT_EQ(actual.mVertexCount, expected.mVertexCount);
        EXPECT_EQ(actual.mTriangleCount, expected.mTriangleCount);
        EXPECT_TRUE(nearPoint(actual.mBoundsMin, expected.mBoundsMin));
        EXPECT_TRUE(nearPoint(actual.mBoundsMax, expected.mBoundsMax));
        EXPECT_EQ(actual.mBoneNames, expected.mBoneNames);
        EXPECT_EQ(actual.mInfluenceOffsets, expected.mInfluenceOffsets);
        EXPECT_EQ(actual.mInfluences, expected.mInfluences);
        EXPECT_EQ(actual.mTriangleIndices, expected.mTriangleIndices);
        EXPECT_EQ(actual.mUvSetCount, expected.mUvSetCount);
    }

    inline std::string describeReport(const Body::Report& report)
    {
        return Body::formatReportText(report);
    }

    inline void expectSameReport(const Body::Report& actual, const Body::Report& expected)
    {
        EXPECT_EQ(actual.mFindings, expected.mFindings) << "actual:\n"
                                                        << describeReport(actual) << "expected:\n"
                                                        << describeReport(expected);
        EXPECT_EQ(actual.mHumanoidGait, expected.mHumanoidGait);
    }

    /// The first finding with this code, or nullptr.
    inline const Body::Finding* findCode(const Body::Report& report, std::string_view code)
    {
        for (const Body::Finding& finding : report.mFindings)
        {
            if (finding.mCode == code)
            {
                return &finding;
            }
        }
        return nullptr;
    }

    // ---- modder packages ---------------------------------------------------------------------------------------

    inline constexpr const char* kPayloadPath = "mesh/body.bodymesh";
    inline constexpr const char* kPackageId = "synthetic_body";

    /// A modder package around one bodymesh skin "body" at kPayloadPath (no inline summary): an inline skeleton
    /// profile mapping the four bones of boneNames(), root bone "Bip Root" (unmapped), a rest skeleton with identity
    /// rotations, one target, a region map over "body" with one region "region-<k>" per payload region, and the
    /// payload's weld groups (tolerance 0) when it has any.
    inline Body::BodyPackage makePackage(const Body::MergedBody& payload)
    {
        Body::BodyPackage package;
        package.mIdentity.mId = kPackageId;
        package.mIdentity.mName = "Synthetic body";
        package.mIdentity.mVersion = "1.0.0";
        package.mIdentity.mAuthor = "OpenEden tests";
        package.mIdentity.mLicense = "CC0-1.0";

        Body::Target target;
        target.mRace = "synthetic_race";
        target.mSex = "male";
        package.mTargets.push_back(target);

        opas::anim::SkeletonProfile profile;
        profile.name = "synthetic";
        profile.bones
            = { { "Pelvis", "Bip Pelvis" }, { "Spine", "Bip Spine" }, { "Neck", "Bip Neck" }, { "Head", "Bip Head" } };
        package.mBinding.mSkeletonProfile = profile;
        package.mBinding.mRootBone = "Bip Root";
        package.mBinding.mUnmappedBones = { "Bip Root" };
        std::vector<Body::RestBone> rest;
        rest.push_back(Body::RestBone{ "Bip Root", -1, { 0, 0, 0 }, { 0, 0, 0, 1 } });
        rest.push_back(Body::RestBone{ "Bip Pelvis", 0, { 0, 0, 0.875 }, { 0, 0, 0, 1 } });
        rest.push_back(Body::RestBone{ "Bip Spine", 1, { 0, 0, 0.25 }, { 0, 0, 0, 1 } });
        rest.push_back(Body::RestBone{ "Bip Neck", 2, { 0, 0, 0.375 }, { 0, 0, 0, 1 } });
        rest.push_back(Body::RestBone{ "Bip Head", 3, { 0, 0, 0.125 }, { 0, 0, 0, 1 } });
        package.mBinding.mRestSkeleton = rest;

        Body::MeshEntry mesh;
        mesh.mId = "body";
        mesh.mPath = kPayloadPath;
        mesh.mFormat = Body::MeshFormat::BodyMesh;
        mesh.mLod = 0;
        mesh.mRole = Body::MeshRole::Skin;
        package.mMeshes.push_back(mesh);

        Body::RegionMap regions;
        regions.mOrigin = Body::Origin::Authored;
        regions.mMesh = "body";
        for (std::size_t k = 0; k < payload.mRegions.size(); ++k)
        {
            const Body::RegionRange& range = payload.mRegions[k];
            regions.mRegions.push_back(Body::Region{ "region-" + std::to_string(k),
                { Body::TriangleRange{ range.mFirstTriangle, range.mFirstTriangle + range.mTriangleCount } } });
        }
        package.mRegionMap = regions;

        if (!payload.mWeldGroups.empty())
        {
            Body::WeldGroups welds;
            welds.mOrigin = Body::Origin::Authored;
            welds.mMesh = "body";
            welds.mToleranceM = 0;
            welds.mGroups = payload.mWeldGroups;
            package.mWeldGroups = welds;
        }
        return package;
    }

    /// body.json (the package's canonical text) and the payload at kPayloadPath.
    inline Body::PackageFiles packageFiles(const Body::BodyPackage& package, const std::vector<std::uint8_t>& payload)
    {
        Body::PackageFiles files;
        files.mFiles["body.json"] = bytesOf(Body::writeBodyPackage(package));
        files.mFiles[kPayloadPath] = payload;
        return files;
    }

    /// A package, its payload body and its files, kept together.
    struct PackageFixture
    {
        Body::MergedBody mPayload; // the body written at kPayloadPath (metres)
        Body::BodyPackage mPackage;
        Body::PackageFiles mFiles;

        /// Writes body.json again after mPackage changed (the payload file is kept).
        void refresh() { mFiles.mFiles["body.json"] = bytesOf(Body::writeBodyPackage(mPackage)); }
    };

    /// The standard package: two stacked quads 1.75 m tall and 0.5 m wide (4 triangles, 8 vertices, two weld groups).
    inline PackageFixture makePackageFixture()
    {
        PackageFixture fixture;
        fixture.mPayload = roundTrip(makeStackedBody(1.75f, 2, 0.25f));
        fixture.mPackage = makePackage(fixture.mPayload);
        fixture.mFiles = packageFiles(fixture.mPackage, payloadBytes(fixture.mPayload));
        return fixture;
    }

    /// A package around a strip body of `quads` quads, 1.75 m tall.
    inline PackageFixture makeStripPackageFixture(std::uint32_t quads)
    {
        PackageFixture fixture;
        fixture.mPayload = roundTrip(makeStripBody(quads, 1.75f, 0.25f));
        fixture.mPackage = makePackage(fixture.mPayload);
        fixture.mFiles = packageFiles(fixture.mPackage, payloadBytes(fixture.mPayload));
        return fixture;
    }

    /// The package as body.json reads back (what the view validates).
    inline Body::BodyPackage readBack(const Body::PackageFiles& files)
    {
        const auto json = files.mFiles.find("body.json");
        if (json == files.mFiles.end())
        {
            ADD_FAILURE() << "the fixture has no body.json";
            return {};
        }
        const Body::BodyLoadResult loaded = Body::readBodyPackage(textOf(json->second), "body.json");
        if (!loaded.mValue)
        {
            ADD_FAILURE() << "the fixture's body.json does not read: " << loaded.mError.toString();
            return {};
        }
        return *loaded.mValue;
    }

    /// Gives the summary of a payload by path, for format "bodymesh" only (the test's own mesh source).
    class SummaryTable final : public Body::MeshSource
    {
    public:
        std::map<std::string, Body::MeshSummary> mByPath;

        std::optional<Body::MeshSummary> summarize(std::string_view path, std::string_view format) const override
        {
            if (format != "bodymesh")
            {
                return std::nullopt;
            }
            const auto it = mByPath.find(std::string(path));
            if (it == mByPath.end())
            {
                return std::nullopt;
            }
            return it->second;
        }
    };

    /// What the view must report for a package: the validator on body.json as it reads back, with a reader of the
    /// package's own files (by safe path) and the summaries of the given payloads (path -> body, positions as stored).
    inline Body::Report expectedPackageReport(
        const Body::PackageFiles& files, const std::map<std::string, Body::MergedBody>& payloads)
    {
        const Body::BodyPackage package = readBack(files);
        SummaryTable table;
        for (const auto& [payloadPath, payload] : payloads)
        {
            table.mByPath[payloadPath] = referenceSummary(payload, 1.0);
        }
        Body::ValidateOptions options;
        options.mMeshSource = &table;
        options.mReadFile = [&files](std::string_view wanted) -> std::optional<std::string> {
            if (!Body::isSafePackagePath(wanted))
            {
                return std::nullopt;
            }
            const auto it = files.mFiles.find(std::string(wanted));
            if (it == files.mFiles.end())
            {
                return std::nullopt;
            }
            return textOf(it->second);
        };
        return Body::validateBodyPackage(package, options);
    }

    /// Writes the files under `folder` (subfolders made as needed). Returns the path of body.json.
    inline std::filesystem::path writeFolder(const std::filesystem::path& folder, const Body::PackageFiles& files)
    {
        std::filesystem::create_directories(folder);
        for (const auto& [name, bytes] : files.mFiles)
        {
            writeBytes(folder / std::filesystem::path(name), bytes);
        }
        return folder / "body.json";
    }

    /// Writes the files as one .edenpak (the library's packer). Returns `pack`.
    inline std::filesystem::path writePack(const std::filesystem::path& pack, const Body::PackageFiles& files)
    {
        std::string error;
        const bool written = Body::writePackageFile(files, pack, error);
        EXPECT_TRUE(written) << error;
        return pack;
    }

    // ---- merged caches -----------------------------------------------------------------------------------------

    inline constexpr const char* kView = "third";

    /// One body of a synthetic merged cache (game units, ~128 tall).
    struct CacheBody
    {
        std::string mRace;
        std::string mSex; // "male" or "female"
        Body::MergedBody mBody;
        char mKeyDigit = 'a'; // the key is 64 of this lower-case hex digit
    };

    inline Body::CacheEntry entryOf(const CacheBody& body)
    {
        return Body::CacheEntry{ Body::bodyEntryName(body.mRace, body.mSex, kView), body.mRace, body.mSex, kView,
            std::string(64, body.mKeyDigit) };
    }

    inline std::string titleOf(const Body::CacheEntry& entry)
    {
        return entry.mRace + " " + entry.mSex + " " + entry.mView;
    }

    /// The manifest of the bodies as the cache reader gives it back: entries sorted by name, failures by (race, sex,
    /// view), merger version bodyMergerVersion, layout version 0.
    inline Body::CacheManifest manifestOf(
        const std::vector<CacheBody>& bodies, const std::vector<Body::CacheFailure>& failures = {})
    {
        Body::CacheManifest manifest;
        manifest.mMergerVersion = Body::bodyMergerVersion;
        manifest.mLayoutVersion = Body::bodyMeshLayoutVersion;
        for (const CacheBody& body : bodies)
        {
            manifest.mEntries.push_back(entryOf(body));
        }
        std::sort(manifest.mEntries.begin(), manifest.mEntries.end(),
            [](const Body::CacheEntry& a, const Body::CacheEntry& b) { return a.mName < b.mName; });
        manifest.mFailures = failures;
        std::sort(manifest.mFailures.begin(), manifest.mFailures.end(),
            [](const Body::CacheFailure& a, const Body::CacheFailure& b) {
                return std::tie(a.mRace, a.mSex, a.mView) < std::tie(b.mRace, b.mSex, b.mView);
            });
        return manifest;
    }

    /// Payload bytes by entry name.
    inline std::map<std::string, std::vector<std::uint8_t>> payloadsOf(const std::vector<CacheBody>& bodies)
    {
        std::map<std::string, std::vector<std::uint8_t>> payloads;
        for (const CacheBody& body : bodies)
        {
            payloads[entryOf(body).mName] = payloadBytes(body.mBody);
        }
        return payloads;
    }

    /// manifest.txt and one file per payload.
    inline Body::PackageFiles cacheFiles(
        const Body::CacheManifest& manifest, const std::map<std::string, std::vector<std::uint8_t>>& payloads)
    {
        Body::PackageFiles files;
        files.mFiles["manifest.txt"] = bytesOf(Body::writeManifest(manifest));
        for (const auto& [name, bytes] : payloads)
        {
            files.mFiles[name] = bytes;
        }
        return files;
    }

    /// Publishes the files as merged-<digest>.edenpak in `folder` and points `current` at it. Returns the pack name.
    inline std::string publishFiles(const std::filesystem::path& folder, const Body::PackageFiles& files)
    {
        std::string packName;
        std::string error;
        const bool published = Body::publishCache(folder, files, packName, error);
        EXPECT_TRUE(published) << error;
        return packName;
    }

    inline std::string publishBodies(const std::filesystem::path& folder, const std::vector<CacheBody>& bodies,
        const std::vector<Body::CacheFailure>& failures = {})
    {
        return publishFiles(folder, cacheFiles(manifestOf(bodies, failures), payloadsOf(bodies)));
    }

    /// Three bodies that differ in race, sex, height and number of slots (3, 2, 2), all valid.
    inline std::vector<CacheBody> standardCacheBodies()
    {
        std::vector<CacheBody> bodies;
        bodies.push_back(CacheBody{ "Ash Folk", "female", roundTrip(makeStackedBody(126.f, 3, 14.f)), 'a' });
        bodies.push_back(CacheBody{ "Ash Folk", "male", roundTrip(makeStackedBody(128.f, 2, 16.f)), 'b' });
        bodies.push_back(CacheBody{ "Bog Kin", "male", roundTrip(makeStackedBody(120.f, 2, 18.f)), 'c' });
        return bodies;
    }

    /// The body of the list whose entry has this name.
    inline const CacheBody* bodyNamed(const std::vector<CacheBody>& bodies, const std::string& name)
    {
        for (const CacheBody& body : bodies)
        {
            if (entryOf(body).mName == name)
            {
                return &body;
            }
        }
        return nullptr;
    }

    // ---- stage a report texts ----------------------------------------------------------------------------------

    inline std::string jsonNumber(double value)
    {
        char buffer[64];
        const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

    struct SeamText
    {
        std::string mLabel = "forearm-wrist";
        std::string mSlotA = "rforearm";
        std::string mSlotB = "rwrist";
        bool mTouches = true;
        std::uint64_t mCount = 40;
        double mMin = 0;
        double mMedian = 0.0003;
        double mP95 = 0.001;
        double mMax = 0.0011;
        std::array<std::uint64_t, 6> mLadder{ 12, 30, 40, 40, 40, 40 };
        std::uint64_t mOverlap = 0;
        std::uint64_t mGap = 0;
        std::uint64_t mMeeting = 40;
        double mWeightMax = 1;
    };

    struct UnitText
    {
        std::string mRace;
        bool mFemale = false;
        bool mBeast = false;
        std::string mSkeleton = "meshes/skeleton_test.nif";
        std::uint64_t mVertices = 1483;
        std::uint64_t mTriangles = 2200;
        std::uint64_t mBones = 30;
        std::uint64_t mMaterials = 4;
        std::uint64_t mFiles = 12;
        std::uint64_t mFileBytes = 456789;
        std::uint64_t mSkinnedParts = 3;
        std::uint64_t mRigidParts = 16;
        std::vector<SeamText> mSeams;
        std::string mExtraField; // appended to the unit object as-is when not empty (e.g. "\"extra\": [[1]]")
    };

    inline std::string distributionJson(std::uint64_t count, double min, double median, double p95, double max)
    {
        return "{\"count\": " + std::to_string(count) + ", \"min\": " + jsonNumber(min) + ", \"median\": "
            + jsonNumber(median) + ", \"p95\": " + jsonNumber(p95) + ", \"max\": " + jsonNumber(max) + "}";
    }

    /// A seam object in the shape the stage a writer uses (with some of the fields the panel does not read).
    inline std::string seamJson(const SeamText& seam)
    {
        std::string ladder = "[";
        for (std::size_t i = 0; i < seam.mLadder.size(); ++i)
        {
            ladder += (i == 0 ? "" : ", ") + std::to_string(seam.mLadder[i]);
        }
        ladder += "]";
        return "{\"label\": \"" + seam.mLabel + "\", \"slotA\": \"" + seam.mSlotA + "\", \"slotB\": \"" + seam.mSlotB
            + "\", \"touches\": " + (seam.mTouches ? "true" : "false")
            + ", \"boundaryA\": " + std::to_string(seam.mCount) + ", \"boundaryB\": " + std::to_string(seam.mCount)
            + ", \"distance\": " + distributionJson(seam.mCount, seam.mMin, seam.mMedian, seam.mP95, seam.mMax)
            + ", \"withinLadder\": " + ladder + ", \"signedGap\": " + distributionJson(seam.mCount, 0, 0, 0, 0)
            + ", \"overlapPairs\": " + std::to_string(seam.mOverlap) + ", \"gapPairs\": " + std::to_string(seam.mGap)
            + ", \"meetingPairs\": " + std::to_string(seam.mMeeting) + ", \"weightDisagreement\": "
            + distributionJson(seam.mCount, 0, seam.mWeightMax, seam.mWeightMax, seam.mWeightMax)
            + ", \"boneSetDiffers\": 0, \"uvDiffers\": 0}";
    }

    inline std::string unitJson(const UnitText& unit)
    {
        std::string seams = "[";
        for (std::size_t i = 0; i < unit.mSeams.size(); ++i)
        {
            seams += (i == 0 ? "" : ", ") + seamJson(unit.mSeams[i]);
        }
        seams += "]";
        std::string text = "{\"race\": \"" + unit.mRace + "\", \"skeleton\": \"" + unit.mSkeleton
            + "\", \"skeletonBones\": 3, \"female\": " + (unit.mFemale ? "true" : "false")
            + ", \"beast\": " + (unit.mBeast ? "true" : "false") + ", \"playable\": true, \"totals\": {\"vertices\": "
            + std::to_string(unit.mVertices) + ", \"triangles\": " + std::to_string(unit.mTriangles)
            + ", \"bones\": " + std::to_string(unit.mBones) + ", \"materials\": " + std::to_string(unit.mMaterials)
            + ", \"files\": " + std::to_string(unit.mFiles) + ", \"fileBytes\": " + std::to_string(unit.mFileBytes)
            + ", \"skinnedParts\": " + std::to_string(unit.mSkinnedParts) + ", \"rigidParts\": "
            + std::to_string(unit.mRigidParts) + "}, \"slots\": [], \"seams\": " + seams + ", \"heads\": []";
        if (!unit.mExtraField.empty())
        {
            text += ", " + unit.mExtraField;
        }
        return text + "}";
    }

    /// A whole report ("format", "version", "units", and an "aggregate" the panel does not read).
    inline std::string reportJson(
        const std::vector<UnitText>& units, std::string_view format = "openeden.bodyanalysis", int version = 1)
    {
        std::string text = "{\n  \"format\": \"" + std::string(format)
            + "\",\n  \"version\": " + std::to_string(version) + ",\n  \"units\": [\n";
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            text += "    " + unitJson(units[i]) + (i + 1 < units.size() ? ",\n" : "\n");
        }
        text += "  ],\n  \"aggregate\": {\"humanoid\": [], \"beast\": []}\n}\n";
        return text;
    }

    inline UnitText unitText(std::string race, bool female, std::uint64_t vertices, std::vector<SeamText> seams = {})
    {
        UnitText unit;
        unit.mRace = std::move(race);
        unit.mFemale = female;
        unit.mVertices = vertices;
        unit.mSeams = std::move(seams);
        return unit;
    }

    // ---- an overlay sink that records everything ---------------------------------------------------------------

    struct DrawnLine
    {
        edit::Vec3 mA;
        edit::Vec3 mB;
        edit::Colour mColour;
    };

    struct DrawnPoint
    {
        edit::Vec3 mPosition;
        edit::Colour mColour;
    };

    class RecordingSink final : public edit::OverlaySink
    {
    public:
        void line(const edit::Vec3& a, const edit::Vec3& b, const edit::Colour& colour) override
        {
            mLines.push_back(DrawnLine{ a, b, colour });
        }
        void point(const edit::Vec3& p, const edit::Colour& colour, float) override
        {
            mPoints.push_back(DrawnPoint{ p, colour });
        }
        void label(const edit::Vec3&, std::string_view, const edit::Colour&) override { ++mLabels; }

        std::vector<DrawnLine> mLines;
        std::vector<DrawnPoint> mPoints;
        int mLabels = 0;
    };

    inline Point toPoint(const edit::Vec3& v)
    {
        return Point{ v.x, v.y, v.z };
    }
}

#endif
