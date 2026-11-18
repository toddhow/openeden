// Task 0301: an animal or monster that the combat AI makes raise its guard asked for the person's
// `1HM_BlockIdle` in its
// own animations folder (the log said "meshes/actors/horker/animations/1hm_blockidle.hkx not found"): a creature has no
// guard clip, so the AI plays none for it. A person's guard is unchanged.
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

    constexpr WorldId kBase = 100;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 100.f;
            npc.mSkills[0] = 50;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4;
            npc.mAi.mStyle.mDefensive = 5.f; // almost always blocks when the player swings
            npc.mAi.mStyle.mOffensive = 0.f;
            mProfiles[kBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
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
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest& request) override
        {
            mClips.emplace_back(request.slot, std::string(request.clip));
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
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
        std::size_t guardClips() const
        {
            std::size_t n = 0;
            for (const auto& c : mClips)
                if (c.first == ClipSlot::BlockStart)
                    ++n;
            return n;
        }
        std::vector<std::pair<ClipSlot, std::string>> mClips;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    // The guard of task 0197's test, with the actor's profile marked as a creature or not: how many guard clips the AI
    // asked for in 200 steps of 0.05 s while the player swings.
    std::size_t guardClipsOf(const CreatureProfile* creature, std::string* firstName = nullptr)
    {
        FakeData data;
        if (creature != nullptr)
            data.mProfiles[kBase].mCreature = creature;
        SkyrimCombat combat;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        EXPECT_TRUE(combat.setTestHostile("guard"));
        ActorInfo info;
        info.handle = { 10, 1 };
        info.base = kBase;
        info.editorId = "Guard";
        combat.onActorAdded(info);
        FakeWriter writer;
        writer.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
        writer.mPlayerState = { 100.0, 600.0, 5.0, 0.f, 90.f, 100.f };
        writer.mPlayerState.y = 100.0;
        std::vector<SkyrimCombat::AiMoveIntent> moves;
        for (int i = 0; i < 4; ++i)
        {
            combat.combatUpdate(0.1f, writer);
            combat.drainAiMoves(moves);
        }
        combat.setTestPlayerSwing(0.5f);
        for (int i = 0; i < 200; ++i)
        {
            combat.combatUpdate(0.05f, writer);
            combat.drainAiMoves(moves);
        }
        if (firstName != nullptr)
            for (const auto& c : writer.mClips)
                if (c.first == ClipSlot::BlockStart)
                {
                    *firstName = c.second;
                    break;
                }
        return writer.guardClips();
    }
}

TEST(SkyrimCreaturesGuard, aPersonRaisesItsGuardWithTheGuardClip)
{
    std::string name;
    EXPECT_GE(guardClipsOf(nullptr, &name), 1u);
    EXPECT_EQ(name, "1HM_BlockIdle");
}

TEST(SkyrimCreaturesGuard, aCreatureAsksForNoGuardClip)
{
    for (const char* skeleton : { "actors/horker/character assets/skeleton.nif",
             "actors/icewraith/character assets/skeleton.nif", "actors/canine/character assets wolf/skeleton.nif",
             "actors/bear/character assets/skeleton.nif" })
    {
        const CreatureProfile* creature = creatureProfileFor(skeleton);
        ASSERT_NE(creature, nullptr) << skeleton;
        EXPECT_EQ(guardClipsOf(creature), 0u) << skeleton;
    }
}
