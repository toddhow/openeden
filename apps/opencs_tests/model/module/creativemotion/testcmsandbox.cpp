// Task 0065 stage C: the push sandbox of CreativeMotion's editor module, over Editor Module API 1.3. Written from the
// spec alone, before the module code existed.
//
// Contract pinned here (a fake SimulationSandbox stands in for Studio's):
//  - The module registers a panel "creativemotion.sandbox", commands "creativemotion.sandbox.push" (menu
//    Sandbox/Push) and "creativemotion.sandbox.reset" (Sandbox/Reset) and a gizmo provider
//    "creativemotion.sandbox.gizmos".
//  - The commands are disabled while the host has no sandbox, or no rig or no profile has been opened (the sandbox is
//    built from the two files' paths).
//  - The panel lists the bone (a Choice of the rig's body bones), the direction (a Vec3, default 1,0,0), the magnitude
//    in newton seconds (a Float, default 50, 0 to 5000) and a read-only status line. Edits are commands that undo; a
//    bone that is not a body bone and a magnitude out of range are refused.
//  - Push builds the sandbox once from the opened rig and profile (offset: dropOffsetM(rig), yaw 0), then queues
//    unit(direction) * magnitude at the chosen body's centre. A zero direction is refused with a log line. A second
//    push does not build again; opening a rig again makes the next push build again.
//  - Reset builds when nothing is built, otherwise asks the sandbox to reset.
//  - A host selection {"joint", bone} of a body bone becomes the panel's bone.
//  - The gizmo draws nothing until the sandbox is built; then the ground grid at z = 0, every body's shape at its
//    pose, and an arrow for the last push (red while it waits, green after a step).
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/creativemotion_editor/documents.hpp"
#include "editor_modules/creativemotion_editor/sandbox.hpp"

#include "cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace CMTest
{
    namespace
    {
        using namespace openeden::editor;

        class FakeSandbox final : public SimulationSandbox
        {
        public:
            struct PushCall
            {
                std::string bone;
                Vec3 impulse;
                Vec3 point;
            };

            bool build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
                const Vec3& offsetM, double yaw, std::string& error) override
            {
                ++mBuilds;
                mRigPath = rigPath;
                mProfilePath = profilePath;
                mOffset = offsetM;
                mYaw = yaw;
                if (mFailBuild)
                {
                    error = "the fake refuses";
                    mBuilt = false;
                    return false;
                }
                mBuilt = true;
                mPending = 0;
                return true;
            }
            bool built() const override { return mBuilt; }
            std::vector<std::string> bodyNames() const override { return mBuilt ? mNames : std::vector<std::string>(); }
            bool push(std::string_view bone, const Vec3& impulse, const Vec3& point) override
            {
                if (!mBuilt)
                    return false;
                mPushes.push_back(PushCall{ std::string(bone), impulse, point });
                ++mPending;
                return true;
            }
            std::size_t pendingPushes() const override { return mPending; }
            bool bodyPose(std::string_view bone, SandboxBodyPose& out) const override
            {
                if (!mBuilt)
                    return false;
                const auto it = mPoses.find(std::string(bone));
                out = it != mPoses.end() ? it->second : SandboxBodyPose{ { 0.0, 0.0, 1.0 } };
                return true;
            }
            bool reset(std::string& error) override
            {
                if (!mBuilt)
                {
                    error = "nothing built";
                    return false;
                }
                ++mResets;
                mPending = 0;
                return true;
            }
            void release() override { mBuilt = false; }

            bool mBuilt = false;
            bool mFailBuild = false;
            int mBuilds = 0;
            int mResets = 0;
            std::size_t mPending = 0;
            std::filesystem::path mRigPath;
            std::filesystem::path mProfilePath;
            Vec3 mOffset;
            double mYaw = -1.0;
            std::vector<std::string> mNames{ "Pelvis", "Spine", "LeftThigh", "LeftShin" };
            std::map<std::string, SandboxBodyPose> mPoses;
            std::vector<PushCall> mPushes;
        };

        class SandboxContext final : public EditorContext
        {
        public:
            explicit SandboxContext(CSMModule::ModuleRegistry& registry)
                : mRegistry(registry)
                , mSelection(registry.bus())
            {
            }
            void pushCommand(std::unique_ptr<Command> command) override
            {
                command->redo();
                mCommands.push_back(std::move(command));
            }
            void markDirty(std::string_view) override {}
            void log(LogLevel level, std::string_view message) override
            {
                mLog.emplace_back(level, std::string(message));
            }
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
            Selection selection() const override { return mSelection.current(); }
            void select(const Selection& selection) override { mSelection.select(selection); }
            void publish(const EditorEvent& event) override { mRegistry.bus().publish(event); }
            SimulationSandbox* simulationSandbox() override { return mSandbox; }

            bool logged(LogLevel level, const std::string& fragment) const
            {
                return std::any_of(mLog.begin(), mLog.end(), [&](const auto& entry) {
                    return entry.first == level && entry.second.find(fragment) != std::string::npos;
                });
            }

            FakeSandbox* mSandbox = nullptr;
            std::vector<std::unique_ptr<Command>> mCommands;
            std::vector<std::pair<LogLevel, std::string>> mLog;
            int mRedraws = 0;

        private:
            CSMModule::ModuleRegistry& mRegistry;
            CSMModule::SelectionModel mSelection;
        };

        const CSMModule::ModuleRegistry::CommandEntry* findCommand(CSMModule::ModuleRegistry& registry, const char* id)
        {
            for (const auto& entry : registry.commands())
                if (entry.mDesc.id == id)
                    return &entry;
            return nullptr;
        }

        class CmSandboxTest : public TempDirTest
        {
        protected:
            void SetUp() override
            {
                TempDirTest::SetUp();
                mContext = std::make_unique<SandboxContext>(mRegistry);
                mContext->mSandbox = &mSandbox;
                mHost = std::make_unique<CSMModule::ModuleHost>(mRegistry, *mContext);
                mHost->loadStatic("creativemotion_editor", *creativemotion_editor::moduleDescriptor(),
                    CSMModule::ModuleSource::Origin::Core);
                ASSERT_EQ(mHost->modules().at(0).mState, CSMModule::ModuleInfo::State::Loaded)
                    << mHost->modules().at(0).mMessage;
            }
            void TearDown() override
            {
                mDocuments.clear();
                mHost.reset();
                mContext.reset();
                TempDirTest::TearDown();
            }

            void openBoth()
            {
                mRigPath = writeRigFile("limits", makeLimitsRig());
                mProfilePath = writeProfileFile("limits", makeProfile());
                openRig();
                std::string error;
                mDocuments.push_back(mRegistry.openAsset(mProfilePath, error));
                ASSERT_NE(mDocuments.back(), nullptr) << error;
            }
            void openRig()
            {
                std::string error;
                mDocuments.push_back(mRegistry.openAsset(mRigPath, error));
                ASSERT_NE(mDocuments.back(), nullptr) << error;
            }

            PropertySource* panel() { return mRegistry.panelSource(creativemotion_editor::kSandboxPanelId); }
            bool run(const char* id) { return mRegistry.runCommand(id); }

            void set(const char* id, const PropertyValue& value)
            {
                ASSERT_NE(panel(), nullptr);
                auto command = panel()->makeSetCommand(id, value);
                ASSERT_NE(command, nullptr) << id;
                command->redo();
                mSet.push_back(std::move(command));
            }

            CSMModule::ModuleRegistry mRegistry;
            FakeSandbox mSandbox;
            std::unique_ptr<SandboxContext> mContext;
            std::unique_ptr<CSMModule::ModuleHost> mHost;
            std::vector<std::unique_ptr<AssetDocument>> mDocuments;
            std::vector<std::unique_ptr<Command>> mSet;
            std::filesystem::path mRigPath;
            std::filesystem::path mProfilePath;
        };

        TEST_F(CmSandboxTest, itsCommandsHaveTheirMenuPaths)
        {
            const auto* push = findCommand(mRegistry, creativemotion_editor::kSandboxPushCommandId);
            const auto* reset = findCommand(mRegistry, creativemotion_editor::kSandboxResetCommandId);
            ASSERT_NE(push, nullptr);
            ASSERT_NE(reset, nullptr);
            EXPECT_EQ(push->mDesc.menuPath, "Sandbox/Push");
            EXPECT_EQ(reset->mDesc.menuPath, "Sandbox/Reset");
            bool provider = false;
            for (const auto& entry : mRegistry.gizmoProviders())
                provider = provider || entry.mDesc.id == creativemotion_editor::kSandboxGizmoProviderId;
            EXPECT_TRUE(provider);
            bool sandboxPanel = false;
            for (const auto& entry : mRegistry.panels())
                sandboxPanel = sandboxPanel || entry.mDesc.id == creativemotion_editor::kSandboxPanelId;
            EXPECT_TRUE(sandboxPanel);
        }

        TEST_F(CmSandboxTest, theCommandsAreDisabledUntilARigAndAProfileAreOpen)
        {
            EXPECT_FALSE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxPushCommandId));
            mRigPath = writeRigFile("limits", makeLimitsRig());
            mProfilePath = writeProfileFile("limits", makeProfile());
            openRig();
            EXPECT_FALSE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxPushCommandId));
            std::string error;
            mDocuments.push_back(mRegistry.openAsset(mProfilePath, error));
            EXPECT_TRUE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_TRUE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxResetCommandId));
        }

        TEST_F(CmSandboxTest, theCommandsAreDisabledWhenTheHostHasNoSandbox)
        {
            mContext->mSandbox = nullptr;
            openBoth();
            EXPECT_FALSE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_FALSE(mRegistry.isCommandEnabled(creativemotion_editor::kSandboxResetCommandId));
            EXPECT_FALSE(run(creativemotion_editor::kSandboxPushCommandId));
        }

        TEST_F(CmSandboxTest, thePanelListsTheBoneTheDirectionTheMagnitudeAndAStatus)
        {
            openBoth();
            ASSERT_NE(panel(), nullptr);
            const std::vector<PropertyDesc> descs = panel()->describe();
            auto find = [&](const char* id) -> const PropertyDesc* {
                for (const PropertyDesc& desc : descs)
                    if (desc.id == id)
                        return &desc;
                return nullptr;
            };
            const PropertyDesc* bone = find(creativemotion_editor::kSandboxBoneId);
            ASSERT_NE(bone, nullptr);
            EXPECT_EQ(bone->type, PropertyType::Choice);
            EXPECT_EQ(bone->choices, (std::vector<std::string>{ "Pelvis", "Spine", "LeftThigh", "LeftShin" }));
            const PropertyDesc* direction = find(creativemotion_editor::kSandboxDirectionId);
            ASSERT_NE(direction, nullptr);
            EXPECT_EQ(direction->type, PropertyType::Vec3);
            const PropertyDesc* magnitude = find(creativemotion_editor::kSandboxMagnitudeId);
            ASSERT_NE(magnitude, nullptr);
            EXPECT_EQ(magnitude->type, PropertyType::Float);
            EXPECT_EQ(magnitude->min, 0.0);
            EXPECT_EQ(magnitude->max, creativemotion_editor::kMaxPushNs);
            const PropertyDesc* status = find(creativemotion_editor::kSandboxStatusId);
            ASSERT_NE(status, nullptr);
            EXPECT_TRUE(status->readOnly);

            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxBoneId)), "Pelvis");
            const Vec3 d = std::get<Vec3>(panel()->get(creativemotion_editor::kSandboxDirectionId));
            EXPECT_EQ(d.x, 1.0);
            EXPECT_EQ(d.y, 0.0);
            EXPECT_EQ(d.z, 0.0);
            EXPECT_EQ(std::get<double>(panel()->get(creativemotion_editor::kSandboxMagnitudeId)),
                creativemotion_editor::kDefaultPushNs);
            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxStatusId)), "not built");
        }

        TEST_F(CmSandboxTest, editsUndoAndBadValuesAreRefused)
        {
            openBoth();
            ASSERT_NE(panel(), nullptr);
            auto command = panel()->makeSetCommand(creativemotion_editor::kSandboxMagnitudeId, 120.0);
            ASSERT_NE(command, nullptr);
            command->redo();
            EXPECT_EQ(std::get<double>(panel()->get(creativemotion_editor::kSandboxMagnitudeId)), 120.0);
            command->undo();
            EXPECT_EQ(std::get<double>(panel()->get(creativemotion_editor::kSandboxMagnitudeId)),
                creativemotion_editor::kDefaultPushNs);

            EXPECT_EQ(panel()->makeSetCommand(creativemotion_editor::kSandboxMagnitudeId, -1.0), nullptr);
            EXPECT_EQ(panel()->makeSetCommand(creativemotion_editor::kSandboxMagnitudeId, 1.0e9), nullptr);
            EXPECT_EQ(panel()->makeSetCommand(creativemotion_editor::kSandboxBoneId, std::string("Head")), nullptr);
            EXPECT_EQ(panel()->makeSetCommand(creativemotion_editor::kSandboxStatusId, std::string("x")), nullptr);
            EXPECT_EQ(panel()->makeSetCommand("sandbox.nothing", 1.0), nullptr);
            EXPECT_EQ(panel()->makeSetCommand(creativemotion_editor::kSandboxMagnitudeId, std::string("x")), nullptr);
        }

        TEST_F(CmSandboxTest, theFirstPushBuildsFromTheOpenedFilesAndQueuesTheScaledUnitDirectionAtTheBodyCentre)
        {
            openBoth();
            mSandbox.mPoses["LeftShin"] = SandboxBodyPose{ { 0.5, -0.25, 0.75 } };
            set(creativemotion_editor::kSandboxBoneId, std::string("LeftShin"));
            set(creativemotion_editor::kSandboxDirectionId, Vec3{ 0.0, 3.0, 4.0 });
            set(creativemotion_editor::kSandboxMagnitudeId, 10.0);

            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_EQ(mSandbox.mBuilds, 1);
            EXPECT_EQ(mSandbox.mRigPath, mRigPath);
            EXPECT_EQ(mSandbox.mProfilePath, mProfilePath);
            EXPECT_EQ(mSandbox.mYaw, 0.0);
            EXPECT_EQ(mSandbox.mOffset.x, 0.0);
            EXPECT_EQ(mSandbox.mOffset.y, 0.0);
            EXPECT_NEAR(mSandbox.mOffset.z, creativemotion_editor::dropOffsetM(makeLimitsRig()), 1.0e-12);

            ASSERT_EQ(mSandbox.mPushes.size(), 1u);
            const FakeSandbox::PushCall& call = mSandbox.mPushes[0];
            EXPECT_EQ(call.bone, "LeftShin");
            EXPECT_NEAR(call.impulse.x, 0.0, 1.0e-12);
            EXPECT_NEAR(call.impulse.y, 6.0, 1.0e-12);
            EXPECT_NEAR(call.impulse.z, 8.0, 1.0e-12);
            EXPECT_NEAR(call.point.x, 0.5, 1.0e-12);
            EXPECT_NEAR(call.point.y, -0.25, 1.0e-12);
            EXPECT_NEAR(call.point.z, 0.75, 1.0e-12);
            EXPECT_GE(mContext->mRedraws, 1);
            // A push is not project data: nothing went through the context's undo stack.
            EXPECT_TRUE(mContext->mCommands.empty());
        }

        TEST_F(CmSandboxTest, aSecondPushDoesNotBuildAgainButAnOpenedRigMakesTheNextOneBuildAgain)
        {
            openBoth();
            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));
            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_EQ(mSandbox.mBuilds, 1);
            EXPECT_EQ(mSandbox.mPushes.size(), 2u);

            openRig();
            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_EQ(mSandbox.mBuilds, 2);
            EXPECT_EQ(mSandbox.mPushes.size(), 3u);
        }

        TEST_F(CmSandboxTest, aZeroDirectionIsRefusedWithALogLineAndNothingIsQueued)
        {
            openBoth();
            set(creativemotion_editor::kSandboxDirectionId, Vec3{ 0.0, 0.0, 0.0 });
            run(creativemotion_editor::kSandboxPushCommandId);
            EXPECT_TRUE(mSandbox.mPushes.empty());
            EXPECT_TRUE(mContext->logged(LogLevel::Warning, "direction"));
        }

        TEST_F(CmSandboxTest, aFailedBuildIsLoggedWithTheHostsMessageAndNothingIsQueued)
        {
            openBoth();
            mSandbox.mFailBuild = true;
            run(creativemotion_editor::kSandboxPushCommandId);
            EXPECT_TRUE(mSandbox.mPushes.empty());
            EXPECT_TRUE(mContext->logged(LogLevel::Error, "the fake refuses"));
        }

        TEST_F(CmSandboxTest, resetBuildsWhenNothingIsBuiltAndOtherwiseAsksTheSandboxToReset)
        {
            openBoth();
            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            EXPECT_EQ(mSandbox.mBuilds, 1);
            EXPECT_EQ(mSandbox.mResets, 0);
            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            EXPECT_EQ(mSandbox.mBuilds, 1);
            EXPECT_EQ(mSandbox.mResets, 1);
        }

        TEST_F(CmSandboxTest, theStatusSaysWhatIsBuiltAndHowManyPushesWait)
        {
            openBoth();
            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));
            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxStatusId)),
                "built: 4 bodies, 1 push waiting");
            mSandbox.mPending = 0;
            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxStatusId)),
                "built: 4 bodies, 0 pushes waiting");
        }

        TEST_F(CmSandboxTest, aSimulationStepAsksForARedrawOnlyWhileTheSandboxIsBuilt)
        {
            openBoth();
            EditorEvent step;
            step.type = EditorEventType::SimulationStepped;
            step.step = 1;
            const int before = mContext->mRedraws;
            mContext->publish(step);
            EXPECT_EQ(mContext->mRedraws, before) << "nothing built: nothing moves";

            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            const int built = mContext->mRedraws;
            mContext->publish(step);
            EXPECT_EQ(mContext->mRedraws, built + 1);
        }

        TEST_F(CmSandboxTest, aJointSelectionOfABodyBoneBecomesThePanelsBone)
        {
            openBoth();
            mContext->select({ "joint", "LeftThigh" });
            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxBoneId)), "LeftThigh");
            // A joint on a bone with no body leaves the choice alone.
            mContext->select({ "joint", "NoBody" });
            EXPECT_EQ(std::get<std::string>(panel()->get(creativemotion_editor::kSandboxBoneId)), "LeftThigh");
        }

        class CmSandboxGizmoTest : public CmSandboxTest
        {
        protected:
            std::vector<DrawnLine> draw()
            {
                std::vector<Gizmo*> gizmos = mRegistry.gizmosOf(creativemotion_editor::kSandboxGizmoProviderId);
                RecordingSink sink;
                for (Gizmo* gizmo : gizmos)
                    gizmo->draw(sink);
                return sink.mLines;
            }
        };

        TEST_F(CmSandboxGizmoTest, itDrawsNothingUntilTheSandboxIsBuilt)
        {
            openBoth();
            EXPECT_TRUE(draw().empty());
        }

        TEST_F(CmSandboxGizmoTest, onceBuiltItDrawsTheGroundAtZ0AndEveryBodyAtItsPose)
        {
            openBoth();
            mSandbox.mPoses["Pelvis"] = SandboxBodyPose{ { 5.0, 5.0, 5.0 } };
            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            const std::vector<DrawnLine> lines = draw();
            ASSERT_FALSE(lines.empty());

            std::size_t onGround = 0;
            std::size_t nearPelvis = 0;
            for (const DrawnLine& line : lines)
            {
                if (line.a.z == 0.0 && line.b.z == 0.0)
                    ++onGround;
                // The pelvis box has half extents 0.1, 0.12, 0.08: every corner is within 0.2 of its centre.
                if (distanceBetween(line.a, math::Vec3{ 5.0, 5.0, 5.0 }) < 0.2
                    && distanceBetween(line.b, math::Vec3{ 5.0, 5.0, 5.0 }) < 0.2)
                    ++nearPelvis;
            }
            EXPECT_GE(onGround, 4u);
            EXPECT_EQ(nearPelvis, 12u) << "the 12 edges of the pelvis box";
        }

        TEST_F(CmSandboxGizmoTest, aRotatedBodyIsDrawnRotated)
        {
            openBoth();
            const double half = std::sqrt(0.5);
            mSandbox.mPoses["Pelvis"] = SandboxBodyPose{ { 5.0, 5.0, 5.0 }, 0.0, 0.0, half, half }; // 90 deg about Z
            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            // The box's x half extent 0.1 and y half extent 0.12 swap: some edge now runs along x with length 0.24.
            bool found = false;
            for (const DrawnLine& line : draw())
                if (distanceBetween(line.a, math::Vec3{ 5.0, 5.0, 5.0 }) < 0.2
                    && std::abs(std::abs(line.b.x - line.a.x) - 0.24) < 1.0e-9
                    && std::abs(line.b.y - line.a.y) < 1.0e-9)
                    found = true;
            EXPECT_TRUE(found);
        }

        TEST_F(CmSandboxGizmoTest, thePendingPushIsAnRedArrowFromTheBodyAndCyanOnceItWasApplied)
        {
            openBoth();
            mSandbox.mPoses["Spine"] = SandboxBodyPose{ { 1.0, 2.0, 3.0 } };
            set(creativemotion_editor::kSandboxBoneId, std::string("Spine"));
            set(creativemotion_editor::kSandboxDirectionId, Vec3{ 0.0, 0.0, 2.0 });
            ASSERT_TRUE(run(creativemotion_editor::kSandboxPushCommandId));

            auto arrowLines = [&](const Colour& colour) {
                std::vector<DrawnLine> result;
                for (const DrawnLine& line : draw())
                    if (line.colour.r == colour.r && line.colour.g == colour.g && line.colour.b == colour.b)
                        result.push_back(line);
                return result;
            };
            const Colour orange{ 1.0f, 0.15f, 0.15f, 1.0f }; // pending: red
            const Colour cyan{ 0.2f, 1.0f, 0.2f, 1.0f }; // applied: green
            std::vector<DrawnLine> pending = arrowLines(orange);
            ASSERT_FALSE(pending.empty());
            // The shaft starts at the push point and goes up (the direction is +Z).
            bool shaft = false;
            for (const DrawnLine& line : pending)
                if (distanceBetween(line.a, math::Vec3{ 1.0, 2.0, 3.0 }) < 1.0e-9 && line.b.z > line.a.z
                    && std::abs(line.b.x - line.a.x) < 1.0e-9)
                    shaft = true;
            EXPECT_TRUE(shaft);

            mSandbox.mPending = 0; // the step applied it
            EXPECT_TRUE(arrowLines(orange).empty());
            EXPECT_FALSE(arrowLines(cyan).empty());

            ASSERT_TRUE(run(creativemotion_editor::kSandboxResetCommandId));
            EXPECT_TRUE(arrowLines(cyan).empty()) << "a reset forgets the arrow";
        }

        TEST(CmSandboxDrop, theRigIsMovedUpSoItsLowestShapeIsTheClearanceAboveTheGround)
        {
            rig::Rig single;
            single.name = "single";
            single.skeleton.bones = { makeBone("Pelvis", -1, { 0.0, 0.0, 1.0 }, math::Quat{}) };
            single.bodies = { makeBody("Pelvis", rig::SphereShape{ 0.2 }, math::Transform{}, 10.0) };
            // The lowest point is at 1.0 - 0.2 = 0.8.
            EXPECT_NEAR(
                creativemotion_editor::dropOffsetM(single), creativemotion_editor::kDropClearanceM - 0.8, 1.0e-12);
        }
    }
}
