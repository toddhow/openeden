#ifndef ADVANCEDBODIES_EDITOR_BODYVIEW_H
#define ADVANCEDBODIES_EDITOR_BODYVIEW_H

// The model of AdvancedBodies' body package panel (task 0069, stage f; the design is the spec's "Stage f, design").
// Pure: it reads files through components/body and returns plain data; no Qt, no Studio type, no engine type. Nothing
// here writes a file. Threads: main thread (no shared state; one view per call). Determinism: no clock, no randomness,
// no unordered iteration.
//
// Three sources open: a package folder (its body.json), a packed package (a .edenpak holding body.json) and a merged
// body cache (the folder's `current` pointer, or a merged-<digest>.edenpak holding manifest.txt). A package is one
// body; a merged cache has one body per manifest entry. For each body the view holds the package (for a merged entry,
// the one derived from what the cache knows, mergedEntryPackage), the validator's report, the mesh it can draw (only
// bodymesh payloads, bind pose, in metres), its regions with their colours, the region lines and weld points of the
// two gizmos, and for a merged entry the stage a numbers of its race and sex.

#include <components/body/bodycache.hpp>
#include <components/body/bodymerge.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodypackage.hpp>
#include <components/body/bodyvalidate.hpp>
#include <components/misc/constants.hpp>

#include <openeden/editor_module/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace advancedbodies_editor
{
    /// A merged cache stores the game's units (the bind space of the race's skeleton file); the package contract and
    /// the Editor Module API are metres. The one factor: Constants::UnitsPerMeter (components/misc/constants.hpp), the
    /// same one Studio multiplies by again when it draws a gizmo (CSVModule::ModuleUi, LineSink::convert).
    inline constexpr double kGameUnitsPerMetre = Constants::UnitsPerMeter;

    /// Drawing caps. The overlay a view draws holds 65,536 vertices (32,768 lines, apps/opencs/view/render/
    /// physicshub.cpp, cLineVertexCapacity); a weld point is three lines. A stock body has about 4,000 edges and 30
    /// weld groups.
    inline constexpr std::size_t kMaxRegionLines = 28000;
    inline constexpr std::size_t kMaxWeldPoints = 1000;
    /// The filled regions (Editor Module API 1.1): at most this many triangles of the drawn mesh are filled (a stock
    /// body has about 3,000; Studio's own cap is 60,000, CSMModule::kMaxOverlayTriangles), and their alpha. Under the
    /// edges' full alpha, so the surface behind a region shows through and the edges stay readable.
    inline constexpr std::size_t kMaxFillTriangles = 40000;
    inline constexpr float kFillAlpha = 0.35f;

    /// The stage a report reader's caps (its real size is about 1.4 MB for 20 bodies).
    inline constexpr std::uintmax_t kMaxAnalysisBytes = std::uintmax_t{ 64 } << 20;
    inline constexpr std::size_t kMaxAnalysisNesting = 32;
    inline constexpr std::size_t kMaxAnalysisUnits = 1024;
    inline constexpr std::size_t kMaxAnalysisSeams = 256;
    /// Values (commas, arrays, objects) counted before nlohmann builds anything: the real 20-body report holds about
    /// 58,000; a million is 17 times that and keeps a hostile 64 MiB file of `0,0,0...` from becoming a gigabyte tree
    /// (fresh review of stage f, finding 2).
    inline constexpr std::size_t kMaxAnalysisValues = 1000000;
    /// The longest text field read (a race, a slot or a seam label is a few dozen bytes; components/body caps 4096).
    inline constexpr std::size_t kMaxAnalysisTextBytes = 4096;

    inline constexpr std::size_t kRegionPaletteSize = 12;
    /// The colour of region `regionIndex`: the fixed palette of the design, by regionIndex % kRegionPaletteSize.
    openeden::editor::Colour regionColour(std::size_t regionIndex);
    /// Edges of triangles that no region holds.
    inline constexpr openeden::editor::Colour kUnassignedColour{ 0.45f, 0.45f, 0.45f, 1.0f };
    inline constexpr openeden::editor::Colour kWeldColour{ 1.0f, 0.2f, 0.9f, 1.0f };

    enum class SourceKind
    {
        PackageFolder,
        PackageFile,
        MergedCache,
    };

    struct RegionInfo
    {
        std::string mId;
        std::uint32_t mTriangles = 0; // triangles of the drawn mesh in this region (0 when nothing is drawn)
        openeden::editor::Colour mColour;
    };

    /// The mesh drawn: bind pose, never skinned, metres.
    struct DrawnMesh
    {
        std::string mMeshId; // the package's mesh id ("body" for a merged entry)
        std::vector<std::array<double, 3>> mPositions;
        std::vector<std::uint32_t> mTriangles; // three vertex indices per triangle
        std::vector<int> mTriangleRegion; // per triangle: an index into BodyData::mRegions, or -1
        std::vector<std::vector<std::uint32_t>> mWeldGroups; // the weld groups of this mesh, as the package has them
    };

    struct OverlayLine
    {
        std::array<double, 3> mFrom{};
        std::array<double, 3> mTo{};
        openeden::editor::Colour mColour;
    };

    /// One seam of the stage a report (game units, as the report has them).
    struct AnalysisSeam
    {
        std::string mLabel;
        std::string mSlotA;
        std::string mSlotB;
        bool mTouches = false;
        std::uint64_t mPairs = 0; // "distance": "count"
        double mMinDistance = 0;
        double mMedianDistance = 0;
        double mMaxDistance = 0;
        std::array<std::uint64_t, 6> mWithin{}; // "withinLadder": pairs within 0, 1e-4, 1e-3, 1e-2, 0.1, 0.5 units
        std::uint64_t mOverlapPairs = 0;
        std::uint64_t mGapPairs = 0;
        std::uint64_t mMeetingPairs = 0;
        double mMaxWeightDisagreement = 0; // "weightDisagreement": "max"
    };

    /// One unit (race and sex) of the stage a report.
    struct AnalysisUnit
    {
        std::string mRace;
        bool mFemale = false;
        bool mBeast = false;
        std::string mSkeleton;
        std::uint64_t mVertices = 0;
        std::uint64_t mTriangles = 0;
        std::uint64_t mBones = 0;
        std::uint64_t mMaterials = 0;
        std::uint64_t mFiles = 0;
        std::uint64_t mFileBytes = 0;
        std::uint64_t mSkinnedParts = 0;
        std::uint64_t mRigidParts = 0;
        std::vector<AnalysisSeam> mSeams; // in the report's order
    };

    struct AnalysisResult
    {
        std::optional<AnalysisUnit> mUnit; // empty: mError says why (no such unit, or a refused report)
        std::string mError;
    };

    /// Reads a stage a report ("format": "openeden.bodyanalysis", "version": 1) and returns the unit whose race equals
    /// `race` ignoring ASCII case and whose "female" equals `female`. Never throws; a refused report or no such unit
    /// gives mError (one line naming the field or the reason).
    AnalysisResult readAnalysisUnit(std::string_view text, std::string_view race, bool female);

    /// Where the stage a report of a merged cache is: <cacheFolder>/../../analysis/bodies.json.
    std::filesystem::path analysisReportFor(const std::filesystem::path& cacheFolder);

    /// Gives the validator the summary of a mesh of format "bodymesh" whose path is a file of the package (its
    /// payload as stored; positions are taken as metres, the package's unit). Any other mesh: no summary.
    class PayloadMeshSource final : public Body::MeshSource
    {
    public:
        explicit PayloadMeshSource(const Body::PackageFiles& files);
        std::optional<Body::MeshSummary> summarize(std::string_view path, std::string_view format) const override;

    private:
        const Body::PackageFiles& mFiles;
    };

    /// The summary of a merged body as the validator sees it, with every position divided by `unitsPerMetre`.
    Body::MeshSummary summarizeMergedBody(const Body::MergedBody& body, double unitsPerMetre);

    /// The package a merged cache entry stands for (see the design): identity, one target, no binding, mesh "body"
    /// with the payload's inline summary in metres, a derived region map ("slot-<n>"), derived weld groups and their
    /// provenance records.
    Body::BodyPackage mergedEntryPackage(
        const Body::CacheEntry& entry, const Body::MergedBody& payload, const Body::CacheManifest& manifest);

    /// One body as the panel shows it.
    struct BodyData
    {
        std::string mTitle; // "<race> <sex> <view>" for a merged entry, the package id otherwise
        std::optional<Body::BodyPackage> mPackage; // empty when a merged entry's payload does not read
        Body::Report mReport; // validateBodyPackage of mPackage (empty when there is none)
        std::string mPayloadError; // a merged entry's payload reader message; empty when it reads
        std::optional<DrawnMesh> mMesh;
        std::string mMeshNote; // why nothing is drawn, or what is drawn
        std::vector<RegionInfo> mRegions; // the package's region map, in its order
        std::vector<OverlayLine> mRegionLines; // at most kMaxRegionLines
        std::size_t mRegionLinesTotal = 0; // every distinct edge, before the cap
        std::vector<std::array<double, 3>> mWeldPoints; // at most kMaxWeldPoints
        std::size_t mWeldGroups = 0; // of the drawn mesh
        std::size_t mWeldMembers = 0;
        std::optional<AnalysisUnit> mAnalysis;
        std::string mAnalysisNote; // why there are no stage a numbers, or where they came from
    };

    class BodyView
    {
    public:
        SourceKind kind() const { return mKind; }
        const std::filesystem::path& path() const { return mPath; }

        /// 1 for a package; the number of manifest entries of a merged cache (at least 1).
        std::size_t bodyCount() const;
        /// Empty for a package.
        const std::vector<Body::CacheEntry>& entries() const { return mManifest.mEntries; }
        const std::vector<Body::CacheFailure>& failures() const { return mManifest.mFailures; }

        /// The data of body `index` (< bodyCount()), worked out the first time it is asked for. A bad index gives the
        /// selected body's data.
        const BodyData& body(std::size_t index) const;
        std::size_t selected() const { return mSelected; }
        /// False (and nothing changes) for an index >= bodyCount().
        bool select(std::size_t index);
        const BodyData& current() const { return body(mSelected); }

    private:
        friend struct BodyViewAccess;

        SourceKind mKind = SourceKind::PackageFolder;
        std::filesystem::path mPath;
        std::filesystem::path mCacheFolder; // a merged cache's folder (for the stage a report)
        Body::PackageFiles mFiles;
        Body::CacheManifest mManifest;
        std::size_t mSelected = 0;
        mutable std::vector<std::unique_ptr<BodyData>> mBodies;
    };

    struct OpenResult
    {
        std::unique_ptr<BodyView> mView; // empty: mError says why
        std::string mError;
    };

    /// A file named `current`: the merged cache of its folder; a *.edenpak: openPackageFile; a name ending in
    /// "body.json": the package folder that holds it. Anything else is refused.
    OpenResult openBodySource(const std::filesystem::path& path);
    /// A folder holding body.json (read with Body::readPackageFolder).
    OpenResult openPackageFolder(const std::filesystem::path& folder);
    /// A packed package (body.json in the pack) or a merged cache pack (manifest.txt in the pack).
    OpenResult openPackageFile(const std::filesystem::path& pack);
    /// The merged cache in `cacheFolder`, through its `current` pointer (Body::loadCurrentCache).
    OpenResult openMergedCache(const std::filesystem::path& cacheFolder);
}

#endif
