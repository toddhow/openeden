#ifndef OPENEDEN_APPS_COMPONENTS_TESTS_BODY_BODYFIXTURES_H
#define OPENEDEN_APPS_COMPONENTS_TESTS_BODY_BODYFIXTURES_H

// Task 0063 (AdvancedBodies B1): fixtures for the body contract tests, generated in code.
// Everything here is synthetic: no game asset, no real data, neutral bodies only.

#include <components/body/bodycontract.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodypackage.hpp>
#include <components/body/bodyvalidate.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BodyTest
{
    /// A synthetic mesh summary: a triangle fan over vertexCount vertices (triangleCount = vertexCount - 2). Vertex v
    /// has one influence (weight 1) when v is even and two (0.25 and 0.75) when v is odd, on bones chosen round robin
    /// from boneNames; with no boneNames the mesh is not skinned (no influence arrays).
    Body::MeshSummary makeSummary(std::uint32_t vertexCount, const std::vector<std::string>& boneNames,
        std::array<double, 3> boundsMin, std::array<double, 3> boundsMax);

    /// Replaces the influences of one vertex of a summary (rebuilds the offsets). Bone indices are not checked.
    void setInfluences(
        Body::MeshSummary& summary, std::uint32_t vertex, const std::vector<Body::MeshSummary::Influence>& influences);

    /// A valid humanoid: about 20 bones, an inline skeleton profile (canonical sources Pelvis, LeftFoot, RightFoot...),
    /// a rest skeleton, 5 meshes (indices: 0 "body" LOD 0 skin, 1 "body_lod1", 2 "head", 3 "hair" (not skinned),
    /// 4 "hands"), a morph set ("height", on mesh "body"), a region map over "body" (regions "torso" [0,4) and "legs"
    /// [4,8) of its 8 triangles) and a proxy. Zero findings.
    Body::BodyPackage humanoid();

    /// A valid non-humanoid: no pelvis, four legs, a tail chain, no feet in the canonical sense. Meshes 0 "body",
    /// 1 "head", 2 "tail". Has a proxy, a physics profile (shapes, masses, joint limits), measurements, landmarks and
    /// soft tissue. Zero findings, humanoidGait false.
    Body::BodyPackage tailedQuadruped();

    /// Every optional part present, one in each origin; the skeleton is a path (no inline profile, no rest skeleton).
    /// For serialization tests only: it is not meant to validate cleanly.
    Body::BodyPackage kitchenSink();

    /// A MeshSource that maps a path to a summary and remembers how it was asked.
    class FakeMeshSource : public Body::MeshSource
    {
    public:
        std::map<std::string, Body::MeshSummary> mSummaries;
        mutable std::vector<std::pair<std::string, std::string>> mCalls; // path, format

        std::optional<Body::MeshSummary> summarize(std::string_view path, std::string_view format) const override;
    };

    /// Moves every inline summary of the package into the source (keyed by the mesh's path).
    void moveSummariesToSource(Body::BodyPackage& package, FakeMeshSource& source);

    /// The text of a file in apps/components_tests/body/data with every '\r' removed (Windows checkouts convert).
    std::string readDataFile(const std::string& name);

    /// A fresh folder under the system's temp folder, removed (recursively) on destruction.
    class TempFolder
    {
    public:
        TempFolder();
        ~TempFolder();
        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;
        const std::filesystem::path& path() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    void writeTextFile(const std::filesystem::path& path, const std::string& text);
    std::string readTextFile(const std::filesystem::path& path);

    /// The codes of a report in report order.
    std::vector<std::string> codesOf(const Body::Report& report);
    /// The first finding with the code, or nullptr.
    const Body::Finding* findCode(const Body::Report& report, std::string_view code);
    /// The report as text, for failure messages.
    std::string describe(const Body::Report& report);

    /// 1-based line number of the first occurrence of needle in text (0 when absent).
    std::size_t lineOf(const std::string& text, std::string_view needle);
    /// Replaces the first occurrence; returns text unchanged (and sets ok false) when needle is absent.
    std::string replaceFirst(const std::string& text, std::string_view needle, std::string_view replacement, bool* ok);
    /// Removes the whole line (including its newline) holding the first occurrence of needle.
    std::string removeLineWith(const std::string& text, std::string_view needle, bool* ok);
    /// Inserts a line holding `line` after the line holding the first occurrence of needle.
    std::string insertLineAfter(const std::string& text, std::string_view needle, std::string_view line, bool* ok);
}

#endif
