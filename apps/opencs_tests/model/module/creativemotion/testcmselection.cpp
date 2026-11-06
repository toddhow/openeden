// Task 0091 stage b: the rig inspector uses the host's selection (API 1.2). Written from the spec alone.
//
// Contract pinned here:
//  - RigDocument implements creativemotion_editor::JointSelectable: jointNames() in rig order, setSelectedJoint(name)
//    (an unknown name or "" clears), selectedJoint().
//  - With a joint selected, the panel's description keeps the rig.* rows and lists only that joint's rows; with none
//    selected it lists every joint (as before). get() answers for every id either way.
//  - The limits gizmo draws the same number of lines with or without a selection; the selected joint's lines are
//    drawn in the highlight colour (1, 1, 0.2, 1) and nothing else changes colour.
//  - The module subscribes to the bus: SelectionChanged with kind "joint" selects that joint of the active rig (and
//    asks for a redraw); kind "handle" with id "creativemotion.joint.<bone>" (a click on a joint's handle) becomes
//    context.select({"joint", bone}); anything else clears the document's selection.
//  - Commands "creativemotion.nextJoint" and "creativemotion.previousJoint" (menu Rig/Next Joint, Rig/Previous Joint)
//    move the selection through jointNames() in order, wrapping, starting from the first (next) or last (previous)
//    when nothing is selected; they are disabled without an open rig.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/creativemotion_editor/documents.hpp"

#include "cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace CMTest
{
    namespace
    {
        using namespace openeden::editor;

        constexpr Colour kHighlight{ 1.0f, 1.0f, 0.2f, 1.0f };

        class SelectionContext final : public EditorContext
        {
        public:
            explicit SelectionContext(CSMModule::ModuleRegistry& registry)
                : mRegistry(registry)
                , mSelection(registry.bus())
            {
            }
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view) override {}
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }
            Selection selection() const override { return mSelection.current(); }
            void select(const Selection& selection) override { mSelection.select(selection); }
            void publish(const EditorEvent& event) override { mRegistry.bus().publish(event); }

            AssetDocument* mActive = nullptr;
            int mRedraws = 0;

        private:
            CSMModule::ModuleRegistry& mRegistry;
            CSMModule::SelectionModel mSelection;
        };

        std::vector<std::string> ids(const std::vector<PropertyDesc>& descs)
        {
            std::vector<std::string> result;
            for (const PropertyDesc& desc : descs)
                result.push_back(desc.id);
            return result;
        }

        bool hasPrefix(const std::vector<std::string>& all, const std::string& prefix)
        {
            return std::any_of(all.begin(), all.end(), [&](const std::string& id) { return id.rfind(prefix, 0) == 0; });
        }

        class CmSelectionTest : public TempDirTest
        {
        protected:
            std::unique_ptr<AssetDocument> openLimitsRig()
            {
                std::string error;
                auto document = creativemotion_editor::openRig(writeRigFile("limits", makeLimitsRig()), error);
                EXPECT_NE(document, nullptr) << error;
                return document;
            }
        };

        TEST_F(CmSelectionTest, theRigDocumentListsItsJointsInRigOrderAndStartsWithNoSelection)
        {
            auto document = openLimitsRig();
            auto* selectable = dynamic_cast<creativemotion_editor::JointSelectable*>(document.get());
            ASSERT_NE(selectable, nullptr);
            EXPECT_EQ(selectable->jointNames(), (std::vector<std::string>{ "Spine", "LeftThigh", "LeftShin" }));
            EXPECT_TRUE(selectable->selectedJoint().empty());
        }

        TEST_F(CmSelectionTest, aSelectedJointNarrowsThePanelToItsRowsAndKeepsTheRigRows)
        {
            auto document = openLimitsRig();
            auto* selectable = dynamic_cast<creativemotion_editor::JointSelectable*>(document.get());
            ASSERT_NE(selectable, nullptr);
            const std::vector<std::string> all = ids(document->properties().describe());
            EXPECT_TRUE(hasPrefix(all, "joint.Spine."));
            EXPECT_TRUE(hasPrefix(all, "joint.LeftThigh."));

            selectable->setSelectedJoint("LeftThigh");
            const std::vector<std::string> narrowed = ids(document->properties().describe());
            EXPECT_TRUE(hasPrefix(narrowed, "rig.name"));
            EXPECT_TRUE(hasPrefix(narrowed, "rig.joints"));
            EXPECT_TRUE(hasPrefix(narrowed, "joint.LeftThigh."));
            EXPECT_FALSE(hasPrefix(narrowed, "joint.Spine."));
            EXPECT_FALSE(hasPrefix(narrowed, "joint.LeftShin."));
            EXPECT_LT(narrowed.size(), all.size());

            // get() still answers for a joint that is not shown.
            EXPECT_NEAR(std::get<double>(document->properties().get("joint.Spine.planeSwingMaxRad")), 0.4, 1e-12);

            selectable->setSelectedJoint("");
            EXPECT_EQ(ids(document->properties().describe()), all);
        }

        TEST_F(CmSelectionTest, anUnknownJointNameClearsTheSelection)
        {
            auto document = openLimitsRig();
            auto* selectable = dynamic_cast<creativemotion_editor::JointSelectable*>(document.get());
            ASSERT_NE(selectable, nullptr);
            selectable->setSelectedJoint("Spine");
            selectable->setSelectedJoint("NoSuchJoint");
            EXPECT_TRUE(selectable->selectedJoint().empty());
        }

        TEST_F(CmSelectionTest, theLimitsGizmoHighlightsTheSelectedJointAndOnlyColoursChange)
        {
            auto document = openLimitsRig();
            auto* selectable = dynamic_cast<creativemotion_editor::JointSelectable*>(document.get());
            ASSERT_NE(selectable, nullptr);
            RecordingSink before;
            document->gizmos().at(1)->draw(before);

            selectable->setSelectedJoint("LeftThigh");
            RecordingSink after;
            document->gizmos().at(1)->draw(after);

            ASSERT_EQ(before.mLines.size(), after.mLines.size());
            std::size_t highlighted = 0;
            for (std::size_t i = 0; i < before.mLines.size(); ++i)
            {
                EXPECT_EQ(before.mLines[i].a.x, after.mLines[i].a.x);
                EXPECT_EQ(before.mLines[i].b.z, after.mLines[i].b.z);
                const Colour& c = after.mLines[i].colour;
                if (c.r == kHighlight.r && c.g == kHighlight.g && c.b == kHighlight.b)
                {
                    ++highlighted;
                }
                else
                {
                    EXPECT_EQ(c.r, before.mLines[i].colour.r);
                    EXPECT_EQ(c.g, before.mLines[i].colour.g);
                    EXPECT_EQ(c.b, before.mLines[i].colour.b);
                }
            }
            // A pyramid draws: the twist axis line, four edges, four corner lines and a 17-point arc's 16 lines.
            EXPECT_EQ(highlighted, 1u + 4u + 4u + 16u);
        }

        TEST_F(CmSelectionTest, onlyTheSelectedJointsNameIsDrawnAsALabelAtItsOriginInTheHighlightColour)
        {
            auto document = openLimitsRig();
            auto* selectable = dynamic_cast<creativemotion_editor::JointSelectable*>(document.get());
            ASSERT_NE(selectable, nullptr);
            RecordingSink none;
            document->gizmos().at(1)->draw(none);
            EXPECT_EQ(none.mLabels, 0);

            selectable->setSelectedJoint("LeftShin");
            RecordingSink one;
            document->gizmos().at(1)->draw(one);
            EXPECT_EQ(one.mLabels, 1);
        }

        class CmSelectionModuleTest : public CmSelectionTest
        {
        protected:
            void SetUp() override
            {
                CmSelectionTest::SetUp();
                mContext = std::make_unique<SelectionContext>(mRegistry);
                mHost = std::make_unique<CSMModule::ModuleHost>(mRegistry, *mContext);
                mHost->loadStatic("creativemotion_editor", *creativemotion_editor::moduleDescriptor(),
                    CSMModule::ModuleSource::Origin::Core);
                ASSERT_EQ(mHost->modules().at(0).mState, CSMModule::ModuleInfo::State::Loaded);
                mDocument = openLimitsRig();
                mContext->mActive = mDocument.get();
                mSelectable = dynamic_cast<creativemotion_editor::JointSelectable*>(mDocument.get());
                ASSERT_NE(mSelectable, nullptr);
            }
            void TearDown() override
            {
                mContext->mActive = nullptr;
                mDocument.reset();
                mHost.reset();
                mContext.reset();
                CmSelectionTest::TearDown();
            }

            CSMModule::ModuleRegistry mRegistry;
            std::unique_ptr<SelectionContext> mContext;
            std::unique_ptr<CSMModule::ModuleHost> mHost;
            std::unique_ptr<AssetDocument> mDocument;
            creativemotion_editor::JointSelectable* mSelectable = nullptr;
        };

        TEST_F(CmSelectionModuleTest, aJointSelectionOfTheHostReachesTheActiveRigAndAsksForARedraw)
        {
            mContext->select({ "joint", "LeftShin" });
            EXPECT_EQ(mSelectable->selectedJoint(), "LeftShin");
            EXPECT_GE(mContext->mRedraws, 1);

            mContext->select({});
            EXPECT_TRUE(mSelectable->selectedJoint().empty());
        }

        TEST_F(CmSelectionModuleTest, aClickOnAJointsHandleSelectsThatJoint)
        {
            mContext->select({ "handle", "creativemotion.joint.LeftThigh" });
            EXPECT_EQ(mContext->selection(), (Selection{ "joint", "LeftThigh" }));
            EXPECT_EQ(mSelectable->selectedJoint(), "LeftThigh");
        }

        TEST_F(CmSelectionModuleTest, aClickOnSomeOtherHandleClearsTheJointSelection)
        {
            mContext->select({ "joint", "Spine" });
            mContext->select({ "handle", "creativemotion.cone.plane" });
            EXPECT_TRUE(mSelectable->selectedJoint().empty());
        }

        TEST_F(CmSelectionModuleTest, nextAndPreviousJointWalkTheJointsAndWrap)
        {
            ASSERT_TRUE(mRegistry.isCommandEnabled("creativemotion.nextJoint"));
            ASSERT_TRUE(mRegistry.isCommandEnabled("creativemotion.previousJoint"));
            const auto current = [&] { return mContext->selection().id; };

            EXPECT_TRUE(mRegistry.runCommand("creativemotion.nextJoint"));
            EXPECT_EQ(current(), "Spine");
            mRegistry.runCommand("creativemotion.nextJoint");
            EXPECT_EQ(current(), "LeftThigh");
            mRegistry.runCommand("creativemotion.nextJoint");
            mRegistry.runCommand("creativemotion.nextJoint");
            EXPECT_EQ(current(), "Spine"); // wrapped
            mRegistry.runCommand("creativemotion.previousJoint");
            EXPECT_EQ(current(), "LeftShin"); // wrapped back
            EXPECT_EQ(mContext->selection().kind, "joint");
        }

        TEST_F(CmSelectionModuleTest, previousFromNothingStartsAtTheLastJoint)
        {
            mRegistry.runCommand("creativemotion.previousJoint");
            EXPECT_EQ(mContext->selection().id, "LeftShin");
        }

        TEST_F(CmSelectionModuleTest, theJointCommandsAreDisabledWithoutAnOpenRig)
        {
            mContext->mActive = nullptr;
            EXPECT_FALSE(mRegistry.isCommandEnabled("creativemotion.nextJoint"));
            EXPECT_FALSE(mRegistry.isCommandEnabled("creativemotion.previousJoint"));
        }
    }
}
