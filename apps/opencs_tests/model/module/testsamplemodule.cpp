// Task 0031, stage A: the sample editor module's behaviour, written from the spec alone.
//
// Assumptions about the sample module (sdk/examples/sample_editor_module/), which its author makes match:
//  - It exports openeden_editor_module_descriptor() (OPENEDEN_EDITOR_MODULE); its source is compiled into
//    eden-studio-tests so that the descriptor can be loaded with ModuleHost::loadStatic. The descriptor is declared
//    here with C linkage, and its id is "sample".
//  - Registered ids: workspace "sample.workspace", panel "sample.panel", command "sample.command", gizmo provider
//    "sample.gizmos", asset type formatId "sample.note" with extension ".note.json", inspector section
//    "sample.section", track provider "sample.tracks".
//  - A note document has the properties "title" (String) and "weight" (Float, min 0, max 10, unit "kg"). Its file
//    format is {"format":"sample.note","title":"...","weight":N}.
//  - Edits through makeSetCommand mark the document dirty once redo() ran; save() clears the dirty flag.
//    Weight edits carry a mergeId >= 0 (the same for both edits); mergeWith absorbs the later command into the
//    earlier one, so that the earlier command's undo() restores the value from before both.
//  - The document's gizmos() returns exactly one gizmo, which draws exactly 3 lines (an axis cross) and 1 point at the
//  note's position, and
//    has one handle "sample.handle" at that position. drag("sample.handle", p) returns a command that moves the handle
//    to p on redo() and back on undo().
//  - The document's tracks() returns one track with 2 keys at 0.0 and 1.5 seconds.
//  - The inspector section's create(document) returns a PropertySource over the same document (title, weight).
//  - The note document is not dirty right after open.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

extern "C" const openeden::editor::EditorModuleDescriptor* openeden_editor_module_descriptor();

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        class NullContext : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view) override {}
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
        };

        class RecordingSink : public OverlaySink
        {
        public:
            void line(const Vec3& a, const Vec3& b, const Colour&) override { mLines.emplace_back(a, b); }
            void point(const Vec3& p, const Colour&, float) override { mPoints.push_back(p); }
            void label(const Vec3&, std::string_view text, const Colour&) override { mLabels.emplace_back(text); }

            std::vector<std::pair<Vec3, Vec3>> mLines;
            std::vector<Vec3> mPoints;
            std::vector<std::string> mLabels;
        };

        class SampleModuleTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-sample-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);

                mHost = std::make_unique<ModuleHost>(mRegistry, mContext);
                const EditorModuleDescriptor* descriptor = openeden_editor_module_descriptor();
                ASSERT_NE(descriptor, nullptr);
                mHost->loadStatic("sample", *descriptor, ModuleSource::Origin::Extension);
            }

            void TearDown() override
            {
                mDocument.reset();
                mHost.reset();
                std::filesystem::remove_all(mDir);
            }

            std::filesystem::path writeNote(const std::string& name, const std::string& title, double weight)
            {
                const std::filesystem::path path = mDir / (name + ".note.json");
                std::ofstream(path) << "{\"format\":\"sample.note\",\"title\":\"" << title << "\",\"weight\":" << weight
                                    << "}";
                return path;
            }

            AssetDocument& open(const std::filesystem::path& path)
            {
                const auto* type = mRegistry.findAssetType(path);
                EXPECT_NE(type, nullptr);
                std::string error;
                mDocument = type->mDesc.open(path, error);
                EXPECT_NE(mDocument, nullptr) << error;
                return *mDocument;
            }

            static std::string readAll(const std::filesystem::path& path)
            {
                std::ifstream in(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }

            ModuleRegistry mRegistry;
            NullContext mContext;
            std::unique_ptr<ModuleHost> mHost;
            std::unique_ptr<AssetDocument> mDocument;
            std::filesystem::path mDir;
        };

        TEST_F(SampleModuleTest, registersOneOfEveryKind)
        {
            const auto modules = mHost->modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

            ASSERT_EQ(mRegistry.workspaces().size(), 1u);
            EXPECT_EQ(mRegistry.workspaces().front().mDesc.id, "sample.workspace");
            ASSERT_EQ(mRegistry.panels().size(), 1u);
            EXPECT_EQ(mRegistry.panels().front().mDesc.id, "sample.panel");
            EXPECT_EQ(mRegistry.panels().front().mDesc.workspaceId, "sample.workspace");
            ASSERT_EQ(mRegistry.commands().size(), 2u);
            EXPECT_EQ(mRegistry.commands().front().mDesc.id, "sample.command");
            ASSERT_EQ(mRegistry.gizmoProviders().size(), 1u);
            EXPECT_EQ(mRegistry.gizmoProviders().front().mDesc.id, "sample.gizmos");
            ASSERT_EQ(mRegistry.assetTypes().size(), 1u);
            EXPECT_EQ(mRegistry.assetTypes().front().mDesc.formatId, "sample.note");
            EXPECT_EQ(mRegistry.assetTypes().front().mDesc.fileExtension, ".note.json");
            ASSERT_EQ(mRegistry.sections().size(), 1u);
            EXPECT_EQ(mRegistry.sections().front().mDesc.id, "sample.section");
            ASSERT_EQ(mRegistry.trackProviders().size(), 1u);
            EXPECT_EQ(mRegistry.trackProviders().front().mDesc.id, "sample.tracks");
        }

        TEST_F(SampleModuleTest, theSampleCommandRunsWithoutThrowing)
        {
            EXPECT_TRUE(mRegistry.runCommand("sample.command"));
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);
        }

        TEST_F(SampleModuleTest, aNoteOpensFromATempFileAndDescribesItsProperties)
        {
            AssetDocument& document = open(writeNote("one", "First", 2.5));
            EXPECT_FALSE(document.isDirty());

            const std::vector<PropertyDesc> descs = document.properties().describe();
            const auto title
                = std::find_if(descs.begin(), descs.end(), [](const PropertyDesc& d) { return d.id == "title"; });
            const auto weight
                = std::find_if(descs.begin(), descs.end(), [](const PropertyDesc& d) { return d.id == "weight"; });
            ASSERT_NE(title, descs.end());
            ASSERT_NE(weight, descs.end());
            EXPECT_EQ(title->type, PropertyType::String);
            EXPECT_EQ(weight->type, PropertyType::Float);
            EXPECT_EQ(weight->min, 0.0);
            EXPECT_EQ(weight->max, 10.0);
            EXPECT_EQ(weight->unit, "kg");

            EXPECT_EQ(std::get<std::string>(document.properties().get("title")), "First");
            EXPECT_DOUBLE_EQ(std::get<double>(document.properties().get("weight")), 2.5);
        }

        TEST_F(SampleModuleTest, openingAMissingOrMalformedFileGivesAnError)
        {
            const auto* type = mRegistry.findAssetType("x.note.json");
            ASSERT_NE(type, nullptr);

            std::string error;
            EXPECT_EQ(type->mDesc.open(mDir / "missing.note.json", error), nullptr);
            EXPECT_FALSE(error.empty());

            const std::filesystem::path bad = mDir / "bad.note.json";
            std::ofstream(bad) << "{ this is not json";
            error.clear();
            EXPECT_EQ(type->mDesc.open(bad, error), nullptr);
            EXPECT_FALSE(error.empty());

            const std::filesystem::path wrongFormat = mDir / "wrong.note.json";
            std::ofstream(wrongFormat) << "{\"format\":\"other\",\"title\":\"x\",\"weight\":1}";
            error.clear();
            EXPECT_EQ(type->mDesc.open(wrongFormat, error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        TEST_F(SampleModuleTest, editsGoThroughMakeSetCommandAndRedoUndoRestoreTheValue)
        {
            AssetDocument& document = open(writeNote("edit", "First", 2.5));
            PropertySource& properties = document.properties();

            auto titleCommand = properties.makeSetCommand("title", PropertyValue(std::string("Second")));
            ASSERT_NE(titleCommand, nullptr);
            EXPECT_FALSE(titleCommand->label().empty());
            // makeSetCommand changes nothing itself.
            EXPECT_EQ(std::get<std::string>(properties.get("title")), "First");
            EXPECT_FALSE(document.isDirty());

            titleCommand->redo();
            EXPECT_EQ(std::get<std::string>(properties.get("title")), "Second");
            EXPECT_TRUE(document.isDirty());
            titleCommand->undo();
            EXPECT_EQ(std::get<std::string>(properties.get("title")), "First");
            titleCommand->redo();
            EXPECT_EQ(std::get<std::string>(properties.get("title")), "Second");
        }

        TEST_F(SampleModuleTest, badEditsAreRefusedWithANullCommand)
        {
            AssetDocument& document = open(writeNote("refuse", "First", 2.5));
            PropertySource& properties = document.properties();

            EXPECT_EQ(properties.makeSetCommand("unknown", PropertyValue(1.0)), nullptr);
            EXPECT_EQ(properties.makeSetCommand("weight", PropertyValue(std::string("heavy"))), nullptr); // wrong type
            EXPECT_EQ(properties.makeSetCommand("weight", PropertyValue(10.5)), nullptr); // above max
            EXPECT_EQ(properties.makeSetCommand("weight", PropertyValue(-0.5)), nullptr); // below min
            EXPECT_EQ(properties.makeSetCommand("title", PropertyValue(true)), nullptr); // wrong type
            EXPECT_DOUBLE_EQ(std::get<double>(properties.get("weight")), 2.5);
        }

        TEST_F(SampleModuleTest, twoWeightEditsWithTheSameMergeIdMergeIntoOneStep)
        {
            AssetDocument& document = open(writeNote("merge", "First", 2.5));
            PropertySource& properties = document.properties();

            auto first = properties.makeSetCommand("weight", PropertyValue(3.0));
            ASSERT_NE(first, nullptr);
            first->redo();
            auto second = properties.makeSetCommand("weight", PropertyValue(4.0));
            ASSERT_NE(second, nullptr);
            second->redo();

            ASSERT_GE(first->mergeId(), 0);
            EXPECT_EQ(first->mergeId(), second->mergeId());
            EXPECT_TRUE(first->mergeWith(*second));
            EXPECT_DOUBLE_EQ(std::get<double>(properties.get("weight")), 4.0);

            // One undo step restores the value from before both edits; redo brings the latest one back.
            first->undo();
            EXPECT_DOUBLE_EQ(std::get<double>(properties.get("weight")), 2.5);
            first->redo();
            EXPECT_DOUBLE_EQ(std::get<double>(properties.get("weight")), 4.0);
        }

        TEST_F(SampleModuleTest, editsOfDifferentPropertiesDoNotMerge)
        {
            AssetDocument& document = open(writeNote("nomerge", "First", 2.5));
            PropertySource& properties = document.properties();

            auto weight = properties.makeSetCommand("weight", PropertyValue(3.0));
            auto title = properties.makeSetCommand("title", PropertyValue(std::string("T")));
            ASSERT_NE(weight, nullptr);
            ASSERT_NE(title, nullptr);
            weight->redo();
            title->redo();
            if (weight->mergeId() >= 0 && weight->mergeId() == title->mergeId())
                EXPECT_FALSE(weight->mergeWith(*title));
            else
                SUCCEED();
        }

        TEST_F(SampleModuleTest, saveWritesTextThatReadsBack)
        {
            const std::filesystem::path path = writeNote("save", "First", 2.5);
            AssetDocument& document = open(path);

            auto title = document.properties().makeSetCommand("title", PropertyValue(std::string("Saved title")));
            auto weight = document.properties().makeSetCommand("weight", PropertyValue(7.25));
            ASSERT_NE(title, nullptr);
            ASSERT_NE(weight, nullptr);
            title->redo();
            weight->redo();
            EXPECT_TRUE(document.isDirty());

            std::string error;
            ASSERT_TRUE(document.save(error)) << error;
            EXPECT_TRUE(error.empty());
            EXPECT_FALSE(document.isDirty());

            const std::string text = readAll(path);
            EXPECT_NE(text.find("sample.note"), std::string::npos) << text;
            EXPECT_NE(text.find("Saved title"), std::string::npos) << text;

            // Saving twice gives identical bytes (canonical text).
            std::string again;
            ASSERT_TRUE(document.save(again)) << again;
            EXPECT_EQ(readAll(path), text);

            mDocument.reset();
            AssetDocument& reopened = open(path);
            EXPECT_EQ(std::get<std::string>(reopened.properties().get("title")), "Saved title");
            EXPECT_DOUBLE_EQ(std::get<double>(reopened.properties().get("weight")), 7.25);
            EXPECT_FALSE(reopened.isDirty());
        }

        TEST_F(SampleModuleTest, saveToAnUnwritablePathReportsAnError)
        {
            const std::filesystem::path path = writeNote("unwritable", "First", 1.0);
            AssetDocument& document = open(path);
            // Remove the folder so the save has nowhere to go.
            std::filesystem::remove_all(mDir);
            std::string error;
            EXPECT_FALSE(document.save(error));
            EXPECT_FALSE(error.empty());
            std::filesystem::create_directories(mDir);
        }

        TEST_F(SampleModuleTest, theGizmoDrawsAnAxisCrossAndOnePointIntoARecordingSink)
        {
            AssetDocument& document = open(writeNote("gizmo", "First", 1.0));
            const std::vector<Gizmo*> gizmos = document.gizmos();
            ASSERT_EQ(gizmos.size(), 1u);
            ASSERT_NE(gizmos.front(), nullptr);
            EXPECT_FALSE(gizmos.front()->id().empty());

            RecordingSink sink;
            gizmos.front()->draw(sink);
            EXPECT_EQ(sink.mLines.size(), 3u);
            ASSERT_EQ(sink.mPoints.size(), 1u);

            // The axis cross's lines all pass through the note's position, the single point.
            const Vec3 centre = sink.mPoints.front();
            for (const auto& [a, b] : sink.mLines)
            {
                const bool touches = (a.x == centre.x && a.y == centre.y && a.z == centre.z)
                    || (b.x == centre.x && b.y == centre.y && b.z == centre.z)
                    || (0.5 * (a.x + b.x) == centre.x && 0.5 * (a.y + b.y) == centre.y
                        && 0.5 * (a.z + b.z) == centre.z);
                EXPECT_TRUE(touches);
            }

            const std::vector<GizmoHandle> handles = gizmos.front()->handles();
            ASSERT_EQ(handles.size(), 1u);
            EXPECT_EQ(handles.front().id, "sample.handle");
            EXPECT_EQ(handles.front().position.x, centre.x);
            EXPECT_EQ(handles.front().position.y, centre.y);
            EXPECT_EQ(handles.front().position.z, centre.z);
        }

        TEST_F(SampleModuleTest, aHandleDragReturnsACommandThatMovesIt)
        {
            AssetDocument& document = open(writeNote("drag", "First", 1.0));
            const std::vector<Gizmo*> gizmos = document.gizmos();
            ASSERT_EQ(gizmos.size(), 1u);
            Gizmo& gizmo = *gizmos.front();
            const Vec3 before = gizmo.handles().front().position;

            auto command = gizmo.drag("sample.handle", Vec3{ 1.0, 2.0, 3.0 });
            ASSERT_NE(command, nullptr);
            // Nothing moves until the command runs.
            EXPECT_EQ(gizmo.handles().front().position.x, before.x);

            command->redo();
            ASSERT_EQ(gizmo.handles().size(), 1u);
            EXPECT_EQ(gizmo.handles().front().position.x, 1.0);
            EXPECT_EQ(gizmo.handles().front().position.y, 2.0);
            EXPECT_EQ(gizmo.handles().front().position.z, 3.0);

            // The drawn point follows the handle.
            RecordingSink sink;
            gizmo.draw(sink);
            ASSERT_EQ(sink.mPoints.size(), 1u);
            EXPECT_EQ(sink.mPoints.front().x, 1.0);
            EXPECT_EQ(sink.mPoints.front().y, 2.0);
            EXPECT_EQ(sink.mPoints.front().z, 3.0);

            command->undo();
            EXPECT_EQ(gizmo.handles().front().position.x, before.x);
            EXPECT_EQ(gizmo.handles().front().position.y, before.y);
            EXPECT_EQ(gizmo.handles().front().position.z, before.z);
        }

        TEST_F(SampleModuleTest, draggingAnUnknownHandleReturnsNull)
        {
            AssetDocument& document = open(writeNote("dragunknown", "First", 1.0));
            const std::vector<Gizmo*> gizmos = document.gizmos();
            ASSERT_EQ(gizmos.size(), 1u);
            EXPECT_EQ(gizmos.front()->drag("no-such-handle", Vec3{}), nullptr);
        }

        TEST_F(SampleModuleTest, theTrackReturnsItsTwoKeys)
        {
            AssetDocument& document = open(writeNote("track", "First", 1.0));
            const std::vector<TimelineTrack*> tracks = document.tracks();
            ASSERT_EQ(tracks.size(), 1u);
            ASSERT_NE(tracks.front(), nullptr);
            EXPECT_FALSE(tracks.front()->id().empty());
            EXPECT_FALSE(tracks.front()->title().empty());

            const std::vector<TimelineKey> keys = tracks.front()->keys();
            ASSERT_EQ(keys.size(), 2u);
            EXPECT_DOUBLE_EQ(keys[0].timeSeconds, 0.0);
            EXPECT_DOUBLE_EQ(keys[1].timeSeconds, 1.5);
        }

        TEST_F(SampleModuleTest, theInspectorSectionIsBuiltOverTheDocument)
        {
            AssetDocument& document = open(writeNote("section", "First", 1.0));
            const auto& section = mRegistry.sections().front().mDesc;
            EXPECT_EQ(section.selectionKind, "sample.note");

            PropertySource* source = section.create(document);
            ASSERT_NE(source, nullptr);
            const std::vector<PropertyDesc> descs = source->describe();
            const bool hasTitle
                = std::any_of(descs.begin(), descs.end(), [](const PropertyDesc& d) { return d.id == "title"; });
            const bool hasWeight
                = std::any_of(descs.begin(), descs.end(), [](const PropertyDesc& d) { return d.id == "weight"; });
            EXPECT_TRUE(hasTitle);
            EXPECT_TRUE(hasWeight);
            EXPECT_EQ(std::get<std::string>(source->get("title")), "First");
        }

        TEST_F(SampleModuleTest, thePanelAndTheGizmoProviderCallbacksDoNotThrow)
        {
            // Without an open asset they may return null or empty; they must not throw or disable the module.
            EXPECT_NO_THROW(mRegistry.panelSource("sample.panel"));
            EXPECT_NO_THROW(mRegistry.gizmosOf("sample.gizmos"));
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);
        }

        TEST_F(SampleModuleTest, shutdownWithdrawsEverything)
        {
            mHost->shutdown();
            EXPECT_TRUE(mRegistry.workspaces().empty());
            EXPECT_TRUE(mRegistry.panels().empty());
            EXPECT_TRUE(mRegistry.commands().empty());
            EXPECT_TRUE(mRegistry.gizmoProviders().empty());
            EXPECT_TRUE(mRegistry.assetTypes().empty());
            EXPECT_TRUE(mRegistry.sections().empty());
            EXPECT_TRUE(mRegistry.trackProviders().empty());
        }
    }
}
