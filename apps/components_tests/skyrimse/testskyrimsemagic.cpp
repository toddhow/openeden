// Task 0186 (milestone M-S5, stage S5-8): the pure magic maths and the spell and magic effect decoders
// (game_skyrimse/magic.hpp, perks.hpp). No game data. Every number is derived by hand; HOW the numbers combine is
// RECALLED-VERIFY, these tests pin THIS implementation (see the task log).
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/perks.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatSubject;

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    TEST(SkyrimMagicMath, onlyHealthMagickaAndStaminaAreVitals)
    {
        EXPECT_TRUE(isVitalValue(24));
        EXPECT_TRUE(isVitalValue(25));
        EXPECT_TRUE(isVitalValue(26));
        EXPECT_FALSE(isVitalValue(23));
        EXPECT_FALSE(isVitalValue(27));
        EXPECT_FALSE(isVitalValue(-1));
        EXPECT_FALSE(isVitalValue(63));
    }

    TEST(SkyrimMagicMath, timingFollowsTheCastTypeTheDurationAndTheFlags)
    {
        SpellEffect effect;
        MagicEffectRecord record;
        record.mHasData = true;
        record.mPrimaryValue = 24;
        // A held concentration spell ticks whatever the duration says.
        effect.mDuration = 1;
        EXPECT_EQ(effectTiming(MagicCast::Concentration, effect, record), EffectTiming::PerSecond);
        // Fire and forget: no duration is instant; a duration on a vital ticks; with the Recover flag or on another
        // value it is a status kept for its duration.
        effect.mDuration = 0;
        EXPECT_EQ(effectTiming(MagicCast::FireAndForget, effect, record), EffectTiming::Instant);
        effect.mDuration = 5;
        EXPECT_EQ(effectTiming(MagicCast::FireAndForget, effect, record), EffectTiming::PerSecond);
        record.mFlags = MagicFlag::Recover;
        EXPECT_EQ(effectTiming(MagicCast::FireAndForget, effect, record), EffectTiming::Timed);
        record.mFlags = 0;
        record.mPrimaryValue = 30;
        EXPECT_EQ(effectTiming(MagicCast::FireAndForget, effect, record), EffectTiming::Timed);
    }

    TEST(SkyrimMagicMath, aDetrimentalEffectHasANegativeMagnitude)
    {
        EXPECT_FLOAT_EQ(signedMagnitude(25.f, MagicFlag::Hostile | MagicFlag::Detrimental), -25.f);
        EXPECT_FLOAT_EQ(signedMagnitude(10.f, 0), 10.f);
        EXPECT_FLOAT_EQ(signedMagnitude(10.f, MagicFlag::Hostile), 10.f); // hostile alone does not change the sign
        EXPECT_FLOAT_EQ(signedMagnitude(kNan, MagicFlag::Detrimental), 0.f);
    }

    TEST(SkyrimMagicMath, aResistanceTakesItsPercentOffAndIsCappedAt85)
    {
        EXPECT_FLOAT_EQ(resistedMagnitude(25.f, 0.f), 25.f);
        EXPECT_FLOAT_EQ(resistedMagnitude(25.f, 50.f), 12.5f);
        EXPECT_FLOAT_EQ(resistedMagnitude(25.f, 100.f), 3.75f); // 25 x (1 - 0.85)
        EXPECT_FLOAT_EQ(resistedMagnitude(25.f, -10.f), 25.f); // never makes it larger
        EXPECT_FLOAT_EQ(resistedMagnitude(kNan, 10.f), 0.f);
        EXPECT_FLOAT_EQ(resistedMagnitude(25.f, kNan), 25.f);
    }

    TEST(SkyrimMagicMath, aWardHoldsThePeakTimesTheMaxBase)
    {
        MagicSettings settings; // 1.3 and 0
        EXPECT_FLOAT_EQ(wardMaxPower(40.f, settings, 0.f), 52.f);
        settings.mWardPowerMaxMult = 0.01f;
        EXPECT_FLOAT_EQ(wardMaxPower(40.f, settings, 50.f), 72.f); // 40 x (1.3 + 0.5)
        EXPECT_FLOAT_EQ(wardMaxPower(-40.f, settings, 0.f), 0.f);
        EXPECT_FLOAT_EQ(wardMaxPower(kNan, settings, 0.f), 0.f);
    }

    TEST(SkyrimMagicMath, aWardChargesAtItsRateAndStopsAtTheMaximum)
    {
        EXPECT_FLOAT_EQ(wardCharge(0.f, 52.f, 40.f, 0.5f), 20.f);
        EXPECT_FLOAT_EQ(wardCharge(50.f, 52.f, 40.f, 1.f), 52.f);
        EXPECT_FLOAT_EQ(wardCharge(10.f, 52.f, 40.f, -1.f), 10.f);
        EXPECT_FLOAT_EQ(wardCharge(10.f, 52.f, kNan, 1.f), 10.f);
        EXPECT_FLOAT_EQ(wardCharge(60.f, 52.f, 40.f, 0.f), 52.f); // never above the maximum
    }

    TEST(SkyrimMagicMath, aWardTakesWhatItCanAndLetsTheRestThrough)
    {
        float power = 30.f;
        EXPECT_FLOAT_EQ(wardAbsorb(power, 25.f), 0.f);
        EXPECT_FLOAT_EQ(power, 5.f);
        power = 10.f;
        EXPECT_FLOAT_EQ(wardAbsorb(power, 25.f), 15.f);
        EXPECT_FLOAT_EQ(power, 0.f);
        power = 0.f;
        EXPECT_FLOAT_EQ(wardAbsorb(power, 25.f), 25.f);
        power = 12.f;
        EXPECT_FLOAT_EQ(wardAbsorb(power, 0.f), 0.f);
        EXPECT_FLOAT_EQ(wardAbsorb(power, -4.f), 0.f);
        EXPECT_FLOAT_EQ(wardAbsorb(power, kNan), 0.f);
        EXPECT_FLOAT_EQ(power, 12.f); // nothing taken
    }

    ActiveEffect effectOn(std::uint32_t actor, float seconds)
    {
        ActiveEffect e;
        e.mTarget = CombatSubject::of(ActorHandle{ actor, 1 });
        e.mCaster = CombatSubject::of(ActorHandle{ 99, 1 });
        e.mRemaining = seconds;
        e.mTiming = EffectTiming::PerSecond;
        e.mPerSecond = -2.f;
        return e;
    }

    TEST(SkyrimMagicEffectList, anEffectEndsOnTimeAndTheLastStepIsPartial)
    {
        EffectList list;
        ASSERT_TRUE(list.add(effectOn(1, 1.0f)));
        std::vector<float> steps;
        std::vector<std::uint32_t> ended;
        for (int i = 0; i < 6; ++i)
            list.advance(
                0.25f, [&](ActiveEffect&, float used) { steps.push_back(used); },
                [&](const ActiveEffect& e) { ended.push_back(e.mId); });
        ASSERT_EQ(steps.size(), 4u); // four quarter seconds, then nothing is left to tick
        for (const float s : steps)
            EXPECT_FLOAT_EQ(s, 0.25f);
        ASSERT_EQ(ended.size(), 1u);
        EXPECT_EQ(ended[0], 1u);
        EXPECT_EQ(list.size(), 0u);

        ASSERT_TRUE(list.add(effectOn(1, 0.1f)));
        steps.clear();
        ended.clear();
        list.advance(
            0.25f, [&](ActiveEffect&, float used) { steps.push_back(used); },
            [&](const ActiveEffect& e) { ended.push_back(e.mId); });
        ASSERT_EQ(steps.size(), 1u);
        EXPECT_FLOAT_EQ(steps[0], 0.1f); // only what was left of the step
        EXPECT_EQ(ended.size(), 1u);
    }

    TEST(SkyrimMagicEffectList, effectsTickAndEndInTheOrderTheyWereAdded)
    {
        EffectList list;
        ASSERT_TRUE(list.add(effectOn(1, 0.5f)));
        ASSERT_TRUE(list.add(effectOn(2, 0.25f)));
        ASSERT_TRUE(list.add(effectOn(3, 0.5f)));
        std::vector<std::uint32_t> order;
        std::vector<std::uint32_t> ended;
        list.advance(
            0.25f, [&](ActiveEffect& e, float) { order.push_back(e.mId); },
            [&](const ActiveEffect& e) { ended.push_back(e.mId); });
        EXPECT_EQ(order, (std::vector<std::uint32_t>{ 1, 2, 3 }));
        EXPECT_EQ(ended, (std::vector<std::uint32_t>{ 2 }));
        order.clear();
        ended.clear();
        list.advance(
            0.25f, [&](ActiveEffect& e, float) { order.push_back(e.mId); },
            [&](const ActiveEffect& e) { ended.push_back(e.mId); });
        EXPECT_EQ(order, (std::vector<std::uint32_t>{ 1, 3 }));
        EXPECT_EQ(ended, (std::vector<std::uint32_t>{ 1, 3 }));
    }

    TEST(SkyrimMagicEffectList, noTimeAndBadTimeChangeNothing)
    {
        EffectList list;
        ASSERT_TRUE(list.add(effectOn(1, 1.f)));
        int ticks = 0;
        auto tick = [&](ActiveEffect&, float) { ++ticks; };
        auto ended = [&](const ActiveEffect&) { ++ticks; };
        list.advance(0.f, tick, ended);
        list.advance(-1.f, tick, ended);
        list.advance(kNan, tick, ended);
        list.advance(1.0e9f, tick, ended);
        EXPECT_EQ(ticks, 0);
        EXPECT_FLOAT_EQ(list.effects()[0].mRemaining, 1.f);
    }

    TEST(SkyrimMagicEffectList, anEffectWithNoTimeLeftOrAFullListIsRefused)
    {
        EffectList list;
        EXPECT_FALSE(list.add(effectOn(1, 0.f)));
        EXPECT_FALSE(list.add(effectOn(1, kNan)));
        EXPECT_EQ(list.size(), 0u);
        for (std::size_t i = 0; i < kMaxActiveEffects; ++i)
            EXPECT_TRUE(list.add(effectOn(1, 5.f)));
        EXPECT_FALSE(list.add(effectOn(1, 5.f)));
        EXPECT_EQ(list.size(), kMaxActiveEffects);
        EXPECT_GE(list.refused(), 1u);
    }

    TEST(SkyrimMagicEffectList, anActorThatGoesTakesItsEffectsWithIt)
    {
        EffectList list;
        ASSERT_TRUE(list.add(effectOn(1, 5.f)));
        ASSERT_TRUE(list.add(effectOn(2, 5.f)));
        ASSERT_TRUE(list.add(effectOn(1, 5.f)));
        EXPECT_EQ(list.removeTarget(CombatSubject::of(ActorHandle{ 1, 1 })), 2u);
        EXPECT_EQ(list.size(), 1u);
        EXPECT_EQ(list.removeCaster(CombatSubject::of(ActorHandle{ 99, 1 })), 1u); // all started by actor 99
        EXPECT_EQ(list.size(), 0u);
    }

    TEST(SkyrimMagicEffectList, idsCountFromOne)
    {
        EffectList list;
        ASSERT_TRUE(list.add(effectOn(1, 5.f)));
        ASSERT_TRUE(list.add(effectOn(1, 5.f)));
        EXPECT_EQ(list.effects()[0].mId, 1u);
        EXPECT_EQ(list.effects()[1].mId, 2u);
    }

    // ---- the records ---------------------------------------------------------------------------------------------

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
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

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    TEST(SkyrimMagicRecords, aSpellKeepsItsCostCastTypeDeliveryAndPerEffectConditions)
    {
        Fixture f;
        std::string why;
        Bytes spel;
        spel.append(sub4("EDID", Bytes().zstring("Firebolt")));
        Bytes spit;
        // cost 41, flags 0, type 0, charge time 0, cast type 1, delivery 2, cast duration 0.5, range 7 (36 bytes)
        spit.u32(41).u32(0).u32(0).u32(0).u32(1).u32(2).f32(0.5f).f32(7.f).u32(0);
        spel.append(sub4("SPIT", spit));
        spel.append(sub4("EFID", Bytes().u32(0x00000555)));
        spel.append(sub4("EFIT", Bytes().f32(25.f).u32(0).u32(0)));
        spel.append(sub4("EFID", Bytes().u32(0x00000556)));
        spel.append(sub4("EFIT", Bytes().f32(0.25f).u32(0).u32(0)));
        Bytes ctda;
        ctda.u8(0).u8(0).u8(0).u8(0).f32(1.f).u16(1).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        spel.append(sub4("CTDA", ctda));
        spel.append(sub4("CTDA", ctda));
        SpellRecord s;
        ASSERT_TRUE(decodeSpell(view(spel), raw("SPEL", 0x01000B00), f.context(), s, why)) << why;
        EXPECT_EQ(s.mCost, 41u);
        EXPECT_EQ(s.mCastType, 1u);
        EXPECT_EQ(s.mDelivery, 2u);
        EXPECT_FLOAT_EQ(s.mCastDuration, 0.5f);
        EXPECT_FLOAT_EQ(s.mRange, 7.f);
        ASSERT_EQ(s.mEffects.size(), 2u);
        EXPECT_EQ(s.mEffects[0].mConditions, 0u);
        EXPECT_EQ(s.mEffects[1].mConditions, 2u); // the conditions after an EFIT belong to that effect
    }

    TEST(SkyrimMagicRecords, aMagicEffectKeepsItsCastingNumbersProjectileAndScriptMark)
    {
        Fixture f;
        std::string why;
        Bytes mgef;
        mgef.append(sub4("EDID", Bytes().zstring("FireDamageFFAimed")));
        mgef.append(sub4("VMAD", Bytes().u16(5).u16(2).u16(0)));
        Bytes ctda;
        ctda.u8(0).u8(0).u8(0).u8(0).f32(1.f).u16(1).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        mgef.append(sub4("CTDA", ctda));
        mgef.append(sub4("CTDA", ctda));
        Bytes data;
        for (int i = 0; i < 38; ++i)
            data.u32(0); // 152 bytes
        std::string text = data.str();
        auto putU = [&](std::size_t offset, std::uint32_t v) { std::memcpy(text.data() + offset, &v, 4); };
        auto putF = [&](std::size_t offset, float v) { std::memcpy(text.data() + offset, &v, 4); };
        putU(0, 0x10221805);
        putF(4, 1.2f);
        putU(16, 41);
        putF(60, 0.5f);
        putU(64, 0);
        putU(68, 24);
        putU(72, 0x00000777);
        putU(80, 1);
        putU(84, 2);
        putU(88, 26);
        Bytes dataBytes;
        dataBytes.text(text);
        mgef.append(sub4("DATA", dataBytes));
        MagicEffectRecord m;
        ASSERT_TRUE(decodeMagicEffect(view(mgef), raw("MGEF", 0x01000C10), f.context(), m, why)) << why;
        EXPECT_FLOAT_EQ(m.mBaseCost, 1.2f);
        EXPECT_EQ(m.mResistValue, 41);
        EXPECT_FLOAT_EQ(m.mSecondaryWeight, 0.5f);
        EXPECT_EQ(m.mProjectile, *f.mResolver.resolve(0x777));
        EXPECT_EQ(m.mCastType, 1u);
        EXPECT_EQ(m.mDelivery, 2u);
        EXPECT_EQ(m.mSecondaryValue, 26);
        EXPECT_TRUE(m.mHasScript);
        EXPECT_EQ(m.mConditions, 2u);
        EXPECT_EQ(m.mFlags & MagicFlag::Detrimental, MagicFlag::Detrimental);
    }

    TEST(SkyrimMagicRecords, aMagicEffectWithNoProjectileHasNone)
    {
        Fixture f;
        std::string why;
        Bytes mgef;
        mgef.append(sub4("EDID", Bytes().zstring("RestoreHealthFFSelf")));
        Bytes data;
        for (int i = 0; i < 38; ++i)
            data.u32(0);
        mgef.append(sub4("DATA", data));
        MagicEffectRecord m;
        ASSERT_TRUE(decodeMagicEffect(view(mgef), raw("MGEF", 0x01000C11), f.context(), m, why)) << why;
        EXPECT_EQ(m.mProjectile, kNullFormId);
        EXPECT_FALSE(m.mHasScript);
    }
}
