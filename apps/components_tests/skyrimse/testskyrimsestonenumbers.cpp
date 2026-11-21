// Task 0241: the numbers of the standing stones' abilities and the shrine blessings (maximum vitals, carry weight,
// resistances and weaknesses, regeneration rates), the stones' powers, Actor.AddPerk in the save, and the blessing
// clock. Groups (every expected number is derived by hand from the lists below; the shapes are the data's, VERIFIED by
// tools/skyrimse_stones_survey/stones.py):
//  - StoneNumbersPure: sumAbilityNumbers and the resistance and weakness rules on a fake library;
//  - StoneNumbersCombat: the combat runtime (maximums, current values, the engine's health report, rates, hits,
//    carry weight, perks, the invisibility power);
//  - StoneNumbersPowers: the character service lists the stones' powers beside the race's and casts them;
//  - StoneNumbersInventory: the carry weight bonus;
//  - StoneNumbersHost: Actor.AddPerk in the Papyrus host's save state;
//  - StoneNumbersClock: a blessing follows the game clock a wait or a sleep moves.
#include <game_skyrimse/ability_numbers.hpp>
#include <game_skyrimse/character_service.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/inventory_service.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/magic_status.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/stones.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    // ---- the records: the shapes of the real stones and blessings
    // ----------------------------------------------------

    constexpr FormId kAtronach = 0x5001, kLady = 0x5002, kSteed = 0x5003, kLord = 0x5004, kApprentice = 0x5005,
                     kApprenticeNeg = 0x5006, kRates = 0x5007, kNanSpell = 0x5008, kHugeSpell = 0x5009;
    constexpr FormId kBlessMagicka = 0x5010, kBlessHealth = 0x5011, kBlessStamina = 0x5012, kBlessRate = 0x5013,
                     kBlessSkill = 0x5014;
    constexpr FormId kHealing = 0x5020, kHostilePeak = 0x5021, kFortifyValueMod = 0x5022;
    constexpr FormId kFireburn = 0x5030, kShadow = 0x5031, kSerpent = 0x5032, kRitual = 0x5033, kBoost = 0x5034,
                     kSpend = 0x5035;
    constexpr FormId kEffAbsorb = 0x6001, kEffMagickaRateDown = 0x6002, kEffFortifyMagicka = 0x6003,
                     kEffHealthRate = 0x6004, kEffStaminaRate = 0x6005, kEffCarry = 0x6006, kEffDamageResist = 0x6007,
                     kEffMagicResist = 0x6008, kEffMagickaRateUp = 0x6009, kEffWeakMagic = 0x600A, kEffRateH = 0x600B,
                     kEffRateM = 0x600C, kEffHealing = 0x600D, kEffHostilePeak = 0x600E, kEffFortifyHealth = 0x600F,
                     kEffFortifyStamina = 0x6010, kEffSkill = 0x6011, kEffFire = 0x6012, kEffInvis = 0x6013,
                     kEffParalyse = 0x6014, kEffReanimate = 0x6015, kEffBoost = 0x6016, kEffSpend = 0x6017,
                     kEffFortifyValueMod = 0x6018;
    constexpr FormId kProjectile = 0x7000;
    constexpr WorldId kArcherBase = 200;

    constexpr std::uint32_t kPeak = MagicArchetype::PeakValueModifier, kMod = MagicArchetype::ValueModifier;
    constexpr std::uint32_t kPlain = 0x200802; // Peak, Recover (a Fortify)
    constexpr std::uint32_t kDetrimental = 0x200806; // Recover + Detrimental (a Damage ... Rate ability)
    constexpr std::uint32_t kInvisibilityArchetype = 11;

    class AbLibrary final : public PerkLibrary
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

        void effect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            mEffects[id] = m;
        }
        struct E
        {
            FormId mEffect;
            float mMagnitude;
            std::uint32_t mDuration;
        };
        void spell(FormId id, const char* name, std::uint32_t type, std::uint32_t castType, std::vector<E> effects,
            std::uint32_t delivery = MagicDelivery::Self)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mType = type;
            s.mCastType = castType;
            s.mDelivery = delivery;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                se.mDuration = e.mDuration;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    constexpr std::uint32_t kAbility = SpellRecord::kTypeAbility;

    AbLibrary makeLibrary()
    {
        AbLibrary lib;
        lib.effect(kEffAbsorb, kPeak, 83, 0x200002);
        lib.effect(kEffMagickaRateDown, kMod, 156, 0x208806); // AbDamageMagickaRate: Detrimental
        lib.effect(kEffFortifyMagicka, kPeak, 25, 0x200a02);
        lib.effect(kEffHealthRate, kPeak, 155, kPlain);
        lib.effect(kEffStaminaRate, kPeak, 157, kPlain);
        lib.effect(kEffCarry, kPeak, 32, kPlain);
        lib.effect(kEffDamageResist, kMod, 39, 0x200002);
        lib.effect(kEffMagicResist, kMod, 44, 0x200002);
        lib.effect(kEffMagickaRateUp, kPeak, 156, kPlain);
        lib.effect(kEffWeakMagic, kMod, 44, 0x200206); // AbWeaknessMagicConstant: Detrimental
        lib.effect(kEffRateH, kPeak, 27, kPlain);
        lib.effect(kEffRateM, kPeak, 28, kPlain);
        lib.effect(kEffHealing, kMod, 24, 0x2);
        lib.effect(kEffHostilePeak, kPeak, 24, 0x7);
        lib.effect(kEffFortifyHealth, kPeak, 24, 0x200902);
        lib.effect(kEffFortifyStamina, kPeak, 26, 0x200902);
        lib.effect(kEffSkill, kPeak, 107, 0x200902); // Fortify Speechcraft
        lib.effect(kEffFortifyValueMod, kMod, 25, 0x200902);
        lib.effect(kEffFire, kMod, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
        lib.mEffects[kEffFire].mResistValue = 41;
        lib.effect(kEffInvis, kInvisibilityArchetype, 54, 0x401c02);
        lib.effect(kEffParalyse, StatusArchetype::Paralysis, 53, 0x401403);
        lib.effect(kEffReanimate, 22, -1, 0x401102);
        lib.effect(kEffBoost, kPeak, 28, kPlain);
        lib.effect(kEffSpend, kPeak, 83, kPlain);

        lib.spell(kAtronach, "doomAtronachAbility", kAbility, 0,
            { { kEffAbsorb, 50.f, 0 }, { kEffMagickaRateDown, 50.f, 0 }, { kEffFortifyMagicka, 50.f, 0 } });
        lib.spell(kLady, "doomLadyAbility", kAbility, 0, { { kEffHealthRate, 25.f, 0 }, { kEffStaminaRate, 25.f, 0 } });
        lib.spell(kSteed, "doomSteedAbility", kAbility, 0, { { kEffCarry, 100.f, 0 } });
        lib.spell(
            kLord, "doomLordAbility", kAbility, 0, { { kEffDamageResist, 50.f, 0 }, { kEffMagicResist, 25.f, 0 } });
        lib.spell(kApprentice, "doomApprenticeAbility", kAbility, 0, { { kEffMagickaRateUp, 100.f, 0 } });
        lib.spell(kApprenticeNeg, "doomApprenticeNegativeAbility", kAbility, 0, { { kEffWeakMagic, 100.f, 0 } });
        lib.spell(kRates, "RateAbility", kAbility, 0, { { kEffRateH, 10.f, 0 }, { kEffRateM, 25.f, 0 } });
        lib.spell(kNanSpell, "NanAbility", kAbility, 0, { { kEffCarry, std::numeric_limits<float>::quiet_NaN(), 0 } });
        lib.spell(kHugeSpell, "HugeAbility", kAbility, 0, { { kEffCarry, 1.0e9f, 0 }, { kEffCarry, 1.0e9f, 0 } });
        // the blessings: fire and forget, 28,800 s, a Fortify (Peak) and a cure
        lib.spell(
            kBlessMagicka, "AltarJulianosSpell", 0, MagicCast::FireAndForget, { { kEffFortifyMagicka, 25.f, 28800 } });
        lib.spell(kBlessHealth, "AltarArkaySpell", 0, MagicCast::FireAndForget, { { kEffFortifyHealth, 25.f, 28800 } });
        lib.spell(
            kBlessStamina, "AltarKynarethSpell", 0, MagicCast::FireAndForget, { { kEffFortifyStamina, 25.f, 28800 } });
        lib.spell(kBlessRate, "AltarAkatoshSpell", 0, MagicCast::FireAndForget, { { kEffMagickaRateUp, 10.f, 28800 } });
        lib.spell(kBlessSkill, "AltarZenitharSpell", 0, MagicCast::FireAndForget, { { kEffSkill, 10.f, 28800 } });
        // what must NOT count: a known instant spell, a hostile Peak with a duration, a ValueModifier with a duration
        lib.spell(kHealing, "Healing", 0, MagicCast::FireAndForget, { { kEffHealing, 10.f, 0 } });
        lib.spell(kHostilePeak, "Drain", 0, MagicCast::FireAndForget, { { kEffHostilePeak, 20.f, 60 } });
        lib.spell(kFortifyValueMod, "TimedMod", 0, MagicCast::FireAndForget, { { kEffFortifyValueMod, 30.f, 60 } });
        return lib;
    }

    float vitalOf(const AbilityNumbers& n, int i)
    {
        return n.mVital[i];
    }

    // ---- StoneNumbersPure -------------------------------------------------------------------------------------------

    TEST(StoneNumbersPure, theAtronachSignIsMagickaPlusFiftyHalfTheMagickaRateAndAbsorbFifty)
    {
        const AbLibrary lib = makeLibrary();
        const FormId list[] = { kAtronach };
        const AbilityNumbers n = sumAbilityNumbers(lib, list);
        EXPECT_FLOAT_EQ(vitalOf(n, 1), 50.f);
        EXPECT_FLOAT_EQ(n.mRegenMult[1], -50.f) << "the Detrimental flag makes the 50 negative";
        EXPECT_FLOAT_EQ(n.mAbsorb, 50.f);
        EXPECT_FLOAT_EQ(vitalOf(n, 0), 0.f);
        EXPECT_FLOAT_EQ(vitalOf(n, 2), 0.f);
        EXPECT_TRUE(n.any());
        EXPECT_TRUE(n.hasRegen());
    }

    TEST(StoneNumbersPure, theLadyRegeneratesHealthAndStaminaByAQuarterMore)
    {
        const AbLibrary lib = makeLibrary();
        const FormId list[] = { kLady };
        const AbilityNumbers n = sumAbilityNumbers(lib, list);
        EXPECT_FLOAT_EQ(n.mRegenMult[0], 25.f);
        EXPECT_FLOAT_EQ(n.mRegenMult[2], 25.f);
        EXPECT_FLOAT_EQ(n.mRegenMult[1], 0.f);
        EXPECT_FLOAT_EQ(n.mRegenRate[0], 0.f);
    }

    TEST(StoneNumbersPure, theSteedCarriesAHundredMoreAndTheLordResistsMagicButHisDamageResistIsNotHere)
    {
        const AbLibrary lib = makeLibrary();
        const FormId steed[] = { kSteed };
        EXPECT_FLOAT_EQ(sumAbilityNumbers(lib, steed).mCarry, 100.f);
        const FormId lord[] = { kLord };
        const AbilityNumbers n = sumAbilityNumbers(lib, lord);
        EXPECT_FLOAT_EQ(n.mResist[4], 25.f); // actor value 44
        for (const int i : { 0, 1, 2, 3, 5 })
            EXPECT_FLOAT_EQ(n.mResist[i], 0.f) << i;
        EXPECT_FLOAT_EQ(n.mCarry, 0.f); // actor value 39 is the existing damage resistance path
    }

    TEST(StoneNumbersPure, theApprenticeDoublesMagickaRegenerationAndIsWeakToMagic)
    {
        const AbLibrary lib = makeLibrary();
        const FormId both[] = { kApprentice, kApprenticeNeg };
        const AbilityNumbers n = sumAbilityNumbers(lib, both);
        EXPECT_FLOAT_EQ(n.mRegenMult[1], 100.f);
        EXPECT_FLOAT_EQ(n.mResist[4], -100.f);
    }

    TEST(StoneNumbersPure, theRateValuesAddToTheRateNotToThePercent)
    {
        const AbLibrary lib = makeLibrary();
        const FormId list[] = { kRates };
        const AbilityNumbers n = sumAbilityNumbers(lib, list);
        EXPECT_FLOAT_EQ(n.mRegenRate[0], 10.f);
        EXPECT_FLOAT_EQ(n.mRegenRate[1], 25.f);
        EXPECT_FLOAT_EQ(n.mRegenMult[0], 0.f);
    }

    TEST(StoneNumbersPure, aBlessingCountsThroughItsPeakFortifyEffectsAndNothingElse)
    {
        const AbLibrary lib = makeLibrary();
        const FormId a[] = { kBlessMagicka };
        EXPECT_FLOAT_EQ(vitalOf(sumAbilityNumbers(lib, a), 1), 25.f);
        const FormId b[] = { kBlessHealth };
        EXPECT_FLOAT_EQ(vitalOf(sumAbilityNumbers(lib, b), 0), 25.f);
        const FormId c[] = { kBlessStamina };
        EXPECT_FLOAT_EQ(vitalOf(sumAbilityNumbers(lib, c), 2), 25.f);
        const FormId d[] = { kBlessRate };
        EXPECT_FLOAT_EQ(sumAbilityNumbers(lib, d).mRegenMult[1], 10.f);
        const FormId e[] = { kBlessSkill }; // Fortify Speechcraft: a skill, not one of these numbers
        EXPECT_FALSE(sumAbilityNumbers(lib, e).any());
    }

    TEST(StoneNumbersPure, aKnownSpellOfNoAbilityCountsForNothing)
    {
        const AbLibrary lib = makeLibrary();
        const FormId none[] = { kHealing, kHostilePeak, kFortifyValueMod, 0x424242 };
        const AbilityNumbers n = sumAbilityNumbers(lib, none);
        EXPECT_FALSE(n.any()) << "Healing is instant, a hostile Peak harms, a timed ValueModifier is not a Fortify";
        EXPECT_FALSE(n.hasRegen());
    }

    TEST(StoneNumbersPure, twoSpellsAddAndTheOrderOfTheListDoesNotMatter)
    {
        const AbLibrary lib = makeLibrary();
        const FormId one[] = { kAtronach, kBlessMagicka, kLady };
        const FormId two[] = { kLady, kAtronach, kBlessMagicka };
        const AbilityNumbers a = sumAbilityNumbers(lib, one);
        const AbilityNumbers b = sumAbilityNumbers(lib, two);
        EXPECT_FLOAT_EQ(vitalOf(a, 1), 75.f); // 50 + 25
        EXPECT_TRUE(a == b);
    }

    TEST(StoneNumbersPure, aNumberThatIsNotFiniteCountsAsZeroAndASumIsCut)
    {
        const AbLibrary lib = makeLibrary();
        const FormId nan[] = { kNanSpell };
        EXPECT_FLOAT_EQ(sumAbilityNumbers(lib, nan).mCarry, 0.f);
        const FormId huge[] = { kHugeSpell };
        EXPECT_FLOAT_EQ(sumAbilityNumbers(lib, huge).mCarry, kMaxAbilityNumber);
    }

    TEST(StoneNumbersPure, aResistanceCombinesTheElementAndMagicAsTwoShares)
    {
        AbilityNumbers n;
        n.mResist[1] = 50.f; // fire
        n.mResist[4] = 25.f; // magic
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 41), 62.5f); // 1 - 0.5 x 0.75
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 44), 25.f) << "magic is not counted twice";
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 43), 25.f) << "frost has none of its own: magic only";
        EXPECT_FLOAT_EQ(abilityResistPercent(n, -1), 25.f) << "no resist value: magic only";
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 40), 0.f) << "poison ignores magic";
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 45), 0.f) << "disease ignores magic";
        n.mResist[1] = 90.f;
        n.mResist[4] = 90.f;
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 41), kMaxResistPercent) << "cut at the game's cap";
    }

    TEST(StoneNumbersPure, aNegativeNetIsAWeaknessAndNotAResistance)
    {
        AbilityNumbers n;
        n.mResist[4] = -100.f;
        EXPECT_FLOAT_EQ(abilityResistPercent(n, 41), 0.f);
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(n, 41), 100.f);
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(n, 44), 100.f);
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(n, 40), 0.f) << "poison is not magic";
        n.mResist[1] = -30.f;
        n.mResist[4] = -20.f;
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(n, 41), 50.f);
        n.mResist[1] = -80.f;
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(n, 41), kMaxWeaknessPercent) << "cut at 100";
        EXPECT_FLOAT_EQ(abilityWeaknessPercent(AbilityNumbers(), 41), 0.f);
    }

    // ---- StoneNumbersCombat -----------------------------------------------------------------------------------------

    class NumData final : public CombatData
    {
    public:
        NumData()
            : mLib(makeLibrary())
        {
            mLib.spell(
                kFireburn, "Fireburn", 0, MagicCast::FireAndForget, { { kEffFire, 40.f, 0 } }, MagicDelivery::Aimed);
            mLib.mSpells[kFireburn].mCost = 5;
            mLib.mEffects[kEffFire].mProjectile = kProjectile;
            mLib.spell(kShadow, "doomShadowAbility", 2, MagicCast::FireAndForget, { { kEffInvis, 0.f, 60 } });
            mLib.spell(kSerpent, "doomSerpentAbility", 2, MagicCast::FireAndForget, { { kEffParalyse, 0.f, 5 } });
            mLib.spell(kRitual, "doomRitualAbility", 2, MagicCast::FireAndForget, { { kEffReanimate, 75.f, 200 } });
            mLib.spell(kBoost, "Boost", 0, MagicCast::FireAndForget, { { kEffBoost, 25.f, 60 } });
            mLib.mSpells[kBoost].mCost = 10;
            mLib.spell(kSpend, "Spend", 0, MagicCast::FireAndForget, { { kEffSpend, 1.f, 60 } });
            mLib.mSpells[kSpend].mCost = 60;
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "TestBoltProjectile";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\TestBolt.nif";
            FighterProfile npc;
            npc.mHealthMax = 50.f;
            npc.mStaminaMax = 50.f;
            npc.mMagickaMax = 100.f;
            npc.mMagickaRegenPercent = 3.f;
            npc.mLevel = 3;
            npc.mAi.mKnown = true;
            mProfiles[kArcherBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mHealthRegenPercent = 0.7f;
            mPlayer.mMagickaRegenPercent = 3.f;
            mPlayer.mStaminaRegenPercent = 5.f;
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

        AbLibrary mLib;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class NumWriter final : public CombatRequestWriter
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

    struct StoneNumbersCombat : ::testing::Test
    {
        NumData mData;
        SkyrimCombat mCombat;
        NumWriter mWriter;
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start()
        {
            mCombat.setData(&mData);
            mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
            SubjectState s;
            s.x = 100.0;
            mWriter.mStates[10] = s;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
            update(); // the profiles are made on the first update
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n)
        {
            for (int i = 0; i < n; ++i)
                update();
        }
        void give(std::vector<FormId> spells) { mCombat.setPlayerExtraAbilities(std::move(spells)); }
        SkyrimCombat::FighterView player()
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
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

    TEST_F(StoneNumbersCombat, theAtronachSignRaisesTheMagickaMaximumAndTheCurrentValueAndItsRemovalLowersBoth)
    {
        start();
        EXPECT_FLOAT_EQ(player().mMagickaMax, 100.f);
        give({ kAtronach });
        EXPECT_FLOAT_EQ(player().mMagickaMax, 150.f);
        EXPECT_FLOAT_EQ(player().mMagicka, 150.f);
        EXPECT_FLOAT_EQ(mCombat.playerAbilityNumbers().mAbsorb, 50.f);
        give({});
        EXPECT_FLOAT_EQ(player().mMagickaMax, 100.f);
        EXPECT_FLOAT_EQ(player().mMagicka, 100.f);
        EXPECT_FALSE(mCombat.playerAbilityNumbers().any());
    }

    TEST_F(StoneNumbersCombat, theSameListAgainChangesNothingAndTheRevisionMovesOnlyWithTheNumbers)
    {
        start();
        const std::uint64_t r0 = mCombat.abilityRevision();
        give({ kAtronach });
        const std::uint64_t r1 = mCombat.abilityRevision();
        EXPECT_GT(r1, r0);
        give({ kAtronach });
        EXPECT_EQ(mCombat.abilityRevision(), r1);
        EXPECT_FLOAT_EQ(player().mMagickaMax, 150.f) << "not counted twice";
        give({ kAtronach, kHealing }); // a known spell that counts for nothing
        EXPECT_EQ(mCombat.abilityRevision(), r1);
        EXPECT_FLOAT_EQ(player().mMagickaMax, 150.f);
    }

    TEST_F(StoneNumbersCombat, aHealthBlessingRaisesTheEnginesMaximumThroughAZeroDamageReportAndEndingLowersIt)
    {
        start();
        give({ kBlessHealth });
        EXPECT_FLOAT_EQ(player().mHealthMax, 125.f);
        mWriter.mDamage.clear();
        update();
        const auto gain = std::find_if(mWriter.mDamage.begin(), mWriter.mDamage.end(),
            [](const DamageReport& r) { return r.victimIsPlayer && r.amount == 0.f && r.healthMax == 125.f; });
        ASSERT_NE(gain, mWriter.mDamage.end());
        EXPECT_FLOAT_EQ(gain->healthAfter, 125.f); // 100 + 25
        give({});
        EXPECT_FLOAT_EQ(player().mHealthMax, 100.f);
        mWriter.mDamage.clear();
        update();
        const auto loss = std::find_if(mWriter.mDamage.begin(), mWriter.mDamage.end(),
            [](const DamageReport& r) { return r.victimIsPlayer && r.amount == 0.f && r.healthMax == 100.f; });
        ASSERT_NE(loss, mWriter.mDamage.end());
        EXPECT_FLOAT_EQ(loss->healthAfter, 75.f); // the engine still said 100: 100 - 25
    }

    TEST_F(StoneNumbersCombat, aStaminaBlessingRaisesTheStaminaMaximum)
    {
        start();
        give({ kBlessStamina });
        EXPECT_FLOAT_EQ(player().mStaminaMax, 125.f);
        EXPECT_FLOAT_EQ(player().mStamina, 125.f);
    }

    TEST_F(StoneNumbersCombat, theLadyRaisesTheHealthAndStaminaRatesAndNotTheMagickaRate)
    {
        start();
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 0), 0.7f);
        give({ kLady });
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 0), 0.7f * 1.25f);
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 2), 5.f * 1.25f);
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 3.f);
        give({});
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 0), 0.7f);
    }

    TEST_F(StoneNumbersCombat, theAtronachHalvesTheMagickaRateAndATimedBoostAddsBeforeTheMultiplier)
    {
        start();
        give({ kAtronach });
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 1.5f); // 3 x 0.5
        selfCast("Boost"); // +25 for 60 s on actor value 28
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 14.f); // (3 + 25) x 0.5
    }

    TEST_F(StoneNumbersCombat, theApprenticeMakesMagickaComeBackFasterThanWithoutTheSign)
    {
        start();
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 3.f);
        give({ kApprentice });
        EXPECT_FLOAT_EQ(mCombat.regenRatePercent(CombatSubject::ofPlayer(), 1), 6.f);
        selfCast("Spend");
        const float spent = player().mMagicka;
        updates(32); // 8 s: 48 back with the sign (6 a second), 24 without (3), neither at the maximum yet
        const float withSign = player().mMagicka - spent;

        NumData data;
        SkyrimCombat control;
        NumWriter writer;
        control.setData(&data);
        writer.mPlayerState.healthMax = 100.f;
        writer.mPlayerState.health = 100.f;
        control.combatUpdate(0.25f, writer);
        ASSERT_TRUE(control.setTestPlayerSpell("Spend"));
        control.onPlayerAttackEdge(true);
        for (int i = 0; i < 3; ++i)
            control.combatUpdate(0.25f, writer);
        control.onPlayerAttackEdge(false);
        for (int i = 0; i < 2; ++i)
            control.combatUpdate(0.25f, writer);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(control.fighterView(CombatSubject::ofPlayer(), v));
        const float controlSpent = v.mMagicka;
        for (int i = 0; i < 32; ++i)
            control.combatUpdate(0.25f, writer);
        ASSERT_TRUE(control.fighterView(CombatSubject::ofPlayer(), v));
        const float without = v.mMagicka - controlSpent;
        EXPECT_GT(without, 0.f);
        EXPECT_GT(withSign, 1.5f * without) << "with " << withSign << " without " << without;
    }

    TEST_F(StoneNumbersCombat, aRestUsesTheAbilityRatesToo)
    {
        start();
        give({ kApprentice });
        selfCast("Spend");
        const float spent = player().mMagicka;
        mCombat.requestRest(5.f); // 5 s at 6 percent of 100 a second (3 x 2): 30 back, 15 without the sign
        update(); // and the 0.25 s step's own regeneration at the same rate: 1.5
        EXPECT_NEAR(player().mMagicka - spent, 31.5f, 0.1f);
    }

    TEST_F(StoneNumbersCombat, aFireHitWithNoSignIsTheFortyTheSpellSays)
    {
        start();
        EXPECT_FLOAT_EQ(fireburnHit(), 40.f);
        EXPECT_FLOAT_EQ(mCombat.playerResistPercent(41), 0.f);
        EXPECT_FLOAT_EQ(mCombat.playerWeaknessPercent(41), 0.f);
    }

    TEST_F(StoneNumbersCombat, theLordTakesAQuarterOffAFireHitAndOnlyMagicIsCounted)
    {
        start();
        give({ kLord });
        EXPECT_FLOAT_EQ(mCombat.playerResistPercent(41), 25.f);
        EXPECT_FLOAT_EQ(mCombat.playerResistPercent(40), 0.f);
        EXPECT_FLOAT_EQ(fireburnHit(), 30.f) << "40 x 0.75";
    }

    TEST_F(StoneNumbersCombat, theApprenticesWeaknessToMagicAddsToTheFireHit)
    {
        start();
        give({ kApprentice, kApprenticeNeg });
        EXPECT_FLOAT_EQ(mCombat.playerWeaknessPercent(41), 100.f);
        EXPECT_FLOAT_EQ(mCombat.playerResistPercent(41), 0.f);
        const float hit = fireburnHit();
        EXPECT_FLOAT_EQ(hit, 80.f) << "40 x (1 + 100 / 100)";
    }

    TEST_F(StoneNumbersCombat, theSteedsCarryWeightIsExposedAndGoneWithTheSign)
    {
        start();
        EXPECT_FLOAT_EQ(mCombat.playerAbilityNumbers().mCarry, 0.f);
        give({ kSteed });
        EXPECT_FLOAT_EQ(mCombat.playerAbilityNumbers().mCarry, 100.f);
        give({});
        EXPECT_FLOAT_EQ(mCombat.playerAbilityNumbers().mCarry, 0.f);
    }

    TEST_F(StoneNumbersCombat, aKnownSpellOfNoAbilityChangesNoNumberOfThePlayer)
    {
        start();
        give({ kHealing, kHostilePeak, kFortifyValueMod });
        EXPECT_FLOAT_EQ(player().mHealthMax, 100.f);
        EXPECT_FLOAT_EQ(player().mMagickaMax, 100.f);
        EXPECT_FALSE(mCombat.playerAbilityNumbers().any());
    }

    TEST_F(StoneNumbersCombat, aListGivenBeforeTheProfileExistsIsAppliedWhenItIs)
    {
        mCombat.setData(&mData);
        mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
        SubjectState s;
        s.x = 100.0;
        mWriter.mStates[10] = s;
        mWriter.mPlayerState.healthMax = 100.f;
        mWriter.mPlayerState.health = 100.f;
        give({ kAtronach });
        update();
        EXPECT_FLOAT_EQ(player().mMagickaMax, 150.f);
    }

    TEST_F(StoneNumbersCombat, extraPerksReachThePlayersPerkListAndAreTraced)
    {
        start();
        mCombat.setPlayerExtraPerks({ 0x777, 0x778 });
        EXPECT_TRUE(traced("COMBAT player extra perks: 2"));
        EXPECT_TRUE(traced("COMBAT perks player: 2 perks"));
        mTrace.clear();
        mCombat.setPlayerExtraPerks({ 0x777, 0x778 }); // the same list again: nothing new
        EXPECT_FALSE(traced("COMBAT player extra perks"));
    }

    TEST_F(StoneNumbersCombat, theShadowPowerMakesThePlayerInvisibleForSixtySecondsAndTheSupportClassesAreRight)
    {
        start();
        EXPECT_FALSE(mCombat.playerInvisible());
        const std::vector<SkyrimCombat::RacePower> powers = mCombat.racePowers({ kShadow, kSerpent, kRitual });
        ASSERT_EQ(powers.size(), 3u);
        EXPECT_EQ(powers[0].mSupport, SkyrimCombat::PowerSupport::Works) << "invisibility";
        EXPECT_EQ(powers[1].mSupport, SkyrimCombat::PowerSupport::Works) << "paralysis";
        EXPECT_EQ(powers[2].mSupport, SkyrimCombat::PowerSupport::Unsupported) << "reanimate";
        ASSERT_TRUE(mCombat.castPlayerPower(kShadow));
        updates(8);
        EXPECT_TRUE(mCombat.playerInvisible());
        updates(200); // 50 s more: about 52 s in all
        EXPECT_TRUE(mCombat.playerInvisible());
        updates(40); // 62 s
        EXPECT_FALSE(mCombat.playerInvisible());
    }

    TEST_F(StoneNumbersCombat, aRunOfTheSameSequenceGivesTheSameTrace)
    {
        const auto once = [] {
            std::vector<std::string> trace;
            NumData data;
            SkyrimCombat combat;
            NumWriter writer;
            combat.setTrace([&](const std::string& line) { trace.push_back(line); });
            combat.setData(&data);
            writer.mPlayerState.healthMax = 100.f;
            writer.mPlayerState.health = 80.f;
            combat.combatUpdate(0.25f, writer);
            combat.setPlayerExtraAbilities({ kAtronach, kLady, kBlessHealth });
            for (int i = 0; i < 20; ++i)
                combat.combatUpdate(0.25f, writer);
            combat.setPlayerExtraAbilities({ kLady });
            for (int i = 0; i < 20; ++i)
                combat.combatUpdate(0.25f, writer);
            return trace;
        };
        const std::vector<std::string> first = once();
        EXPECT_FALSE(first.empty());
        EXPECT_EQ(once(), first);
        EXPECT_EQ(once(), first);
    }

    // ---- StoneNumbersPowers -----------------------------------------------------------------------------------------

    constexpr FormId kRacePower = 0x9001;

    struct StoneNumbersPowers : ::testing::Test
    {
        std::vector<std::vector<FormId>> mAsked;
        std::vector<FormId> mCast;
        std::vector<FormId> mExtra;
        std::unique_ptr<character::CharacterService> mService;

        void SetUp() override
        {
            character::Hooks h;
            h.mPowers = [this](const std::vector<FormId>& spells) {
                mAsked.push_back(spells);
                std::vector<character::PowerInfo> out;
                for (const FormId id : spells)
                    if (id == kShadow || id == kSerpent || id == kRacePower)
                        out.push_back({ id, id == kShadow ? "Shadow" : (id == kSerpent ? "Serpent" : "Race"),
                            character::PowerSupport::Works, {} });
                return out;
            };
            h.mCastPower = [this](FormId id) {
                mCast.push_back(id);
                return true;
            };
            h.mExtraPowerSpells = [this] { return mExtra; };
            mService = std::make_unique<character::CharacterService>(std::move(h));
        }
    };

    TEST_F(StoneNumbersPowers, aStonesPowerIsListedWithNoRaceAndCastByItsIndex)
    {
        EXPECT_TRUE(mService->powers().empty());
        mExtra = { kAtronach, kShadow }; // an ability is asked about and filtered out by the combat's rule
        const std::vector<character::PowerInfo> list = mService->powers();
        ASSERT_EQ(list.size(), 1u);
        EXPECT_EQ(list[0].mId, kShadow);
        ASSERT_FALSE(mAsked.empty());
        EXPECT_EQ(mAsked.back(), (std::vector<FormId>{ kAtronach, kShadow }));
        EXPECT_TRUE(mService->powerReady(0, 1000));
        EXPECT_TRUE(mService->usePower(0, 1000));
        EXPECT_EQ(mCast, std::vector<FormId>{ kShadow });
        EXPECT_FALSE(mService->powerReady(0, 1000 + character::kPowerCooldownSeconds - 1)) << "once a game day";
        EXPECT_TRUE(mService->powerReady(0, 1000 + character::kPowerCooldownSeconds));
    }

    TEST_F(StoneNumbersPowers, aLeftStoneRemovesItsPowerAndTwoStonesGiveTwoInSpellOrder)
    {
        mExtra = { kShadow, kSerpent };
        EXPECT_EQ(mService->powers().size(), 2u);
        mExtra = { kSerpent };
        const std::vector<character::PowerInfo> list = mService->powers();
        ASSERT_EQ(list.size(), 1u);
        EXPECT_EQ(list[0].mId, kSerpent);
        mExtra.clear();
        EXPECT_TRUE(mService->powers().empty());
    }

    TEST_F(StoneNumbersPowers, withNoExtraHookTheListIsTheRacesAlone)
    {
        character::Hooks h;
        h.mPowers = [](const std::vector<FormId>&) { return std::vector<character::PowerInfo>{}; };
        character::CharacterService plain(std::move(h));
        EXPECT_TRUE(plain.powers().empty());
        EXPECT_FALSE(plain.usePower(0, 5));
    }

    // ---- StoneNumbersInventory --------------------------------------------------------------------------------------

    TEST(StoneNumbersInventory, theCarryWeightIsTheBasePlusTheBonusAndABadBonusChangesNothing)
    {
        inventory::InventoryService service(inventory::Hooks(), 0.f, 1);
        EXPECT_FLOAT_EQ(service.capacity(), inventory::kBaseCarryWeight);
        service.setCapacityBonus(100.f);
        EXPECT_FLOAT_EQ(service.capacity(), inventory::kBaseCarryWeight + 100.f);
        service.setCapacityBonus(std::numeric_limits<float>::quiet_NaN());
        EXPECT_FLOAT_EQ(service.capacity(), inventory::kBaseCarryWeight + 100.f);
        service.setCapacityBonus(-1.0e9f); // never below zero capacity
        EXPECT_FLOAT_EQ(service.capacity(), 0.f);
        service.setCapacityBonus(0.f);
        EXPECT_FLOAT_EQ(service.capacity(), inventory::kBaseCarryWeight);
    }

    // ---- StoneNumbersHost -------------------------------------------------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kPerkA = 0x01000F01;
    constexpr FormId kPerkB = 0x01000F02;
    constexpr FormId kRefA = 0x01000A00;
    constexpr FormId kPlayerRef = 0x14;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes questRecord()
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("QPerk")));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("INDX", Bytes().u16(10).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return record4("QUST", 0u, kQuestA, b);
    }

    class PerkData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    struct PerkRig
    {
        PerkRig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder perk("Perk");
            perk.parent("Form");
            mData.add(perk);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("AddPerk").native().param("akPerk", "Perk");
            mData.add(actor);
            ScriptBuilder game("Game");
            game.function("GetPlayer", "Actor").global().native();
            mData.add(game);
            ScriptBuilder s("PerkGiver");
            s.parent("ObjectReference");
            s.autoProperty("First", "Perk");
            s.autoProperty("Second", "Perk");
            s.function("Give")
                .local("p", "Actor")
                .callStatic("Game", "GetPlayer", Id("p"))
                .local("n", "None")
                .callMethod("AddPerk", Id("p"), Id("n"), { Id("::First_var") })
                .callMethod("AddPerk", Id("p"), Id("n"), { Id("::Second_var") })
                .callMethod("AddPerk", Id("p"), Id("n"), { Id("::First_var") });
            mData.add(s);
            mData.mClasses[kRefA] = "ObjectReference";
            mData.mClasses[kPerkA] = "Perk";
            mData.mClasses[kPerkB] = "Perk";
            mHost = std::make_unique<PapyrusHost>(mData);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("QUST"), 0, questRecord()));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            vmad::Script script;
            script.mName = "PerkGiver";
            for (const auto& [name, form2] : { std::pair<const char*, FormId>{ "First", kPerkA },
                     std::pair<const char*, FormId>{ "Second", kPerkB } })
            {
                vmad::Property p;
                p.mName = name;
                p.mType = 1;
                p.mValue.mKind = vmad::ValueKind::Object;
                p.mValue.mForm = form2;
                p.mValue.mAlias = -1;
                script.mProperties.push_back(p);
            }
            EXPECT_EQ(mHost->attachReference(kRefA, 0x01000B00, false, { script }), 1u);
            mHost->startGame();
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        PerkData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
    };

    TEST(StoneNumbersHost, AddPerkIsRecordedOncePerPerkAndSurvivesASaveAndALoad)
    {
        PerkRig a;
        ASSERT_TRUE(a.mHost->callFunction(kRefA, "PerkGiver", "Give"));
        EXPECT_EQ(a.mHost->perksOf(kPlayerRef), (std::vector<FormId>{ kPerkA, kPerkB }))
            << "the same perk twice is one";
        const std::uint64_t revision = a.mHost->perkRevision();
        EXPECT_GT(revision, 0u);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_FALSE(bytes.empty());

        PerkRig b;
        EXPECT_TRUE(b.mHost->perksOf(kPlayerRef).empty());
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        ASSERT_TRUE(outcome.mOk) << outcome.mError;
        EXPECT_EQ(b.mHost->perksOf(kPlayerRef), (std::vector<FormId>{ kPerkA, kPerkB }));
        EXPECT_GT(b.mHost->perkRevision(), 0u) << "the pack reads the list again after a load";
        EXPECT_EQ(b.mHost->saveState(), bytes) << "restore then save is the same bytes";
    }

    TEST(StoneNumbersHost, aStateWithNoPerksIsStillRestoredAndHasNone)
    {
        PerkRig a;
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_FALSE(bytes.empty());
        PerkRig b;
        ASSERT_TRUE(b.mHost->callFunction(kRefA, "PerkGiver", "Give"));
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        EXPECT_TRUE(b.mHost->perksOf(kPlayerRef).empty()) << "a load replaces the perks of the running session";
    }

    TEST(StoneNumbersHost, aDamagedStateIsRefusedAndKeepsThePerks)
    {
        PerkRig a;
        ASSERT_TRUE(a.mHost->callFunction(kRefA, "PerkGiver", "Give"));
        std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_GT(bytes.size(), 20u);
        bytes[bytes.size() / 2] ^= 0x5A;
        PerkRig b;
        ASSERT_TRUE(b.mHost->callFunction(kRefA, "PerkGiver", "Give"));
        EXPECT_FALSE(b.mHost->restoreSavedState(bytes).mOk);
        EXPECT_EQ(b.mHost->perksOf(kPlayerRef).size(), 2u);
    }

    // ---- StoneNumbersClock ------------------------------------------------------------------------------------------

    struct ClockRig
    {
        AbLibrary mLib = makeLibrary();
        double mNow = 1000000.0;
        std::vector<std::vector<FormId>> mApplied;
        std::unique_ptr<stones::StonesService> mService;
        ClockRig()
        {
            stones::StonesService::Hooks hooks;
            hooks.mLibrary = [this]() -> const PerkLibrary* { return &mLib; };
            hooks.mPersistent = [] { return std::vector<FormId>(); };
            hooks.mNow = [this] { return mNow; };
            hooks.mApply
                = [this](std::span<const FormId> spells) { mApplied.emplace_back(spells.begin(), spells.end()); };
            mService = std::make_unique<stones::StonesService>(std::move(hooks));
        }
    };

    TEST(StoneNumbersClock, aBlessingEndsWhenTheGameClockHasMovedEightHoursEvenInOneJump)
    {
        ClockRig rig;
        ASSERT_TRUE(rig.mService->cast(kBlessMagicka, 0x14));
        rig.mService->poll();
        ASSERT_FALSE(rig.mApplied.empty());
        EXPECT_EQ(rig.mApplied.back(), std::vector<FormId>{ kBlessMagicka });
        rig.mNow += 7.0 * 3600.0 + 3599.0; // a wait of seven hours and a bit: still on
        rig.mService->poll();
        EXPECT_EQ(rig.mService->timed().size(), 1u);
        rig.mNow += 2.0; // the rest of the eight hours: a wait or a sleep moves the clock in one step
        rig.mService->poll();
        EXPECT_TRUE(rig.mService->timed().empty());
        EXPECT_TRUE(rig.mApplied.back().empty()) << "the combat is told the numbers are gone";
    }

    TEST(StoneNumbersClock, theTimedSpellsAreSavedWithTheGameClockTheyEndOn)
    {
        ClockRig rig;
        ASSERT_TRUE(rig.mService->cast(kBlessHealth, 0x14));
        const std::vector<stones::StonesService::Timed>& timed = rig.mService->timedSpells();
        ASSERT_EQ(timed.size(), 1u);
        EXPECT_DOUBLE_EQ(timed[0].mEnds, 1000000.0 + 28800.0);
    }
}
