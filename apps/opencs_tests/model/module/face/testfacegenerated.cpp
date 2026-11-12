// Task 0147 slice 2: writing generated weights into an open curve document as ONE undoable step.
#include "editor_modules/face_editor/face_document.hpp"

#include <openeden/editor_module/registrar.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

namespace
{
    using namespace face_editor;
    using namespace openeden::editor;
    namespace fs = std::filesystem;

    /// 30 fps, 31 frames, jawopen / eyeblinkleft weight channels and a signed gazex.
    std::unique_ptr<FaceDocument> openSample(const char* name)
    {
        const fs::path dir = fs::temp_directory_path() / ("oe_face_gen_" + std::string(name));
        fs::remove_all(dir);
        fs::create_directories(dir);
        Face::Curves c;
        c.mAudioName = "line.mp3";
        c.mAudioDurationSeconds = 1.0;
        c.mAudioSha256 = std::string(64, '0');
        c.mGeneratorName = "manual";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = 31;
        Face::Channel jaw, blink, gaze;
        jaw.mName = "jawopen";
        jaw.mValues.assign(31, 50);
        blink.mName = "eyeblinkleft";
        blink.mValues.assign(31, 0);
        gaze.mName = "gazex";
        gaze.mKind = Face::ChannelKind::Signed;
        gaze.mValues.assign(31, 0);
        for (int i = 0; i < 31; ++i)
            gaze.mValues[i] = static_cast<std::int16_t>(i * 3 - 40);
        c.mChannels = { jaw, blink, gaze };
        const auto text = Face::writeJson(c);
        EXPECT_TRUE(text.has_value());
        const fs::path file = dir / "line.facecurves.json";
        std::ofstream(file, std::ios::binary) << *text;
        std::string error;
        auto doc = FaceDocument::open(file, {}, error);
        EXPECT_TRUE(doc != nullptr) << error;
        return doc;
    }

    /// 60 fps over 1.5 s: jawopen rises 0 -> 0.9 linearly, eyeblinkleft is 0.3 once and 0 else, an unknown pose.
    GeneratedWeights sampleWeights()
    {
        GeneratedWeights w;
        w.mNames = { "jawopen", "cheekpuff", "eyeblinkleft" };
        w.mFrameRate = 60;
        const std::size_t n = 91;
        w.mWeights.assign(n * 3, 0.f);
        for (std::size_t f = 0; f < n; ++f)
        {
            w.mWeights[f * 3 + 0] = 0.9f * static_cast<float>(f) / 90.f;
            w.mWeights[f * 3 + 1] = 1.0f;
            w.mWeights[f * 3 + 2] = f == 30 ? 0.3f : 0.f;
        }
        return w;
    }

    GeneratedMeta meta()
    {
        GeneratedMeta m;
        m.mName = "audio2face-onnx";
        m.mVersion = "v2.3-mark";
        m.mEmotion = "neutral";
        m.mSeed = 12345;
        return m;
    }

    std::vector<std::int16_t> expectedInts(const GeneratedWeights& w, std::size_t pose, std::uint32_t rate,
        std::uint32_t frames)
    {
        std::vector<std::int16_t> v;
        for (std::uint32_t j = 0; j < frames; ++j)
        {
            const double t = static_cast<double>(j) / rate;
            v.push_back(static_cast<std::int16_t>(std::lround(std::clamp(sampleWeight(w, pose, t), 0.f, 1.f) * 255.0)));
        }
        return v;
    }

    class Ctx final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command> c) override
        {
            c->redo();
            mSteps.push_back(std::move(c));
        }
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const fs::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
        std::vector<std::unique_ptr<Command>> mSteps;
    };

    TEST(FaceGenerated, writesTheMatchingChannelsAtTheDocumentsFrameRate)
    {
        auto doc = openSample("write");
        const GeneratedWeights w = sampleWeights();
        std::vector<std::string> written, skipped;
        auto cmd = doc->makeGeneratedCommand(w, meta(), &written, &skipped);
        ASSERT_TRUE(cmd != nullptr);
        EXPECT_EQ(written, (std::vector<std::string>{ "jawopen", "eyeblinkleft" }));
        EXPECT_EQ(skipped, (std::vector<std::string>{ "cheekpuff" }));
        EXPECT_FALSE(doc->isDirty());
        cmd->redo();
        EXPECT_TRUE(doc->isDirty());
        EXPECT_EQ(resample(doc->keysOf("jawopen"), 30, 31, 0, 255), expectedInts(w, 0, 30, 31));
        EXPECT_EQ(resample(doc->keysOf("eyeblinkleft"), 30, 31, 0, 255), expectedInts(w, 2, 30, 31));
        // 60 fps frame 30 is t = 0.5 s = 30 fps frame 15: the blink peak of 0.3 -> round(76.5) = 77 (std::lround).
        EXPECT_EQ(resample(doc->keysOf("eyeblinkleft"), 30, 31, 0, 255)[15], 77);
    }

    TEST(FaceGenerated, aChannelTheGeneratorDoesNotMakeIsLeftAlone)
    {
        auto doc = openSample("alone");
        const auto gazeBefore = doc->keysOf("gazex");
        auto cmd = doc->makeGeneratedCommand(sampleWeights(), meta(), nullptr, nullptr);
        ASSERT_TRUE(cmd != nullptr);
        cmd->redo();
        const auto gazeAfter = doc->keysOf("gazex");
        ASSERT_EQ(gazeBefore.size(), gazeAfter.size());
        for (std::size_t i = 0; i < gazeBefore.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(gazeBefore[i].timeSeconds, gazeAfter[i].timeSeconds);
            EXPECT_DOUBLE_EQ(gazeBefore[i].value, gazeAfter[i].value);
        }
    }

    TEST(FaceGenerated, undoRestoresEveryChannelAndRedoAppliesAgain)
    {
        auto doc = openSample("undo");
        const auto before = doc->keysOf("jawopen");
        auto cmd = doc->makeGeneratedCommand(sampleWeights(), meta(), nullptr, nullptr);
        ASSERT_TRUE(cmd != nullptr);
        cmd->redo();
        const auto after = doc->keysOf("jawopen");
        EXPECT_NEAR(after.front().value, 0.0, 0.005);
        EXPECT_NEAR(after.back().value, 0.6, 0.005); // t = 1 s is 60 fps frame 60: 0.9 * 60 / 90
        cmd->undo();
        const auto undone = doc->keysOf("jawopen");
        ASSERT_EQ(undone.size(), before.size());
        for (std::size_t i = 0; i < before.size(); ++i)
            EXPECT_DOUBLE_EQ(undone[i].value, before[i].value);
        EXPECT_FALSE(doc->isDirty());
        cmd->redo();
        EXPECT_EQ(doc->keysOf("jawopen").size(), after.size());
    }

    TEST(FaceGenerated, isOneStepOnTheUndoStack)
    {
        auto doc = openSample("onestep");
        Ctx ctx;
        ctx.pushCommand(doc->makeGeneratedCommand(sampleWeights(), meta(), nullptr, nullptr));
        EXPECT_EQ(ctx.mSteps.size(), 1u);
        EXPECT_FALSE(ctx.mSteps[0]->label().empty());
    }

    TEST(FaceGenerated, nothingToWriteGivesNoCommand)
    {
        auto doc = openSample("none");
        GeneratedWeights w;
        w.mNames = { "cheekpuff" };
        w.mFrameRate = 60;
        w.mWeights = { 1.f, 1.f };
        EXPECT_TRUE(doc->makeGeneratedCommand(w, meta(), nullptr, nullptr) == nullptr);
        EXPECT_TRUE(doc->makeGeneratedCommand(GeneratedWeights{}, meta(), nullptr, nullptr) == nullptr);
    }

    TEST(FaceGenerated, saveWritesTheGeneratorRecordAndTheValuesAndRoundTrips)
    {
        auto doc = openSample("save");
        const GeneratedWeights w = sampleWeights();
        auto cmd = doc->makeGeneratedCommand(w, meta(), nullptr, nullptr);
        ASSERT_TRUE(cmd != nullptr);
        cmd->redo();
        std::string error;
        ASSERT_TRUE(doc->save(error)) << error;
        EXPECT_FALSE(doc->isDirty());
        auto again = FaceDocument::open(doc->path(), {}, error);
        ASSERT_TRUE(again != nullptr) << error;
        const Face::Curves& c = again->curves();
        EXPECT_EQ(c.mGeneratorName, "audio2face-onnx");
        EXPECT_EQ(c.mGeneratorVersion, "v2.3-mark");
        EXPECT_EQ(c.mSeed, 12345u);
        EXPECT_EQ(c.mEmotion, "neutral");
        EXPECT_EQ(c.mChannels[0].mValues, expectedInts(w, 0, 30, 31));
    }

    TEST(FaceGenerated, undoPutsTheGeneratorRecordBack)
    {
        auto doc = openSample("undometa");
        auto cmd = doc->makeGeneratedCommand(sampleWeights(), meta(), nullptr, nullptr);
        ASSERT_TRUE(cmd != nullptr);
        cmd->redo();
        cmd->undo();
        std::string error;
        ASSERT_TRUE(doc->save(error)) << error;
        auto again = FaceDocument::open(doc->path(), {}, error);
        ASSERT_TRUE(again != nullptr) << error;
        EXPECT_EQ(again->curves().mGeneratorName, "manual");
        EXPECT_EQ(again->curves().mGeneratorVersion, "1");
    }
}
