// Task 0248: the magic effects the older tasks left without a rule: Berserker Rage (a timed perk), Voice of the Emperor
// (Calm), Ancestor's Wrath (a Cloak), Command Animal, Night Eye, spell absorb, the skill and shout fortifies of the
// blessings, and the diseases' health maximum, carry weight and skill modifiers. The record shapes are the data's
// (tools/skyrimse_effects_survey/survey.py, VERIFIED); every expected number is derived by hand in the test.
// Groups: RaceFxPure (race_effects.hpp, sumAbilityNumbers, modifiersOfExtended), RaceFxCombat (the combat runtime).
#include <game_skyrimse/ability_numbers.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/diseases.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/magic_status.hpp>
#include <game_skyrimse/race_effects.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBerserk = 0x5101, kCalm = 0x5102, kCloak = 0x5103, kCommand = 0x5104, kNightEye = 0x5105,
                     kAbsorbPower = 0x5106, kDibella = 0x5107, kZenithar = 0x5108, kTalos = 0x5109, kFireburn = 0x510A,
                     kCloakDamage = 0x510B, kSteed = 0x510C, kMara = 0x510D;
    constexpr FormId kEffBerserk = 0x6101, kEffCalm = 0x6102, kEffCloak = 0x6103, kEffCommand = 0x6104,
                     kEffNight = 0x6105, kEffAbsorb = 0x6106, kEffDibella = 0x6107, kEffZenithar = 0x6108,
                     kEffTalos = 0x6109, kEffFire = 0x610A, kEffCloakDamage = 0x610B, kEffCarry = 0x610C,
                     kEffMara = 0x610D;
    constexpr FormId kPerkBerserk = 0x7101, kProjectile = 0x7000;
    constexpr WorldId kArcherBase = 200;

    constexpr std::uint32_t kPeak = MagicArchetype::PeakValueModifier;
    constexpr std::uint32_t kPower = 2;

    class FxLibrary final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId) const override { return nullptr; }
        const SpellRecord* spell(FormId id) const override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* magicEffect(FormId id) const override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        void effect(FormId id, const char* editorId, std::uint32_t archetype, std::int32_t value, std::uint32_t flags)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = editorId;
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            mEffects[id] = m;
        }
        void spell(FormId id, const char* name, std::uint32_t type, std::uint32_t cast, FormId effect, float magnitude,
            std::uint32_t duration, std::uint32_t area = 0, std::uint32_t delivery = MagicDelivery::Self)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mType = type;
            s.mCastType = cast;
            s.mDelivery = delivery;
            SpellEffect e;
            e.mEffect = effect;
            e.mMagnitude = magnitude;
            e.mDuration = duration;
            e.mArea = area;
            s.mEffects.push_back(e);
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    FxLibrary makeLibrary()
    {
        FxLibrary lib;
        // VERIFIED shapes (survey): flags as in the data
        lib.effect(kEffBerserk, "RaceOrcBerserkEffect", 1, -1, 0x201802);
        lib.mEffects[kEffBerserk].mPerkToApply = kPerkBerserk;
        lib.effect(kEffCalm, "RaceImperialPacify", 6, 0, 0x4421112);
        lib.effect(kEffCloak, "PowerDarkElfFireCloakFFSelf", 35, -1, 0x10221900);
        lib.mEffects[kEffCloak].mAssociatedItem = kCloakDamage;
        lib.effect(kEffCommand, "RaceWoodElfCommandAnimal", 1, -1, 0x401102);
        lib.effect(kEffNight, "NightEyeEffect", 1, -1, 0x8020900);
        lib.effect(kEffAbsorb, "RaceBretonAbsorbSpellChance", kPeak, 83, 0x2);
        lib.effect(kEffDibella, "FortifyPersuasionFFSelf", kPeak, 17, 0x200902);
        lib.effect(kEffZenithar, "FortifySpeechcraftFFSelf", kPeak, 107, 0x200902);
        lib.effect(kEffTalos, "FortifyShoutTimerFFSelf", kPeak, 86, 0x200902);
        lib.effect(kEffMara, "FortifyRestorationFFSelf", kPeak, 112, 0x200902);
        lib.effect(kEffFire, "FireDamage", MagicArchetype::ValueModifier, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
        lib.mEffects[kEffFire].mResistValue = 41;
        lib.mEffects[kEffFire].mProjectile = kProjectile;
        lib.effect(kEffCloakDamage, "FireDamageConcAimedCloak", MagicArchetype::ValueModifier, 24,
            MagicFlag::Hostile | MagicFlag::Detrimental);
        lib.effect(kEffCarry, "Carry", kPeak, 32, 0x200802);

        lib.spell(kBerserk, "PowerOrcBerserk", kPower, MagicCast::FireAndForget, kEffBerserk, 1.f, 60);
        lib.spell(kCalm, "PowerImperialPacify", kPower, MagicCast::FireAndForget, kEffCalm, 99.f, 60, 75);
        lib.spell(kCloak, "PowerDarkElfFlameCloak", kPower, MagicCast::FireAndForget, kEffCloak, 10.f, 60);
        lib.spell(kCommand, "PowerWoodElfCommandAnimal", kPower, MagicCast::FireAndForget, kEffCommand, 99.f, 60, 75);
        lib.spell(kNightEye, "PowerKhajiitNightEye", 3, MagicCast::FireAndForget, kEffNight, 0.f, 60);
        lib.spell(kAbsorbPower, "PowerBretonAbsorbSpell", kPower, MagicCast::FireAndForget, kEffAbsorb, 100.f, 60);
        lib.spell(kDibella, "AltarDibellaSpell", 0, MagicCast::FireAndForget, kEffDibella, 10.f, 28800);
        lib.spell(kZenithar, "AltarZenitharSpell", 0, MagicCast::FireAndForget, kEffZenithar, 10.f, 28800);
        lib.spell(kTalos, "AltarTalosSpell", 0, MagicCast::FireAndForget, kEffTalos, 0.2f, 28800);
        lib.spell(kMara, "AltarMaraSpell", 0, MagicCast::FireAndForget, kEffMara, 10.f, 28800);
        lib.spell(kFireburn, "Fireburn", 0, MagicCast::FireAndForget, kEffFire, 40.f, 0, 0, MagicDelivery::Aimed);
        lib.mSpells[kFireburn].mCost = 5;
        lib.spell(kCloakDamage, "PowerDarkELfFlameCloakDmg", 0, MagicCast::Concentration, kEffCloakDamage, 8.f, 1);
        lib.spell(kSteed, "doomSteedAbility", SpellRecord::kTypeAbility, 0, kEffCarry, 100.f, 0);
        return lib;
    }

    // ---- RaceFxPure -------------------------------------------------------------------------------------------------

    TEST(RaceFxPure, aScriptEffectIsKnownByItsEditorIdAndOnlyAtArchetypeOne)
    {
        using race_effects::ScriptKind;
        EXPECT_EQ(race_effects::scriptKindOf(1, "RaceOrcBerserkEffect"), ScriptKind::Berserk);
        EXPECT_EQ(race_effects::scriptKindOf(1, "raceorcberserkeffect"), ScriptKind::Berserk);
        EXPECT_EQ(race_effects::scriptKindOf(1, "NightEyeEffect"), ScriptKind::NightEye);
        EXPECT_EQ(race_effects::scriptKindOf(1, "RaceWoodElfCommandAnimal"), ScriptKind::CommandAnimal);
        EXPECT_EQ(race_effects::scriptKindOf(1, "VoiceClearSkies1Self"), ScriptKind::None);
        EXPECT_EQ(race_effects::scriptKindOf(34, "RaceOrcBerserkEffect"), ScriptKind::None);
    }

    TEST(RaceFxPure, skillValuesAndTheirPercentModifiersAreSixAndNinetySixPlusTheSlot)
    {
        EXPECT_EQ(race_effects::skillPointsSlot(17), 11); // Speechcraft (Dibella)
        EXPECT_EQ(race_effects::skillPointsSlot(15), 9); // Sneak (Nocturnal)
        EXPECT_EQ(race_effects::skillPointsSlot(5), -1);
        EXPECT_EQ(race_effects::skillPointsSlot(24), -1);
        EXPECT_EQ(race_effects::skillPowerSlot(112), 16); // Restoration (Mara)
        EXPECT_EQ(race_effects::skillPowerSlot(99), 3); // Block (Stendarr)
        EXPECT_EQ(race_effects::skillPowerSlot(107), 11); // Speechcraft (Zenithar)
        EXPECT_EQ(race_effects::skillPowerSlot(114), -1);
    }

    TEST(RaceFxPure, anEffectiveSkillAddsPointsThenScalesAndStaysInRange)
    {
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(15.f, 10.f, 0.f), 25.f);
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(40.f, 0.f, 10.f), 44.f);
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(40.f, 10.f, -25.f), 37.5f);
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(40.f, 0.f, -400.f), 0.f);
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(250.f, 100.f, 0.f), 255.f);
        EXPECT_FLOAT_EQ(race_effects::effectiveSkill(std::nanf(""), 10.f, 0.f), 10.f);
    }

    TEST(RaceFxPure, anAbsorbChanceIsTheSumCutToAHundredAndAZeroChanceNeverAbsorbs)
    {
        EXPECT_FLOAT_EQ(race_effects::absorbChancePercent(50.f, 0.f), 50.f);
        EXPECT_FLOAT_EQ(race_effects::absorbChancePercent(50.f, 80.f), 100.f);
        EXPECT_FLOAT_EQ(race_effects::absorbChancePercent(-10.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(race_effects::absorbChancePercent(std::nanf(""), 20.f), 20.f);
        EXPECT_TRUE(race_effects::absorbs(50.f, 49.9f));
        EXPECT_FALSE(race_effects::absorbs(50.f, 50.f));
        EXPECT_FALSE(race_effects::absorbs(0.f, 0.f));
        EXPECT_FALSE(race_effects::absorbs(std::nanf(""), 0.f));
    }

    TEST(RaceFxPure, theShoutTimerShortensTheRecoveryAndTheScaleIsKeptInRange)
    {
        EXPECT_FLOAT_EQ(race_effects::shoutRecoveryScale(0.f), 1.f);
        EXPECT_FLOAT_EQ(race_effects::shoutRecoveryScale(0.2f), 0.8f);
        EXPECT_FLOAT_EQ(race_effects::shoutRecoveryScale(5.f), race_effects::kMinShoutRecoveryScale);
        EXPECT_FLOAT_EQ(race_effects::shoutRecoveryScale(-5.f), race_effects::kMaxShoutRecoveryScale);
        EXPECT_FLOAT_EQ(race_effects::shoutRecoveryScale(std::nanf("")), 1.f);
    }

    TEST(RaceFxPure, aCloakRadiusIsFeetInUnitsAndItsDamageIsRateTimesSeconds)
    {
        EXPECT_FLOAT_EQ(race_effects::cloakRadiusUnits(10.f), 213.3f);
        EXPECT_FLOAT_EQ(race_effects::cloakRadiusUnits(0.f), 0.f);
        EXPECT_FLOAT_EQ(race_effects::cloakRadiusUnits(std::nanf("")), 0.f);
        EXPECT_FLOAT_EQ(race_effects::cloakRadiusUnits(1.0e9f), race_effects::kMaxCloakRadius);
        EXPECT_FLOAT_EQ(race_effects::cloakDamage(8.f, 0.25f), 2.f);
        EXPECT_FLOAT_EQ(race_effects::cloakDamage(8.f, -1.f), 0.f);
        EXPECT_FLOAT_EQ(race_effects::cloakDamage(-8.f, 1.f), 0.f);
    }

    TEST(RaceFxPure, theBlessingsSkillShoutAndPercentValuesGoToTheirOwnNumbersAndAnyStaysFalse)
    {
        const FxLibrary lib = makeLibrary();
        const FormId d[] = { kDibella };
        const AbilityNumbers dibella = sumAbilityNumbers(lib, d);
        EXPECT_FLOAT_EQ(dibella.mSkillPoints[11], 10.f);
        EXPECT_FALSE(dibella.any()) << "the older pin: a skill blessing is not one of the vital numbers";
        EXPECT_TRUE(dibella.anySkillOrShout());
        const FormId z[] = { kZenithar, kMara };
        const AbilityNumbers both = sumAbilityNumbers(lib, z);
        EXPECT_FLOAT_EQ(both.mSkillPower[11], 10.f);
        EXPECT_FLOAT_EQ(both.mSkillPower[16], 10.f);
        const FormId t[] = { kTalos };
        EXPECT_FLOAT_EQ(sumAbilityNumbers(lib, t).mShoutTimer, 0.2f);
        EXPECT_FALSE(dibella == both) << "the revision must move when a skill number changes";
        EXPECT_TRUE(sumAbilityNumbers(lib, d) == dibella);
    }

    TEST(RaceFxPure, theExtendedDiseaseRuleAddsHealthCarryAndSkillSlotsAndTheOldRuleIsUnchanged)
    {
        FxLibrary lib;
        lib.effect(0x6201, "DisDamageHealthVampire", kPeak, 24, 0x400a06);
        lib.effect(0x6202, "DisDamageCarry", kPeak, 32, 0x400a06);
        lib.effect(0x6203, "DisDamagePickpocketLockpick", MagicArchetype::Dual, 104, 0x400a06);
        lib.mEffects[0x6203].mSecondaryValue = 103;
        lib.mEffects[0x6203].mSecondaryWeight = 0.6f;
        lib.spell(0x5201, "DiseaseSanguinareVampiris", SpellRecord::kTypeDisease, 0, 0x6201, 25.f, 0);
        lib.spell(0x5202, "DLC2DiseaseBlackHeartBlight", SpellRecord::kTypeDisease, 0, 0x6202, 10.f, 0);
        lib.spell(0x5203, "DiseaseAtaxia", SpellRecord::kTypeDisease, 0, 0x6203, 25.f, 0);
        const FormId all[] = { 0x5201, 0x5202, 0x5203 };
        std::vector<std::string> ignored;
        const PlayerValueModifiers older = diseases::modifiersOf(lib, all, &ignored);
        EXPECT_TRUE(older.isZero()) << "the older rule applies none of the three";
        EXPECT_EQ(ignored.size(), 3u);
        const PlayerValueModifiers m = diseases::modifiersOfExtended(lib, all, nullptr);
        EXPECT_FLOAT_EQ(m.mHealthMax, -25.f);
        EXPECT_FLOAT_EQ(m.mCarry, -10.f);
        EXPECT_FLOAT_EQ(m.mSkillPercent[8], -25.f); // lockpicking (104 - 96)
        EXPECT_FLOAT_EQ(m.mSkillPercent[7], -15.f); // pickpocket (103 - 96), weight 0.6
        EXPECT_FALSE(m.isZero());
        PlayerValueModifiers other = m;
        other.mCarry = 0.f;
        EXPECT_FALSE(m == other);
    }

    // ---- RaceFxCombat -----------------------------------------------------------------------------------------------

    class FxData final : public CombatData
    {
    public:
        FxData()
            : mLib(makeLibrary())
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "TestBoltProjectile";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\TestBolt.nif";
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 50.f;
            npc.mMagickaMax = 100.f;
            npc.mMagickaRegenPercent = 3.f;
            npc.mLevel = 3;
            npc.mAi.mKnown = true;
            mProfiles[kArcherBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mHealthRegenPercent = 0.f;
            mPlayer.mMagickaRegenPercent = 0.f;
            mPlayer.mStaminaRegenPercent = 0.f;
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
        const PerkLibrary* perkLibrary() override { return &mLib; }
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mLib.mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override { return mLib.spell(id); }
        const MagicEffectRecord* effectById(FormId id) override { return mLib.magicEffect(id); }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }

        FxLibrary mLib;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FxWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire)
                return false;
            if (clip == "MRh_Release")
                out = 0.167f;
            else if (clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest& request) override
        {
            mLaunches.push_back(request.token);
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
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
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        struct Clip
        {
            CombatSubject mSubject;
            std::uint32_t mToken;
        };
        std::vector<Clip> mClips;
        std::vector<std::uint32_t> mLaunches;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct RaceFxCombat : ::testing::Test
    {
        FxData mData;
        SkyrimCombat mCombat;
        FxWriter mWriter;
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start(double archerX = 100.0)
        {
            mCombat.setData(&mData);
            mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
            SubjectState s;
            s.x = archerX;
            mWriter.mStates[10] = s;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
            update();
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        SkyrimCombat::FighterView view(CombatSubject subject)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(subject, v));
            return v;
        }
        [[nodiscard]] bool traced(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        void selfCast(const char* spell)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            mCombat.onPlayerAttackEdge(true);
            updates(3);
            mCombat.onPlayerAttackEdge(false);
            updates(2);
        }
        // The Archer casts Fireburn at the player and the bolt hits: the harm the report says (0 when none).
        float fireburnHit()
        {
            EXPECT_TRUE(mCombat.setTestCasts("Archer:Fireburn"));
            mCombat.setTestCastEvery(1.f);
            updates(4);
            EXPECT_FALSE(mWriter.mClips.empty());
            if (mWriter.mClips.empty())
                return -1.f;
            ClipEvent fire;
            fire.subject = mWriter.mClips.back().mSubject;
            fire.slot = ClipSlot::Cast;
            fire.token = mWriter.mClips.back().mToken;
            fire.kind = ClipEventKind::SpellFire;
            mCombat.onClipEvent(fire);
            update();
            EXPECT_FALSE(mWriter.mLaunches.empty());
            if (mWriter.mLaunches.empty())
                return -1.f;
            mWriter.mDamage.clear();
            ProjectileResult r;
            r.owner = fire.subject;
            r.token = mWriter.mLaunches.back();
            r.fate = ProjectileFate::HitPlayer;
            mCombat.onProjectileResult(r);
            update();
            for (const DamageReport& d : mWriter.mDamage)
                if (d.victimIsPlayer && d.amount > 0.f)
                    return d.amount;
            return 0.f;
        }
    };

    TEST_F(RaceFxCombat, berserkerRageHoldsItsPerkForSixtySecondsAndThenLetsItGo)
    {
        start();
        EXPECT_FALSE(mCombat.holdsPerk(CombatSubject::ofPlayer(), kPerkBerserk));
        selfCast("powerorcberserk");
        EXPECT_TRUE(mCombat.holdsPerk(CombatSubject::ofPlayer(), kPerkBerserk));
        EXPECT_TRUE(traced("COMBAT effect berserk on player for 60.000 s"));
        updates(230); // 57.5 s more: the cast took about 1.25 s
        EXPECT_TRUE(mCombat.holdsPerk(CombatSubject::ofPlayer(), kPerkBerserk));
        updates(30);
        EXPECT_FALSE(mCombat.holdsPerk(CombatSubject::ofPlayer(), kPerkBerserk)) << "ended with its effect";
    }

    TEST_F(RaceFxCombat, theVoiceOfTheEmperorCalmsTheOthersWithinItsAreaAndEndsAfterSixtySeconds)
    {
        start(500.0); // 500 units is inside 75 feet (1,600 units)
        EXPECT_FALSE(mCombat.calmed(CombatSubject::of({ 10, 1 })));
        selfCast("powerimperialpacify");
        EXPECT_TRUE(mCombat.calmed(CombatSubject::of({ 10, 1 })));
        EXPECT_FALSE(mCombat.calmed(CombatSubject::ofPlayer())) << "the caster is not calmed";
        EXPECT_TRUE(traced("COMBAT effect calm on actor 10 for 60.000 s"));
        updates(240);
        EXPECT_FALSE(mCombat.calmed(CombatSubject::of({ 10, 1 })));
    }

    TEST_F(RaceFxCombat, theVoiceOfTheEmperorMissesWhatStandsBeyondItsArea)
    {
        start(5000.0);
        selfCast("powerimperialpacify");
        EXPECT_FALSE(mCombat.calmed(CombatSubject::of({ 10, 1 })));
    }

    TEST_F(RaceFxCombat, calmIsResistedByAHigherLevelThanItsMagnitude)
    {
        mData.mLib.mSpells[kCalm].mEffects[0].mMagnitude = 2.f; // the Archer is level 3
        start(500.0);
        selfCast("powerimperialpacify");
        EXPECT_FALSE(mCombat.calmed(CombatSubject::of({ 10, 1 })));
        EXPECT_TRUE(traced("COMBAT effect calm on actor 10 resisted"));
    }

    TEST_F(RaceFxCombat, commandAnimalPutsTheOthersWithinItsAreaOnThePlayersSide)
    {
        start(500.0);
        selfCast("powerwoodelfcommandanimal");
        EXPECT_TRUE(mCombat.commanded(CombatSubject::of({ 10, 1 })));
        EXPECT_TRUE(traced("COMBAT effect command animal on actor 10 for 60.000 s"));
        updates(240);
        EXPECT_FALSE(mCombat.commanded(CombatSubject::of({ 10, 1 })));
    }

    TEST_F(RaceFxCombat, nightEyeIsAStatusForItsDurationAndSaysSo)
    {
        start();
        EXPECT_FALSE(mCombat.playerNightEye());
        selfCast("powerkhajiitnighteye");
        EXPECT_TRUE(mCombat.playerNightEye());
        EXPECT_TRUE(traced("a status only"));
        updates(240);
        EXPECT_FALSE(mCombat.playerNightEye());
    }

    TEST_F(RaceFxCombat, theCloakHurtsTheOthersInRangeEightASecondWhateverTheStepSize)
    {
        start(100.0); // inside 10 feet = 213 units
        selfCast("powerdarkelfflamecloak");
        const float before = view(CombatSubject::of({ 10, 1 })).mHealth;
        updates(8, 0.25f); // 2 s
        const float coarse = before - view(CombatSubject::of({ 10, 1 })).mHealth;
        EXPECT_NEAR(coarse, 16.f, 0.5f) << "8 a second for 2 s";
    }

    TEST_F(RaceFxCombat, theCloakDamageDoesNotDependOnTheStepSize)
    {
        start(100.0);
        selfCast("powerdarkelfflamecloak");
        const float a0 = view(CombatSubject::of({ 10, 1 })).mHealth;
        updates(16, 0.125f); // 2 s in small steps
        const float fine = a0 - view(CombatSubject::of({ 10, 1 })).mHealth;
        EXPECT_NEAR(fine, 16.f, 0.5f);
    }

    TEST_F(RaceFxCombat, theCloakDoesNotReachWhatStandsOutsideItsRadius)
    {
        start(400.0); // 10 feet is 213 units
        selfCast("powerdarkelfflamecloak");
        const float before = view(CombatSubject::of({ 10, 1 })).mHealth;
        updates(8);
        EXPECT_FLOAT_EQ(view(CombatSubject::of({ 10, 1 })).mHealth, before);
    }

    TEST_F(RaceFxCombat, aSpellThatLandsOnADragonskinPlayerIsAbsorbedAndANoneBretonTakesIt)
    {
        start();
        const float plain = fireburnHit();
        EXPECT_GT(plain, 0.f) << "no Dragonskin: the bolt hurts";
        EXPECT_FALSE(traced("COMBAT spell absorb")) << "no roll is drawn without a chance";
    }

    TEST_F(RaceFxCombat, dragonskinAtAHundredPercentAbsorbsEveryBoltAndGivesTheMagickaBack)
    {
        start();
        selfCast("powerbretonabsorbspell");
        mTrace.clear();
        EXPECT_FLOAT_EQ(fireburnHit(), 0.f);
        EXPECT_TRUE(traced("COMBAT spell absorb player chance 100.000"));
        EXPECT_TRUE(traced("absorbed"));
    }

    TEST_F(RaceFxCombat, theAbsorbRollsAreTheSameInTwoRuns)
    {
        std::vector<std::string> first;
        {
            mData.mLib.mSpells[kAbsorbPower].mEffects[0].mMagnitude = 50.f;
            start();
            selfCast("powerbretonabsorbspell");
            mTrace.clear();
            (void)fireburnHit();
            for (const std::string& line : mTrace)
                if (line.find("spell absorb") != std::string::npos)
                    first.push_back(line);
        }
        FxData data2;
        data2.mLib.mSpells[kAbsorbPower].mEffects[0].mMagnitude = 50.f;
        SkyrimCombat combat2;
        FxWriter writer2;
        std::vector<std::string> second;
        combat2.setTrace([&](const std::string& line) {
            if (line.find("spell absorb") != std::string::npos)
                second.push_back(line);
        });
        combat2.setData(&data2);
        combat2.onActorAdded(infoOf(10, kArcherBase, "Archer"));
        SubjectState s;
        s.x = 100.0;
        writer2.mStates[10] = s;
        writer2.mPlayerState.healthMax = 100.f;
        writer2.mPlayerState.health = 100.f;
        combat2.combatUpdate(0.25f, writer2);
        ASSERT_TRUE(combat2.setTestPlayerSpell("powerbretonabsorbspell"));
        combat2.onPlayerAttackEdge(true);
        for (int i = 0; i < 3; ++i)
            combat2.combatUpdate(0.25f, writer2);
        combat2.onPlayerAttackEdge(false);
        for (int i = 0; i < 2; ++i)
            combat2.combatUpdate(0.25f, writer2);
        ASSERT_TRUE(combat2.setTestCasts("Archer:Fireburn"));
        combat2.setTestCastEvery(1.f);
        for (int i = 0; i < 4; ++i)
            combat2.combatUpdate(0.25f, writer2);
        ASSERT_FALSE(writer2.mClips.empty());
        ClipEvent fire;
        fire.subject = writer2.mClips.back().mSubject;
        fire.slot = ClipSlot::Cast;
        fire.token = writer2.mClips.back().mToken;
        fire.kind = ClipEventKind::SpellFire;
        combat2.onClipEvent(fire);
        combat2.combatUpdate(0.25f, writer2);
        ASSERT_FALSE(writer2.mLaunches.empty());
        ProjectileResult r;
        r.owner = fire.subject;
        r.token = writer2.mLaunches.back();
        r.fate = ProjectileFate::HitPlayer;
        combat2.onProjectileResult(r);
        combat2.combatUpdate(0.25f, writer2);
        ASSERT_EQ(first.size(), 1u) << "one roll for one spell that lands";
        ASSERT_EQ(second.size(), 1u);
        EXPECT_EQ(first[0], second[0]);
    }

    TEST_F(RaceFxCombat, aBlessingGivesSkillPointsAPercentModifierAndAShoutTimer)
    {
        start();
        mCombat.setPlayerExtraAbilities({ kDibella, kZenithar, kTalos });
        update();
        const SkyrimCombat::SkillModifiers mods = mCombat.playerSkillModifiers();
        EXPECT_FLOAT_EQ(mods.mPoints[11], 10.f);
        EXPECT_FLOAT_EQ(mods.mPower[11], 10.f);
        EXPECT_FLOAT_EQ(mCombat.playerShoutRecoveryScale(), 0.8f);
        const std::uint64_t revision = mCombat.skillModRevision();
        mCombat.setPlayerExtraAbilities({});
        update();
        EXPECT_NE(mCombat.skillModRevision(), revision) << "the revision moves when the blessing goes";
        EXPECT_FLOAT_EQ(mCombat.playerSkillModifiers().mPoints[11], 0.f);
        EXPECT_FLOAT_EQ(mCombat.playerShoutRecoveryScale(), 1.f);
    }

    TEST_F(RaceFxCombat, aRestorationBlessingMakesAHealingSpellStrongerByItsPercent)
    {
        start();
        mCombat.setPlayerExtraAbilities({ kMara });
        update();
        EXPECT_FLOAT_EQ(mCombat.playerSkillModifiers().mPower[16], 10.f);
    }

    TEST_F(RaceFxCombat, aDiseasesHealthMaximumLowersTheMaximumAndGoesBackWhenItIsCured)
    {
        start();
        EXPECT_FLOAT_EQ(view(CombatSubject::ofPlayer()).mHealthMax, 100.f);
        PlayerValueModifiers sick;
        sick.mHealthMax = -25.f;
        mCombat.setPlayerValueModifiers(sick);
        EXPECT_FLOAT_EQ(view(CombatSubject::ofPlayer()).mHealthMax, 75.f);
        mCombat.setPlayerValueModifiers(sick); // the same again changes nothing
        EXPECT_FLOAT_EQ(view(CombatSubject::ofPlayer()).mHealthMax, 75.f);
        update(); // the engine's health follows by the zero-damage report
        mCombat.setPlayerValueModifiers(PlayerValueModifiers());
        EXPECT_FLOAT_EQ(view(CombatSubject::ofPlayer()).mHealthMax, 100.f);
    }

    TEST_F(RaceFxCombat, theCarryWeightAddsASteedsHundredAndABlightsMinusTen)
    {
        start();
        mCombat.setPlayerExtraAbilities({ kSteed });
        update();
        EXPECT_FLOAT_EQ(mCombat.playerCarryBonus(), 100.f);
        PlayerValueModifiers blight;
        blight.mCarry = -10.f;
        const std::uint64_t revision = mCombat.abilityRevision();
        mCombat.setPlayerValueModifiers(blight);
        EXPECT_FLOAT_EQ(mCombat.playerCarryBonus(), 90.f);
        EXPECT_NE(mCombat.abilityRevision(), revision);
    }

    TEST_F(RaceFxCombat, aDiseasesSkillPercentReachesTheSkillModifiers)
    {
        start();
        PlayerValueModifiers ataxia;
        ataxia.mSkillPercent[8] = -25.f; // lockpicking
        mCombat.setPlayerValueModifiers(ataxia);
        EXPECT_FLOAT_EQ(mCombat.playerSkillModifiers().mPower[8], -25.f);
    }
}
