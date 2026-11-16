// Task 0257: Night Eye and the image space modifiers of Skyrim SE (game_skyrimse/image_space_mod.hpp): the IMAD
// decoder, the sampling of a record and the intro / hold / outro tracker. Generated records only; every number is
// worked out by hand. The layout is in docs/tasks/0257-skyrim-night-eye.md. Written before the code.
#include <game_skyrimse/image_space_mod.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::imagespace;

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    struct Fixture
    {
        LoadOrder mOrder = [] {
            LoadOrder order;
            std::string err;
            EXPECT_TRUE(order.add("Skyrim.esm", false, err));
            EXPECT_TRUE(order.add("test.esp", false, err));
            return order;
        }();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC("IMAD");
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    Bytes dnam(std::uint32_t flags, float duration)
    {
        Bytes d;
        d.u32(flags).f32(duration).fixed("", 236); // 244 bytes: the key counts follow, none of them are read
        return sub4("DNAM", d);
    }

    Bytes keys(const char (&name)[5], const std::vector<std::pair<float, float>>& list)
    {
        Bytes d;
        for (const auto& k : list)
            d.f32(k.first).f32(k.second);
        return sub4(name, d);
    }

    Bytes tint(const std::vector<std::vector<float>>& list)
    {
        Bytes d;
        for (const auto& k : list)
            for (float v : k)
                d.f32(v);
        return sub4("TNAM", d);
    }

    // The intro of Night Eye as Skyrim.esm stores it (4 s; Update.esm overrides it with a 1 s one), cut down to what the core reads.
    Bytes nightEyeIntroSubs()
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("NightEyeImodIntro")));
        b.append(dnam(1, 4.f));
        b.append(tint({ { 0.f, 0.592f, 0.773f, 0.875f, 0.f }, { 0.2f, 0.592f, 0.773f, 0.875f, 0.502f },
            { 1.f, 0.592f, 0.773f, 0.875f, 0.502f } }));
        b.append(keys("\x11IAD", { { 0.f, 1.f }, { 0.087f, 0.55f }, { 0.2f, 0.5f }, { 1.f, 0.5f } }));
        b.append(keys("QIAD", { { 0.f, 0.f }, { 1.f, 0.f } }));
        b.append(keys("\x12IAD", { { 0.f, 1.f }, { 1.f, 1.f } }));
        b.append(keys("RIAD", { { 0.f, 0.f }, { 0.2f, 1.1f }, { 1.f, 1.1f } }));
        b.append(keys("\x13IAD", { { 0.f, 1.f }, { 1.f, 1.f } }));
        b.append(keys("SIAD", { { 0.f, 0.f }, { 0.2f, 0.2f }, { 1.f, 0.2f } }));
        return b;
    }

    // ---- the decoder -----------------------------------------------------------------------------------------------

    TEST(SkyrimImageModDecode, theTracksTheCoreDrawsAreRead)
    {
        Fixture f;
        std::string why;
        const Bytes subs = nightEyeIntroSubs();
        ImageModifierRecord r;
        ASSERT_TRUE(decodeImageModifier(view(subs), raw(0x6b10a), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "NightEyeImodIntro");
        EXPECT_TRUE(r.mHasData);
        EXPECT_TRUE(r.mAnimatable);
        EXPECT_FLOAT_EQ(r.mDuration, 4.f);
        ASSERT_EQ(r.mTint.size(), 3u);
        EXPECT_FLOAT_EQ(r.mTint[1].mTime, 0.2f);
        EXPECT_FLOAT_EQ(r.mTint[1].mRgb[0], 0.592f);
        EXPECT_FLOAT_EQ(r.mTint[1].mRgb[2], 0.875f);
        EXPECT_FLOAT_EQ(r.mTint[1].mAmount, 0.502f);
        ASSERT_EQ(r.mSaturationMult.size(), 4u);
        EXPECT_FLOAT_EQ(r.mSaturationMult[1].mTime, 0.087f);
        EXPECT_FLOAT_EQ(r.mSaturationMult[1].mValue, 0.55f);
        EXPECT_EQ(r.mSaturationAdd.size(), 2u);
        EXPECT_EQ(r.mBrightnessMult.size(), 2u);
        ASSERT_EQ(r.mBrightnessAdd.size(), 3u);
        EXPECT_FLOAT_EQ(r.mBrightnessAdd[1].mValue, 1.1f);
        EXPECT_EQ(r.mContrastMult.size(), 2u);
        ASSERT_EQ(r.mContrastAdd.size(), 3u);
        EXPECT_FLOAT_EQ(r.mContrastAdd[2].mValue, 0.2f);
    }

    TEST(SkyrimImageModDecode, aHeldStateHasNoAnimatableFlag)
    {
        Fixture f;
        std::string why;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Main")));
        subs.append(dnam(0, 1.f));
        ImageModifierRecord r;
        ASSERT_TRUE(decodeImageModifier(view(subs), raw(0x6b108), f.context(), r, why)) << why;
        EXPECT_TRUE(r.mHasData);
        EXPECT_FALSE(r.mAnimatable);
    }

    TEST(SkyrimImageModDecode, aTrackOfAnOddSizeOrWithANonFiniteKeyIsLeftOutAndCounted)
    {
        Fixture f;
        std::string why;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Odd")));
        subs.append(dnam(1, 1.f));
        subs.append(sub4("\x11IAD", Bytes().u32(1).u32(2).u32(3))); // 12 bytes: not whole keys
        subs.append(keys("QIAD", { { 0.f, kNan } })); // not finite
        subs.append(sub4("TNAM", Bytes().f32(0.f).f32(1.f).f32(1.f))); // 12 bytes: not a tint key
        subs.append(keys("RIAD", { { 0.f, 0.f }, { 1.f, 1.f } })); // good
        ImageModifierRecord r;
        ASSERT_TRUE(decodeImageModifier(view(subs), raw(0x6b10c), f.context(), r, why)) << why;
        EXPECT_TRUE(r.mSaturationMult.empty());
        EXPECT_TRUE(r.mSaturationAdd.empty());
        EXPECT_TRUE(r.mTint.empty());
        EXPECT_EQ(r.mBrightnessAdd.size(), 2u);
        EXPECT_GE(f.mNotes.mOddSubrecords, 3u);
    }

    TEST(SkyrimImageModDecode, aTrackOverTheKeyCapIsLeftOut)
    {
        Fixture f;
        std::string why;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Many")));
        subs.append(dnam(1, 1.f));
        std::vector<std::pair<float, float>> many;
        for (std::size_t i = 0; i <= kMaxKeysPerTrack; ++i)
            many.push_back({ static_cast<float>(i) / 100.f, 1.f });
        subs.append(keys("\x12IAD", many));
        ImageModifierRecord r;
        ASSERT_TRUE(decodeImageModifier(view(subs), raw(0x6b10d), f.context(), r, why)) << why;
        EXPECT_TRUE(r.mBrightnessMult.empty());
        EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimImageModDecode, aDnamOfTheWrongSizeIsOddAndGivesNoData)
    {
        Fixture f;
        std::string why;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Short")));
        subs.append(sub4("DNAM", Bytes().u32(1).f32(2.f)));
        ImageModifierRecord r;
        ASSERT_TRUE(decodeImageModifier(view(subs), raw(0x6b10e), f.context(), r, why)) << why;
        EXPECT_FALSE(r.mHasData);
        EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimImageModDecode, aCutShortSubrecordRefusesTheRecord)
    {
        Fixture f;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("Cut"));
        subs.text(std::string("TNAM\x20\x00", 6)); // claims 32 bytes, none follow
        ImageModifierRecord r;
        EXPECT_FALSE(decodeImageModifier(view(subs), raw(0x6b10f), f.context(), r, why));
        EXPECT_FALSE(why.empty());
    }

    // ---- sampling --------------------------------------------------------------------------------------------------

    ImageModifierRecord introRecord()
    {
        Fixture f;
        std::string why;
        const Bytes subs = nightEyeIntroSubs();
        ImageModifierRecord r;
        EXPECT_TRUE(decodeImageModifier(view(subs), raw(0x6b10a), f.context(), r, why)) << why;
        return r;
    }

    TEST(SkyrimImageModSample, theFirstAndLastKeysAreHeldOutsideTheirTimes)
    {
        const ImageModifierRecord r = introRecord();
        const State start = sample(r, -3.f);
        EXPECT_TRUE(start.active);
        EXPECT_FLOAT_EQ(start.saturationMult, 1.f);
        EXPECT_FLOAT_EQ(start.brightnessAdd, 0.f);
        EXPECT_FLOAT_EQ(start.tintAmount, 0.f);
        const State end = sample(r, 7.f);
        EXPECT_FLOAT_EQ(end.saturationMult, 0.5f);
        EXPECT_FLOAT_EQ(end.brightnessAdd, 1.1f);
        EXPECT_FLOAT_EQ(end.contrastAdd, 0.2f);
        EXPECT_FLOAT_EQ(end.tintAmount, 0.502f);
        EXPECT_FLOAT_EQ(end.tint[0], 0.592f);
        EXPECT_FLOAT_EQ(end.tint[1], 0.773f);
        EXPECT_FLOAT_EQ(end.tint[2], 0.875f);
    }

    TEST(SkyrimImageModSample, valuesBetweenKeysAreLinear)
    {
        const ImageModifierRecord r = introRecord();
        // brightness add: 0 at 0, 1.1 at 0.2: at 0.1 half of it
        EXPECT_NEAR(sample(r, 0.1f).brightnessAdd, 0.55f, 1e-6f);
        // saturation mult: 0.55 at 0.087, 0.5 at 0.2: at 0.1435 the middle
        EXPECT_NEAR(sample(r, 0.1435f).saturationMult, 0.525f, 1e-5f);
        // tint amount: 0 at 0, 0.502 at 0.2: at 0.05 a quarter of it
        EXPECT_NEAR(sample(r, 0.05f).tintAmount, 0.1255f, 1e-5f);
    }

    TEST(SkyrimImageModSample, aTrackWithNoKeysIsNeutral)
    {
        const ImageModifierRecord r;
        const State s = sample(r, 0.5f);
        EXPECT_TRUE(s.active);
        EXPECT_FLOAT_EQ(s.saturationMult, 1.f);
        EXPECT_FLOAT_EQ(s.saturationAdd, 0.f);
        EXPECT_FLOAT_EQ(s.brightnessMult, 1.f);
        EXPECT_FLOAT_EQ(s.contrastMult, 1.f);
        EXPECT_FLOAT_EQ(s.tintAmount, 0.f);
    }

    TEST(SkyrimImageModSample, aTimeThatIsNotANumberSamplesTheStart)
    {
        const ImageModifierRecord r = introRecord();
        EXPECT_FLOAT_EQ(sample(r, kNan).brightnessAdd, 0.f);
    }

    TEST(SkyrimImageModLerp, blendsEveryNumberAndClampsTheShare)
    {
        State a;
        a.active = true;
        a.saturationMult = 1.f;
        a.brightnessAdd = 0.f;
        a.tintAmount = 0.f;
        a.tint = { 0.f, 0.f, 0.f };
        State b;
        b.active = true;
        b.saturationMult = 0.5f;
        b.brightnessAdd = 1.f;
        b.tintAmount = 0.5f;
        b.tint = { 1.f, 0.5f, 0.f };
        const State m = lerp(a, b, 0.5f);
        EXPECT_TRUE(m.active);
        EXPECT_FLOAT_EQ(m.saturationMult, 0.75f);
        EXPECT_FLOAT_EQ(m.brightnessAdd, 0.5f);
        EXPECT_FLOAT_EQ(m.tintAmount, 0.25f);
        EXPECT_FLOAT_EQ(m.tint[0], 0.5f);
        EXPECT_FLOAT_EQ(m.tint[1], 0.25f);
        EXPECT_FLOAT_EQ(lerp(a, b, 7.f).saturationMult, 0.5f);
        EXPECT_FLOAT_EQ(lerp(a, b, -7.f).saturationMult, 1.f);
        EXPECT_FLOAT_EQ(lerp(a, b, kNan).saturationMult, 1.f);
    }

    // ---- the tracker -----------------------------------------------------------------------------------------------

    // An intro of 2 s whose saturation goes 1 -> 0.5, an outro of 1 s whose saturation goes 0.5 -> 1.
    struct Pair
    {
        ImageModifierRecord mIntro, mOutro;
        Pair()
        {
            mIntro.mHasData = mOutro.mHasData = true;
            mIntro.mAnimatable = mOutro.mAnimatable = true;
            mIntro.mDuration = 2.f;
            mOutro.mDuration = 1.f;
            mIntro.mSaturationMult = { { 0.f, 1.f }, { 1.f, 0.5f } };
            mOutro.mSaturationMult = { { 0.f, 0.5f }, { 1.f, 1.f } };
        }
    };

    TEST(SkyrimNightEyeTracker, whileItIsNotRunningNothingChanges)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        EXPECT_FALSE(t.update(false, 0.f).active);
        EXPECT_FALSE(t.update(false, 5.f).active);
    }

    TEST(SkyrimNightEyeTracker, theIntroFadesInThenTheLastKeyIsHeld)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        const State s = t.update(true, 10.f);
        EXPECT_TRUE(s.active);
        EXPECT_FLOAT_EQ(s.saturationMult, 1.f);
        EXPECT_NEAR(t.update(true, 11.f).saturationMult, 0.75f, 1e-6f); // half the 2 s intro
        EXPECT_NEAR(t.update(true, 12.f).saturationMult, 0.5f, 1e-6f);
        EXPECT_NEAR(t.update(true, 50.f).saturationMult, 0.5f, 1e-6f); // held
    }

    TEST(SkyrimNightEyeTracker, theOutroPlaysFromTheHeldStateAndEnds)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        (void)t.update(true, 10.f);
        (void)t.update(true, 20.f);
        EXPECT_NEAR(t.update(false, 21.f).saturationMult, 0.5f, 1e-6f);
        EXPECT_NEAR(t.update(false, 21.5f).saturationMult, 0.75f, 1e-6f);
        EXPECT_TRUE(t.update(false, 21.9f).active);
        EXPECT_FALSE(t.update(false, 22.f).active);
        EXPECT_FALSE(t.update(false, 30.f).active);
    }

    TEST(SkyrimNightEyeTracker, anIntroEndedEarlyFadesOutFromWhereItWas)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        (void)t.update(true, 10.f);
        EXPECT_NEAR(t.update(true, 11.f).saturationMult, 0.75f, 1e-6f);
        // the effect ends half way into the intro: the outro starts at 0.5 and is scaled by the half that had played,
        // so the picture is continuous: 1 + (0.5 - 1) * 0.5 = 0.75
        EXPECT_NEAR(t.update(false, 11.f).saturationMult, 0.75f, 1e-6f);
        // a quarter into the outro: outro 0.625, scaled: 1 + (0.625 - 1) * 0.5 = 0.8125
        EXPECT_NEAR(t.update(false, 11.25f).saturationMult, 0.8125f, 1e-6f);
        EXPECT_FALSE(t.update(false, 12.f).active);
    }

    TEST(SkyrimNightEyeTracker, runningAgainStartsTheIntroAgain)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        (void)t.update(true, 0.f);
        (void)t.update(true, 5.f);
        (void)t.update(false, 6.f);
        (void)t.update(false, 7.f); // gone
        EXPECT_FALSE(t.update(false, 7.5f).active);
        EXPECT_FLOAT_EQ(t.update(true, 8.f).saturationMult, 1.f);
        EXPECT_NEAR(t.update(true, 9.f).saturationMult, 0.75f, 1e-6f);
    }

    TEST(SkyrimNightEyeTracker, withoutAnIntroRecordNothingEverChanges)
    {
        NightEyeTracker t;
        EXPECT_FALSE(t.hasRecords());
        EXPECT_FALSE(t.update(true, 1.f).active);
        Pair p;
        t.setRecords(nullptr, &p.mOutro);
        EXPECT_FALSE(t.update(true, 2.f).active);
    }

    TEST(SkyrimNightEyeTracker, withoutAnOutroRecordTheEffectEndsAtOnce)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, nullptr);
        (void)t.update(true, 0.f);
        EXPECT_TRUE(t.update(true, 3.f).active);
        EXPECT_FALSE(t.update(false, 3.1f).active);
    }

    TEST(SkyrimNightEyeTracker, aClockThatIsNotANumberIsIgnoredAndOneThatGoesBackIsNoTime)
    {
        Pair p;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        (void)t.update(true, 10.f);
        EXPECT_NEAR(t.update(true, 11.f).saturationMult, 0.75f, 1e-6f);
        EXPECT_NEAR(t.update(true, kNan).saturationMult, 0.75f, 1e-6f); // the last state again
        EXPECT_NEAR(t.update(true, 4.f).saturationMult, 0.75f, 1e-6f); // goes back: no time passes
    }

    TEST(SkyrimNightEyeTracker, theContrastCanBeKeptNeutralWhileEverythingElseFollowsTheRecords)
    {
        // The core's contrast pivots on mid grey, the game's on the scene's average luma: a night scene would only get
        // darker, so the glue asks for no contrast change (docs/tasks/0257-skyrim-night-eye.md).
        Pair p;
        p.mIntro.mContrastAdd = { { 0.f, 0.f }, { 1.f, 0.2f } };
        p.mIntro.mContrastMult = { { 0.f, 1.f }, { 1.f, 0.9f } };
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        const State with = t.update(true, 0.f);
        EXPECT_FLOAT_EQ(with.contrastAdd, 0.f); // the start key is neutral anyway
        EXPECT_NEAR(t.update(true, 5.f).contrastAdd, 0.2f, 1e-6f);
        NightEyeTracker n;
        n.setNeutralContrast(true);
        n.setRecords(&p.mIntro, &p.mOutro);
        (void)n.update(true, 0.f);
        const State held = n.update(true, 5.f);
        EXPECT_TRUE(held.active);
        EXPECT_FLOAT_EQ(held.contrastAdd, 0.f);
        EXPECT_FLOAT_EQ(held.contrastMult, 1.f);
        EXPECT_NEAR(held.saturationMult, 0.5f, 1e-6f); // the rest is untouched
    }

    TEST(SkyrimNightEyeTracker, anIntroOfNoDurationIsHeldAtOnce)
    {
        Pair p;
        p.mIntro.mDuration = 0.f;
        NightEyeTracker t;
        t.setRecords(&p.mIntro, &p.mOutro);
        EXPECT_NEAR(t.update(true, 1.f).saturationMult, 0.5f, 1e-6f);
    }

    TEST(SkyrimNightEyeTracker, twoTrackersGivenTheSameTimesGiveTheSameStates)
    {
        Pair p;
        NightEyeTracker a, b;
        a.setRecords(&p.mIntro, &p.mOutro);
        b.setRecords(&p.mIntro, &p.mOutro);
        struct Step
        {
            bool running;
            float now;
        };
        const std::vector<Step> steps{ { true, 0.f }, { true, 0.4f }, { true, 1.1f }, { false, 1.5f }, { false, 1.9f },
            { true, 2.0f }, { true, 3.3f }, { false, 9.f }, { false, 9.4f }, { false, 11.f } };
        for (const Step& s : steps)
        {
            const State x = a.update(s.running, s.now);
            const State y = b.update(s.running, s.now);
            EXPECT_EQ(x.active, y.active);
            EXPECT_EQ(x.saturationMult, y.saturationMult);
        }
    }
}
