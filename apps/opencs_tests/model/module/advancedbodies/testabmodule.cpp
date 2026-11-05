// Task 0069 stage f: the advancedbodies_editor documents and the module as Eden Studio loads it (documents.hpp),
// written from the design alone (the section "Stage f, design" of
// docs/tasks/0069-advancedbodies-merged-legacy-body.md), including the amendment "Choosing the body of a merged
// cache" (2026-11-04): every row is read only, a body is chosen with selectBody or the two module commands
// advancedbodies.nextBody / advancedbodies.previousBody, and choosing never touches the undo stack.
//
// Assumptions where the design left room (see the report):
//  - Row ids are numbered from 0: "finding.0", "region.0". A finding row's label is "<error|warning> <code>" and its
//    value the text "<path>: <message>"; a region row's label is the region id and its value text holding
//    "<n> triangles". "finding.more" exists only when the report has more than kMaxFindingRows findings.
//  - "report.errors" and "report.warnings" hold the counts as an Int, a whole Float or a text of digits.
//  - "source.entry" (merged cache only, read only) holds the shown body's title as text.
//  - The panel and the gizmos read the document's selected body: properties() returns that body's own source (the
//    same object every time it is selected), gizmos() draw that body's lines and points; gizmos() is asked again
//    after a change of selection.
//  - selectBody(document, index) with the index already selected returns true (it is in range) and changes nothing;
//    for a package, index 0 is in range and 1 is not.
//  - The two commands are runnable through ModuleRegistry::runCommand only while enabled (the registry checks
//    enabledWhen, so a disabled run is false and runs nothing); an enabled run moves the selection by exactly one
//    and calls requestRedraw() at least once (counted as "more than before"); a disabled run calls neither.
//    Neither command ever calls pushCommand or markDirty. Their labels are not pinned.
//  - Review fixes (2026-11-04): a finding row's PropertyDesc::tooltip is exactly its value text. The rows added by the
//    review: "source.position" (text "<i> of <n>"; whether i counts from 0 or 1 is not pinned, only " of <n>" and
//    that another body gives another text) and "source.failures" exist for a merged cache and not for a package;
//    "source.package" for a package and not for a merged cache; "report.note" for a merged cache entry.
//  - The region gizmo draws only lines (no points or labels), the weld gizmo only points (no lines or labels), in
//    metres at the body's own origin, exactly the view's mRegionLines and mWeldPoints.
//  - The inspector section ids are "advancedbodies.section.<format id>"; a section of this module shows
//    summaryOf(document). The panel's title is "Body Package" and it docks right.
//  - summaryOf(document) is the same object for the document's life, also across a change of the selected entry.
//  - Documents made by the shared library are never passed to viewOf or summaryOf here (RTTI across a library
//    boundary is not part of the contract).
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/advancedbodies_editor/bodyview.hpp"
#include "editor_modules/advancedbodies_editor/documents.hpp"

#include "abtestsupport.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ABTest
{
    namespace
    {
        using CSMModule::ModuleHost;
        using CSMModule::ModuleInfo;
        using CSMModule::ModuleRegistry;
        using CSMModule::ModuleSource;
        using edit::AssetDocument;
        using edit::Command;
        using edit::EditorContext;
        using edit::Gizmo;
        using edit::PropertyDesc;
        using edit::PropertySource;
        using edit::PropertyValue;

        bool hasModulePrefix(const std::string& id)
        {
            return id == "advancedbodies" || id.rfind("advancedbodies.", 0) == 0;
        }

        void expectEveryIdHasThePrefix(const ModuleRegistry& registry)
        {
            for (const auto& entry : registry.workspaces())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
            for (const auto& entry : registry.panels())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
            for (const auto& entry : registry.commands())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
            for (const auto& entry : registry.gizmoProviders())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
            for (const auto& entry : registry.sections())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
            for (const auto& entry : registry.trackProviders())
            {
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            }
        }

        void expectRegistryEmpty(const ModuleRegistry& registry)
        {
            EXPECT_TRUE(registry.workspaces().empty());
            EXPECT_TRUE(registry.panels().empty());
            EXPECT_TRUE(registry.commands().empty());
            EXPECT_TRUE(registry.gizmoProviders().empty());
            EXPECT_TRUE(registry.assetTypes().empty());
            EXPECT_TRUE(registry.sections().empty());
            EXPECT_TRUE(registry.trackProviders().empty());
        }

        class ActiveAssetContext : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command> command) override
            {
                command->redo();
                mPushed.push_back(std::move(command));
            }
            void markDirty(std::string_view) override { ++mDirtyMarks; }
            void log(edit::LogLevel, std::string_view) override {}
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }

            AssetDocument* mActive = nullptr;
            std::vector<std::unique_ptr<Command>> mPushed;
            int mRedraws = 0;
            int mDirtyMarks = 0;
        };

        constexpr const char* kNextBody = "advancedbodies.nextBody";
        constexpr const char* kPreviousBody = "advancedbodies.previousBody";

        /// Another module's document.
        class ForeignProperties final : public PropertySource
        {
        public:
            std::vector<PropertyDesc> describe() const override { return {}; }
            PropertyValue get(std::string_view) const override { return std::string(); }
            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }
        };

        class ForeignDocument final : public AssetDocument
        {
        public:
            std::string_view assetId() const override { return "foreign"; }
            PropertySource& properties() override { return mProperties; }
            bool isDirty() const override { return false; }
            bool save(std::string&) override { return false; }

        private:
            ForeignProperties mProperties;
        };

        const PropertyDesc* findDesc(const std::vector<PropertyDesc>& descs, std::string_view id)
        {
            const auto it = std::find_if(descs.begin(), descs.end(), [&](const PropertyDesc& d) { return d.id == id; });
            return it == descs.end() ? nullptr : &*it;
        }

        const Gizmo* findGizmo(const std::vector<Gizmo*>& gizmos, std::string_view id)
        {
            for (const Gizmo* gizmo : gizmos)
            {
                if (gizmo != nullptr && gizmo->id() == id)
                {
                    return gizmo;
                }
            }
            return nullptr;
        }

        /// A count shown as an Int, a whole Float or a text of digits; -1 for anything else.
        long long countOf(const PropertyValue& value)
        {
            if (const auto* integer = std::get_if<std::int64_t>(&value))
            {
                return static_cast<long long>(*integer);
            }
            if (const auto* real = std::get_if<double>(&value))
            {
                return *real == std::floor(*real) ? static_cast<long long>(*real) : -1;
            }
            if (const auto* text = std::get_if<std::string>(&value))
            {
                long long parsed = -1;
                const char* end = text->data() + text->size();
                const std::from_chars_result result = std::from_chars(text->data(), end, parsed);
                return (result.ec == std::errc() && result.ptr == end) ? parsed : -1;
            }
            return -1;
        }

        /// One "finding.<i>" row per finding of the report in order (at most kMaxFindingRows), then "finding.more"
        /// when there are more.
        void expectFindingRows(const PropertySource& source, const Body::Report& report)
        {
            const std::vector<PropertyDesc> descs = source.describe();
            const std::size_t shown = std::min(report.mFindings.size(), ab::kMaxFindingRows);
            for (std::size_t i = 0; i < shown; ++i)
            {
                const Body::Finding& finding = report.mFindings[i];
                const std::string id = "finding." + std::to_string(i);
                const PropertyDesc* desc = findDesc(descs, id);
                if (desc == nullptr)
                {
                    ADD_FAILURE() << "no row " << id;
                    continue;
                }
                const std::string severity = finding.mSeverity == Body::Severity::Error ? "error" : "warning";
                EXPECT_EQ(desc->label, severity + " " + finding.mCode) << id;
                EXPECT_TRUE(desc->readOnly) << id;
                const PropertyValue value = source.get(id);
                const std::string* text = std::get_if<std::string>(&value);
                if (text == nullptr)
                {
                    ADD_FAILURE() << id << " is not text";
                    continue;
                }
                EXPECT_EQ(*text, finding.mPath + ": " + finding.mMessage) << id;
            }
            EXPECT_EQ(findDesc(descs, "finding." + std::to_string(shown)), nullptr) << "a row past the findings";
            const bool more = report.mFindings.size() > ab::kMaxFindingRows;
            EXPECT_EQ(findDesc(descs, "finding.more") != nullptr, more);
        }

        /// One "region.<i>" row per region of the body, labelled with its id.
        void expectRegionRows(const PropertySource& source, const ab::BodyData& data)
        {
            const std::vector<PropertyDesc> descs = source.describe();
            for (std::size_t i = 0; i < data.mRegions.size(); ++i)
            {
                const std::string id = "region." + std::to_string(i);
                const PropertyDesc* desc = findDesc(descs, id);
                if (desc == nullptr)
                {
                    ADD_FAILURE() << "no row " << id;
                    continue;
                }
                EXPECT_EQ(desc->label, data.mRegions[i].mId) << id;
                EXPECT_TRUE(desc->readOnly) << id;
                const PropertyValue value = source.get(id);
                const std::string* text = std::get_if<std::string>(&value);
                if (text == nullptr)
                {
                    ADD_FAILURE() << id << " is not text";
                    continue;
                }
                EXPECT_TRUE(contains(*text, std::to_string(data.mRegions[i].mTriangles) + " triangles")) << *text;
            }
            EXPECT_EQ(findDesc(descs, "region." + std::to_string(data.mRegions.size())), nullptr);
        }

        /// The two gizmos draw exactly the body's region lines and weld points.
        void expectGizmosDraw(AssetDocument& document, const ab::BodyData& data)
        {
            const std::vector<Gizmo*> gizmos = document.gizmos();
            ASSERT_EQ(gizmos.size(), 2u);
            const Gizmo* regions = findGizmo(gizmos, "advancedbodies.body.regions");
            const Gizmo* welds = findGizmo(gizmos, "advancedbodies.body.welds");
            ASSERT_NE(regions, nullptr);
            ASSERT_NE(welds, nullptr);

            RecordingSink regionSink;
            regions->draw(regionSink);
            ASSERT_EQ(regionSink.mLines.size(), data.mRegionLines.size());
            for (std::size_t i = 0; i < regionSink.mLines.size(); ++i)
            {
                const DrawnLine& drawn = regionSink.mLines[i];
                const ab::OverlayLine& line = data.mRegionLines[i];
                EXPECT_TRUE(toPoint(drawn.mA) == line.mFrom) << "line " << i;
                EXPECT_TRUE(toPoint(drawn.mB) == line.mTo) << "line " << i;
                EXPECT_TRUE(sameColour(drawn.mColour, line.mColour)) << "line " << i;
            }
            EXPECT_TRUE(regionSink.mPoints.empty());
            EXPECT_EQ(regionSink.mLabels, 0);

            RecordingSink weldSink;
            welds->draw(weldSink);
            EXPECT_TRUE(weldSink.mLines.empty());
            EXPECT_EQ(weldSink.mLabels, 0);
            ASSERT_EQ(weldSink.mPoints.size(), data.mWeldPoints.size());
            for (std::size_t i = 0; i < weldSink.mPoints.size(); ++i)
            {
                EXPECT_TRUE(toPoint(weldSink.mPoints[i].mPosition) == data.mWeldPoints[i]) << "point " << i;
                EXPECT_TRUE(sameColour(weldSink.mPoints[i].mColour, kDesignWeld)) << "point " << i;
            }
        }

        std::vector<std::string> titlesOf(const std::vector<Body::CacheEntry>& entries)
        {
            std::vector<std::string> titles;
            for (const Body::CacheEntry& entry : entries)
            {
                titles.push_back(titleOf(entry));
            }
            return titles;
        }

        class AbModuleTest : public AbTempTest
        {
        protected:
            void SetUp() override
            {
                AbTempTest::SetUp();
                mHost = std::make_unique<ModuleHost>(mRegistry, mContext);
            }

            void TearDown() override
            {
                mContext.mActive = nullptr;
                mDocument.reset();
                mHost.reset();
                AbTempTest::TearDown();
            }

            void loadStatic()
            {
                const edit::EditorModuleDescriptor* descriptor = ab::moduleDescriptor();
                ASSERT_NE(descriptor, nullptr);
                mHost->loadStatic(ab::kModuleId, *descriptor, ModuleSource::Origin::Core);
            }

            /// The standard package as a folder; returns its body.json.
            std::filesystem::path writeStandardPackage()
            {
                return writeFolder(mDir / "package", makePackageFixture().mFiles);
            }

            /// The standard package as a pack.
            std::filesystem::path writeStandardPack()
            {
                return writePack(mDir / "package.edenpak", makePackageFixture().mFiles);
            }

            /// The standard merged cache (three bodies); returns its folder and sets `packName`.
            std::filesystem::path writeStandardCache(std::string& packName)
            {
                const std::filesystem::path folder = mDir / "cache";
                packName = publishBodies(folder, standardCacheBodies());
                return folder;
            }

            std::unique_ptr<AssetDocument> openAsset(const std::filesystem::path& path)
            {
                std::string error;
                std::unique_ptr<AssetDocument> document = ab::openBodyAsset(path, error);
                EXPECT_NE(document, nullptr) << path << ": " << error;
                return document;
            }

            ModuleRegistry mRegistry;
            ActiveAssetContext mContext;
            std::unique_ptr<ModuleHost> mHost;
            std::unique_ptr<AssetDocument> mDocument;
        };

        // ---- ids ------------------------------------------------------------------------------------------------

        TEST(AbModuleIds, theFormatsAndSuffixesAreTheDesigns)
        {
            EXPECT_STREQ(ab::kModuleId, "advancedbodies_editor");
            EXPECT_STREQ(ab::kPackageFormat, "openeden.body");
            EXPECT_STREQ(ab::kPackFormat, "openeden.bodypack");
            EXPECT_STREQ(ab::kCacheFormat, "openeden.bodycache");
            EXPECT_STREQ(ab::kPackageSuffix, "body.json");
            EXPECT_STREQ(ab::kPackSuffix, ".edenpak");
            EXPECT_STREQ(ab::kCacheSuffix, "current");
            EXPECT_EQ(ab::kMaxFindingRows, 200u);
        }

        // ---- documents ------------------------------------------------------------------------------------------

        TEST_F(AbModuleTest, openBodyAssetOpensEverySourceReadOnly)
        {
            std::string pack;
            const std::filesystem::path cache = writeStandardCache(pack);
            const std::vector<std::filesystem::path> sources{ writeStandardPackage(), writeStandardPack(),
                cache / "current", cache / pack };
            const std::vector<ab::SourceKind> kinds{ ab::SourceKind::PackageFolder, ab::SourceKind::PackageFile,
                ab::SourceKind::MergedCache, ab::SourceKind::MergedCache };
            for (std::size_t i = 0; i < sources.size(); ++i)
            {
                std::string error;
                const std::unique_ptr<AssetDocument> document = ab::openBodyAsset(sources[i], error);
                ASSERT_NE(document, nullptr) << sources[i] << ": " << error;
                EXPECT_FALSE(document->isDirty());
                EXPECT_FALSE(document->assetId().empty());
                std::string saveError;
                EXPECT_FALSE(document->save(saveError));
                EXPECT_FALSE(saveError.empty());
                EXPECT_FALSE(document->isDirty());
                const ab::BodyView* view = ab::viewOf(*document);
                ASSERT_NE(view, nullptr);
                EXPECT_EQ(view->kind(), kinds[i]) << sources[i];
            }
        }

        TEST_F(AbModuleTest, openBodyAssetRefusesWhatCannotBeOpened)
        {
            std::string error;
            EXPECT_EQ(ab::openBodyAsset(mDir / "missing" / "body.json", error), nullptr);
            EXPECT_FALSE(error.empty());

            error.clear();
            writeText(mDir / "notes.txt", "hello\n");
            EXPECT_EQ(ab::openBodyAsset(mDir / "notes.txt", error), nullptr);
            EXPECT_FALSE(error.empty());

            error.clear();
            writeText(mDir / "cache" / "current", "not a pack name\n");
            EXPECT_EQ(ab::openBodyAsset(mDir / "cache" / "current", error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        TEST_F(AbModuleTest, theDocumentsViewAgreesWithOpenBodySource)
        {
            std::string pack;
            const std::filesystem::path cache = writeStandardCache(pack);
            for (const std::filesystem::path& source : { writeStandardPackage(), cache / "current" })
            {
                const std::unique_ptr<AssetDocument> document = openAsset(source);
                ASSERT_NE(document, nullptr);
                const ab::BodyView* view = ab::viewOf(*document);
                ASSERT_NE(view, nullptr);
                const ab::OpenResult direct = ab::openBodySource(source);
                ASSERT_NE(direct.mView, nullptr) << direct.mError;
                EXPECT_EQ(view->kind(), direct.mView->kind());
                EXPECT_TRUE(samePath(view->path(), direct.mView->path())) << view->path();
                EXPECT_EQ(view->bodyCount(), direct.mView->bodyCount());
                EXPECT_TRUE(view->entries() == direct.mView->entries());
                EXPECT_EQ(view->selected(), 0u);
                EXPECT_EQ(view->current().mTitle, direct.mView->current().mTitle);
                expectSameReport(view->current().mReport, direct.mView->current().mReport);
                expectSameLines(view->current().mRegionLines, direct.mView->current().mRegionLines);
            }
        }

        TEST(AbModuleForeign, viewOfAndSummaryOfAreNullForAnotherModulesDocument)
        {
            ForeignDocument foreign;
            EXPECT_EQ(ab::viewOf(foreign), nullptr);
            EXPECT_EQ(ab::summaryOf(foreign), nullptr);
        }

        TEST_F(AbModuleTest, theSummaryIsOneObjectForTheDocumentsLife)
        {
            const std::unique_ptr<AssetDocument> package = openAsset(writeStandardPackage());
            ASSERT_NE(package, nullptr);
            PropertySource* first = ab::summaryOf(*package);
            ASSERT_NE(first, nullptr);
            EXPECT_EQ(ab::summaryOf(*package), first);
            EXPECT_FALSE(first->describe().empty());

            std::string pack;
            const std::unique_ptr<AssetDocument> cache = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(cache, nullptr);
            PropertySource* summary = ab::summaryOf(*cache);
            ASSERT_NE(summary, nullptr);
            const ab::BodyView* view = ab::viewOf(*cache);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            ASSERT_TRUE(ab::selectBody(*cache, 1));
            ASSERT_EQ(view->selected(), 1u);
            EXPECT_EQ(ab::summaryOf(*cache), summary);
        }

        // ---- the panel's rows -----------------------------------------------------------------------------------

        TEST_F(AbModuleTest, aPackagesRowsShowItsSourceReportFindingsAndRegionsAllReadOnly)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mRegionMap->mRegions[0].mRanges = { Body::TriangleRange{ 0, 1 } }; // a gap: GEOM-005
            fixture.refresh();
            const std::unique_ptr<AssetDocument> document = openAsset(writeFolder(mDir / "package", fixture.mFiles));
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            const Body::Report& report = view->current().mReport;
            ASSERT_FALSE(report.mFindings.empty());

            PropertySource& source = document->properties();
            const std::vector<PropertyDesc> descs = source.describe();
            for (const char* id : { "source.kind", "source.path", "report.errors", "report.warnings" })
            {
                EXPECT_NE(findDesc(descs, id), nullptr) << id;
            }
            EXPECT_EQ(findDesc(descs, "source.entry"), nullptr) << "a package has one body";
            std::set<std::string> ids;
            for (const PropertyDesc& desc : descs)
            {
                EXPECT_TRUE(desc.readOnly) << desc.id;
                EXPECT_TRUE(ids.insert(desc.id).second) << "the row " << desc.id << " is listed twice";
            }
            EXPECT_EQ(countOf(source.get("report.errors")), static_cast<long long>(report.errorCount()));
            EXPECT_EQ(countOf(source.get("report.warnings")), static_cast<long long>(report.warningCount()));
            expectFindingRows(source, report);
            expectRegionRows(source, view->current());

            // No row can be set.
            for (const PropertyDesc& desc : descs)
            {
                EXPECT_EQ(source.makeSetCommand(desc.id, source.get(desc.id)), nullptr) << desc.id;
            }
            EXPECT_EQ(source.makeSetCommand("source.entry", PropertyValue{ std::string(kPackageId) }), nullptr);
        }

        TEST_F(AbModuleTest, findingRowsStopAtTheCapWithARowSayingThereAreMore)
        {
            PackageFixture fixture = makePackageFixture();
            Body::WeldGroups welds;
            welds.mMesh = "body";
            for (std::uint32_t g = 0; g < 300; ++g)
            {
                welds.mGroups.push_back({ 1000 + 2 * g, 1001 + 2 * g }); // every group past the 8 vertices: WELD-005
            }
            fixture.mPackage.mWeldGroups = welds;
            fixture.refresh();
            const Body::Report expected = expectedPackageReport(fixture.mFiles, { { kPayloadPath, fixture.mPayload } });
            ASSERT_GT(expected.mFindings.size(), ab::kMaxFindingRows)
                << "the fixture must give more findings than rows";

            const std::unique_ptr<AssetDocument> document = openAsset(writeFolder(mDir / "package", fixture.mFiles));
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            expectSameReport(view->current().mReport, expected);
            const std::vector<PropertyDesc> descs = document->properties().describe();
            EXPECT_NE(findDesc(descs, "finding.199"), nullptr);
            EXPECT_EQ(findDesc(descs, "finding.200"), nullptr);
            EXPECT_NE(findDesc(descs, "finding.more"), nullptr);
            expectFindingRows(document->properties(), view->current().mReport);
        }

        TEST_F(AbModuleTest, aFindingRowsTooltipIsTheSameTextAsItsValue)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mRegionMap->mRegions[0].mRanges = { Body::TriangleRange{ 0, 1 } }; // a gap: GEOM-005
            fixture.refresh();
            const std::unique_ptr<AssetDocument> package = openAsset(writeFolder(mDir / "package", fixture.mFiles));
            std::string pack;
            const std::unique_ptr<AssetDocument> cache = openAsset(writeStandardCache(pack) / "current");
            for (AssetDocument* document : { package.get(), cache.get() })
            {
                ASSERT_NE(document, nullptr);
                const ab::BodyView* view = ab::viewOf(*document);
                ASSERT_NE(view, nullptr);
                const std::size_t findings = std::min(view->current().mReport.mFindings.size(), ab::kMaxFindingRows);
                ASSERT_GT(findings, 0u);
                PropertySource& source = document->properties();
                const std::vector<PropertyDesc> descs = source.describe();
                for (std::size_t i = 0; i < findings; ++i)
                {
                    const std::string id = "finding." + std::to_string(i);
                    const PropertyDesc* desc = findDesc(descs, id);
                    if (desc == nullptr)
                    {
                        ADD_FAILURE() << "no row " << id;
                        continue;
                    }
                    const PropertyValue value = source.get(id);
                    const std::string* text = std::get_if<std::string>(&value);
                    if (text == nullptr)
                    {
                        ADD_FAILURE() << id << " is not text";
                        continue;
                    }
                    EXPECT_EQ(desc->tooltip, *text) << id;
                }
            }
        }

        TEST_F(AbModuleTest, theSourceRowsAddedByTheReviewBelongToTheirKindOfSource)
        {
            const std::unique_ptr<AssetDocument> package = openAsset(writeStandardPackage());
            ASSERT_NE(package, nullptr);
            const std::vector<PropertyDesc> packageRows = package->properties().describe();
            EXPECT_NE(findDesc(packageRows, "source.package"), nullptr);
            EXPECT_EQ(findDesc(packageRows, "source.position"), nullptr) << "a package has one body";
            EXPECT_EQ(findDesc(packageRows, "source.failures"), nullptr);

            std::string pack;
            const std::unique_ptr<AssetDocument> cache = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(cache, nullptr);
            const std::vector<PropertyDesc> cacheRows = cache->properties().describe();
            EXPECT_NE(findDesc(cacheRows, "source.position"), nullptr);
            EXPECT_NE(findDesc(cacheRows, "source.failures"), nullptr);
            EXPECT_NE(findDesc(cacheRows, "report.note"), nullptr) << "the expected binding findings of a cache entry";
            EXPECT_EQ(findDesc(cacheRows, "source.package"), nullptr);

            // "<i> of <n>": the count of bodies, and another position for another body.
            const PropertyValue firstValue = cache->properties().get("source.position");
            const std::string* first = std::get_if<std::string>(&firstValue);
            ASSERT_NE(first, nullptr);
            EXPECT_TRUE(contains(*first, " of 3")) << *first;
            ASSERT_TRUE(ab::selectBody(*cache, 2));
            const PropertyValue lastValue = cache->properties().get("source.position");
            const std::string* last = std::get_if<std::string>(&lastValue);
            ASSERT_NE(last, nullptr);
            EXPECT_TRUE(contains(*last, " of 3")) << *last;
            EXPECT_NE(*first, *last);
        }

        TEST_F(AbModuleTest, aMergedCacheHasAReadOnlyEntryRowWithTheShownBodysTitle)
        {
            std::string pack;
            const std::unique_ptr<AssetDocument> document = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            const std::vector<std::string> titles = titlesOf(manifestOf(standardCacheBodies()).mEntries);
            ASSERT_EQ(titlesOf(view->entries()), titles);

            PropertySource& source = document->properties();
            const std::vector<PropertyDesc> descs = source.describe();
            const PropertyDesc* entry = findDesc(descs, "source.entry");
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(entry->readOnly);
            const PropertyValue value = source.get("source.entry");
            const std::string* text = std::get_if<std::string>(&value);
            ASSERT_NE(text, nullptr);
            EXPECT_EQ(*text, titles[0]);
            std::set<std::string> ids;
            for (const PropertyDesc& desc : descs)
            {
                EXPECT_TRUE(desc.readOnly) << desc.id;
                EXPECT_TRUE(ids.insert(desc.id).second) << "the row " << desc.id << " is listed twice";
            }
            for (const char* id : { "source.kind", "source.path", "report.errors", "report.warnings" })
            {
                EXPECT_NE(findDesc(descs, id), nullptr) << id;
            }
            expectFindingRows(source, view->current().mReport);
            expectRegionRows(source, view->current());
        }

        TEST_F(AbModuleTest, makeSetCommandRefusesEveryRowOfAMergedCacheTheEntryRowIncluded)
        {
            std::string pack;
            const std::unique_ptr<AssetDocument> document = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            const std::vector<std::string> titles = titlesOf(view->entries());
            ASSERT_EQ(titles.size(), 3u);
            PropertySource& source = document->properties();

            // Not even the title of another body: choosing what to view is never a property edit.
            for (const std::string& title : titles)
            {
                EXPECT_EQ(source.makeSetCommand("source.entry", PropertyValue{ title }), nullptr) << title;
            }
            const PropertyValue nobody{ std::string("Nobody none third") };
            EXPECT_EQ(source.makeSetCommand("source.entry", nobody), nullptr);
            EXPECT_EQ(source.makeSetCommand("source.entry", PropertyValue{ std::int64_t{ 1 } }), nullptr);
            EXPECT_EQ(source.makeSetCommand("source.entry", PropertyValue{ true }), nullptr);
            EXPECT_EQ(source.makeSetCommand("source.entry", PropertyValue{ 1.0 }), nullptr);
            EXPECT_EQ(source.makeSetCommand("no.such.row", PropertyValue{ titles[1] }), nullptr);
            for (const PropertyDesc& desc : source.describe())
            {
                EXPECT_EQ(source.makeSetCommand(desc.id, source.get(desc.id)), nullptr) << desc.id;
            }
            EXPECT_EQ(view->selected(), 0u);
            EXPECT_EQ(&document->properties(), &source);
            EXPECT_FALSE(document->isDirty());
        }

        // ---- choosing the shown body --------------------------------------------------------------------------

        TEST_F(AbModuleTest, selectBodyShowsTheBodyAndItsOwnRowsAndComesBack)
        {
            std::string pack;
            const std::unique_ptr<AssetDocument> document = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            const std::vector<std::string> titles = titlesOf(view->entries());
            ASSERT_EQ(titles.size(), 3u);
            const std::size_t target = 2;
            // The two bodies differ in their number of regions (three and two slots), so their rows differ.
            ASSERT_NE(view->body(0).mRegions.size(), view->body(target).mRegions.size());

            PropertySource* before = &document->properties();
            EXPECT_TRUE(ab::selectBody(*document, target));
            EXPECT_EQ(view->selected(), target);
            PropertySource* after = &document->properties();
            EXPECT_NE(after, before) << "a new address makes Studio's panel rebuild its rows";
            const PropertyValue value = after->get("source.entry");
            const std::string* text = std::get_if<std::string>(&value);
            ASSERT_NE(text, nullptr);
            EXPECT_EQ(*text, titles[target]);
            expectFindingRows(*after, view->current().mReport);
            expectRegionRows(*after, view->current());
            EXPECT_FALSE(document->isDirty()) << "choosing a body never changes the files";

            EXPECT_TRUE(ab::selectBody(*document, 0));
            EXPECT_EQ(view->selected(), 0u);
            EXPECT_EQ(&document->properties(), before) << "each body keeps its own source";
            expectRegionRows(*before, view->current());

            EXPECT_TRUE(ab::selectBody(*document, target));
            EXPECT_EQ(&document->properties(), after);

            // Selecting the body already shown is in range: true, and nothing moves.
            EXPECT_TRUE(ab::selectBody(*document, target));
            EXPECT_EQ(view->selected(), target);
            EXPECT_EQ(&document->properties(), after);
        }

        TEST_F(AbModuleTest, selectBodyRefusesAnIndexOutOfRangeAndAnotherModulesDocument)
        {
            std::string pack;
            const std::unique_ptr<AssetDocument> document = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            ASSERT_TRUE(ab::selectBody(*document, 1));
            PropertySource* shown = &document->properties();

            EXPECT_FALSE(ab::selectBody(*document, 3));
            EXPECT_FALSE(ab::selectBody(*document, 1000));
            EXPECT_EQ(view->selected(), 1u);
            EXPECT_EQ(&document->properties(), shown);

            ForeignDocument foreign;
            EXPECT_FALSE(ab::selectBody(foreign, 0));

            // A package is one body.
            const std::unique_ptr<AssetDocument> package = openAsset(writeStandardPackage());
            ASSERT_NE(package, nullptr);
            PropertySource* packageSource = &package->properties();
            EXPECT_TRUE(ab::selectBody(*package, 0));
            EXPECT_FALSE(ab::selectBody(*package, 1));
            ASSERT_NE(ab::viewOf(*package), nullptr);
            EXPECT_EQ(ab::viewOf(*package)->selected(), 0u);
            EXPECT_EQ(&package->properties(), packageSource);
        }

        // ---- gizmos -----------------------------------------------------------------------------------------------

        TEST_F(AbModuleTest, thePackagesGizmosDrawItsRegionLinesAndWeldPoints)
        {
            const PackageFixture fixture = makePackageFixture();
            const std::unique_ptr<AssetDocument> document = openAsset(writeFolder(mDir / "package", fixture.mFiles));
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            const ab::BodyData& data = view->current();
            // Anchor the view's lists to the design's rule before comparing the gizmos with them.
            const std::vector<Point> positions = positionsInMetres(fixture.mPayload, 1.0);
            expectSameLines(data.mRegionLines,
                referenceRegionLines(
                    positions, fixture.mPayload.mTriangles, referenceTriangleRegions(*fixture.mPackage.mRegionMap, 4)));
            ASSERT_FALSE(data.mRegionLines.empty());
            ASSERT_EQ(data.mWeldPoints.size(), 2u);
            expectGizmosDraw(*document, data);
        }

        TEST_F(AbModuleTest, theGizmosFollowTheSelectedBody)
        {
            std::string pack;
            const std::unique_ptr<AssetDocument> document = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(document, nullptr);
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            ASSERT_FALSE(view->current().mRegionLines.empty());
            expectGizmosDraw(*document, view->current());
            const std::size_t firstLines = view->current().mRegionLines.size();

            ASSERT_TRUE(ab::selectBody(*document, 2));
            ASSERT_EQ(view->selected(), 2u);
            EXPECT_NE(view->current().mRegionLines.size(), firstLines);
            expectGizmosDraw(*document, view->current());

            ASSERT_TRUE(ab::selectBody(*document, 0));
            EXPECT_EQ(view->current().mRegionLines.size(), firstLines);
            expectGizmosDraw(*document, view->current());
        }

        // ---- the module as Studio loads it ------------------------------------------------------------------------

        // Added by the implementer after the fresh review of stage f (finding 4), not by the test writer: Studio's core
        // list names this module after CreativeMotion's. It pins existing behaviour, so it was not run red first.
        TEST(AbModuleCoreList, theModuleIsOnTheCoreListAfterCreativeMotions)
        {
            const std::vector<CSMModule::CoreModule> core = CSMModule::coreModules();
            ASSERT_GE(core.size(), 2u);
            EXPECT_EQ(core[0].mId, "creativemotion_editor");
            EXPECT_EQ(core[1].mId, "advancedbodies_editor");
            EXPECT_NE(core[1].mLibrary.find("advancedbodies_editor"), std::string::npos) << core[1].mLibrary;
        }

        TEST_F(AbModuleTest, theDescriptorHasTheModulesIdAndTheCurrentApiVersion)
        {
            const edit::EditorModuleDescriptor* descriptor = ab::moduleDescriptor();
            ASSERT_NE(descriptor, nullptr);
            EXPECT_STREQ(descriptor->id, "advancedbodies_editor");
            EXPECT_TRUE(openeden::sdk::isCompatible(descriptor->apiVersion, edit::kApiVersion));
            EXPECT_NE(descriptor->create, nullptr);
            EXPECT_NE(descriptor->destroy, nullptr);
        }

        TEST_F(AbModuleTest, staticLoadingGoesLoadedAndRegistersTheDocumentedUi)
        {
            loadStatic();
            const std::vector<ModuleInfo> modules = mHost->modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mId, "advancedbodies_editor");
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

            ASSERT_EQ(mRegistry.workspaces().size(), 1u);
            EXPECT_EQ(mRegistry.workspaces().front().mDesc.id, "advancedbodies");
            EXPECT_EQ(mRegistry.workspaces().front().mDesc.title, "AdvancedBodies");

            ASSERT_EQ(mRegistry.panels().size(), 1u);
            const edit::PanelDesc& panel = mRegistry.panels().front().mDesc;
            EXPECT_EQ(panel.id, "advancedbodies.panel");
            EXPECT_EQ(panel.title, "Body Package");
            EXPECT_EQ(panel.workspaceId, "advancedbodies");
            EXPECT_EQ(panel.dock, edit::DockArea::Right);
            EXPECT_TRUE(static_cast<bool>(panel.source));

            EXPECT_TRUE(mRegistry.gizmoProviders().empty());
            EXPECT_TRUE(mRegistry.trackProviders().empty());

            // The two commands that choose the shown body (the amendment of 2026-11-04).
            ASSERT_EQ(mRegistry.commands().size(), 4u);
            const auto findCommand = [this](const std::string& id) -> const edit::CommandDesc* {
                for (const auto& command : mRegistry.commands())
                {
                    if (command.mDesc.id == id)
                    {
                        return &command.mDesc;
                    }
                }
                return nullptr;
            };
            const edit::CommandDesc* nextCommand = findCommand(kNextBody);
            const edit::CommandDesc* previousCommand = findCommand(kPreviousBody);
            ASSERT_NE(nextCommand, nullptr);
            ASSERT_NE(previousCommand, nullptr);
            EXPECT_EQ(nextCommand->menuPath, "Bodies/Next Body");
            EXPECT_EQ(nextCommand->defaultShortcut, "Ctrl+Alt+PgDown");
            EXPECT_EQ(previousCommand->menuPath, "Bodies/Previous Body");
            EXPECT_EQ(previousCommand->defaultShortcut, "Ctrl+Alt+PgUp");
            for (const edit::CommandDesc* command : { nextCommand, previousCommand })
            {
                EXPECT_TRUE(static_cast<bool>(command->handler)) << command->id;
                EXPECT_TRUE(static_cast<bool>(command->enabledWhen)) << command->id << ": not always enabled";
            }

            ASSERT_EQ(mRegistry.assetTypes().size(), 3u);
            const auto findType = [this](const std::string& format) -> const edit::AssetTypeDesc* {
                for (const auto& type : mRegistry.assetTypes())
                {
                    if (type.mDesc.formatId == format)
                    {
                        return &type.mDesc;
                    }
                }
                return nullptr;
            };
            const edit::AssetTypeDesc* packageType = findType("openeden.body");
            const edit::AssetTypeDesc* packType = findType("openeden.bodypack");
            const edit::AssetTypeDesc* cacheType = findType("openeden.bodycache");
            ASSERT_NE(packageType, nullptr);
            ASSERT_NE(packType, nullptr);
            ASSERT_NE(cacheType, nullptr);
            EXPECT_EQ(packageType->fileExtension, "body.json");
            EXPECT_EQ(packType->fileExtension, ".edenpak");
            EXPECT_EQ(cacheType->fileExtension, "current");
            for (const edit::AssetTypeDesc* type : { packageType, packType, cacheType })
            {
                EXPECT_FALSE(type->label.empty()) << type->formatId;
                EXPECT_TRUE(static_cast<bool>(type->open)) << type->formatId;
            }

            // Files are routed by their name's ending.
            const auto* byPackage = mRegistry.findAssetType(mDir / "package" / "body.json");
            const auto* byPack = mRegistry.findAssetType(mDir / "synthetic.edenpak");
            const auto* byCache = mRegistry.findAssetType(mDir / "cache" / "current");
            ASSERT_NE(byPackage, nullptr);
            ASSERT_NE(byPack, nullptr);
            ASSERT_NE(byCache, nullptr);
            EXPECT_EQ(byPackage->mDesc.formatId, "openeden.body");
            EXPECT_EQ(byPack->mDesc.formatId, "openeden.bodypack");
            EXPECT_EQ(byCache->mDesc.formatId, "openeden.bodycache");

            ASSERT_EQ(mRegistry.sections().size(), 3u);
            std::vector<std::string> kinds;
            for (const auto& section : mRegistry.sections())
            {
                kinds.push_back(section.mDesc.selectionKind);
                EXPECT_EQ(section.mDesc.id, "advancedbodies.section." + section.mDesc.selectionKind);
                EXPECT_FALSE(section.mDesc.title.empty());
                EXPECT_TRUE(static_cast<bool>(section.mDesc.create));
            }
            std::sort(kinds.begin(), kinds.end());
            EXPECT_EQ(kinds, (std::vector<std::string>{ "openeden.body", "openeden.bodycache", "openeden.bodypack" }));

            expectEveryIdHasThePrefix(mRegistry);
        }

        TEST_F(AbModuleTest, thePanelShowsTheActiveAssetOnlyWhenItIsThisModulesDocument)
        {
            loadStatic();
            mContext.mActive = nullptr;
            EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), nullptr);

            ForeignDocument foreign;
            mContext.mActive = &foreign;
            EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), nullptr);

            mDocument = openAsset(writeStandardPackage());
            ASSERT_NE(mDocument, nullptr);
            mContext.mActive = mDocument.get();
            EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), &mDocument->properties());

            // A merged cache: the panel shows the selected body's source, a new address for another body.
            std::string pack;
            std::unique_ptr<AssetDocument> cache = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(cache, nullptr);
            mContext.mActive = cache.get();
            PropertySource* first = mRegistry.panelSource("advancedbodies.panel");
            EXPECT_EQ(first, &cache->properties());
            ASSERT_TRUE(ab::selectBody(*cache, 1));
            PropertySource* second = mRegistry.panelSource("advancedbodies.panel");
            EXPECT_EQ(second, &cache->properties());
            EXPECT_NE(second, first);
            ASSERT_TRUE(ab::selectBody(*cache, 0));
            EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), first);
            mContext.mActive = nullptr;

            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded) << mHost->modules()[0].mMessage;
        }

        TEST_F(AbModuleTest, theRegistryOpensEachSourceAndItsSectionShowsTheSummary)
        {
            loadStatic();
            std::string pack;
            const std::filesystem::path cache = writeStandardCache(pack);
            struct Case
            {
                std::filesystem::path mPath;
                std::string mFormat;
                ab::SourceKind mKind;
            };
            const std::vector<Case> cases{ { writeStandardPackage(), "openeden.body", ab::SourceKind::PackageFolder },
                { writeStandardPack(), "openeden.bodypack", ab::SourceKind::PackageFile },
                { cache / "current", "openeden.bodycache", ab::SourceKind::MergedCache } };
            for (const Case& item : cases)
            {
                std::string error;
                mDocument = mRegistry.openAsset(item.mPath, error);
                ASSERT_NE(mDocument, nullptr) << item.mPath << ": " << error;
                EXPECT_FALSE(mDocument->isDirty());
                const ab::BodyView* view = ab::viewOf(*mDocument);
                ASSERT_NE(view, nullptr);
                EXPECT_EQ(view->kind(), item.mKind) << item.mPath;
                EXPECT_EQ(mDocument->gizmos().size(), 2u);
                PropertySource* section = mRegistry.sectionSource("advancedbodies.section." + item.mFormat, *mDocument);
                ASSERT_NE(section, nullptr) << item.mFormat;
                EXPECT_EQ(section, ab::summaryOf(*mDocument));
            }

            // A pointer that names no merged pack is refused through the registry as well.
            mDocument.reset();
            writeText(mDir / "bad" / "current", "not a pack name\n");
            std::string error;
            EXPECT_EQ(mRegistry.openAsset(mDir / "bad" / "current", error), nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);
        }

        TEST_F(AbModuleTest, shutdownWithdrawsEverything)
        {
            loadStatic();
            ASSERT_FALSE(mRegistry.assetTypes().empty());
            mHost->shutdown();
            expectRegistryEmpty(mRegistry);
        }

        // ---- the two body commands ------------------------------------------------------------------------------

        TEST_F(AbModuleTest, theBodyCommandsAreDisabledUnlessTheActiveAssetIsThisModulesDocumentWithSeveralBodies)
        {
            loadStatic();
            const auto expectDisabled = [this](const std::string& why) {
                const int redraws = mContext.mRedraws;
                for (const char* id : { kNextBody, kPreviousBody })
                {
                    EXPECT_FALSE(mRegistry.isCommandEnabled(id)) << id << ", " << why;
                    EXPECT_FALSE(mRegistry.runCommand(id)) << id << ", " << why;
                }
                EXPECT_EQ(mContext.mRedraws, redraws) << why;
            };

            mContext.mActive = nullptr;
            expectDisabled("no active asset");

            ForeignDocument foreign;
            mContext.mActive = &foreign;
            expectDisabled("another module's document");

            mDocument = openAsset(writeStandardPackage());
            ASSERT_NE(mDocument, nullptr);
            mContext.mActive = mDocument.get();
            expectDisabled("a package");

            std::vector<CacheBody> one;
            one.push_back(CacheBody{ "Ash Folk", "male", roundTrip(makeStackedBody(128.f, 2, 16.f)), 'a' });
            publishBodies(mDir / "one", one);
            const std::unique_ptr<AssetDocument> single = openAsset(mDir / "one" / "current");
            ASSERT_NE(single, nullptr);
            const ab::BodyView* singleView = ab::viewOf(*single);
            ASSERT_NE(singleView, nullptr);
            ASSERT_EQ(singleView->bodyCount(), 1u);
            mContext.mActive = single.get();
            expectDisabled("a cache of one body");
            EXPECT_EQ(singleView->selected(), 0u);

            // The control: a cache of three bodies enables both.
            std::string pack;
            const std::unique_ptr<AssetDocument> cache = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(cache, nullptr);
            mContext.mActive = cache.get();
            EXPECT_TRUE(mRegistry.isCommandEnabled(kNextBody));
            EXPECT_TRUE(mRegistry.isCommandEnabled(kPreviousBody));

            mContext.mActive = nullptr;
            EXPECT_TRUE(mContext.mPushed.empty());
            EXPECT_EQ(mContext.mDirtyMarks, 0);
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded) << mHost->modules()[0].mMessage;
        }

        TEST_F(AbModuleTest, nextBodyMovesTheActiveSelectionForwardWrappingAroundAndNeverPushesACommand)
        {
            loadStatic();
            std::string pack;
            mDocument = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(mDocument, nullptr);
            const ab::BodyView* view = ab::viewOf(*mDocument);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            mContext.mActive = mDocument.get();

            // The panel's source per body, recorded the first time each is shown.
            std::vector<PropertySource*> sourceOf(3, nullptr);
            sourceOf[0] = mRegistry.panelSource("advancedbodies.panel");
            ASSERT_EQ(sourceOf[0], &mDocument->properties());
            const std::array<std::size_t, 6> order{ 1, 2, 0, 1, 2, 0 };
            for (const std::size_t expected : order)
            {
                const int redraws = mContext.mRedraws;
                EXPECT_TRUE(mRegistry.runCommand(kNextBody));
                EXPECT_EQ(view->selected(), expected);
                EXPECT_GT(mContext.mRedraws, redraws) << "the command asks Studio to redraw";
                PropertySource* shown = mRegistry.panelSource("advancedbodies.panel");
                EXPECT_EQ(shown, &mDocument->properties());
                if (sourceOf[expected] == nullptr)
                {
                    sourceOf[expected] = shown;
                }
                else
                {
                    EXPECT_EQ(shown, sourceOf[expected]) << "body " << expected << " keeps its source";
                }
                const PropertyValue value = shown->get("source.entry");
                const std::string* text = std::get_if<std::string>(&value);
                ASSERT_NE(text, nullptr);
                EXPECT_EQ(*text, titleOf(view->entries()[expected]));
            }
            EXPECT_NE(sourceOf[0], sourceOf[1]);
            EXPECT_NE(sourceOf[1], sourceOf[2]);
            EXPECT_NE(sourceOf[0], sourceOf[2]);
            EXPECT_TRUE(mContext.mPushed.empty()) << "choosing what to view is never an undo step";
            EXPECT_EQ(mContext.mDirtyMarks, 0);
            EXPECT_FALSE(mDocument->isDirty());
            mContext.mActive = nullptr;
        }

        TEST_F(AbModuleTest, previousBodyMovesTheActiveSelectionBackwardWrappingAround)
        {
            loadStatic();
            std::string pack;
            mDocument = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(mDocument, nullptr);
            const ab::BodyView* view = ab::viewOf(*mDocument);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            mContext.mActive = mDocument.get();
            PropertySource* start = mRegistry.panelSource("advancedbodies.panel");

            const std::array<std::size_t, 3> order{ 2, 1, 0 };
            for (const std::size_t expected : order)
            {
                const int redraws = mContext.mRedraws;
                EXPECT_TRUE(mRegistry.runCommand(kPreviousBody));
                EXPECT_EQ(view->selected(), expected);
                EXPECT_GT(mContext.mRedraws, redraws);
                EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), &mDocument->properties());
            }
            EXPECT_EQ(mRegistry.panelSource("advancedbodies.panel"), start) << "round the three and back";

            // The commands go on from a selection made with selectBody, both ways.
            ASSERT_TRUE(ab::selectBody(*mDocument, 2));
            EXPECT_TRUE(mRegistry.runCommand(kNextBody));
            EXPECT_EQ(view->selected(), 0u);
            EXPECT_TRUE(mRegistry.runCommand(kPreviousBody));
            EXPECT_EQ(view->selected(), 2u);
            EXPECT_TRUE(mRegistry.runCommand(kPreviousBody));
            EXPECT_EQ(view->selected(), 1u);

            EXPECT_TRUE(mContext.mPushed.empty());
            EXPECT_EQ(mContext.mDirtyMarks, 0);
            mContext.mActive = nullptr;
        }

        TEST_F(AbModuleTest, theGizmosFollowTheBodyCommands)
        {
            loadStatic();
            std::string pack;
            mDocument = openAsset(writeStandardCache(pack) / "current");
            ASSERT_NE(mDocument, nullptr);
            const ab::BodyView* view = ab::viewOf(*mDocument);
            ASSERT_NE(view, nullptr);
            ASSERT_EQ(view->bodyCount(), 3u);
            mContext.mActive = mDocument.get();
            expectGizmosDraw(*mDocument, view->current());
            const std::size_t firstLines = view->current().mRegionLines.size();

            ASSERT_TRUE(mRegistry.runCommand(kNextBody));
            ASSERT_EQ(view->selected(), 1u);
            // Body 0 has three slots, body 1 two: other lines.
            EXPECT_NE(view->current().mRegionLines.size(), firstLines);
            expectGizmosDraw(*mDocument, view->current());

            ASSERT_TRUE(mRegistry.runCommand(kPreviousBody));
            ASSERT_EQ(view->selected(), 0u);
            EXPECT_EQ(view->current().mRegionLines.size(), firstLines);
            expectGizmosDraw(*mDocument, view->current());
            mContext.mActive = nullptr;
        }

        TEST_F(AbModuleTest, theBodyCommandsMoveOnlyTheActiveAssetsSelection)
        {
            loadStatic();
            std::string pack;
            const std::filesystem::path folder = writeStandardCache(pack);
            const std::unique_ptr<AssetDocument> first = openAsset(folder / "current");
            const std::unique_ptr<AssetDocument> second = openAsset(folder / "current");
            ASSERT_NE(first, nullptr);
            ASSERT_NE(second, nullptr);
            const ab::BodyView* firstView = ab::viewOf(*first);
            const ab::BodyView* secondView = ab::viewOf(*second);
            ASSERT_NE(firstView, nullptr);
            ASSERT_NE(secondView, nullptr);

            mContext.mActive = second.get();
            EXPECT_TRUE(mRegistry.runCommand(kNextBody));
            EXPECT_EQ(secondView->selected(), 1u);
            EXPECT_EQ(firstView->selected(), 0u);

            mContext.mActive = first.get();
            EXPECT_TRUE(mRegistry.runCommand(kPreviousBody));
            EXPECT_EQ(firstView->selected(), 2u);
            EXPECT_EQ(secondView->selected(), 1u);
            mContext.mActive = nullptr;
            EXPECT_TRUE(mContext.mPushed.empty());
        }

        TEST_F(AbModuleTest, theSharedLibraryLoadsAndAShutdownLeavesTheRegistryEmpty)
        {
            const std::filesystem::path library = ADVANCEDBODIES_EDITOR_PATH;
            ASSERT_TRUE(std::filesystem::exists(library)) << library;

            mHost->load({ ModuleSource{ "advancedbodies_editor", library, ModuleSource::Origin::Core } });
            const std::vector<ModuleInfo> modules = mHost->modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mId, "advancedbodies_editor");
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

            EXPECT_EQ(mRegistry.workspaces().size(), 1u);
            EXPECT_EQ(mRegistry.panels().size(), 1u);
            EXPECT_TRUE(mRegistry.gizmoProviders().empty());
            EXPECT_EQ(mRegistry.commands().size(), 4u);
            EXPECT_EQ(mRegistry.assetTypes().size(), 3u);
            EXPECT_EQ(mRegistry.sections().size(), 3u);
            EXPECT_NE(mRegistry.findAssetType(mDir / "body.json"), nullptr);
            EXPECT_NE(mRegistry.findAssetType(mDir / "a.edenpak"), nullptr);
            EXPECT_NE(mRegistry.findAssetType(mDir / "current"), nullptr);
            expectEveryIdHasThePrefix(mRegistry);

            // A document opened through the library works, and is released before the library goes.
            std::string error;
            mDocument = mRegistry.openAsset(writeStandardPackage(), error);
            ASSERT_NE(mDocument, nullptr) << error;
            EXPECT_EQ(mDocument->gizmos().size(), 2u);
            EXPECT_FALSE(mDocument->isDirty());

            // The library's commands move the selection of a merged cache it opened (seen through the address of
            // the shown body's source: no RTTI across the library boundary).
            mContext.mActive = mDocument.get();
            EXPECT_FALSE(mRegistry.isCommandEnabled(kNextBody)) << "a package has one body";
            mContext.mActive = nullptr;
            std::string pack;
            mDocument = mRegistry.openAsset(writeStandardCache(pack) / "current", error);
            ASSERT_NE(mDocument, nullptr) << error;
            mContext.mActive = mDocument.get();
            PropertySource* shown = &mDocument->properties();
            const int redraws = mContext.mRedraws;
            EXPECT_TRUE(mRegistry.runCommand(kNextBody));
            EXPECT_NE(&mDocument->properties(), shown);
            EXPECT_TRUE(mRegistry.runCommand(kPreviousBody));
            EXPECT_EQ(&mDocument->properties(), shown);
            EXPECT_GT(mContext.mRedraws, redraws);
            EXPECT_TRUE(mContext.mPushed.empty());
            mContext.mActive = nullptr;
            mDocument.reset();

            mHost->shutdown();
            expectRegistryEmpty(mRegistry);
        }
    }
}
