// Task 0147 slice 1, Editor Module API 1.4: the pure parts of the curve canvas, waveform track, playhead, dialogs and
// the undoable key edits. Written from the spec (docs/tasks/0147-studio-face-timeline.md) before the code.
#include "apps/opencs/model/module/timelinemodel.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace openeden::editor;
    using namespace CSMModule;

    CurveChannelInfo unitChannel()
    {
        CurveChannelInfo c;
        c.id = "jawopen";
        c.label = "Jaw open";
        c.minValue = 0.0;
        c.maxValue = 1.0;
        return c;
    }

    class SetKeys final : public Command
    {
    public:
        SetKeys(std::vector<CurveKey>& target, std::vector<CurveKey> after)
            : mTarget(target)
            , mBefore(target)
            , mAfter(std::move(after))
        {
        }
        std::string_view label() const override { return "Edit keys"; }
        void redo() override { mTarget = mAfter; }
        void undo() override { mTarget = mBefore; }

    private:
        std::vector<CurveKey>& mTarget;
        std::vector<CurveKey> mBefore, mAfter;
    };

    class FakeTrack final : public CurveTrack
    {
    public:
        std::string_view id() const override { return "fake"; }
        std::string_view title() const override { return "Fake"; }
        std::vector<TimelineKey> keys() const override { return {}; }
        std::vector<CurveChannelInfo> channels() const override { return { unitChannel() }; }
        std::vector<CurveKey> channelKeys(std::string_view id) const override
        {
            return id == "jawopen" ? mKeys : std::vector<CurveKey>{};
        }
        double durationSeconds() const override { return 2.0; }
        std::unique_ptr<Command> makeAddKeyCommand(std::string_view id, const CurveKey& key) override
        {
            if (id != "jawopen")
                return nullptr;
            auto after = mKeys;
            after.push_back(key);
            return std::make_unique<SetKeys>(mKeys, after);
        }
        std::unique_ptr<Command> makeMoveKeyCommand(std::string_view id, std::size_t index, const CurveKey& key) override
        {
            if (id != "jawopen" || index >= mKeys.size())
                return nullptr;
            auto after = mKeys;
            after[index] = key;
            return std::make_unique<SetKeys>(mKeys, after);
        }
        std::unique_ptr<Command> makeDeleteKeyCommand(std::string_view id, std::size_t index) override
        {
            if (id != "jawopen" || index >= mKeys.size())
                return nullptr;
            auto after = mKeys;
            after.erase(after.begin() + static_cast<std::ptrdiff_t>(index));
            return std::make_unique<SetKeys>(mKeys, after);
        }
        std::vector<CurveKey> mKeys{ { 0.0, 0.0 }, { 1.0, 0.5 } };
    };

    class RecordingContext final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command> command) override
        {
            command->redo();
            mSteps.push_back(std::move(command));
        }
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
        std::vector<std::unique_ptr<Command>> mSteps;
    };

    class PlainTrack final : public TimelineTrack
    {
    public:
        std::string_view id() const override { return "plain"; }
        std::string_view title() const override { return "Plain"; }
        std::vector<TimelineKey> keys() const override { return {}; }
    };

    class WaveTrack final : public WaveformTrack
    {
    public:
        std::string_view id() const override { return "wave"; }
        std::string_view title() const override { return "Wave"; }
        std::vector<TimelineKey> keys() const override { return {}; }
        WaveformData waveform() const override { return { mSamples.data(), mSamples.size(), 100 }; }
        std::vector<float> mSamples = std::vector<float>(250, 0.f);
    };
}

TEST(TimelineAxis, mapsTimeToPixelsAndBack)
{
    TimeAxis axis{ 1.0, 3.0, 200 };
    EXPECT_DOUBLE_EQ(axis.toX(1.0), 0.0);
    EXPECT_DOUBLE_EQ(axis.toX(3.0), 200.0);
    EXPECT_DOUBLE_EQ(axis.toX(2.0), 100.0);
    EXPECT_DOUBLE_EQ(axis.toTime(50.0), 1.5);
}

TEST(TimelineAxis, aDegenerateAxisDoesNotDivideByZero)
{
    TimeAxis axis{ 1.0, 1.0, 100 };
    EXPECT_TRUE(std::isfinite(axis.toX(1.0)));
    EXPECT_TRUE(std::isfinite(axis.toTime(10.0)));
}

TEST(TimelineAxis, valuesMapToRowsWithTheMaximumAtTheTop)
{
    CurveChannelInfo c = unitChannel();
    EXPECT_DOUBLE_EQ(valueToY(c, 1.0, 100), 0.0);
    EXPECT_DOUBLE_EQ(valueToY(c, 0.0, 100), 100.0);
    EXPECT_DOUBLE_EQ(yToValue(c, 25.0, 100), 0.75);
}

TEST(TimelinePeaks, giveOneMinMaxPerColumnOfTheSamplesInIt)
{
    std::vector<float> s(100, 0.25f);
    for (int i = 0; i < 50; ++i)
        s[i] = -0.5f;
    s[75] = 0.9f;
    TimeAxis axis{ 0.0, 1.0, 2 };
    auto peaks = waveformPeaks({ s.data(), s.size(), 100 }, axis);
    ASSERT_EQ(peaks.size(), 2u);
    EXPECT_FLOAT_EQ(peaks[0].mMin, -0.5f);
    EXPECT_FLOAT_EQ(peaks[0].mMax, -0.5f);
    EXPECT_FLOAT_EQ(peaks[1].mMin, 0.25f);
    EXPECT_FLOAT_EQ(peaks[1].mMax, 0.9f);
}

TEST(TimelinePeaks, anEmptyBufferGivesZeros)
{
    auto peaks = waveformPeaks({ nullptr, 0, 100 }, TimeAxis{ 0.0, 1.0, 4 });
    ASSERT_EQ(peaks.size(), 4u);
    for (const auto& p : peaks)
    {
        EXPECT_FLOAT_EQ(p.mMin, 0.f);
        EXPECT_FLOAT_EQ(p.mMax, 0.f);
    }
}

TEST(TimelinePeaks, nonFiniteSamplesCountAsSilenceAndNothingReadsPastTheEnd)
{
    std::vector<float> s{ std::numeric_limits<float>::quiet_NaN(), 0.5f, std::numeric_limits<float>::infinity() };
    auto peaks = waveformPeaks({ s.data(), s.size(), 100 }, TimeAxis{ -1.0, 10.0, 50 });
    ASSERT_EQ(peaks.size(), 50u);
    for (const auto& p : peaks)
    {
        EXPECT_TRUE(std::isfinite(p.mMin));
        EXPECT_TRUE(std::isfinite(p.mMax));
        EXPECT_LE(p.mMax, 0.5f);
    }
}

TEST(TimelinePeaks, aZeroSampleRateOrWidthIsHarmless)
{
    std::vector<float> s{ 1.f };
    EXPECT_EQ(waveformPeaks({ s.data(), 1, 0 }, TimeAxis{ 0.0, 1.0, 5 }).size(), 5u);
    EXPECT_TRUE(waveformPeaks({ s.data(), 1, 100 }, TimeAxis{ 0.0, 1.0, 0 }).empty());
}

TEST(TimelinePick, findsTheNearestKeyWithinTheTolerance)
{
    std::vector<CurveKey> keys{ { 0.0, 0.0 }, { 0.5, 1.0 }, { 1.0, 0.0 } };
    TimeAxis axis{ 0.0, 1.0, 100 };
    auto hit = pickKey(keys, unitChannel(), axis, 100, 51.0, 2.0, 6.0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->mIndex, 1u);
    EXPECT_FALSE(pickKey(keys, unitChannel(), axis, 100, 51.0, 50.0, 6.0).has_value());
}

TEST(TimelinePick, aTieGoesToTheLowerIndex)
{
    std::vector<CurveKey> keys{ { 0.5, 0.5 }, { 0.5, 0.5 } };
    auto hit = pickKey(keys, unitChannel(), TimeAxis{ 0.0, 1.0, 100 }, 100, 50.0, 50.0, 4.0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->mIndex, 0u);
}

TEST(TimelineKeyCheck, refusesNonFiniteOutOfRangeAndPastTheEnd)
{
    auto c = unitChannel();
    EXPECT_TRUE(checkCurveKey(c, { 0.5, 0.5 }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { std::numeric_limits<double>::quiet_NaN(), 0.5 }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { 0.5, std::numeric_limits<double>::infinity() }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { -0.1, 0.5 }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { 2.1, 0.5 }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { 0.5, 1.5 }, 2.0).empty());
    EXPECT_FALSE(checkCurveKey(c, { 0.5, -0.5 }, 2.0).empty());
}

TEST(TimelineKeyCheck, clampKeepsAKeyInsideTheRange)
{
    CurveKey k = clampKey(unitChannel(), { 5.0, -3.0 }, 2.0);
    EXPECT_DOUBLE_EQ(k.timeSeconds, 2.0);
    EXPECT_DOUBLE_EQ(k.value, 0.0);
}

TEST(TimelinePlayhead, isClampedToTheDurationAndReportsChanges)
{
    Playhead p;
    EXPECT_FALSE(p.set(1.0)); // duration 0: stays 0
    EXPECT_FALSE(p.setDuration(2.0));
    EXPECT_TRUE(p.set(1.5));
    EXPECT_DOUBLE_EQ(p.seconds(), 1.5);
    EXPECT_FALSE(p.set(1.5));
    EXPECT_TRUE(p.set(9.0));
    EXPECT_DOUBLE_EQ(p.seconds(), 2.0);
    EXPECT_FALSE(p.set(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_DOUBLE_EQ(p.seconds(), 2.0);
    EXPECT_TRUE(p.setDuration(1.0)); // a shorter track pulls the playhead in
    EXPECT_DOUBLE_EQ(p.seconds(), 1.0);
    EXPECT_TRUE(p.set(-4.0));
    EXPECT_DOUBLE_EQ(p.seconds(), 0.0);
}

TEST(TimelineClassify, splitsTracksByTheirInterfaces)
{
    FakeTrack curve;
    WaveTrack wave;
    PlainTrack plain;
    auto tracks = classifyTracks({ &curve, &wave, &plain, nullptr });
    ASSERT_EQ(tracks.mCurves.size(), 1u);
    ASSERT_EQ(tracks.mWaves.size(), 1u);
    EXPECT_FALSE(tracks.empty());
    EXPECT_DOUBLE_EQ(tracks.duration(), 2.5); // the wave: 250 samples at 100 Hz; the curve track: 2 s
    EXPECT_TRUE(classifyTracks({ &plain }).empty());
}

TEST(TimelineEditor, eachEditIsOneUndoableStep)
{
    RecordingContext ctx;
    FakeTrack track;
    CurveEditor editor(ctx, track);
    ASSERT_TRUE(editor.addKey("jawopen", { 1.5, 0.25 }));
    EXPECT_EQ(track.mKeys.size(), 3u);
    ASSERT_TRUE(editor.moveKey("jawopen", 1, { 0.9, 0.75 }));
    EXPECT_DOUBLE_EQ(track.mKeys[1].value, 0.75);
    ASSERT_TRUE(editor.deleteKey("jawopen", 0));
    EXPECT_EQ(track.mKeys.size(), 2u);
    EXPECT_EQ(ctx.mSteps.size(), 3u);
    for (auto it = ctx.mSteps.rbegin(); it != ctx.mSteps.rend(); ++it)
        (*it)->undo();
    ASSERT_EQ(track.mKeys.size(), 2u);
    EXPECT_DOUBLE_EQ(track.mKeys[1].value, 0.5);
}

TEST(TimelineEditor, refusedEditsPushNothing)
{
    RecordingContext ctx;
    FakeTrack track;
    CurveEditor editor(ctx, track);
    EXPECT_FALSE(editor.addKey("nochannel", { 0.5, 0.5 }));
    EXPECT_FALSE(editor.addKey("jawopen", { 0.5, 7.0 }));
    EXPECT_FALSE(editor.addKey("jawopen", { std::numeric_limits<double>::quiet_NaN(), 0.5 }));
    EXPECT_FALSE(editor.moveKey("jawopen", 9, { 0.5, 0.5 }));
    EXPECT_FALSE(editor.deleteKey("jawopen", 9));
    EXPECT_FALSE(editor.lastRefusal().empty());
    EXPECT_TRUE(ctx.mSteps.empty());
}

TEST(TimelineEditor, aMoveIsClampedAndAMoveToTheSamePlaceIsNoStep)
{
    RecordingContext ctx;
    FakeTrack track;
    CurveEditor editor(ctx, track);
    ASSERT_TRUE(editor.moveKey("jawopen", 1, { 1.0, 9.0 }));
    EXPECT_DOUBLE_EQ(track.mKeys[1].value, 1.0);
    EXPECT_FALSE(editor.moveKey("jawopen", 1, { 1.0, 1.0 }));
    EXPECT_EQ(ctx.mSteps.size(), 1u);
}

TEST(TimelineWav, makesA16BitMonoFileFromTheChosenSecond)
{
    std::vector<float> s(100, 0.f);
    s[50] = 0.5f;
    s[51] = std::numeric_limits<float>::quiet_NaN();
    auto wav = pcmToWav16(s.data(), s.size(), 100, 0.5);
    ASSERT_EQ(wav.size(), 44u + 50u * 2u);
    EXPECT_EQ(std::string(wav.data(), 4), "RIFF");
    EXPECT_EQ(std::string(wav.data() + 8, 8), "WAVEfmt ");
    EXPECT_EQ(std::string(wav.data() + 36, 4), "data");
    auto u16 = [&](std::size_t at) { return static_cast<std::uint16_t>(static_cast<unsigned char>(wav[at]) | (static_cast<unsigned char>(wav[at + 1]) << 8)); };
    auto u32 = [&](std::size_t at) { return static_cast<std::uint32_t>(u16(at)) | (static_cast<std::uint32_t>(u16(at + 2)) << 16); };
    EXPECT_EQ(u16(20), 1u); // PCM
    EXPECT_EQ(u16(22), 1u); // mono
    EXPECT_EQ(u32(24), 100u);
    EXPECT_EQ(u16(34), 16u);
    EXPECT_EQ(u32(40), 100u); // data bytes
    EXPECT_EQ(u32(4), 36u + 100u);
    EXPECT_EQ(static_cast<std::int16_t>(u16(44)), 16384); // 0.5
    EXPECT_EQ(static_cast<std::int16_t>(u16(46)), 0); // NaN is silence
}

TEST(TimelineWav, refusesWhatCannotPlay)
{
    std::vector<float> s(10, 0.1f);
    EXPECT_TRUE(pcmToWav16(nullptr, 10, 100, 0.0).empty());
    EXPECT_TRUE(pcmToWav16(s.data(), 0, 100, 0.0).empty());
    EXPECT_TRUE(pcmToWav16(s.data(), 10, 0, 0.0).empty());
    EXPECT_TRUE(pcmToWav16(s.data(), 10, 100, 5.0).empty()); // past the end
    EXPECT_TRUE(pcmToWav16(s.data(), 10, 100, std::numeric_limits<double>::infinity()).empty());
    EXPECT_FALSE(pcmToWav16(s.data(), 10, 100, -3.0).empty()); // before the start plays from 0
}

TEST(TimelineDialog, refusesBadDescriptions)
{
    DialogDesc d;
    EXPECT_FALSE(checkDialog(d).empty()); // no title
    d.title = "Generate";
    EXPECT_FALSE(checkDialog(d).empty()); // no fields
    PropertyDesc f;
    f.id = "variant";
    f.label = "Variant";
    f.type = PropertyType::Choice;
    d.fields.push_back(f);
    EXPECT_FALSE(checkDialog(d).empty()); // a Choice with no choices
    d.fields[0].choices = { "v2.3", "v3.0" };
    EXPECT_TRUE(checkDialog(d).empty());
    d.fields.push_back(d.fields[0]);
    EXPECT_FALSE(checkDialog(d).empty()); // duplicate id
    d.fields[1].id = "pos";
    d.fields[1].type = PropertyType::Vec3;
    EXPECT_FALSE(checkDialog(d).empty()); // Vec3
    d.fields.resize(1);
    for (std::size_t i = 0; i < kMaxDialogFields; ++i)
    {
        PropertyDesc extra;
        extra.id = "x" + std::to_string(i);
        extra.type = PropertyType::Bool;
        d.fields.push_back(extra);
    }
    EXPECT_FALSE(checkDialog(d).empty()); // too many
}

TEST(TimelineDialog, startValuesFallBackToTheTypesZero)
{
    DialogDesc d;
    d.title = "t";
    PropertyDesc a;
    a.id = "a";
    a.type = PropertyType::Choice;
    a.choices = { "x", "y" };
    PropertyDesc b;
    b.id = "b";
    b.type = PropertyType::Float;
    b.min = 1.0;
    b.max = 5.0;
    PropertyDesc c;
    c.id = "c";
    c.type = PropertyType::Bool;
    d.fields = { a, b, c };
    d.initial = { std::string("y"), std::string("wrong type") };
    auto v = dialogStartValues(d);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(std::get<std::string>(v[0]), "y");
    EXPECT_DOUBLE_EQ(std::get<double>(v[1]), 1.0);
    EXPECT_FALSE(std::get<bool>(v[2]));
    d.initial = { std::string("nope") };
    EXPECT_EQ(std::get<std::string>(dialogStartValues(d)[0]), "x");
}

TEST(TimelineDialog, valuesAreCheckedAgainstTheirFields)
{
    DialogDesc d;
    d.title = "t";
    PropertyDesc b;
    b.id = "b";
    b.type = PropertyType::Int;
    b.min = 0;
    b.max = 10;
    d.fields = { b };
    EXPECT_TRUE(dialogValuesValid(d, { std::int64_t{ 4 } }));
    EXPECT_FALSE(dialogValuesValid(d, { std::int64_t{ 40 } }));
    EXPECT_FALSE(dialogValuesValid(d, { 4.0 }));
    EXPECT_FALSE(dialogValuesValid(d, {}));
}

TEST(TimelineDialog, theServiceRefusesWithoutARunnerAndFiltersTheAnswer)
{
    DialogService service;
    DialogDesc d;
    d.title = "t";
    PropertyDesc b;
    b.id = "b";
    b.type = PropertyType::Int;
    b.min = 0;
    b.max = 10;
    d.fields = { b };
    EXPECT_FALSE(service.show(d).has_value()); // no runner: the host cannot show one
    EXPECT_FALSE(service.lastRefusal().empty());

    service.setRunner([](const DialogDesc&, const std::vector<PropertyValue>& start) {
        return std::optional<std::vector<PropertyValue>>(start);
    });
    auto ok = service.show(d);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(std::get<std::int64_t>((*ok)[0]), 0);
    EXPECT_TRUE(service.lastRefusal().empty());

    service.setRunner([](const DialogDesc&, const std::vector<PropertyValue>&) {
        return std::optional<std::vector<PropertyValue>>(std::vector<PropertyValue>{ std::int64_t{ 99 } });
    });
    EXPECT_FALSE(service.show(d).has_value()); // an answer outside the range is not handed on

    service.setRunner([](const DialogDesc&, const std::vector<PropertyValue>&) {
        return std::optional<std::vector<PropertyValue>>();
    });
    EXPECT_FALSE(service.show(d).has_value()); // cancelled
    EXPECT_TRUE(service.lastRefusal().empty());

    d.title.clear();
    service.setRunner([](const DialogDesc&, const std::vector<PropertyValue>& s) { return std::optional(s); });
    EXPECT_FALSE(service.show(d).has_value()); // invalid description never reaches the runner
}
