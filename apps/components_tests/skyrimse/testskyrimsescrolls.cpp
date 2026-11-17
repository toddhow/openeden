// Task 0234 (milestone M-S6): scrolls. The SCRL decoder (game_skyrimse/scroll.hpp), the player's scroll cast in the
// magic
// runtime (combat.hpp castPlayerScroll, the cast clip and effects of task 0186) and the inventory service's use of a
// scroll (inventory_service.hpp mUseScroll). Generated records, a fake data source and a recording writer; no game
// data.
// Written before the code and run red against stubs first (see the task's stage log). HOW the numbers combine in the
// magic runtime is RECALLED-VERIFY (task 0186): these tests pin THIS implementation.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/inventory_service.hpp>
#include <game_skyrimse/scroll.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr float kPi = 3.14159265358979f;

    // ---- the decoder ---------------------------------------------------------------------------------------------

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct DecodeFixture
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

    // A SCRL as the five masters have it: DATA, then an SPIT of 36 bytes whose cast type is 3, then the effects.
    Bytes scrollBytes(std::uint32_t cost, std::uint32_t castType, std::uint32_t delivery, std::size_t spitBytes = 36)
    {
        Bytes scrl;
        scrl.append(sub4("EDID", Bytes().zstring("FireboltScroll")));
        scrl.append(sub4("DATA", Bytes().u32(30).f32(0.5f)));
        Bytes spit;
        spit.u32(cost).u32(0).u32(0).u32(0).u32(castType).u32(delivery).f32(0.f).f32(0.f).u32(0);
        std::string text = spit.str();
        text.resize(spitBytes);
        scrl.append(sub4("SPIT", Bytes().text(text)));
        scrl.append(sub4("EFID", Bytes().u32(0x00000555)));
        scrl.append(sub4("EFIT", Bytes().f32(25.f).u32(0).u32(0)));
        return scrl;
    }

    TEST(SkyrimScrollDecode, aScrollBecomesACastableSpellWithNoCostAndAFireAndForgetCast)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(
            decodeScroll(view(scrollBytes(25, kScrollCastType, 2)), raw("SCRL", 0x01000B00), f.context(), s, why))
            << why;
        EXPECT_TRUE(s.mScroll);
        EXPECT_TRUE(s.mHasData);
        EXPECT_EQ(s.mCost, 0u) << "a scroll costs no magicka (its SPIT cost field is not a cost)";
        EXPECT_EQ(s.mCastType, MagicCast::FireAndForget) << "cast type 3 is the runtime's fire and forget";
        EXPECT_EQ(s.mDelivery, 2u);
        ASSERT_EQ(s.mEffects.size(), 1u);
        EXPECT_FLOAT_EQ(s.mEffects[0].mMagnitude, 25.f);
        EXPECT_EQ(s.mCommon.mEditorId, "FireboltScroll");
    }

    TEST(SkyrimScrollDecode, aHeldScrollKeepsItsConcentrationCastType)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(decodeScroll(
            view(scrollBytes(2, MagicCast::Concentration, 0)), raw("SCRL", 0x01000B01), f.context(), s, why));
        EXPECT_EQ(s.mCastType, MagicCast::Concentration);
        EXPECT_EQ(s.mCost, 0u);
        EXPECT_TRUE(s.mScroll);
    }

    TEST(SkyrimScrollDecode, aScrollWhoseSpitIsTheWrongSizeHasNoDataAndIsNotCastable)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(
            decodeScroll(view(scrollBytes(25, kScrollCastType, 2, 20)), raw("SCRL", 0x01000B02), f.context(), s, why));
        EXPECT_FALSE(s.mHasData);
        EXPECT_TRUE(s.mScroll);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 1u) << "the odd size is counted, never guessed at";
    }

    TEST(SkyrimScrollDecode, aSpellDecodedAsASpellIsNotAScroll)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(decodeSpell(
            view(scrollBytes(41, MagicCast::FireAndForget, 2)), raw("SPEL", 0x01000B03), f.context(), s, why));
        EXPECT_FALSE(s.mScroll);
        EXPECT_EQ(s.mCost, 41u);
    }

    TEST(SkyrimScrollSupport, theArchetypesTheRuntimeAppliesAreTheOnesItsEffectSwitchHas)
    {
        for (const std::uint32_t archetype : { MagicArchetype::ValueModifier, MagicArchetype::PeakValueModifier,
                 MagicArchetype::Dual, MagicArchetype::Absorb, MagicArchetype::Stagger, MagicArchetype::Demoralize,
                 MagicArchetype::AccumulateMagnitude })
        {
            EXPECT_TRUE(scrollArchetypeApplied(archetype)) << archetype;
        }
        for (const std::uint32_t archetype : { 1u, 6u, 8u, 18u, 19u, 20u, 21u, 22u, 24u, 35u, 38u, 40u, 42u })
        {
            EXPECT_FALSE(scrollArchetypeApplied(archetype)) << archetype;
        }
    }

    TEST(SkyrimScrollSupport, aScrollIsAllSomeOrNoneSupportedByItsEffects)
    {
        std::map<FormId, MagicEffectRecord> effects;
        const auto add = [&](FormId id, std::uint32_t archetype) {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mHasData = true;
            m.mArchetype = archetype;
            effects[id] = m;
        };
        add(1, MagicArchetype::ValueModifier);
        add(2, MagicArchetype::Stagger);
        add(3, 21); // paralysis
        const auto find = [&](FormId id) -> const MagicEffectRecord* {
            const auto it = effects.find(id);
            return it == effects.end() ? nullptr : &it->second;
        };
        const auto scroll = [](std::vector<FormId> ids) {
            SpellRecord s;
            for (const FormId id : ids)
            {
                SpellEffect e;
                e.mEffect = id;
                s.mEffects.push_back(e);
            }
            return s;
        };
        EXPECT_EQ(classifyScroll(scroll({ 1, 2 }), find), ScrollSupport::All);
        EXPECT_EQ(classifyScroll(scroll({ 1, 3 }), find), ScrollSupport::Some);
        EXPECT_EQ(classifyScroll(scroll({ 3 }), find), ScrollSupport::None);
        EXPECT_EQ(classifyScroll(scroll({ 9 }), find), ScrollSupport::None) << "an effect record that is not found";
        EXPECT_EQ(classifyScroll(scroll({}), find), ScrollSupport::None);
    }

    // ---- the combat half -----------------------------------------------------------------------------------------

    constexpr WorldId kCasterBase = 100;
    constexpr FormId kFireScroll = 0x1000, kHealScroll = 0x1001, kEmptyScroll = 0x1002, kPlainSpell = 0x1003,
                     kHeldScroll = 0x1004, kBareScroll = 0x1005, kUnknown = 0x1999;
    constexpr FormId kFireEffect = 0x2000, kHealEffect = 0x2001, kProjectile = 0x3000;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "FireboltProjectile01";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mGravity = 0.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\FireBoltProjectile.nif";
            addEffect(kFireEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile);
            addEffect(kHealEffect, 0, 24, 0x201a00);
            addSpell(kFireScroll, "FireboltScroll", true, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kFireEffect, 25.f } });
            addSpell(kHealScroll, "FastHealingScroll", true, MagicCast::FireAndForget, MagicDelivery::Self,
                { { kHealEffect, 50.f } });
            addSpell(kEmptyScroll, "EmptyScroll", true, MagicCast::FireAndForget, MagicDelivery::Self, {});
            addSpell(kPlainSpell, "Firebolt", false, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kFireEffect, 25.f } });
            mSpells[kPlainSpell].mCost = 41;
            addSpell(kHeldScroll, "HeldScroll", true, MagicCast::Concentration, MagicDelivery::Self,
                { { kHealEffect, 10.f } });
            addSpell(kBareScroll, "BareScroll", true, MagicCast::FireAndForget, MagicDelivery::Self,
                { { kHealEffect, 5.f } });
            mSpells[kBareScroll].mHasData = false;

            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 100.f;
            mProfiles[kCasterBase] = caster;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }

        struct E
        {
            FormId mEffect;
            float mMagnitude;
        };
        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags,
            FormId projectile = kNullFormId)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, bool scroll, std::uint32_t cast, std::uint32_t delivery,
            const std::vector<E>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mScroll = scroll;
            s.mCastType = cast;
            s.mDelivery = delivery;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (!mClipTimingKnown || kind != ClipEventKind::SpellFire)
                return false;
            out = clip == "MRh_SelfRelease" ? 0.367f : 0.167f;
            return clip == "MRh_Release" || clip == "MRh_SelfRelease";
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunches.push_back({ owner, request.speed });
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                return false;
            out = mPlayerState;
            return true;
        }

        struct Launch
        {
            CombatSubject mOwner;
            float mSpeed;
        };
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        SubjectState mPlayerState;
        bool mClipTimingKnown = true;
    };

    struct CombatScroll : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        std::vector<std::string> mTrace;
        std::vector<std::pair<FormId, bool>> mOutcomes;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.setScrollOutcome([this](FormId scroll, bool cast) { mOutcomes.emplace_back(scroll, cast); });
            mWriter.mPlayerState.heading = kPi / 2.f;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void start()
        {
            mCombat.setData(&mData);
            update(); // the combat has run once: the scroll rules accept
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        SkyrimCombat::FighterView viewP()
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
            return v;
        }
    };

    TEST_F(CombatScroll, aScrollOfFireboltLaunchesItsProjectileAndSpendsNoMagicka)
    {
        start();
        ASSERT_TRUE(mCombat.castPlayerScroll(kFireScroll));
        EXPECT_TRUE(mWriter.mLaunches.empty()) << "queued: the cast goes at the next updates";
        updates(4);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mSpeed, 2500.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 100.f) << "a scroll takes no magicka";
        EXPECT_EQ(mCombat.spellsCast(), 1u);
        ASSERT_EQ(mOutcomes.size(), 1u);
        EXPECT_EQ(mOutcomes[0], std::make_pair(kFireScroll, true));
        EXPECT_TRUE(traceHas("COMBAT player scroll FireboltScroll queued"));
    }

    TEST_F(CombatScroll, aScrollOfFastHealingHealsThePlayerAndSpendsNoMagicka)
    {
        start();
        mWriter.mPlayerState.health = 40.f;
        ASSERT_TRUE(mCombat.castPlayerScroll(kHealScroll));
        updates(5);
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 90.f); // 40 + 50
        EXPECT_FLOAT_EQ(viewP().mMagicka, 100.f);
        EXPECT_EQ(mOutcomes, (std::vector<std::pair<FormId, bool>>{ { kHealScroll, true } }));
    }

    TEST_F(CombatScroll, aScrollIsRefusedWhenTheCombatNeverRanSoNothingIsUsedUp)
    {
        mCombat.setData(&mData); // no update yet: with [Game] neutral combat off none ever comes
        EXPECT_FALSE(mCombat.castPlayerScroll(kFireScroll));
        EXPECT_TRUE(mOutcomes.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(CombatScroll, aScrollIsRefusedWithNoDataAnUnknownIdAPlainSpellOrAnEmptyOrDatalessScroll)
    {
        SkyrimCombat bare;
        EXPECT_FALSE(bare.castPlayerScroll(kFireScroll));
        start();
        EXPECT_FALSE(mCombat.castPlayerScroll(kUnknown));
        EXPECT_FALSE(mCombat.castPlayerScroll(kPlainSpell)) << "a spell is cast with its own magicka, not as a scroll";
        EXPECT_FALSE(mCombat.castPlayerScroll(kEmptyScroll));
        EXPECT_FALSE(mCombat.castPlayerScroll(kBareScroll));
        updates(4);
        EXPECT_TRUE(mOutcomes.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(CombatScroll, onlyOneScrollWaitsAtATimeAndTheNextIsAcceptedOnceItWent)
    {
        start();
        ASSERT_TRUE(mCombat.castPlayerScroll(kFireScroll));
        EXPECT_FALSE(mCombat.castPlayerScroll(kHealScroll));
        updates(4);
        ASSERT_EQ(mOutcomes.size(), 1u);
        update();
        update(); // the first spell has gone
        EXPECT_TRUE(mCombat.castPlayerScroll(kHealScroll));
    }

    TEST_F(CombatScroll, aScrollWhoseCastNeverStartsGivesUpAndTheOutcomeSaysItWasNotCast)
    {
        start();
        mWriter.mClipTimingKnown = false; // the cast clip's sidecar never arrives
        ASSERT_TRUE(mCombat.castPlayerScroll(kFireScroll));
        updates(10, 0.25f); // 2.5 s: still waiting
        EXPECT_TRUE(mOutcomes.empty());
        updates(12, 0.25f); // past the five second wait
        ASSERT_EQ(mOutcomes.size(), 1u);
        EXPECT_EQ(mOutcomes[0], std::make_pair(kFireScroll, false));
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("gave up"));
        // and nothing is left waiting: another can be queued
        mWriter.mClipTimingKnown = true;
        EXPECT_TRUE(mCombat.castPlayerScroll(kFireScroll));
    }

    TEST_F(CombatScroll, theOutcomeIsToldOnceWhenTheCastBeginsAndNotAgain)
    {
        start();
        ASSERT_TRUE(mCombat.castPlayerScroll(kHealScroll));
        updates(12);
        EXPECT_EQ(mOutcomes.size(), 1u);
    }

    TEST_F(CombatScroll, aHeldScrollIsCastForTheDefaultHoldAndSpendsNoMagickaEither)
    {
        start();
        mWriter.mPlayerState.health = 40.f;
        ASSERT_TRUE(mCombat.castPlayerScroll(kHeldScroll));
        updates(4);
        EXPECT_EQ(mOutcomes.size(), 1u);
        updates(12);
        EXPECT_GT(mWriter.mPlayerState.health, 40.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 100.f);
    }

    TEST_F(CombatScroll, twoRunsOfTheSameCastGiveTheSameTrace)
    {
        const auto run = [] {
            FakeData data;
            SkyrimCombat combat;
            FakeWriter writer;
            std::vector<std::string> trace;
            combat.setTrace([&](const std::string& line) { trace.push_back(line); });
            writer.mPlayerState.heading = kPi / 2.f;
            writer.mPlayerState.healthMax = 100.f;
            writer.mPlayerState.health = 40.f;
            combat.setData(&data);
            combat.combatUpdate(0.25f, writer);
            EXPECT_TRUE(combat.castPlayerScroll(kHealScroll));
            for (int i = 0; i < 6; ++i)
                combat.combatUpdate(0.25f, writer);
            return trace;
        };
        EXPECT_EQ(run(), run());
    }

    // ---- the inventory half --------------------------------------------------------------------------------------

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kScroll = kSelf | 0x100;
    constexpr FormId kSwordItem = kSelf | 0x101;
    constexpr FormId kMiscItem = kSelf | 0x102;

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args)
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            return it->second.handler->call(args);
        }
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

    struct ScrollInventory : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::vector<FormId> mUsed;
        int mAnswer = 1;
        bool mWithHook = true;
        std::unique_ptr<inventory::InventoryService> mService;
        StubBindings mBindings;

        void SetUp() override
        {
            Bytes all = tes4Header(1.7f, 0, "A", "B", 3, { { "Skyrim.esm", 0 } }, 24, 44);
            const auto data
                = [](std::uint32_t value, float weight) { return sub4("DATA", Bytes().u32(value).f32(weight)); };
            all.append(group4(Content::fourCC("SCRL"), 0, record4("SCRL", 0, kScroll, data(30, 0.5f))));
            all.append(group4(Content::fourCC("WEAP"), 0,
                record4("WEAP", 0, kSwordItem, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)))));
            all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, kMiscItem, data(5, 1.f))));
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_scr_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            inventory::Hooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            if (mWithHook)
                hooks.mUseScroll = [this](FormId item) {
                    mUsed.push_back(item);
                    return mAnswer;
                };
            mService = std::make_unique<inventory::InventoryService>(std::move(hooks), 100.f, 1);
            ASSERT_TRUE(mService->declare(mBindings).ok);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!mService->poll() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mService->ready());
        }
        void TearDown() override
        {
            mService.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        void give(FormId item, std::int32_t count)
        {
            std::vector<inventory::Stack> stacks = mService->model().stacks(inventory::kPlayerOwner);
            stacks.push_back({ item, count });
            mService->model().setPlayer(std::move(stacks));
        }
        int held(FormId item) { return mService->model().count(inventory::kPlayerOwner, item); }
    };

    TEST_F(ScrollInventory, aScrollIsAScrollItemAndNotEquippable)
    {
        const inventory::ItemStat* stat = mService->table().item(kScroll);
        ASSERT_NE(stat, nullptr);
        EXPECT_EQ(stat->mCategory, inventory::Category::Scroll);
        EXPECT_EQ(mService->equipInfoOf(kScroll).mKind, inventory::EquipKind::Scroll);
        EXPECT_EQ(mBindings.call("use_kind", { str(inventory::formatId(kScroll)) }).value.text, "scroll");
        EXPECT_EQ(mBindings.call("use_kind", { str(inventory::formatId(kMiscItem)) }).value.text, "");
        EXPECT_EQ(mBindings.call("use_kind", { str(inventory::formatId(kSwordItem)) }).value.text, "weapon");
        give(kScroll, 1);
        EXPECT_NE(mService->equip(kScroll), inventory::EquipStatus::Ok) << "a scroll is used, not worn";
    }

    TEST_F(ScrollInventory, usingAScrollAsksTheHookAndOnAcceptanceUsesOneUp)
    {
        give(kScroll, 3);
        EXPECT_EQ(mService->use(kScroll), inventory::InventoryService::UseStatus::Ok);
        EXPECT_EQ(mUsed, std::vector<FormId>{ kScroll });
        EXPECT_EQ(held(kScroll), 2);
    }

    TEST_F(ScrollInventory, theUseMutateGoesTheSamePath)
    {
        give(kScroll, 1);
        (void)mBindings.call("use", { str(inventory::formatId(kScroll)) });
        EXPECT_EQ(held(kScroll), 0);
        EXPECT_EQ(mUsed.size(), 1u);
    }

    TEST_F(ScrollInventory, aRefusingHookLeavesTheScrollInTheInventory)
    {
        mAnswer = -1;
        give(kScroll, 2);
        EXPECT_EQ(mService->use(kScroll), inventory::InventoryService::UseStatus::Refused);
        EXPECT_EQ(held(kScroll), 2);
    }

    TEST_F(ScrollInventory, aHookThatDoesNotKnowTheItemLeavesItUnusedToo)
    {
        mAnswer = 0;
        give(kScroll, 2);
        EXPECT_NE(mService->use(kScroll), inventory::InventoryService::UseStatus::Ok);
        EXPECT_EQ(held(kScroll), 2);
    }

    TEST_F(ScrollInventory, aScrollNotHeldIsNotUsedAndTheHookIsNotAsked)
    {
        EXPECT_EQ(mService->use(kScroll), inventory::InventoryService::UseStatus::NotHeld);
        EXPECT_TRUE(mUsed.empty());
    }

    TEST_F(ScrollInventory, theHookIsNotAskedAboutAnythingButAScroll)
    {
        give(kMiscItem, 1);
        give(kSwordItem, 1);
        (void)mService->use(kMiscItem);
        (void)mService->use(kSwordItem);
        EXPECT_TRUE(mUsed.empty());
        EXPECT_EQ(held(kMiscItem), 1);
    }

    struct ScrollInventoryNoHook : ScrollInventory
    {
        ScrollInventoryNoHook() { mWithHook = false; }
    };

    TEST_F(ScrollInventoryNoHook, withNoHookTheScrollIsRefusedAndKept)
    {
        give(kScroll, 1);
        EXPECT_EQ(mService->use(kScroll), inventory::InventoryService::UseStatus::Refused);
        EXPECT_EQ(held(kScroll), 1);
    }
}
