// Task 0301: a creature that casts plays the cast clip of its own folder. The person's MRh_Release and MRh_SelfRelease
// are not files of a chaurus's, a Dwarven spider's or a dragon priest's animations folder (the log said
// "mrh_release not found"); the pack names the creature's own clip (creatureCastClip) and the spell fires on that
// clip's SpellFire event.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/creatures.hpp>

#include <gtest/gtest.h>

#include <map>

#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kCasterBase = 100;
    constexpr FormId kFirebolt = 0x1000, kFastHealing = 0x1002, kFireEffect = 0x2000, kHealEffect = 0x2004,
                     kProjectile = 0x3000;

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
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\FireBoltProjectile.nif";
            addEffect(kFireEffect, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile);
            // stamina, not health: a plain heal is skipped by the test cast rule
            addEffect(kHealEffect, 26, 0x201000, kNullFormId);
            addSpell(kFirebolt, "Firebolt", 41, MagicDelivery::Aimed, kFireEffect);
            addSpell(kFastHealing, "FastHealing", 73, MagicDelivery::Self, kHealEffect);
            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 400.f;
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
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
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
        MagicSettings magicSettings() override { return MagicSettings(); }

        void addEffect(FormId id, std::int32_t value, std::uint32_t flags, FormId projectile)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t delivery, FormId effect)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = delivery;
            SpellEffect se;
            se.mEffect = effect;
            se.mMagnitude = 25.f;
            s.mEffects.push_back(se);
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
        bool playClip(CombatSubject, const ClipRequest& request) override
        {
            mClips.emplace_back(request.clip);
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool launchProjectile(CombatSubject, const ProjectileRequest&) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport&) override { return true; }
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
                out.x = 800.0;
                out.healthMax = 100.f;
                out.health = 100.f;
                return true;
            }
            if (subject.actor.reference != 10 && subject.actor.reference != 20)
                return false;
            out.heading = 1.5707963f;
            out.x = subject.actor.reference == 20 ? 800.0 : 0.0;
            return true;
        }
        std::vector<std::string> mClips;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    // One caster casting a spell by the test rule every 2 s: returns the cast clip the writer was asked for.
    std::string castClipOf(const CreatureProfile* creature, const char* spell)
    {
        FakeData data;
        if (creature != nullptr)
            data.mProfiles[kCasterBase].mCreature = creature;
        SkyrimCombat combat;
        combat.setData(&data);
        combat.onActorAdded(info(10, kCasterBase, "Caster"));
        combat.onActorAdded(info(20, kCasterBase, "Victim"));
        FakeWriter writer;
        EXPECT_TRUE(combat.setTestCasts(std::string("Caster:") + spell));
        combat.setTestCastEvery(2.f);
        for (int i = 0; i < 12 && writer.mClips.empty(); ++i)
            combat.combatUpdate(0.25f, writer);
        return writer.mClips.empty() ? std::string() : writer.mClips.front();
    }
}

TEST(SkyrimCreaturesCast, aPersonStillPlaysTheMagicClips)
{
    EXPECT_EQ(castClipOf(nullptr, "Firebolt"), "MRh_Release");
    EXPECT_EQ(castClipOf(nullptr, "FastHealing"), "MRh_SelfRelease");
}

TEST(SkyrimCreaturesCast, theDragonPriestPlaysItsOwnReleases)
{
    const CreatureProfile* priest = creatureProfileFor("actors/dragonpriest/character assets/skeleton.nif");
    ASSERT_NE(priest, nullptr);
    EXPECT_EQ(castClipOf(priest, "Firebolt"), "mag_rh_ff_r_release");
    EXPECT_EQ(castClipOf(priest, "FastHealing"), "mag_rh_ff_s_release");
}

TEST(SkyrimCreaturesCast, theDwarvenSpiderAndTheChaurusPlayTheirCastingClip)
{
    const CreatureProfile* spider = creatureProfileFor("actors/dwarvenspider/character assets/skeleton.nif");
    const CreatureProfile* chaurus = creatureProfileFor("actors/chaurus/character assets/skeleton.nif");
    ASSERT_NE(spider, nullptr);
    ASSERT_NE(chaurus, nullptr);
    EXPECT_EQ(castClipOf(spider, "Firebolt"), "attack_castinga");
    EXPECT_EQ(castClipOf(chaurus, "Firebolt"), "attack_releasespit");
}

TEST(SkyrimCreaturesCast, aCreatureWithNoCastClipKeepsTheMagicClips)
{
    const CreatureProfile* wolf = creatureProfileFor("actors/canine/character assets wolf/skeleton.nif");
    ASSERT_NE(wolf, nullptr);
    EXPECT_EQ(castClipOf(wolf, "Firebolt"), "MRh_Release");
}
