// Editor Module API 1.1 in the body package panel: the regions drawn as filled, semi-transparent triangles, with the
// edges optional (task 0069 stage f follow-up, toddhow 2026-11-04). Written from the request alone, before the code.
//
// Assumptions where the request left room:
//  - The one "advancedbodies.body.regions" gizmo draws both: the fill first, then the edges. Its gizmo list is
//    unchanged (two gizmos), so the existing gizmo tests still hold. The default shows fill and edges, which is what
//    those tests pin for the edges.
//  - regionDisplay(document) / setRegionDisplay(document, mode) choose between FillAndEdges, Fill and Edges. They
//    refuse (false) another module's document. The choice is not an undo step and never marks anything dirty.
//  - One triangle per triangle of the drawn mesh, in the mesh's order, with the vertices at their positions in metres,
//    in the colour of the triangle's region (kUnassignedColour when it has none) with the alpha kFillAlpha, depth
//    mode Tested. At most kMaxFillTriangles are drawn.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/advancedbodies_editor/bodyview.hpp"
#include "editor_modules/advancedbodies_editor/documents.hpp"

#include "abtestsupport.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ABTest
{
    namespace
    {
        using edit::AssetDocument;
        using edit::Gizmo;

        struct DrawnTriangle
        {
            edit::Vec3 mA;
            edit::Vec3 mB;
            edit::Vec3 mC;
            edit::Colour mColour;
            edit::OverlayDepth mDepth;
        };

        class FillSink final : public edit::OverlaySink
        {
        public:
            void line(const edit::Vec3&, const edit::Vec3&, const edit::Colour&) override { ++mLines; }
            void point(const edit::Vec3&, const edit::Colour&, float) override { ++mPoints; }
            void label(const edit::Vec3&, std::string_view, const edit::Colour&) override {}
            void triangle(const edit::Vec3& a, const edit::Vec3& b, const edit::Vec3& c, const edit::Colour& colour,
                edit::OverlayDepth depth) override
            {
                mTriangles.push_back(DrawnTriangle{ a, b, c, colour, depth });
            }

            std::vector<DrawnTriangle> mTriangles;
            int mLines = 0;
            int mPoints = 0;
        };

        class ForeignDocument final : public AssetDocument
        {
        public:
            std::string_view assetId() const override { return "foreign"; }
            edit::PropertySource& properties() override { return mProperties; }
            bool isDirty() const override { return false; }
            bool save(std::string&) override { return false; }

        private:
            class Properties final : public edit::PropertySource
            {
            public:
                std::vector<edit::PropertyDesc> describe() const override { return {}; }
                edit::PropertyValue get(std::string_view) const override { return std::string(); }
                std::unique_ptr<edit::Command> makeSetCommand(std::string_view, const edit::PropertyValue&) override
                {
                    return nullptr;
                }
            };
            Properties mProperties;
        };

        class FillContext final : public edit::EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<edit::Command>) override { ++mPushed; }
            void markDirty(std::string_view) override { ++mDirtyMarks; }
            void log(edit::LogLevel, std::string_view) override {}
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }

            AssetDocument* mActive = nullptr;
            int mPushed = 0;
            int mDirtyMarks = 0;
            int mRedraws = 0;
        };

        class AbFillTest : public AbTempTest
        {
        protected:
            std::unique_ptr<AssetDocument> open(const PackageFixture& fixture)
            {
                std::string error;
                std::unique_ptr<AssetDocument> document
                    = ab::openBodyAsset(writeFolder(mDir / "package", fixture.mFiles), error);
                EXPECT_NE(document, nullptr) << error;
                return document;
            }

            static const Gizmo* regionsGizmo(AssetDocument& document)
            {
                for (const Gizmo* gizmo : document.gizmos())
                {
                    if (gizmo != nullptr && gizmo->id() == "advancedbodies.body.regions")
                    {
                        return gizmo;
                    }
                }
                return nullptr;
            }
        };

        TEST_F(AbFillTest, theFillIsOneTrianglePerMeshTriangleInTheRegionsColourWithAlpha)
        {
            const PackageFixture fixture = makePackageFixture();
            const std::unique_ptr<AssetDocument> document = open(fixture);
            ASSERT_NE(document, nullptr);
            const Gizmo* gizmo = regionsGizmo(*document);
            ASSERT_NE(gizmo, nullptr);

            FillSink sink;
            gizmo->draw(sink);

            const std::vector<Point> positions = positionsInMetres(fixture.mPayload, 1.0);
            const std::vector<int> regions
                = referenceTriangleRegions(*fixture.mPackage.mRegionMap, fixture.mPayload.mTriangles.size() / 3);
            ASSERT_EQ(sink.mTriangles.size(), fixture.mPayload.mTriangles.size() / 3);
            ASSERT_FALSE(sink.mTriangles.empty());
            for (std::size_t t = 0; t < sink.mTriangles.size(); ++t)
            {
                const DrawnTriangle& drawn = sink.mTriangles[t];
                EXPECT_TRUE(nearPoint(toPoint(drawn.mA), positions[fixture.mPayload.mTriangles[3 * t]])) << t;
                EXPECT_TRUE(nearPoint(toPoint(drawn.mB), positions[fixture.mPayload.mTriangles[3 * t + 1]])) << t;
                EXPECT_TRUE(nearPoint(toPoint(drawn.mC), positions[fixture.mPayload.mTriangles[3 * t + 2]])) << t;
                const edit::Colour expected = designColour(regions[t]);
                EXPECT_EQ(drawn.mColour.r, expected.r) << t;
                EXPECT_EQ(drawn.mColour.g, expected.g) << t;
                EXPECT_EQ(drawn.mColour.b, expected.b) << t;
                EXPECT_EQ(drawn.mColour.a, ab::kFillAlpha) << t;
                EXPECT_EQ(drawn.mDepth, edit::OverlayDepth::Tested) << t;
            }
            EXPECT_GT(ab::kFillAlpha, 0.f);
            EXPECT_LT(ab::kFillAlpha, 1.f);
            // The edges are drawn as before, in the same call.
            EXPECT_GT(sink.mLines, 0);
        }

        TEST_F(AbFillTest, theDefaultShowsFillAndEdgesAndTheDisplayCanBeChosen)
        {
            const std::unique_ptr<AssetDocument> document = open(makePackageFixture());
            ASSERT_NE(document, nullptr);
            const Gizmo* gizmo = regionsGizmo(*document);
            ASSERT_NE(gizmo, nullptr);
            EXPECT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::FillAndEdges);

            ASSERT_TRUE(ab::setRegionDisplay(*document, ab::RegionDisplay::Fill));
            EXPECT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::Fill);
            FillSink fillOnly;
            gizmo->draw(fillOnly);
            EXPECT_FALSE(fillOnly.mTriangles.empty());
            EXPECT_EQ(fillOnly.mLines, 0);

            ASSERT_TRUE(ab::setRegionDisplay(*document, ab::RegionDisplay::Edges));
            FillSink edgesOnly;
            gizmo->draw(edgesOnly);
            EXPECT_TRUE(edgesOnly.mTriangles.empty());
            EXPECT_GT(edgesOnly.mLines, 0);

            ASSERT_TRUE(ab::setRegionDisplay(*document, ab::RegionDisplay::FillAndEdges));
            FillSink both;
            gizmo->draw(both);
            EXPECT_FALSE(both.mTriangles.empty());
            EXPECT_GT(both.mLines, 0);
            EXPECT_FALSE(document->isDirty());
        }

        TEST_F(AbFillTest, theWeldPointsAreDrawnWhateverTheRegionDisplay)
        {
            const std::unique_ptr<AssetDocument> document = open(makePackageFixture());
            ASSERT_NE(document, nullptr);
            ASSERT_TRUE(ab::setRegionDisplay(*document, ab::RegionDisplay::Fill));
            const Gizmo* welds = nullptr;
            for (const Gizmo* gizmo : document->gizmos())
            {
                if (gizmo != nullptr && gizmo->id() == "advancedbodies.body.welds")
                {
                    welds = gizmo;
                }
            }
            ASSERT_NE(welds, nullptr);
            FillSink sink;
            welds->draw(sink);
            EXPECT_EQ(sink.mPoints, 2);
            EXPECT_TRUE(sink.mTriangles.empty());
        }

        TEST_F(AbFillTest, anotherModulesDocumentIsRefused)
        {
            ForeignDocument foreign;
            EXPECT_FALSE(ab::setRegionDisplay(foreign, ab::RegionDisplay::Fill));
            EXPECT_EQ(ab::regionDisplay(foreign), ab::RegionDisplay::FillAndEdges);
        }

        TEST_F(AbFillTest, theFillIsCappedForAHugeBody)
        {
            // Two triangles per quad.
            const std::uint32_t quads = static_cast<std::uint32_t>(ab::kMaxFillTriangles / 2 + 50);
            const PackageFixture fixture = makeStripPackageFixture(quads);
            const std::unique_ptr<AssetDocument> document = open(fixture);
            ASSERT_NE(document, nullptr);
            const Gizmo* gizmo = regionsGizmo(*document);
            ASSERT_NE(gizmo, nullptr);
            ASSERT_TRUE(ab::setRegionDisplay(*document, ab::RegionDisplay::Fill));
            FillSink sink;
            gizmo->draw(sink);
            EXPECT_EQ(sink.mTriangles.size(), ab::kMaxFillTriangles);
        }

        TEST_F(AbFillTest, theFillFollowsTheSelectedBody)
        {
            const std::filesystem::path folder = mDir / "cache";
            const std::string pack = publishBodies(folder, standardCacheBodies());
            ASSERT_FALSE(pack.empty());
            std::string error;
            const std::unique_ptr<AssetDocument> document = ab::openBodyAsset(folder / "current", error);
            ASSERT_NE(document, nullptr) << error;
            const ab::BodyView* view = ab::viewOf(*document);
            ASSERT_NE(view, nullptr);
            ASSERT_GE(view->bodyCount(), 2u);
            const Gizmo* gizmo = regionsGizmo(*document);
            ASSERT_NE(gizmo, nullptr);

            FillSink first;
            gizmo->draw(first);
            ASSERT_TRUE(view->current().mMesh.has_value());
            EXPECT_EQ(first.mTriangles.size(), view->current().mMesh->mTriangles.size() / 3);

            ASSERT_TRUE(ab::selectBody(*document, 2));
            FillSink third;
            gizmo->draw(third);
            ASSERT_TRUE(view->current().mMesh.has_value());
            EXPECT_EQ(third.mTriangles.size(), view->current().mMesh->mTriangles.size() / 3);
            EXPECT_NE(third.mTriangles.size(), first.mTriangles.size());
        }

        TEST_F(AbFillTest, theCycleCommandStepsThroughTheThreeDisplaysWithoutAnUndoStepOrDirtyMark)
        {
            CSMModule::ModuleRegistry registry;
            FillContext context;
            CSMModule::ModuleHost host(registry, context);
            host.loadStatic(ab::kModuleId, *ab::moduleDescriptor(), CSMModule::ModuleSource::Origin::Core);
            const std::unique_ptr<AssetDocument> document = open(makePackageFixture());
            ASSERT_NE(document, nullptr);

            // No body open: the command is not enabled.
            EXPECT_FALSE(registry.isCommandEnabled("advancedbodies.cycleRegionDisplay"));
            EXPECT_FALSE(registry.runCommand("advancedbodies.cycleRegionDisplay"));

            context.mActive = document.get();
            EXPECT_TRUE(registry.isCommandEnabled("advancedbodies.cycleRegionDisplay"));
            ASSERT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::FillAndEdges);
            EXPECT_TRUE(registry.runCommand("advancedbodies.cycleRegionDisplay"));
            EXPECT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::Fill);
            EXPECT_TRUE(registry.runCommand("advancedbodies.cycleRegionDisplay"));
            EXPECT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::Edges);
            EXPECT_TRUE(registry.runCommand("advancedbodies.cycleRegionDisplay"));
            EXPECT_EQ(ab::regionDisplay(*document), ab::RegionDisplay::FillAndEdges);
            EXPECT_EQ(context.mRedraws, 3);
            EXPECT_EQ(context.mPushed, 0);
            EXPECT_EQ(context.mDirtyMarks, 0);
            context.mActive = nullptr;
            host.shutdown();
        }
    }
}
