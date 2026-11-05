// The body package panel's asset document (task 0069, stage f): one opened body source, its panel rows, its inspector
// summary and its two gizmos. Read only: nothing here writes a file.

#include "documents.hpp"

#include "bodyview.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

namespace advancedbodies_editor
{
    using namespace openeden::editor;

    namespace
    {
        /// The size Studio is asked to draw a weld point at, in pixels (Studio draws a point as a small cross).
        constexpr float kWeldPointPixels = 6.f;
        /// Significant digits of the numbers the rows show.
        constexpr int kShownDigits = 6;

        std::string utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        std::string number(double value)
        {
            std::array<char, 64> buffer{};
            const std::to_chars_result result = std::to_chars(
                buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, kShownDigits);
            return result.ec == std::errc() ? std::string(buffer.data(), result.ptr) : std::string("?");
        }

        std::string hexColour(const Colour& colour)
        {
            const auto channel = [](float value) {
                const float clamped = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
                return static_cast<unsigned>(clamped * 255.f + 0.5f);
            };
            std::array<char, 8> text{};
            std::snprintf(
                text.data(), text.size(), "#%02x%02x%02x", channel(colour.r), channel(colour.g), channel(colour.b));
            return std::string(text.data());
        }

        std::string kindText(SourceKind kind)
        {
            switch (kind)
            {
                case SourceKind::PackageFolder:
                    return "package folder";
                case SourceKind::PackageFile:
                    return "packed package";
                case SourceKind::MergedCache:
                    return "merged body cache";
            }
            return "?";
        }

        PropertyDesc textRow(std::string id, std::string label, std::string group, std::string tooltip = {})
        {
            PropertyDesc desc;
            desc.id = std::move(id);
            desc.label = std::move(label);
            desc.type = PropertyType::String;
            desc.readOnly = true;
            desc.group = std::move(group);
            desc.tooltip = std::move(tooltip);
            return desc;
        }

        PropertyDesc countRow(std::string id, std::string label, std::string group)
        {
            PropertyDesc desc = textRow(std::move(id), std::move(label), std::move(group));
            desc.type = PropertyType::Int;
            desc.min = 0;
            return desc;
        }

        class BodyDocument;

        /// The rows of one body, made once: Studio builds a form's rows once and only reads the values again, so every
        /// body of a merged cache has a source of its own (the panel builds a new form when the address changes).
        class BodyProperties final : public PropertySource
        {
        public:
            BodyProperties(const BodyView& view, std::size_t index);

            std::vector<PropertyDesc> describe() const override
            {
                std::vector<PropertyDesc> descs;
                descs.reserve(mRows.size());
                for (const Row& row : mRows)
                {
                    descs.push_back(row.mDesc);
                }
                return descs;
            }

            PropertyValue get(std::string_view id) const override
            {
                for (const Row& row : mRows)
                {
                    if (row.mDesc.id == id)
                    {
                        return row.mValue;
                    }
                }
                return std::string();
            }

            /// Every row is read only. Which body is shown is not an edit (selectBody, the module's commands).
            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }

        private:
            struct Row
            {
                PropertyDesc mDesc;
                PropertyValue mValue;
            };

            void add(PropertyDesc desc, PropertyValue value) { mRows.push_back({ std::move(desc), std::move(value) }); }
            void addFindings(const BodyData& data);
            void addAnalysis(const BodyData& data);

            std::vector<Row> mRows;
        };

        /// The fixed short summary the inspector shows: the same rows for the document's life, the selected body's
        /// values.
        class SummaryProperties final : public PropertySource
        {
        public:
            explicit SummaryProperties(const BodyDocument& document)
                : mDocument(document)
            {
            }

            std::vector<PropertyDesc> describe() const override
            {
                const std::string group = "Body";
                return { textRow("summary.kind", "Source", group), textRow("summary.body", "Body", group),
                    countRow("summary.errors", "Errors", group), countRow("summary.warnings", "Warnings", group),
                    countRow("summary.vertices", "Vertices", group), countRow("summary.triangles", "Triangles", group),
                    countRow("summary.regions", "Regions", group),
                    countRow("summary.weldGroups", "Weld groups", group) };
            }

            PropertyValue get(std::string_view id) const override;

            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }

        private:
            const BodyDocument& mDocument;
        };

        /// The region lines of the selected body, in metres at the body's own origin.
        class RegionsGizmo final : public Gizmo
        {
        public:
            explicit RegionsGizmo(const BodyDocument& document)
                : mDocument(document)
            {
            }
            std::string_view id() const override { return "advancedbodies.body.regions"; }
            void draw(OverlaySink& sink) const override;

        private:
            const BodyDocument& mDocument;
        };

        /// One point per weld group of the selected body.
        class WeldsGizmo final : public Gizmo
        {
        public:
            explicit WeldsGizmo(const BodyDocument& document)
                : mDocument(document)
            {
            }
            std::string_view id() const override { return "advancedbodies.body.welds"; }
            void draw(OverlaySink& sink) const override;

        private:
            const BodyDocument& mDocument;
        };

        class BodyDocument final : public AssetDocument
        {
        public:
            BodyDocument(std::unique_ptr<BodyView> view, const std::filesystem::path& path)
                : mView(std::move(view))
                , mAssetId(utf8(path))
                , mPanels(mView->bodyCount())
                , mSummary(*this)
                , mRegions(*this)
                , mWelds(*this)
            {
            }

            std::string_view assetId() const override { return mAssetId; }

            PropertySource& properties() override
            {
                std::unique_ptr<BodyProperties>& panel = mPanels[mView->selected()];
                if (!panel)
                {
                    panel = std::make_unique<BodyProperties>(*mView, mView->selected());
                }
                return *panel;
            }

            std::vector<Gizmo*> gizmos() override { return { &mRegions, &mWelds }; }
            bool isDirty() const override { return false; }
            bool save(std::string& error) override
            {
                error = "the body package panel only reads: it never writes a file";
                return false;
            }

            const BodyView& view() const { return *mView; }
            RegionDisplay display() const { return mDisplay; }
            void setDisplay(RegionDisplay display) { mDisplay = display; }
            bool selectBody(std::size_t index) { return mView->select(index); }
            PropertySource& summary() { return mSummary; }

        private:
            std::unique_ptr<BodyView> mView;
            std::string mAssetId;
            std::vector<std::unique_ptr<BodyProperties>> mPanels; // one per body, made when first shown
            SummaryProperties mSummary;
            RegionsGizmo mRegions;
            WeldsGizmo mWelds;
            RegionDisplay mDisplay = RegionDisplay::FillAndEdges;
        };

        BodyProperties::BodyProperties(const BodyView& view, std::size_t index)
        {
            const BodyData& data = view.body(index);
            const bool merged = view.kind() == SourceKind::MergedCache;

            const std::string source = "Source";
            add(textRow("source.kind", "Kind", source), kindText(view.kind()));
            add(textRow("source.path", "Path", source), utf8(view.path()));
            if (merged)
            {
                add(textRow("source.entry", "Body", source,
                        "The body of the merged cache the panel and the 3D views show: Extensions > "
                        "advancedbodies_editor > Bodies > Next Body or Previous Body shows another."),
                    data.mTitle);
                add(textRow("source.position", "Of", source),
                    std::to_string(index + 1) + " of " + std::to_string(view.bodyCount()));
                add(countRow("source.failures", "Failed in the bake", source),
                    static_cast<std::int64_t>(view.failures().size()));
            }
            else
            {
                add(textRow("source.package", "Package", source), data.mTitle);
            }

            const std::string validator = "Validator";
            const Body::Report& report = data.mReport;
            if (merged)
            {
                add(textRow("payload.status", "Payload", validator),
                    data.mPayloadError.empty() ? std::string("reads (canonical bodymesh payload)")
                                               : data.mPayloadError);
            }
            const int errors = report.errorCount();
            const int warnings = report.warningCount();
            const std::string verdict = !data.mPackage
                ? std::string("not validated: there is no package")
                : std::to_string(errors) + " error(s), " + std::to_string(warnings) + " warning(s)";
            add(textRow("report.verdict", "Verdict", validator), verdict);
            add(countRow("report.errors", "Errors", validator), static_cast<std::int64_t>(errors));
            add(countRow("report.warnings", "Warnings", validator), static_cast<std::int64_t>(warnings));
            if (merged && data.mPackage)
            {
                add(textRow("report.note", "Note", validator,
                        "A merged cache entry records no skeleton binding: the engine binds the body by bone name to "
                        "its race's skeleton."),
                    std::string("BODY-BIND-001 and BODY-BIND-014 are expected for a cache entry"));
            }
            addFindings(data);

            const std::string mesh = "Mesh";
            add(textRow("mesh.note", "Drawn", mesh), data.mMeshNote);
            if (data.mMesh)
            {
                add(countRow("mesh.vertices", "Vertices", mesh),
                    static_cast<std::int64_t>(data.mMesh->mPositions.size()));
                add(countRow("mesh.triangles", "Triangles", mesh),
                    static_cast<std::int64_t>(data.mMesh->mTriangles.size() / 3));
                add(countRow("mesh.edges", "Edges drawn", mesh), static_cast<std::int64_t>(data.mRegionLines.size()));
            }

            const std::string regions = "Regions (" + std::to_string(data.mRegions.size()) + ")";
            for (std::size_t i = 0; i < data.mRegions.size(); ++i)
            {
                const RegionInfo& region = data.mRegions[i];
                add(textRow("region." + std::to_string(i), region.mId, regions),
                    std::to_string(region.mTriangles) + " triangles, colour " + hexColour(region.mColour));
            }

            const std::string welds = "Weld groups";
            add(countRow("weld.groups", "Groups", welds), static_cast<std::int64_t>(data.mWeldGroups));
            add(countRow("weld.members", "Members", welds), static_cast<std::int64_t>(data.mWeldMembers));
            add(countRow("weld.points", "Points drawn", welds), static_cast<std::int64_t>(data.mWeldPoints.size()));

            addAnalysis(data);
        }

        void BodyProperties::addFindings(const BodyData& data)
        {
            const std::vector<Body::Finding>& findings = data.mReport.mFindings;
            const std::string group = "Findings (" + std::to_string(findings.size()) + ")";
            const std::size_t shown = std::min(findings.size(), kMaxFindingRows);
            for (std::size_t i = 0; i < shown; ++i)
            {
                const Body::Finding& finding = findings[i];
                const std::string severity = finding.mSeverity == Body::Severity::Error ? "error" : "warning";
                const std::string text = finding.mPath + ": " + finding.mMessage;
                // A read-only row is a disabled line edit that cannot scroll: the tooltip holds the whole text.
                add(textRow("finding." + std::to_string(i), severity + " " + finding.mCode, group, text), text);
            }
            if (findings.size() > shown)
            {
                add(textRow("finding.more", "More", group),
                    std::to_string(findings.size() - shown) + " more findings are not listed");
            }
        }

        void BodyProperties::addAnalysis(const BodyData& data)
        {
            const std::string group = "Stage a analysis";
            add(textRow("analysis.note", "Report", group), data.mAnalysisNote);
            if (!data.mAnalysis)
            {
                return;
            }
            const AnalysisUnit& unit = *data.mAnalysis;
            add(textRow("analysis.unit", "Unit", group),
                unit.mRace + (unit.mFemale ? " female" : " male") + (unit.mBeast ? ", beast" : "") + ", skeleton "
                    + unit.mSkeleton);
            add(countRow("analysis.vertices", "Stock vertices", group), static_cast<std::int64_t>(unit.mVertices));
            add(countRow("analysis.triangles", "Stock triangles", group), static_cast<std::int64_t>(unit.mTriangles));
            add(countRow("analysis.bones", "Bones", group), static_cast<std::int64_t>(unit.mBones));
            add(countRow("analysis.materials", "Materials", group), static_cast<std::int64_t>(unit.mMaterials));
            add(countRow("analysis.files", "Files", group), static_cast<std::int64_t>(unit.mFiles));
            add(countRow("analysis.fileBytes", "File bytes", group), static_cast<std::int64_t>(unit.mFileBytes));
            add(countRow("analysis.skinnedParts", "Skinned parts", group),
                static_cast<std::int64_t>(unit.mSkinnedParts));
            add(countRow("analysis.rigidParts", "Rigid parts", group), static_cast<std::int64_t>(unit.mRigidParts));
            const std::string seams = "Stage a seams (game units)";
            for (std::size_t i = 0; i < unit.mSeams.size(); ++i)
            {
                const AnalysisSeam& seam = unit.mSeams[i];
                std::string within;
                for (std::size_t k = 0; k < seam.mWithin.size(); ++k)
                {
                    within += (k == 0 ? "" : " / ") + std::to_string(seam.mWithin[k]);
                }
                add(textRow("analysis.seam." + std::to_string(i), seam.mLabel, seams,
                        "Pairs within 0, 1e-4, 1e-3, 1e-2, 0.1 and 0.5 units; distances min / median / max."),
                    seam.mSlotA + "-" + seam.mSlotB + (seam.mTouches ? ", touches" : ", apart") + ", "
                        + std::to_string(seam.mPairs) + " pairs, distance " + number(seam.mMinDistance) + " / "
                        + number(seam.mMedianDistance) + " / " + number(seam.mMaxDistance) + ", within " + within
                        + ", overlap " + std::to_string(seam.mOverlapPairs) + ", gap " + std::to_string(seam.mGapPairs)
                        + ", meeting " + std::to_string(seam.mMeetingPairs) + ", weight disagreement up to "
                        + number(seam.mMaxWeightDisagreement));
            }
        }

        PropertyValue SummaryProperties::get(std::string_view id) const
        {
            const BodyView& view = mDocument.view();
            const BodyData& data = view.current();
            if (id == "summary.kind")
            {
                return kindText(view.kind());
            }
            if (id == "summary.body")
            {
                return data.mTitle;
            }
            if (id == "summary.errors")
            {
                return static_cast<std::int64_t>(data.mReport.errorCount());
            }
            if (id == "summary.warnings")
            {
                return static_cast<std::int64_t>(data.mReport.warningCount());
            }
            if (id == "summary.vertices")
            {
                return static_cast<std::int64_t>(data.mMesh ? data.mMesh->mPositions.size() : 0);
            }
            if (id == "summary.triangles")
            {
                return static_cast<std::int64_t>(data.mMesh ? data.mMesh->mTriangles.size() / 3 : 0);
            }
            if (id == "summary.regions")
            {
                return static_cast<std::int64_t>(data.mRegions.size());
            }
            if (id == "summary.weldGroups")
            {
                return static_cast<std::int64_t>(data.mWeldGroups);
            }
            return std::string();
        }

        void RegionsGizmo::draw(OverlaySink& sink) const
        {
            const BodyData& data = mDocument.view().current();
            const RegionDisplay display = mDocument.display();
            if (display != RegionDisplay::Edges && data.mMesh)
            {
                // Editor Module API 1.1: each triangle of the drawn mesh filled with its region's colour, see through.
                const DrawnMesh& mesh = *data.mMesh;
                const std::size_t count = std::min(mesh.mTriangles.size() / 3, kMaxFillTriangles);
                for (std::size_t t = 0; t < count; ++t)
                {
                    const std::array<double, 3>* corners[3] = { nullptr, nullptr, nullptr };
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        const std::uint32_t index = mesh.mTriangles[3 * t + c];
                        corners[c] = index < mesh.mPositions.size() ? &mesh.mPositions[index] : nullptr;
                    }
                    if (corners[0] == nullptr || corners[1] == nullptr || corners[2] == nullptr)
                    {
                        continue;
                    }
                    const int region = t < mesh.mTriangleRegion.size() ? mesh.mTriangleRegion[t] : -1;
                    Colour colour = region >= 0 ? regionColour(static_cast<std::size_t>(region)) : kUnassignedColour;
                    colour.a = kFillAlpha;
                    sink.triangle(Vec3{ (*corners[0])[0], (*corners[0])[1], (*corners[0])[2] },
                        Vec3{ (*corners[1])[0], (*corners[1])[1], (*corners[1])[2] },
                        Vec3{ (*corners[2])[0], (*corners[2])[1], (*corners[2])[2] }, colour, OverlayDepth::Tested);
                }
            }
            if (display == RegionDisplay::Fill)
            {
                return;
            }
            for (const OverlayLine& line : data.mRegionLines)
            {
                sink.line(Vec3{ line.mFrom[0], line.mFrom[1], line.mFrom[2] },
                    Vec3{ line.mTo[0], line.mTo[1], line.mTo[2] }, line.mColour);
            }
        }

        void WeldsGizmo::draw(OverlaySink& sink) const
        {
            for (const std::array<double, 3>& point : mDocument.view().current().mWeldPoints)
            {
                sink.point(Vec3{ point[0], point[1], point[2] }, kWeldColour, kWeldPointPixels);
            }
        }
    }

    std::unique_ptr<AssetDocument> openBodyAsset(const std::filesystem::path& path, std::string& error)
    {
        OpenResult result = openBodySource(path);
        if (!result.mView)
        {
            error = result.mError;
            return nullptr;
        }
        return std::make_unique<BodyDocument>(std::move(result.mView), path);
    }

    const BodyView* viewOf(const AssetDocument& document)
    {
        const auto* body = dynamic_cast<const BodyDocument*>(&document);
        return body != nullptr ? &body->view() : nullptr;
    }

    PropertySource* summaryOf(AssetDocument& document)
    {
        auto* body = dynamic_cast<BodyDocument*>(&document);
        return body != nullptr ? &body->summary() : nullptr;
    }

    RegionDisplay regionDisplay(const AssetDocument& document)
    {
        const auto* body = dynamic_cast<const BodyDocument*>(&document);
        return body != nullptr ? body->display() : RegionDisplay::FillAndEdges;
    }

    bool setRegionDisplay(AssetDocument& document, RegionDisplay display)
    {
        auto* body = dynamic_cast<BodyDocument*>(&document);
        if (body == nullptr)
        {
            return false;
        }
        body->setDisplay(display);
        return true;
    }

    bool selectBody(AssetDocument& document, std::size_t index)
    {
        auto* body = dynamic_cast<BodyDocument*>(&document);
        return body != nullptr && body->selectBody(index);
    }
}
