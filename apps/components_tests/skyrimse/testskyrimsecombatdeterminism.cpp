// Task 0183: the Skyrim pack's combat rules count their time from the update on which the combat data became ready,
// not from the session's first update. How long a worker takes to read the records differs from run to run (and with
// it the frame on which combat starts), so a schedule that counted the waiting updates too gave traces that differed
// by a frame of regeneration or by the frame of a test swing. Written first and run against the old code.
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <map>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kBase = 100;
    constexpr float kDt = 0.1f;

    class Data final : public CombatData
    {
    public:
        Data()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mStaminaRegenPercent = 0.5f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId, FighterProfile&) override { return false; }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return {}; }
        bool combatReady() override { return mReady; }

        WeaponRecord mSword;
        FighterProfile mPlayer;
        bool mReady = false;
    };

    class Writer final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweepAt.push_back(mUpdate);
            (void)subject;
            (void)request;
            return true;
        }
        bool reportDamage(const DamageReport&) override { return true; }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = ClipTiming{ true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                return false;
            out = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
            return true;
        }
        std::vector<int> mSweepAt; ///< the update (counted from the first READY one) of each sweep request
        int mUpdate = 0;
    };

    struct Outcome
    {
        std::vector<int> mSweepAt;
        float mStamina = 0.f;
        std::size_t mUpdates = 0;
    };

    /// `waiting` updates with the data not ready, then `ready` updates with it.
    Outcome run(int waiting, int ready)
    {
        Data data;
        SkyrimCombat combat;
        Writer writer;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setTestPlayerSwing(2.f);
        for (int i = 0; i < waiting; ++i)
            combat.combatUpdate(kDt, writer);
        data.mReady = true;
        for (int i = 0; i < ready; ++i)
        {
            writer.mUpdate = i;
            combat.combatUpdate(kDt, writer);
        }
        Outcome out;
        out.mSweepAt = writer.mSweepAt;
        SkyrimCombat::FighterView view;
        if (combat.fighterView(CombatSubject::ofPlayer(), view))
            out.mStamina = view.mStamina;
        out.mUpdates = combat.updatesWaitingForData();
        return out;
    }

    TEST(CombatDeterminism, theFirstTestSwingComesOnTheSameReadyUpdateWhateverTheWait)
    {
        const Outcome none = run(0, 60);
        ASSERT_FALSE(none.mSweepAt.empty());
        for (int waiting : { 3, 17, 45, 400 })
        {
            const Outcome other = run(waiting, 60);
            EXPECT_EQ(other.mSweepAt, none.mSweepAt) << "waiting " << waiting;
        }
    }

    TEST(CombatDeterminism, theStaminaAfterTheSameReadyUpdatesDoesNotDependOnTheWait)
    {
        const Outcome none = run(0, 60);
        EXPECT_LT(none.mStamina, 100.f); // the swing cost something and it is regenerating
        for (int waiting : { 1, 9, 40, 400 })
            EXPECT_EQ(run(waiting, 60).mStamina, none.mStamina) << "waiting " << waiting;
    }

    TEST(CombatDeterminism, theWaitingUpdatesAreStillCounted)
    {
        EXPECT_EQ(run(7, 3).mUpdates, 7u);
    }
}
