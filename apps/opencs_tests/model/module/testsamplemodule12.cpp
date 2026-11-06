// Task 0091, stage f: the sample editor module uses every addition of API 1.2, as the reference for module authors and
// as a test. Written from the spec alone, before the sample changed.
//
// Contract pinned here (sdk/examples/sample_editor_module, id "sample"):
//  - Settings: "sample.greeting" (String, default "Hello from the sample module", the text of the Say Hello command's
//  log
//    line) and "sample.axisHalfLength" (Float, default 3.0, range 0.5 to 10, unit "m": how far the note gizmo's axis
//    cross reaches each way, read at every drawing).
//  - One bus subscription. Every event is counted in the note's read-only Int property "events" (group "Studio");
//    SelectionChanged with kind "handle" and id "sample.handle" marks the handle as selected.
//  - Labels: the note gizmo draws exactly one label, the note's title, at the handle's position; its colour is white
//    {1, 1, 1, 1} until the handle is selected (a click on it) and {1, 1, 0.2, 1} while it is.
//  - Task: the command "sample.command" starts a background task that computes a checksum of the title (a 64-bit FNV-1a
//    hash written as 16 hex digits) in 4 steps with progress; when it is Finished the note's read-only String property
//    "checksum" shows it; a Cancelled or Failed task leaves it empty. With a context that runs no tasks (startTask
//    gives 0) the command only logs the greeting.
#include "apps/opencs/model/module/eventbus.hpp"
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

extern "C" const openeden::editor::EditorModuleDescriptor* openeden_editor_module_descriptor();

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        /// A host as complete as the module needs: selection, events, settings, and tasks run at once on this thread.
        class FullContext final : public EditorContext
        {
        public:
            explicit FullContext(ModuleRegistry& registry)
                : mRegistry(registry)
                , mSelection(registry.bus())
            {
            }

            void pushCommand(std::unique_ptr<Command> command) override
            {
                command->redo();
                mPushed.push_back(std::move(command));
            }
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view message) override { mLog.emplace_back(message); }
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }
            Selection selection() const override { return mSelection.current(); }
            void select(const Selection& selection) override { mSelection.select(selection); }
            void publish(const EditorEvent& event) override { mRegistry.bus().publish(event); }
            std::optional<PropertyValue> setting(std::string_view id) const override
            {
                return mRegistry.settings().get(id);
            }

            TaskId startTask(TaskDesc task) override
            {
                if (!task.work)
                    return 0;
                struct Control final : TaskControl
                {
                    bool mCancel = false;
                    std::vector<double>* mProgress = nullptr;
                    bool cancelled() const override { return mCancel; }
                    void reportProgress(double fraction, std::string_view) override { mProgress->push_back(fraction); }
                } control;
                control.mCancel = mCancelTasks;
                control.mProgress = &mProgressSeen;
                TaskOutcome outcome = TaskOutcome::Finished;
                std::string error;
                try
                {
                    task.work(control);
                    if (control.mCancel)
                        outcome = TaskOutcome::Cancelled;
                }
                catch (const std::exception& e)
                {
                    outcome = TaskOutcome::Failed;
                    error = e.what();
                }
                if (task.done)
                    task.done(outcome, error);
                ++mTasksStarted;
                return 1;
            }

            AssetDocument* mActive = nullptr;
            std::vector<std::string> mLog;
            std::vector<std::unique_ptr<Command>> mPushed;
            std::vector<double> mProgressSeen;
            int mRedraws = 0;
            int mTasksStarted = 0;
            bool mCancelTasks = false;

        private:
            ModuleRegistry& mRegistry;
            SelectionModel mSelection;
        };

        class NoTaskContext final : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view message) override { mLog.emplace_back(message); }
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }
            AssetDocument* mActive = nullptr;
            std::vector<std::string> mLog;
        };

        struct Line
        {
            Vec3 a, b;
        };

        class RecordingSink final : public OverlaySink
        {
        public:
            void line(const Vec3& a, const Vec3& b, const Colour&) override { mLines.push_back({ a, b }); }
            void point(const Vec3&, const Colour&, float) override {}
            void label(const Vec3& p, std::string_view text, const Colour& colour) override
            {
                mLabelPositions.push_back(p);
                mLabels.emplace_back(text);
                mLabelColours.push_back(colour);
            }
            std::vector<Line> mLines;
            std::vector<Vec3> mLabelPositions;
            std::vector<std::string> mLabels;
            std::vector<Colour> mLabelColours;
        };

        class Sample12Test : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-sample12-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
                mContext = std::make_unique<FullContext>(mRegistry);
                mHost = std::make_unique<ModuleHost>(mRegistry, *mContext);
                mHost->loadStatic("sample", *openeden_editor_module_descriptor(), ModuleSource::Origin::Extension);
                ASSERT_EQ(mHost->modules().at(0).mState, ModuleInfo::State::Loaded) << mHost->modules()[0].mMessage;
            }
            void TearDown() override
            {
                mContext->mActive = nullptr;
                mDocument.reset();
                mHost.reset();
                mContext.reset();
                std::filesystem::remove_all(mDir);
            }

            AssetDocument& open(const std::string& title)
            {
                const std::filesystem::path path = mDir / "n.note.json";
                std::ofstream(path) << "{\"format\":\"sample.note\",\"title\":\"" << title << "\",\"weight\":2.0}";
                std::string error;
                mDocument = mRegistry.findAssetType(path)->mDesc.open(path, error);
                EXPECT_NE(mDocument, nullptr) << error;
                mContext->mActive = mDocument.get();
                return *mDocument;
            }

            std::int64_t eventsOf(AssetDocument& document)
            {
                return std::get<std::int64_t>(document.properties().get("events"));
            }

            ModuleRegistry mRegistry;
            std::unique_ptr<FullContext> mContext;
            std::unique_ptr<ModuleHost> mHost;
            std::unique_ptr<AssetDocument> mDocument;
            std::filesystem::path mDir;
        };

        TEST_F(Sample12Test, itRegistersTwoSettingsAndOneSubscription)
        {
            const auto& entries = mRegistry.settings().entries();
            ASSERT_EQ(entries.size(), 2u);
            EXPECT_EQ(entries[0].mDesc.property.id, "sample.greeting");
            EXPECT_EQ(entries[0].mDesc.property.type, PropertyType::String);
            EXPECT_EQ(std::get<std::string>(entries[0].mValue), "Hello from the sample module");
            EXPECT_EQ(entries[1].mDesc.property.id, "sample.axisHalfLength");
            EXPECT_EQ(entries[1].mDesc.property.type, PropertyType::Float);
            EXPECT_EQ(entries[1].mDesc.property.min, 0.5);
            EXPECT_EQ(entries[1].mDesc.property.max, 10.0);
            EXPECT_EQ(entries[1].mDesc.property.unit, "m");
            EXPECT_EQ(std::get<double>(entries[1].mValue), 3.0);
            EXPECT_EQ(mRegistry.bus().subscriberCount(), 1u);
        }

        TEST_F(Sample12Test, theAxisCrossReadsItsLengthFromTheSettingAtEveryDrawing)
        {
            AssetDocument& document = open("First");
            RecordingSink before;
            document.gizmos().at(0)->draw(before);
            ASSERT_EQ(before.mLines.size(), 3u);
            EXPECT_DOUBLE_EQ(before.mLines[0].b.x - before.mLines[0].a.x, 6.0);

            mRegistry.settings().set("sample.axisHalfLength", PropertyValue(5.0));
            RecordingSink after;
            document.gizmos().at(0)->draw(after);
            EXPECT_DOUBLE_EQ(after.mLines[0].b.x - after.mLines[0].a.x, 10.0);
        }

        TEST_F(Sample12Test, theGizmoDrawsTheTitleAsOneLabelAtTheHandle)
        {
            AssetDocument& document = open("First");
            RecordingSink sink;
            document.gizmos().at(0)->draw(sink);
            ASSERT_EQ(sink.mLabels.size(), 1u);
            EXPECT_EQ(sink.mLabels[0], "First");
            const std::vector<GizmoHandle> handles = document.gizmos().at(0)->handles();
            ASSERT_EQ(handles.size(), 1u);
            EXPECT_DOUBLE_EQ(sink.mLabelPositions[0].x, handles[0].position.x);
            EXPECT_DOUBLE_EQ(sink.mLabelPositions[0].z, handles[0].position.z);
            EXPECT_EQ(sink.mLabelColours[0].g, 1.0f);
            EXPECT_EQ(sink.mLabelColours[0].b, 1.0f);
        }

        TEST_F(Sample12Test, aClickOnTheHandleHighlightsTheLabelAndAnotherSelectionClearsIt)
        {
            AssetDocument& document = open("First");
            mContext->select({ "handle", "sample.handle" });
            RecordingSink selected;
            document.gizmos().at(0)->draw(selected);
            EXPECT_EQ(selected.mLabelColours.at(0).b, 0.2f);
            EXPECT_EQ(selected.mLabelColours.at(0).r, 1.0f);

            mContext->select({ "handle", "other" });
            RecordingSink other;
            document.gizmos().at(0)->draw(other);
            EXPECT_EQ(other.mLabelColours.at(0).b, 1.0f);
        }

        TEST_F(Sample12Test, everyEventOfTheBusIsCountedInTheEventsProperty)
        {
            AssetDocument& document = open("First");
            EXPECT_EQ(eventsOf(document), 0);
            EditorEvent step;
            step.type = EditorEventType::SimulationStepped;
            step.step = 7;
            mContext->publish(step);
            mContext->select({ "handle", "sample.handle" });
            EditorEvent changed;
            changed.type = EditorEventType::ParameterChanged;
            changed.subject = "weight";
            mContext->publish(changed);
            EXPECT_EQ(eventsOf(document), 3);
            EXPECT_GE(mContext->mRedraws, 1); // the selection changed what the gizmo draws
        }

        TEST_F(Sample12Test, theCommandRunsABackgroundChecksumTaskAndTheResultShowsInTheChecksumProperty)
        {
            AssetDocument& document = open("First");
            EXPECT_EQ(std::get<std::string>(document.properties().get("checksum")), "");
            ASSERT_TRUE(mRegistry.runCommand("sample.command"));
            EXPECT_EQ(mContext->mTasksStarted, 1);
            // FNV-1a, 64 bit, of "First".
            std::uint64_t hash = 14695981039346656037ull;
            for (const char c : std::string("First"))
            {
                hash ^= static_cast<unsigned char>(c);
                hash *= 1099511628211ull;
            }
            char expected[17];
            std::snprintf(expected, sizeof(expected), "%016llx", static_cast<unsigned long long>(hash));
            EXPECT_EQ(std::get<std::string>(document.properties().get("checksum")), std::string(expected));
            ASSERT_EQ(mContext->mProgressSeen.size(), 4u);
            EXPECT_EQ(mContext->mProgressSeen.back(), 1.0);
            EXPECT_EQ(mContext->mLog.back().find("Hello from the sample module"), 0u) << mContext->mLog.back();
        }

        TEST_F(Sample12Test, aCancelledTaskLeavesTheChecksumEmpty)
        {
            AssetDocument& document = open("First");
            mContext->mCancelTasks = true;
            mRegistry.runCommand("sample.command");
            EXPECT_EQ(std::get<std::string>(document.properties().get("checksum")), "");
        }

        TEST_F(Sample12Test, theGreetingSettingIsTheTextOfTheCommandsLogLine)
        {
            open("First");
            mRegistry.settings().set("sample.greeting", PropertyValue(std::string("Hi there")));
            mRegistry.runCommand("sample.command");
            EXPECT_EQ(mContext->mLog.back(), "Hi there");
        }

        TEST(Sample12NoTasks, withAContextThatRunsNoTasksTheCommandOnlyLogsTheGreeting)
        {
            ModuleRegistry registry;
            NoTaskContext context;
            ModuleHost host(registry, context);
            host.loadStatic("sample", *openeden_editor_module_descriptor(), ModuleSource::Origin::Extension);
            EXPECT_TRUE(registry.runCommand("sample.command"));
            ASSERT_FALSE(context.mLog.empty());
            EXPECT_EQ(context.mLog.back(), "Hello from the sample module");
            EXPECT_EQ(host.modules().at(0).mState, ModuleInfo::State::Loaded);
        }
    }
}
