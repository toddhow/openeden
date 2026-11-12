// Task 0147: the face editor's asset: opening a curve file, key edits as undoable commands, saving canonically, finding
// and loading the audio, creating a new file. Generated files only (no game audio, no model).
#include "editor_modules/face_editor/face_document.hpp"

#include <components/face/sha256.hpp>

#include <openeden/editor_module/registrar.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

namespace
{
    using namespace face_editor;
    using namespace openeden::editor;
    namespace fs = std::filesystem;

    fs::path scratch(const char* name)
    {
        fs::path p = fs::temp_directory_path() / ("oe_face_doc_" + std::string(name));
        fs::remove_all(p);
        fs::create_directories(p);
        return p;
    }

    std::string slurp(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    void spit(const fs::path& p, const std::string& s)
    {
        std::ofstream out(p, std::ios::binary);
        out << s;
    }

    /// 30 fps, 31 frames (1.0 s + one frame), two weight channels and one signed one.
    Face::Curves sample()
    {
        Face::Curves c;
        c.mAudioName = "vo/a/f/line.mp3";
        c.mAudioDurationSeconds = 1.0;
        c.mAudioSha256 = std::string(64, '0');
        c.mGeneratorName = "test";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = 31;
        Face::Channel jaw;
        jaw.mName = "jawopen";
        for (int i = 0; i < 31; ++i)
            jaw.mValues.push_back(static_cast<std::int16_t>(i < 15 ? i * 10 : (30 - i) * 10));
        Face::Channel blink;
        blink.mName = "eyeblinkleft";
        blink.mValues.assign(31, 0);
        blink.mValues[10] = 255;
        Face::Channel gaze;
        gaze.mName = "gazex";
        gaze.mKind = Face::ChannelKind::Signed;
        gaze.mValues.assign(31, 0);
        for (int i = 0; i < 31; ++i)
            gaze.mValues[i] = static_cast<std::int16_t>(i * 17 - 255);
        c.mChannels = { jaw, blink, gaze };
        return c;
    }

    fs::path writeSample(const fs::path& dir, const char* name = "line.facecurves.json")
    {
        auto text = Face::writeJson(sample());
        EXPECT_TRUE(text.has_value());
        fs::path p = dir / name;
        spit(p, *text);
        return p;
    }

    /// The test's EditorContext: runs and records commands.
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

    std::vector<std::int16_t> valuesOf(const Face::Curves& c, const std::string& name)
    {
        for (const auto& ch : c.mChannels)
            if (ch.mName == name)
                return ch.mValues;
        return {};
    }
}

TEST(FaceDocument, opensAFileAndDescribesItsChannels)
{
    fs::path dir = scratch("open");
    std::string error;
    auto doc = FaceDocument::open(writeSample(dir), {}, error);
    ASSERT_TRUE(doc) << error;
    EXPECT_FALSE(doc->isDirty());
    EXPECT_EQ(doc->assetId(), doc->path().generic_string());
    auto channels = doc->curveTrack().channels();
    ASSERT_EQ(channels.size(), 3u);
    EXPECT_EQ(channels[0].id, "jawopen");
    EXPECT_DOUBLE_EQ(channels[0].minValue, 0.0);
    EXPECT_DOUBLE_EQ(channels[0].maxValue, 1.0);
    EXPECT_EQ(channels[2].id, "gazex");
    EXPECT_DOUBLE_EQ(channels[2].minValue, -1.0);
    EXPECT_NEAR(doc->curveTrack().durationSeconds(), 30.0 / 30.0, 1e-9);
    // The sparse keys stand for the frames.
    auto keys = doc->curveTrack().channelKeys("jawopen");
    EXPECT_GE(keys.size(), 3u);
    EXPECT_LT(keys.size(), 31u);
    EXPECT_TRUE(doc->curveTrack().channelKeys("nope").empty());
    // Plain tracks list the document's tracks: the curve track (and no waveform without audio).
    EXPECT_EQ(doc->tracks().size(), 1u);
    EXPECT_EQ(doc->waveformTrack(), nullptr);
}

TEST(FaceDocument, aBadFileGivesALocatedError)
{
    fs::path dir = scratch("bad");
    spit(dir / "x.facecurves.json", "{ \"format\": \"nope\" }");
    std::string error;
    EXPECT_FALSE(FaceDocument::open(dir / "x.facecurves.json", {}, error));
    EXPECT_NE(error.find("x.facecurves.json"), std::string::npos);
    EXPECT_FALSE(FaceDocument::open(dir / "missing.facecurves.json", {}, error));
    EXPECT_FALSE(error.empty());
}

TEST(FaceDocument, savingAnUneditedFileLeavesItsBytesAlone)
{
    fs::path dir = scratch("unedited");
    fs::path p = writeSample(dir);
    const std::string before = slurp(p);
    std::string error;
    auto doc = FaceDocument::open(p, {}, error);
    ASSERT_TRUE(doc) << error;
    ASSERT_TRUE(doc->save(error)) << error;
    EXPECT_EQ(slurp(p), before);
}

TEST(FaceDocument, editsAreUndoableDirtyAndOnlyTheEditedChannelChanges)
{
    fs::path dir = scratch("edit");
    fs::path p = writeSample(dir);
    std::string error;
    auto doc = FaceDocument::open(p, {}, error);
    ASSERT_TRUE(doc) << error;
    Ctx ctx;
    auto keysBefore = doc->keysOf("eyeblinkleft");

    // Add a key at 0.5 s, value 0.8, to eyeblinkleft.
    auto add = doc->curveTrack().makeAddKeyCommand("eyeblinkleft", { 0.5, 0.8 });
    ASSERT_TRUE(add);
    ctx.pushCommand(std::move(add));
    EXPECT_TRUE(doc->isDirty());
    EXPECT_EQ(doc->keysOf("eyeblinkleft").size(), keysBefore.size() + 1);

    ASSERT_TRUE(doc->save(error)) << error;
    EXPECT_FALSE(doc->isDirty());
    auto text = slurp(p);
    Face::LoadError le;
    auto reread = Face::readJson(text, "saved", le);
    ASSERT_TRUE(reread) << le.format();
    const Face::Curves original = sample();
    EXPECT_EQ(valuesOf(*reread, "jawopen"), valuesOf(original, "jawopen"));
    EXPECT_EQ(valuesOf(*reread, "gazex"), valuesOf(original, "gazex"));
    auto blink = valuesOf(*reread, "eyeblinkleft");
    ASSERT_EQ(blink.size(), 31u);
    EXPECT_EQ(blink[15], 204); // 0.8 * 255 at 0.5 s = frame 15
    // The written file is canonical (writing what was read gives the same bytes).
    EXPECT_EQ(*Face::writeJson(*reread), text);

    // Undo the step: keys are back, and the document is dirty again (it differs from the saved file).
    ctx.mSteps.back()->undo();
    EXPECT_EQ(doc->keysOf("eyeblinkleft").size(), keysBefore.size());
    EXPECT_TRUE(doc->isDirty());
}

TEST(FaceDocument, moveAndDeleteKeepKeysSortedAndTheIndexStable)
{
    fs::path dir = scratch("move");
    std::string error;
    auto doc = FaceDocument::open(writeSample(dir), {}, error);
    ASSERT_TRUE(doc) << error;
    Ctx ctx;
    auto keys = doc->keysOf("jawopen");
    ASSERT_GE(keys.size(), 3u);

    // Moving key 1 far past its neighbours is held between them (the index stays 1).
    auto move = doc->curveTrack().makeMoveKeyCommand("jawopen", 1, { 99.0, 0.5 });
    ASSERT_TRUE(move);
    ctx.pushCommand(std::move(move));
    auto after = doc->keysOf("jawopen");
    EXPECT_LE(after[1].timeSeconds, keys[2].timeSeconds);
    EXPECT_GE(after[1].timeSeconds, keys[0].timeSeconds);
    EXPECT_DOUBLE_EQ(after[1].value, 0.5);
    for (std::size_t i = 1; i < after.size(); ++i)
        EXPECT_LE(after[i - 1].timeSeconds, after[i].timeSeconds);

    auto del = doc->curveTrack().makeDeleteKeyCommand("jawopen", 1);
    ASSERT_TRUE(del);
    ctx.pushCommand(std::move(del));
    EXPECT_EQ(doc->keysOf("jawopen").size(), keys.size() - 1);
    ctx.mSteps.back()->undo();
    ctx.mSteps[0]->undo();
    auto restored = doc->keysOf("jawopen");
    ASSERT_EQ(restored.size(), keys.size());
    EXPECT_DOUBLE_EQ(restored[1].timeSeconds, keys[1].timeSeconds);
}

TEST(FaceDocument, commandsRefuseWhatTheyCannotDo)
{
    fs::path dir = scratch("refuse");
    std::string error;
    auto doc = FaceDocument::open(writeSample(dir), {}, error);
    ASSERT_TRUE(doc) << error;
    auto& track = doc->curveTrack();
    EXPECT_FALSE(track.makeAddKeyCommand("nochannel", { 0.1, 0.1 }));
    EXPECT_FALSE(track.makeAddKeyCommand("jawopen", { 0.1, 2.0 }));
    EXPECT_FALSE(track.makeAddKeyCommand("jawopen", { std::nan(""), 0.1 }));
    EXPECT_FALSE(track.makeAddKeyCommand("jawopen", { 5.0, 0.1 })); // past the end
    EXPECT_FALSE(track.makeMoveKeyCommand("jawopen", 999, { 0.1, 0.1 }));
    EXPECT_FALSE(track.makeDeleteKeyCommand("jawopen", 999));
    EXPECT_TRUE(track.makeAddKeyCommand("gazex", { 0.1, -0.5 })); // a signed channel takes negatives
}

TEST(FaceDocument, aChannelWithNoKeysLeftSavesAsZeros)
{
    fs::path dir = scratch("empty");
    fs::path p = writeSample(dir);
    std::string error;
    auto doc = FaceDocument::open(p, {}, error);
    ASSERT_TRUE(doc) << error;
    Ctx ctx;
    while (!doc->keysOf("eyeblinkleft").empty())
        ctx.pushCommand(doc->curveTrack().makeDeleteKeyCommand("eyeblinkleft", 0));
    ASSERT_TRUE(doc->save(error)) << error;
    Face::LoadError le;
    auto reread = Face::readJson(slurp(p), "x", le);
    ASSERT_TRUE(reread);
    for (auto v : valuesOf(*reread, "eyeblinkleft"))
        EXPECT_EQ(v, 0);
}

TEST(FaceDocument, saveToAnUnwritablePlaceIsAnError)
{
    fs::path dir = scratch("unwritable");
    fs::path p = writeSample(dir);
    std::string error;
    auto doc = FaceDocument::open(p, {}, error);
    ASSERT_TRUE(doc) << error;
    Ctx ctx;
    ctx.pushCommand(doc->curveTrack().makeAddKeyCommand("jawopen", { 0.5, 0.9 }));
    fs::remove(p);
    fs::create_directory(p); // a directory where the file was
    EXPECT_FALSE(doc->save(error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(doc->isDirty());
}

TEST(FaceDocument, findsTheAudioNextToTheFileByItsOwnNameThenByTheRecordedName)
{
    fs::path dir = scratch("findaudio");
    Face::Curves c = sample();
    EXPECT_TRUE(findAudio(dir / "line.facecurves.json", c, {}).empty());

    spit(dir / "line.mp3", "x"); // same stem as the curves file
    EXPECT_EQ(findAudio(dir / "line.facecurves.json", c, {}).filename(), "line.mp3");

    fs::path root = scratch("findaudio_root");
    fs::create_directories(root / "vo" / "a" / "f");
    spit(root / "vo" / "a" / "f" / "line.mp3", "x");
    fs::path other = scratch("findaudio_other");
    EXPECT_EQ(findAudio(other / "z.facecurves.json", c, root), root / "vo" / "a" / "f" / "line.mp3");

    // The recorded name can not climb out of the root.
    c.mAudioName = "../../etc/passwd";
    EXPECT_TRUE(findAudio(other / "z.facecurves.json", c, root).empty());
}

TEST(FaceDocument, loadsAWavNextToItAndOffersAWaveform)
{
    fs::path dir = scratch("withaudio");
    std::vector<float> tone(8000);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    const std::string wav = encodeWav16(tone, 8000);
    spit(dir / "line.wav", wav);
    Face::Curves c = sample();
    c.mAudioSha256 = Face::sha256Hex(wav);
    spit(dir / "line.facecurves.json", *Face::writeJson(c));

    std::string error;
    auto doc = FaceDocument::open(dir / "line.facecurves.json", {}, error);
    ASSERT_TRUE(doc) << error;
    ASSERT_NE(doc->waveformTrack(), nullptr);
    auto data = doc->waveformTrack()->waveform();
    EXPECT_EQ(data.sampleRate, 8000u);
    EXPECT_EQ(data.sampleCount, 8000u);
    ASSERT_NE(data.samples, nullptr);
    EXPECT_EQ(doc->tracks().size(), 2u);
    EXPECT_EQ(doc->audioMatchText(), "matches");
    EXPECT_TRUE(doc->audioError().empty());
}

TEST(FaceDocument, audioThatDiffersOrIsMissingIsReportedNotFatal)
{
    fs::path dir = scratch("audiodiff");
    spit(dir / "line.wav", encodeWav16(std::vector<float>(8000, 0.1f), 8000));
    spit(dir / "line.facecurves.json", *Face::writeJson(sample())); // records an all-zero hash
    std::string error;
    auto doc = FaceDocument::open(dir / "line.facecurves.json", {}, error);
    ASSERT_TRUE(doc) << error;
    EXPECT_EQ(doc->audioMatchText(), "differs");
    EXPECT_NE(doc->waveformTrack(), nullptr);

    fs::path dir2 = scratch("noaudio");
    auto doc2 = FaceDocument::open(writeSample(dir2), {}, error);
    ASSERT_TRUE(doc2) << error;
    EXPECT_EQ(doc2->audioMatchText(), "no audio");

    // An unreadable audio file next to the curves: the document opens, the error is kept.
    fs::path dir3 = scratch("badaudio");
    spit(dir3 / "line.wav", "this is not a wav file");
    auto doc3 = FaceDocument::open(writeSample(dir3), {}, error);
    ASSERT_TRUE(doc3) << error;
    EXPECT_EQ(doc3->waveformTrack(), nullptr);
    EXPECT_FALSE(doc3->audioError().empty());
}

TEST(FaceDocument, setAudioKeepsTheOldOneOnFailureAndUpdatesTheRecordOnSuccess)
{
    fs::path dir = scratch("setaudio");
    std::string error;
    auto doc = FaceDocument::open(writeSample(dir), {}, error);
    ASSERT_TRUE(doc) << error;
    spit(dir / "other.wav", "junk");
    EXPECT_FALSE(doc->setAudio(dir / "other.wav", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(doc->waveformTrack(), nullptr);

    const std::string wav = encodeWav16(std::vector<float>(16000, 0.2f), 16000);
    spit(dir / "good.wav", wav);
    ASSERT_TRUE(doc->setAudio(dir / "good.wav", error)) << error;
    EXPECT_NE(doc->waveformTrack(), nullptr);
    EXPECT_TRUE(doc->isDirty()); // the file's record of its audio changes on save
    ASSERT_TRUE(doc->save(error)) << error;
    Face::LoadError le;
    auto reread = Face::readJson(slurp(doc->path()), "x", le);
    ASSERT_TRUE(reread) << le.format();
    EXPECT_EQ(reread->mAudioSha256, Face::sha256Hex(wav));
    EXPECT_NEAR(reread->mAudioDurationSeconds, 1.0, 1e-9);
}

TEST(FaceDocument, createsANewFileForAnAudioFile)
{
    fs::path dir = scratch("create");
    const std::string wav = encodeWav16(std::vector<float>(12000, 0.3f), 8000); // 1.5 s
    spit(dir / "hello.wav", wav);
    std::string error;
    ASSERT_TRUE(createCurvesFile(dir / "hello.wav", dir / "hello.facecurves.json", 30, error)) << error;
    Face::LoadError le;
    auto c = Face::readJson(slurp(dir / "hello.facecurves.json"), "hello", le);
    ASSERT_TRUE(c) << le.format();
    EXPECT_EQ(c->mFrameRate, 30u);
    EXPECT_EQ(c->mFrameCount, 46u); // 1.5 s * 30 + 1
    EXPECT_EQ(c->mAudioSha256, Face::sha256Hex(wav));
    EXPECT_EQ(c->mChannels.size(), defaultChannelNames().size());
    EXPECT_FALSE(defaultChannelNames().empty());
    for (const auto& ch : c->mChannels)
        for (auto v : ch.mValues)
            EXPECT_EQ(v, 0);
    // It opens, and its audio is found next to it.
    auto doc = FaceDocument::open(dir / "hello.facecurves.json", {}, error);
    ASSERT_TRUE(doc) << error;
    EXPECT_NE(doc->waveformTrack(), nullptr);
    EXPECT_EQ(doc->audioMatchText(), "matches");

    EXPECT_FALSE(createCurvesFile(dir / "missing.wav", dir / "x.facecurves.json", 30, error));
    EXPECT_FALSE(createCurvesFile(dir / "hello.wav", dir / "x.facecurves.json", 0, error));
    EXPECT_FALSE(createCurvesFile(dir / "hello.wav", dir / "x.facecurves.json", 1000, error));
}

TEST(FaceDocument, propertiesAreReadOnlyDescriptions)
{
    fs::path dir = scratch("props");
    std::string error;
    auto doc = FaceDocument::open(writeSample(dir), {}, error);
    ASSERT_TRUE(doc) << error;
    auto& props = doc->properties();
    auto desc = props.describe();
    ASSERT_FALSE(desc.empty());
    for (const auto& d : desc)
    {
        EXPECT_TRUE(d.readOnly) << d.id;
        EXPECT_EQ(props.makeSetCommand(d.id, props.get(d.id)), nullptr) << d.id;
    }
    EXPECT_EQ(std::get<std::string>(props.get("face.audio.name")), "vo/a/f/line.mp3");
    EXPECT_EQ(std::get<std::int64_t>(props.get("face.frames")), 31);
}
