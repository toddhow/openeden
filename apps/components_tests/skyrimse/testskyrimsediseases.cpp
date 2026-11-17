// Task 0240 (diseases, M-S7): the pure rules (names, what a disease changes, resistance, the infection chance and its
// roll) and the service (a blow from a carrier infects by a seeded roll, resistance, cures, notifications, the save
// chunk) on a fake record library that holds the effects of the real records (numbers from the five masters, task 0240
// section 1). No game data.
#include <game_skyrimse/diseases.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using game_skyrimse::diseases::DiseaseService;

    constexpr FormId kRockjoint = 0x0B8782, kRattles = 0x0B8781, kBoneBreak = 0x0B877E, kBrainRot = 0x0B877F,
                     kWitbane = 0x0B8783, kSanguinare = 0x0B8780, kAtaxia = 0x0B877C, kNotADisease = 0x0B8800;
    constexpr FormId kArgonian = 0x104ACF, kWoodElf = 0x0AA025, kWerewolfImmunity = 0x0F5BA0, kAltarKynareth = 0x0FB997,
                     kPoisonSpell = 0x0A0001, kPotionEffect = 0x0AE722, kHealEffect = 0x0A0100;
    constexpr FormId kEffRockjoint = 0x0B877A, kEffRattles = 0x0B8779, kEffBone = 0x0B8776, kEffBrain = 0x0B8777,
                     kEffWit = 0x0B877B, kEffVamp = 0x0B8778, kEffAtaxia = 0x0B8775, kEffResist = 0x0E40D3,
                     kEffCureShrine = 0x0FBFF5, kEffFortify = 0x0FB98F;

    class DiseaseFakeLibrary final : public PerkLibrary
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

        void effect(FormId id, std::uint32_t archetype, std::int32_t value, std::int32_t second = -1, bool bad = true)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mSecondaryValue = second;
            m.mSecondaryWeight = 1.f;
            m.mFlags = bad ? (MagicFlag::Recover | MagicFlag::Detrimental) : 0;
            mEffects[id] = m;
        }
        void spell(
            FormId id, const std::string& editorId, std::uint32_t type, std::vector<std::pair<FormId, float>> effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = editorId;
            s.mHasData = true;
            s.mType = type;
            for (const auto& [e, magnitude] : effects)
            {
                SpellEffect x;
                x.mEffect = e;
                x.mMagnitude = magnitude;
                s.mEffects.push_back(x);
            }
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    // Archetypes: 0 ValueModifier, 5 Dual, 34 Peak (the data). Values: the real ones.
    DiseaseFakeLibrary makeLibrary()
    {
        DiseaseFakeLibrary l;
        l.effect(kEffRockjoint, MagicArchetype::Dual, 96, 97); // one-handed and two-handed damage percent
        l.effect(kEffRattles, MagicArchetype::PeakValueModifier, 29); // stamina regeneration
        l.effect(kEffBone, MagicArchetype::PeakValueModifier, 26); // stamina maximum
        l.effect(kEffBrain, MagicArchetype::PeakValueModifier, 25); // magicka maximum
        l.effect(kEffWit, MagicArchetype::PeakValueModifier, 28); // magicka regeneration
        l.effect(kEffVamp, MagicArchetype::PeakValueModifier, 24); // health maximum: not applied
        l.effect(kEffAtaxia, MagicArchetype::Dual, 104, 103); // lockpicking and pickpocket: not applied
        l.effect(kEffResist, MagicArchetype::ValueModifier, 45, -1, false);
        l.effect(kEffCureShrine, 3, -1, -1, false);
        l.effect(kEffFortify, MagicArchetype::PeakValueModifier, 26, -1, false);
        l.effect(kPotionEffect, 3, -1, -1, false);
        l.effect(kHealEffect, MagicArchetype::ValueModifier, 24, -1, false);
        l.spell(kRockjoint, "DiseaseRockjoint", 1, { { kEffRockjoint, 25.f } });
        l.spell(kRattles, "DiseaseRattles", 1, { { kEffRattles, 2.5f } });
        l.spell(kBoneBreak, "DiseaseBoneBreakFever", 1, { { kEffBone, 25.f } });
        l.spell(kBrainRot, "DiseaseBrainRot", 1, { { kEffBrain, 25.f } });
        l.spell(kWitbane, "DiseaseWitbane", 1, { { kEffWit, 0.5f } });
        l.spell(kSanguinare, "DiseaseSanguinareVampiris", 1, { { kEffVamp, 25.f } });
        l.spell(kAtaxia, "DiseaseAtaxia", 1, { { kEffAtaxia, 25.f } });
        l.spell(kNotADisease, "NotADisease", 0, { { kEffRockjoint, 25.f } });
        l.spell(kPoisonSpell, "crChaurusPoisonBite01", 5, { { kEffBone, 5.f } });
        l.spell(kArgonian, "RaceArgonianResistDisease", SpellRecord::kTypeAbility, { { kEffResist, 50.f } });
        l.spell(kWoodElf, "RaceWoodElf", SpellRecord::kTypeAbility, { { kEffResist, 50.f } });
        l.spell(kWerewolfImmunity, "WerewolfImmunity", SpellRecord::kTypeAbility, { { kEffResist, 100.f } });
        l.spell(kAltarKynareth, "AltarKynarethSpell", 0, { { kEffFortify, 25.f }, { kEffCureShrine, 0.f } });
        return l;
    }

    // ---- the rules
    // ------------------------------------------------------------------------------------------------------

    TEST(SkyrimDiseasesRules, names_are_the_editor_id_without_prefixes_split_at_capitals)
    {
        EXPECT_EQ(diseases::displayName("DiseaseRockjoint"), "Rockjoint");
        EXPECT_EQ(diseases::displayName("DiseaseBoneBreakFever"), "Bone Break Fever");
        EXPECT_EQ(diseases::displayName("TrapDiseaseBrainRot"), "Brain Rot");
        EXPECT_EQ(diseases::displayName("DLC2DiseaseBlackHeartBlight"), "Black Heart Blight");
        EXPECT_EQ(diseases::displayName("DLC2DiseaseDroops"), "Droops");
        EXPECT_EQ(diseases::displayName("Odd"), "Odd");
        EXPECT_EQ(diseases::displayName(""), "");
    }

    TEST(SkyrimDiseasesRules, a_disease_is_a_spell_of_type_one)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        EXPECT_TRUE(l.spell(kRockjoint)->isDisease());
        EXPECT_FALSE(l.spell(kNotADisease)->isDisease());
        EXPECT_FALSE(l.spell(kArgonian)->isDisease());
    }

    TEST(SkyrimDiseasesRules, the_effects_of_the_real_diseases_become_signed_modifiers)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        const FormId bone[] = { kBoneBreak };
        EXPECT_FLOAT_EQ(diseases::modifiersOf(l, bone).mStaminaMax, -25.f);
        const FormId brain[] = { kBrainRot };
        EXPECT_FLOAT_EQ(diseases::modifiersOf(l, brain).mMagickaMax, -25.f);
        const FormId rattles[] = { kRattles };
        EXPECT_FLOAT_EQ(diseases::modifiersOf(l, rattles).mStaminaRegen, -2.5f);
        const FormId wit[] = { kWitbane };
        EXPECT_FLOAT_EQ(diseases::modifiersOf(l, wit).mMagickaRegen, -0.5f);
        const FormId rock[] = { kRockjoint };
        const PlayerValueModifiers m = diseases::modifiersOf(l, rock);
        EXPECT_FLOAT_EQ(m.mSkillPercent[0], -25.f); // one-handed
        EXPECT_FLOAT_EQ(m.mSkillPercent[1], -25.f); // two-handed
        EXPECT_FLOAT_EQ(m.mSkillPercent[2], 0.f);
    }

    TEST(SkyrimDiseasesRules, two_diseases_add_and_the_ignored_effects_are_described)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        const FormId both[] = { kBoneBreak, kRattles, kSanguinare, kAtaxia };
        std::vector<std::string> ignored;
        const PlayerValueModifiers m = diseases::modifiersOf(l, both, &ignored);
        EXPECT_FLOAT_EQ(m.mStaminaMax, -25.f);
        EXPECT_FLOAT_EQ(m.mStaminaRegen, -2.5f);
        EXPECT_EQ(ignored.size(), 2u); // the health maximum and the lockpicking and pickpocket modifiers
    }

    TEST(SkyrimDiseasesRules, no_diseases_or_unknown_ids_change_nothing)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        EXPECT_TRUE(diseases::modifiersOf(l, {}).isZero());
        const FormId unknown[] = { 0x777777, kNotADisease, kArgonian };
        EXPECT_TRUE(diseases::modifiersOf(l, unknown).isZero()); // only a disease counts
    }

    TEST(SkyrimDiseasesRules, resistance_sums_the_ability_spells_that_name_actor_value_45)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        const FormId none[] = { kAltarKynareth };
        EXPECT_FLOAT_EQ(diseases::resistanceOf(l, none), 0.f);
        const FormId argonian[] = { kArgonian };
        EXPECT_FLOAT_EQ(diseases::resistanceOf(l, argonian), 50.f);
        const FormId two[] = { kArgonian, kWoodElf };
        EXPECT_FLOAT_EQ(diseases::resistanceOf(l, two), 100.f);
        const FormId wolf[] = { kWerewolfImmunity };
        EXPECT_FLOAT_EQ(diseases::resistanceOf(l, wolf), 100.f);
    }

    TEST(SkyrimDiseasesRules, the_chance_is_the_base_times_what_resistance_leaves)
    {
        EXPECT_FLOAT_EQ(diseases::infectionChance(0.1f, 0.f), 0.1f);
        EXPECT_FLOAT_EQ(diseases::infectionChance(0.1f, 50.f), 0.05f);
        EXPECT_FLOAT_EQ(diseases::infectionChance(0.1f, 100.f), 0.f);
        EXPECT_FLOAT_EQ(diseases::infectionChance(0.1f, 250.f), 0.f); // clamped
        EXPECT_FLOAT_EQ(diseases::infectionChance(0.1f, -40.f), 0.1f); // a negative resistance does not raise it
        EXPECT_FLOAT_EQ(diseases::infectionChance(3.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(diseases::infectionChance(std::nanf(""), 0.f), 0.f);
    }

    TEST(SkyrimDiseasesRules, the_roll_is_deterministic_in_range_and_moves_with_its_inputs)
    {
        EXPECT_EQ(diseases::rollUnit(7, 1), diseases::rollUnit(7, 1));
        EXPECT_NE(diseases::rollUnit(7, 1), diseases::rollUnit(7, 2));
        EXPECT_NE(diseases::rollUnit(7, 1), diseases::rollUnit(8, 1));
        double sum = 0.0;
        for (std::uint64_t i = 0; i < 4000; ++i)
        {
            const float r = diseases::rollUnit(0xABCDEF, i);
            ASSERT_GE(r, 0.f);
            ASSERT_LT(r, 1.f);
            sum += r;
        }
        EXPECT_NEAR(sum / 4000.0, 0.5, 0.03);
    }

    TEST(SkyrimDiseasesRules, the_disease_of_a_race_is_the_first_disease_among_its_attack_spells)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        const FormId chaurus[] = { kPoisonSpell, kRattles, kRockjoint };
        EXPECT_EQ(diseases::diseaseOfAttacks(l, chaurus), kRattles);
        const FormId none[] = { kPoisonSpell, kNotADisease, 0x999999 };
        EXPECT_EQ(diseases::diseaseOfAttacks(l, none), 0u);
        EXPECT_EQ(diseases::diseaseOfAttacks(l, {}), 0u);
    }

    TEST(SkyrimDiseasesRules, a_shrine_spell_and_a_potion_effect_cure_by_their_archetype)
    {
        const DiseaseFakeLibrary l = makeLibrary();
        EXPECT_TRUE(diseases::hasCureDisease(l, kAltarKynareth));
        EXPECT_FALSE(diseases::hasCureDisease(l, kRockjoint));
        EXPECT_FALSE(diseases::hasCureDisease(l, 0x123456));
        EXPECT_TRUE(diseases::isCureDiseaseEffect(l, kPotionEffect));
        EXPECT_FALSE(diseases::isCureDiseaseEffect(l, kHealEffect));
        EXPECT_FALSE(diseases::isCureDiseaseEffect(l, 0x123456));
    }

    // ---- the service
    // ------------------------------------------------------------------------------------------------------

    struct DiseaseRig
    {
        explicit DiseaseRig(float chance = 1.f, std::uint64_t seed = 99)
            : library(makeLibrary())
        {
            DiseaseService::Hooks hooks;
            hooks.mLibrary = [this]() -> const PerkLibrary* { return ready ? &library : nullptr; };
            hooks.mPlayerSpells = [this] { return spells; };
            hooks.mApply = [this](const PlayerValueModifiers& m) { applied.push_back(m); };
            hooks.mLog = [this](const std::string& line) { logs.push_back(line); };
            diseases::Settings settings;
            settings.mChance = chance;
            settings.mSeed = seed;
            service = std::make_unique<DiseaseService>(std::move(hooks), settings);
        }
        DiseaseFakeLibrary library;
        bool ready = true;
        std::vector<FormId> spells;
        std::vector<PlayerValueModifiers> applied;
        std::vector<std::string> logs;
        std::unique_ptr<DiseaseService> service;
    };

    TEST(SkyrimDiseasesService, a_blow_from_a_carrier_infects_and_says_so)
    {
        DiseaseRig rig;
        const FormId wolf[] = { kRockjoint };
        EXPECT_EQ(rig.service->onHit(wolf, 12), DiseaseService::HitResult::Infected);
        ASSERT_EQ(rig.service->infected().size(), 1u);
        EXPECT_EQ(rig.service->infected()[0], kRockjoint);
        EXPECT_EQ(rig.service->notificationCount(), 1u);
        EXPECT_EQ(rig.service->notificationAt(1), "You have contracted Rockjoint");
        EXPECT_EQ(rig.service->nameOf(kRockjoint), "Rockjoint");
    }

    TEST(SkyrimDiseasesService, a_blow_from_something_that_carries_nothing_does_nothing)
    {
        DiseaseRig rig;
        const FormId bandit[] = { kPoisonSpell };
        EXPECT_EQ(rig.service->onHit(bandit, 12), DiseaseService::HitResult::NoDisease);
        EXPECT_EQ(rig.service->onHit({}, 12), DiseaseService::HitResult::NoDisease);
        EXPECT_TRUE(rig.service->infected().empty());
        EXPECT_EQ(rig.service->rolls(), 0u);
    }

    TEST(SkyrimDiseasesService, the_library_not_being_ready_is_not_a_roll)
    {
        DiseaseRig rig;
        rig.ready = false;
        const FormId wolf[] = { kRockjoint };
        EXPECT_EQ(rig.service->onHit(wolf, 12), DiseaseService::HitResult::NotReady);
        EXPECT_EQ(rig.service->rolls(), 0u);
    }

    TEST(SkyrimDiseasesService, a_disease_the_player_has_is_not_rolled_for_again)
    {
        DiseaseRig rig;
        const FormId wolf[] = { kRockjoint };
        ASSERT_EQ(rig.service->onHit(wolf, 12), DiseaseService::HitResult::Infected);
        const std::uint64_t rolls = rig.service->rolls();
        EXPECT_EQ(rig.service->onHit(wolf, 12), DiseaseService::HitResult::Already);
        EXPECT_EQ(rig.service->rolls(), rolls);
        EXPECT_EQ(rig.service->notificationCount(), 1u);
    }

    TEST(SkyrimDiseasesService, full_resistance_spends_no_roll_and_half_resistance_halves_the_chance)
    {
        DiseaseRig immune(1.f);
        immune.spells = { kWerewolfImmunity };
        const FormId wolf[] = { kRockjoint };
        EXPECT_EQ(immune.service->onHit(wolf, 1), DiseaseService::HitResult::Immune);
        EXPECT_EQ(immune.service->rolls(), 0u);
        EXPECT_TRUE(immune.service->infected().empty());

        // 4000 hits of a carrier with chance 0.5 and 50 percent resistance: about a quarter infect (a fresh player each
        // time through a cure)
        DiseaseRig half(0.5f, 5);
        half.spells = { kArgonian };
        int infected = 0;
        for (int i = 0; i < 4000; ++i)
        {
            if (half.service->onHit(wolf, 1) == DiseaseService::HitResult::Infected)
            {
                ++infected;
                half.service->cure();
            }
        }
        EXPECT_NEAR(infected / 4000.0, 0.25, 0.03);
    }

    TEST(SkyrimDiseasesService, the_same_seed_gives_the_same_infections_and_another_seed_another)
    {
        const FormId wolf[] = { kRockjoint };
        const auto run = [&](std::uint64_t seed) {
            DiseaseRig rig(0.3f, seed);
            std::vector<int> hits;
            for (int i = 0; i < 40; ++i)
            {
                if (rig.service->onHit(wolf, 1) == DiseaseService::HitResult::Infected)
                {
                    hits.push_back(i);
                    rig.service->cure();
                }
            }
            return hits;
        };
        EXPECT_EQ(run(11), run(11));
        EXPECT_NE(run(11), run(12));
        EXPECT_FALSE(run(11).empty());
    }

    TEST(SkyrimDiseasesService, a_chance_of_zero_never_infects)
    {
        DiseaseRig rig(0.f);
        const FormId wolf[] = { kRockjoint };
        for (int i = 0; i < 100; ++i)
            EXPECT_EQ(rig.service->onHit(wolf, 1), DiseaseService::HitResult::Missed);
        EXPECT_EQ(rig.service->rolls(), 100u);
    }

    TEST(SkyrimDiseasesService, infect_takes_only_diseases_once_and_fills_at_the_cap)
    {
        DiseaseRig rig;
        EXPECT_FALSE(rig.service->infect(kNotADisease));
        EXPECT_FALSE(rig.service->infect(0x424242));
        EXPECT_TRUE(rig.service->infect(kBoneBreak));
        EXPECT_FALSE(rig.service->infect(kBoneBreak));
        EXPECT_TRUE(rig.service->has(kBoneBreak));
        EXPECT_FALSE(rig.service->has(kRattles));
    }

    TEST(SkyrimDiseasesService, poll_gives_combat_the_modifiers_once_per_change)
    {
        DiseaseRig rig;
        rig.service->poll();
        EXPECT_TRUE(rig.applied.empty() || rig.applied.back().isZero()); // nothing to carry
        const std::size_t before = rig.applied.size();
        rig.service->poll();
        EXPECT_EQ(rig.applied.size(), before);
        ASSERT_TRUE(rig.service->infect(kBoneBreak));
        rig.service->poll();
        ASSERT_GT(rig.applied.size(), before);
        EXPECT_FLOAT_EQ(rig.applied.back().mStaminaMax, -25.f);
        const std::size_t now = rig.applied.size();
        rig.service->poll();
        EXPECT_EQ(rig.applied.size(), now);
        EXPECT_EQ(rig.service->cure(), 1u);
        rig.service->poll();
        EXPECT_TRUE(rig.applied.back().isZero());
    }

    TEST(SkyrimDiseasesService, a_cure_removes_everything_and_tells_the_player_once_each)
    {
        DiseaseRig rig;
        ASSERT_TRUE(rig.service->infect(kBoneBreak));
        ASSERT_TRUE(rig.service->infect(kRattles));
        const std::uint64_t notes = rig.service->notificationCount();
        EXPECT_EQ(rig.service->cure(), 2u);
        EXPECT_TRUE(rig.service->infected().empty());
        EXPECT_EQ(rig.service->notificationCount(), notes + 2);
        EXPECT_EQ(rig.service->notificationAt(notes + 1), "You have been cured of Bone Break Fever");
        EXPECT_EQ(rig.service->cure(), 0u);
        EXPECT_EQ(rig.service->notificationCount(), notes + 2);
    }

    TEST(SkyrimDiseasesService, the_notification_ring_keeps_the_last_sixteen)
    {
        DiseaseRig rig;
        for (int i = 0; i < 20; ++i)
        {
            ASSERT_TRUE(rig.service->infect(kBoneBreak));
            ASSERT_EQ(rig.service->cure(), 1u);
        }
        EXPECT_EQ(rig.service->notificationCount(), 40u);
        EXPECT_FALSE(rig.service->notificationAt(40).empty());
        EXPECT_FALSE(rig.service->notificationAt(25).empty());
        EXPECT_TRUE(rig.service->notificationAt(24).empty()); // left the ring
        EXPECT_TRUE(rig.service->notificationAt(0).empty());
        EXPECT_TRUE(rig.service->notificationAt(41).empty());
    }

    // ---- the chunk
    // ----------------------------------------------------------------------------------------------------------

    class DiseaseMemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class DiseaseMemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit DiseaseMemoryReader(std::vector<std::uint8_t> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mAt, size);
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }

    private:
        std::vector<std::uint8_t> mBytes;
        std::size_t mAt = 0;
    };

    TEST(SkyrimDiseasesChunk, the_infections_and_the_roll_counter_round_trip)
    {
        DiseaseRig a;
        ASSERT_TRUE(a.service->infect(kBoneBreak));
        ASSERT_TRUE(a.service->infect(kRattles));
        const FormId bandit[] = { kRockjoint };
        a.service->setSettings({ 0.f, 99 });
        (void)a.service->onHit(bandit, 1); // one roll spent
        ASSERT_EQ(a.service->rolls(), 1u);
        EXPECT_EQ(a.service->chunk().chunkName(), "diseases");
        EXPECT_EQ(a.service->chunk().chunkVersion(), 1u);
        DiseaseMemoryWriter w;
        a.service->chunk().write(w);

        DiseaseRig b;
        DiseaseMemoryReader r(w.mBytes);
        ASSERT_TRUE(b.service->chunk().read(r, 1));
        EXPECT_EQ(b.service->infected(), a.service->infected());
        EXPECT_EQ(b.service->rolls(), 1u);
        b.service->poll();
        ASSERT_FALSE(b.applied.empty());
        EXPECT_FLOAT_EQ(b.applied.back().mStaminaMax, -25.f); // a load puts the penalty back
    }

    TEST(SkyrimDiseasesChunk, a_damaged_or_newer_chunk_changes_nothing)
    {
        DiseaseRig a;
        ASSERT_TRUE(a.service->infect(kBoneBreak));
        DiseaseMemoryWriter w;
        a.service->chunk().write(w);

        DiseaseRig b;
        ASSERT_TRUE(b.service->infect(kWitbane));
        {
            DiseaseMemoryReader r(w.mBytes);
            EXPECT_FALSE(b.service->chunk().read(r, 2)); // a newer version
        }
        {
            std::vector<std::uint8_t> cut = w.mBytes;
            cut.pop_back();
            DiseaseMemoryReader r(cut);
            EXPECT_FALSE(b.service->chunk().read(r, 1));
        }
        {
            std::vector<std::uint8_t> huge(12, 0);
            huge[0] = 0xFF;
            huge[1] = 0xFF; // a count far over the cap
            DiseaseMemoryReader r(huge);
            EXPECT_FALSE(b.service->chunk().read(r, 1));
        }
        {
            std::vector<std::uint8_t> extra = w.mBytes;
            extra.push_back(1);
            DiseaseMemoryReader r(extra);
            EXPECT_FALSE(b.service->chunk().read(r, 1)); // trailing bytes
        }
        ASSERT_EQ(b.service->infected().size(), 1u);
        EXPECT_EQ(b.service->infected()[0], kWitbane);
    }
}
