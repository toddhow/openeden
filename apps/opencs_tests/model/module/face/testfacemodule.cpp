// Task 0147: the face editor module as Eden Studio loads it, through ModuleRegistry (the module's sources are linked
// into the test; the shared library is loaded by the editor check). Written after the module, so the red evidence is
// the stubbed pure parts; the mutations tried are in the task's stage log.
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/face_editor/face_document.hpp"
#include "editor_modules/face_editor/face_editor_module.hpp"
#include "editor_modules/face_editor/models_folder.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    using namespace openeden::editor;
    namespace fs = std::filesystem;

    class TestContext final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command> c) override
        {
            c->redo();
            mSteps.push_back(std::move(c));
        }
        void markDirty(std::string_view) override { ++mDirty; }
        void log(LogLevel level, std::string_view message) override { mLog.emplace_back(level, std::string(message)); }
        void requestRedraw() override { ++mRedraws; }
        AssetDocument* openAsset(const fs::path& path) override
        {
            std::string error;
            mActive = mRegistry->openAsset(path, error);
            if (!mActive)
                mLog.emplace_back(LogLevel::Error, error);
            return mActive.get();
        }
        AssetDocument* activeAsset() override { return mActive.get(); }
        fs::path userDataPath() const override { return mUserData; }
        std::optional<PropertyValue> setting(std::string_view id) const override
        {
            if (id == "face.audioRoot")
                return PropertyValue(mAudioRoot);
            return std::nullopt;
        }
        std::optional<std::vector<PropertyValue>> showDialog(const DialogDesc& desc) override
        {
            mDialogs.push_back(desc);
            return mAnswer;
        }
        TaskId startTask(TaskDesc task) override
        {
            // Run it at once on this thread, as a pool thread followed by the UI-thread callbacks would.
            struct Control final : TaskControl
            {
                bool cancelled() const override { return false; }
                void reportProgress(double f, std::string_view) override { mProgress.push_back(f); }
                std::vector<double> mProgress;
            } control;
            task.work(control);
            mTaskProgress = control.mProgress;
            if (task.done)
                task.done(TaskOutcome::Finished, {});
            ++mTasks;
            return 1;
        }

        CSMModule::ModuleRegistry* mRegistry = nullptr;
        std::unique_ptr<AssetDocument> mActive;
        fs::path mUserData;
        std::string mAudioRoot;
        std::optional<std::vector<PropertyValue>> mAnswer;
        std::vector<DialogDesc> mDialogs;
        std::vector<std::unique_ptr<Command>> mSteps;
        std::vector<std::pair<LogLevel, std::string>> mLog;
        std::vector<double> mTaskProgress;
        int mDirty = 0, mRedraws = 0, mTasks = 0;
    };

    struct Fixture
    {
        explicit Fixture(const char* name)
        {
            mDir = fs::temp_directory_path() / ("oe_face_mod_" + std::string(name));
            fs::remove_all(mDir);
            fs::create_directories(mDir / "userdata");
            mContext.mRegistry = &mRegistry;
            mContext.mUserData = mDir / "userdata";
            mRegistry.setContext(&mContext);
            mRegistry.beginModule(face_editor::kModuleId);
            mModule.reset(face_editor::moduleDescriptor()->create());
            mModule->registerUi(mRegistry);
            mRegistry.endModule();
        }
        ~Fixture()
        {
            mRegistry.removeModule(face_editor::kModuleId);
            mContext.mActive.reset();
            mModule->shutdown();
            face_editor::moduleDescriptor()->destroy(mModule.release());
        }
        fs::path mDir;
        TestContext mContext;
        CSMModule::ModuleRegistry mRegistry;
        std::unique_ptr<EditorModule> mModule;
    };

    bool hasCommand(const CSMModule::ModuleRegistry& r, const std::string& id)
    {
        for (const auto& c : r.commands())
            if (c.mDesc.id == id)
                return true;
        return false;
    }
}

TEST(FaceModule, descriptorIsForTheCurrentApi)
{
    EXPECT_EQ(face_editor::moduleDescriptor()->apiVersion, kApiVersion);
    EXPECT_STREQ(face_editor::moduleDescriptor()->id, "face_editor");
}

TEST(FaceModule, registersItsWorkspacePanelsAssetTypeCommandsAndSetting)
{
    Fixture f("register");
    EXPECT_EQ(f.mRegistry.workspaces().size(), 1u);
    EXPECT_EQ(f.mRegistry.panels().size(), 2u);
    ASSERT_EQ(f.mRegistry.assetTypes().size(), 1u);
    EXPECT_EQ(f.mRegistry.assetTypes()[0].mDesc.fileExtension, ".facecurves.json");
    for (const char* id : { "face.newCurves", "face.setAudio", "face.generate", "face.verifyModels" })
        EXPECT_TRUE(hasCommand(f.mRegistry, id)) << id;
    // Every id carries the module's prefix.
    for (const auto& c : f.mRegistry.commands())
        EXPECT_EQ(c.mDesc.id.rfind("face.", 0), 0u);
    EXPECT_FALSE(f.mRegistry.settings().entries().empty());
}

TEST(FaceModule, theReadmeIsWrittenIntoTheModelsFolderOnFirstUse)
{
    Fixture f("readme");
    EXPECT_TRUE(fs::exists(f.mDir / "userdata" / "models" / "audio2face" / "README.txt"));
}

TEST(FaceModule, theModelsPanelListsTheThreeVariantsAsMissing)
{
    Fixture f("models");
    PropertySource* source = f.mRegistry.panelSource("face.models");
    ASSERT_NE(source, nullptr);
    auto rows = source->describe();
    ASSERT_EQ(rows.size(), 4u); // the folder and three variants
    EXPECT_NE(std::get<std::string>(source->get("models.v3.0")).find("Missing"), std::string::npos);
    EXPECT_NE(std::get<std::string>(source->get("models.folder")).find("models/audio2face"), std::string::npos);
}

TEST(FaceModule, theCurvesPanelHasNoSourceWithoutAFaceAsset)
{
    Fixture f("nopanel");
    EXPECT_EQ(f.mRegistry.panelSource("face.panel"), nullptr);
}

TEST(FaceModule, newCurvesMakesAFileForAnAudioFileAndOpensIt)
{
    Fixture f("new");
    const std::string wav = face_editor::encodeWav16(std::vector<float>(8000, 0.2f), 8000);
    {
        std::ofstream(f.mDir / "line.wav", std::ios::binary) << wav;
    }
    f.mContext.mAnswer = std::vector<PropertyValue>{ (f.mDir / "line.wav").generic_string(), std::int64_t{ 30 } };
    ASSERT_TRUE(f.mRegistry.runCommand("face.newCurves"));
    ASSERT_EQ(f.mContext.mDialogs.size(), 1u);
    EXPECT_EQ(f.mContext.mDialogs[0].fields.size(), 2u);
    EXPECT_TRUE(fs::exists(f.mDir / "line.facecurves.json"));
    ASSERT_NE(f.mContext.activeAsset(), nullptr);
    EXPECT_NE(f.mRegistry.panelSource("face.panel"), nullptr);
}

TEST(FaceModule, aCancelledDialogDoesNothing)
{
    Fixture f("cancel");
    f.mContext.mAnswer = std::nullopt;
    EXPECT_TRUE(f.mRegistry.runCommand("face.newCurves"));
    EXPECT_EQ(f.mContext.activeAsset(), nullptr);
}

TEST(FaceModule, newCurvesWithAnUnreadableAudioFileLogsAnError)
{
    Fixture f("newbad");
    f.mContext.mAnswer = std::vector<PropertyValue>{ (f.mDir / "nothing.wav").generic_string(), std::int64_t{ 30 } };
    f.mRegistry.runCommand("face.newCurves");
    bool error = false;
    for (const auto& [level, text] : f.mContext.mLog)
        error = error || level == LogLevel::Error;
    EXPECT_TRUE(error);
    EXPECT_EQ(f.mContext.activeAsset(), nullptr);
}

TEST(FaceModule, generateSaysTheRuntimeIsNotThereYet)
{
    Fixture f("generate");
    const std::string wav = face_editor::encodeWav16(std::vector<float>(8000, 0.2f), 8000);
    {
        std::ofstream(f.mDir / "line.wav", std::ios::binary) << wav;
    }
    std::string error;
    ASSERT_TRUE(face_editor::createCurvesFile(f.mDir / "line.wav", f.mDir / "line.facecurves.json", 30, error));
    ASSERT_NE(f.mContext.openAsset(f.mDir / "line.facecurves.json"), nullptr);
    f.mContext.mAnswer = std::vector<PropertyValue>{ std::string("none found"), false };
    ASSERT_TRUE(f.mRegistry.runCommand("face.generate"));
    bool said = false;
    for (const auto& [level, text] : f.mContext.mLog)
        said = said || text.find("ONNX runtime") != std::string::npos;
    EXPECT_TRUE(said);
    // It needs an open face asset.
    f.mContext.mActive.reset();
    EXPECT_FALSE(f.mRegistry.isCommandEnabled("face.generate"));
}

TEST(FaceModule, verifyModelsHashesACompleteVariantAsABackgroundTask)
{
    Fixture f("verify");
    // Nothing complete: says so and starts no task.
    ASSERT_TRUE(f.mRegistry.runCommand("face.verifyModels"));
    EXPECT_EQ(f.mContext.mTasks, 0);

    // A complete v2.3 variant of the right sizes, with sparse content (the hash will not match).
    const auto root = face_editor::modelsRoot(f.mContext.mUserData);
    for (const auto& spec : face_editor::knownVariants())
        if (spec.mId == "v2.3-mark")
            for (const auto& file : spec.mFiles)
            {
                fs::create_directories(root / spec.mId);
                {
                    std::ofstream(root / spec.mId / file.mName, std::ios::binary) << "x";
                }
                fs::resize_file(root / spec.mId / file.mName, file.mSize);
            }
    ASSERT_TRUE(f.mRegistry.runCommand("face.verifyModels"));
    EXPECT_EQ(f.mContext.mTasks, 1);
    ASSERT_FALSE(f.mContext.mTaskProgress.empty());
    EXPECT_NEAR(f.mContext.mTaskProgress.back(), 1.0, 1e-9);
    bool mismatch = false;
    for (const auto& [level, text] : f.mContext.mLog)
        mismatch = mismatch || (level == LogLevel::Error && text.find("v2.3-mark") != std::string::npos);
    EXPECT_TRUE(mismatch);
}
