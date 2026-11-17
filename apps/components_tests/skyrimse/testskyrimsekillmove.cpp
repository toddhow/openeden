// Task 0278: kill moves (the rules, the state machine over a fake core, and the combat hook). Over fakes, no game data.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/killmove.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::killmove;
    using namespace openeden::sdk::game;

    constexpr double kPi = std::numbers::pi;

    constexpr WorldId kFighterBase = 100;

    class CombatFakeData final : public CombatData
    {
    public:
        CombatFakeData()
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
            mProfiles[kFighterBase] = npc;
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

    class CombatFakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
            return true;
        }
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
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
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

    struct KillMoveCombat : ::testing::Test
    {
        CombatFakeData mData;
        SkyrimCombat mCombat;
        CombatFakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        std::vector<ActorHandle> mBleedouts;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.setBleedoutSink([this](ActorHandle h) { mBleedouts.push_back(h); });
            mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
            mWriter.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 100.0, 100.0, 5.0, 0.f, 90.f, 100.f };
        }
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mCombat.combatUpdate(dt, mWriter);
                mCombat.drainAiMoves(mMoves);
            }
        }
        void playerHits()
        {
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6; ++i)
                run(1);
            ASSERT_FALSE(mWriter.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mA;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps.back().second.token, &hit, 1);
            run(1);
        }
    };


    // ---- a fake core for the state machine ------------------------------------------------------------------
    class Core final : public ActorRequestWriter, public RidingWriter, public PairedWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override
        {
            ++mMovements;
            return true;
        }
        bool setHeading(ActorHandle, float heading) override
        {
            mHeading = heading;
            return true;
        }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            if (!mVictimThere)
                return false;
            x = mVictimX;
            y = mVictimY;
            z = mVictimZ;
            return true;
        }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = 0.0;
            y = 0.0;
            z = 0.0;
            return true;
        }
        bool carryPlayer(double x, double y, double z, float heading) override
        {
            ++mCarries;
            mCarry.mX = x;
            mCarry.mY = y;
            mCarry.mZ = z;
            mCarry.mHeading = heading;
            return true;
        }
        bool releasePlayer(double x, double y, double z) override
        {
            ++mReleases;
            mRelease.mX = x;
            mRelease.mY = y;
            mRelease.mZ = z;
            return true;
        }
        PairedStatus pairedClipInfo(std::string_view name, PairedClipInfo& out) override
        {
            ++mAsks;
            out = mInfo;
            EXPECT_EQ(name, kKillerClip);
            return mStatus;
        }
        bool playPairedClip(ActorHandle actor, std::string_view name) override
        {
            mPlays.push_back({ actor.reference, std::string(name) });
            return true;
        }

        bool mVictimThere = true;
        double mVictimX = 0.0, mVictimY = 100.0, mVictimZ = 5.0; // 100 units north of the player at the origin
        int mMovements = 0, mCarries = 0, mReleases = 0, mAsks = 0;
        float mHeading = 99.f;
        Anchor mCarry;
        WorldPoint mRelease;
        PairedStatus mStatus = PairedStatus::Ready;
        PairedClipInfo mInfo;
        std::vector<std::pair<WorldId, std::string>> mPlays;

        Core()
        {
            mInfo.duration = 2.0f;
            mInfo.end = { 10.0, 20.0, 0.0, 0.0 };
            mInfo.secondStart = { 0.0, 60.0, 0.0, kPi };
            mInfo.secondEnd = { 0.0, 60.0, 0.0, kPi };
            mInfo.hasSecond = true;
        }
    };

    ActorHandle victimHandle()
    {
        ActorHandle h;
        h.reference = 55;
        h.session = 3;
        return h;
    }

    Candidate lethalSwordBlow()
    {
        Candidate c;
        c.mAttackerIsPlayer = true;
        c.mWeaponAnimType = kSwordAnimType;
        return c;
    }

    struct Rig
    {
        KillMoves mMoves;
        Core mCore;
        std::vector<std::pair<WorldId, bool>> mHolds;
        std::vector<ActorHandle> mFinished;
        std::vector<std::string> mLog;

        Rig()
        {
            mMoves.setEnabled(true);
            mMoves.setChance(1.f);
            mMoves.setHold([this](WorldId reference, bool hold) { mHolds.push_back({ reference, hold }); });
            mMoves.setFinish([this](ActorHandle victim) { mFinished.push_back(victim); });
            mMoves.setLog([this](const std::string& line) { mLog.push_back(line); });
        }
    };
}

// ---- the pure rules ---------------------------------------------------------------------------------------------

TEST(SkyrimKillMoveRules, only_the_player_with_a_sword_against_a_living_non_essential_person_qualifies)
{
    EXPECT_TRUE(eligible(lethalSwordBlow()));
    Candidate c = lethalSwordBlow();
    c.mAttackerIsPlayer = false;
    EXPECT_FALSE(eligible(c));
    c = lethalSwordBlow();
    c.mVictimIsPlayer = true;
    EXPECT_FALSE(eligible(c));
    for (std::uint8_t type : { std::uint8_t(0), std::uint8_t(2), std::uint8_t(3), std::uint8_t(4), std::uint8_t(5),
             std::uint8_t(7) })
    {
        c = lethalSwordBlow();
        c.mWeaponAnimType = type;
        EXPECT_FALSE(eligible(c)) << int(type);
    }
    c = lethalSwordBlow();
    c.mVictimIsCreature = true;
    EXPECT_FALSE(eligible(c));
    c = lethalSwordBlow();
    c.mVictimEssential = true;
    EXPECT_FALSE(eligible(c));
    c = lethalSwordBlow();
    c.mVictimDead = true;
    EXPECT_FALSE(eligible(c));
    c = lethalSwordBlow();
    c.mBash = true;
    EXPECT_FALSE(eligible(c));
    c = lethalSwordBlow();
    c.mActive = true;
    EXPECT_FALSE(eligible(c));
}

TEST(SkyrimKillMoveRules, the_roll_is_deterministic_in_the_unit_interval_and_spread_evenly)
{
    EXPECT_EQ(roll(7, 1), roll(7, 1));
    EXPECT_NE(roll(7, 1), roll(7, 2));
    EXPECT_NE(roll(7, 1), roll(8, 1));
    int below = 0;
    for (std::uint64_t i = 0; i < 4000; ++i)
    {
        const float r = roll(12345, i);
        ASSERT_GE(r, 0.f);
        ASSERT_LT(r, 1.f);
        if (r < 0.25f)
            ++below;
    }
    EXPECT_NEAR(below / 4000.0, 0.25, 0.03);
}

TEST(SkyrimKillMoveRules, a_chance_of_nothing_never_and_of_one_always_and_nan_never)
{
    for (std::uint64_t i = 0; i < 100; ++i)
    {
        EXPECT_FALSE(chosen(1, i, 0.f));
        EXPECT_FALSE(chosen(1, i, -3.f));
        EXPECT_FALSE(chosen(1, i, std::nanf("")));
        EXPECT_TRUE(chosen(1, i, 1.f));
        EXPECT_TRUE(chosen(1, i, 7.f));
    }
}

TEST(SkyrimKillMoveRules, the_heading_looks_at_the_target_and_keeps_the_fallback_for_nothing)
{
    EXPECT_NEAR(headingToward(0, 0, 0, 10, 9.f), 0.f, 1e-6f);
    EXPECT_NEAR(headingToward(0, 0, 10, 0, 9.f), static_cast<float>(kPi / 2), 1e-6f);
    EXPECT_NEAR(headingToward(0, 0, -10, 0, 9.f), -static_cast<float>(kPi / 2), 1e-6f);
    EXPECT_EQ(headingToward(1, 1, 1, 1, 9.f), 9.f);
    EXPECT_EQ(headingToward(std::nan(""), 1, 1, 1, 9.f), 9.f);
}

TEST(SkyrimKillMoveRules, the_anchor_puts_the_clips_victim_node_where_the_victim_stands)
{
    // the bear's numbers: the victim node 157 units in front of the killer, facing back
    const PairedPose root{ 0.0, 157.0, 0.0, kPi };
    for (float heading : { 0.f, 0.7f, 1.57f, 3.0f, -2.0f })
    {
        const Anchor a = anchorFor(300.0, -200.0, 12.0, heading, root);
        const WorldPoint back = toWorld(a, root.x, root.y, root.z);
        EXPECT_NEAR(back.mX, 300.0, 1e-9) << heading;
        EXPECT_NEAR(back.mY, -200.0, 1e-9) << heading;
        EXPECT_NEAR(back.mZ, 12.0, 1e-9) << heading;
        EXPECT_EQ(a.mHeading, heading);
    }
    // facing north (heading 0) the killer stands 157 units south of his victim
    const Anchor north = anchorFor(0.0, 157.0, 0.0, 0.f, root);
    EXPECT_NEAR(north.mX, 0.0, 1e-9);
    EXPECT_NEAR(north.mY, 0.0, 1e-9);
    // a point to the killer's right is east when he faces north, south when he faces east
    EXPECT_NEAR(toWorld(north, 10.0, 0.0, 0.0).mX, 10.0, 1e-9);
    Anchor east;
    east.mHeading = static_cast<float>(kPi / 2);
    EXPECT_NEAR(toWorld(east, 10.0, 0.0, 0.0).mY, -10.0, 1e-6);
    EXPECT_NEAR(toWorld(east, 0.0, 10.0, 0.0).mX, 10.0, 1e-6);
}

TEST(SkyrimKillMoveRules, the_victims_heading_is_the_killers_minus_the_clips_turn_wrapped)
{
    EXPECT_NEAR(victimHeading(1.0f, 0.0), 1.0f, 1e-6f);
    EXPECT_NEAR(std::fabs(victimHeading(0.f, kPi)), static_cast<float>(kPi), 1e-5f);
    EXPECT_NEAR(victimHeading(0.5f, 0.25), 0.25f, 1e-6f);
    EXPECT_NEAR(victimHeading(3.0f, -1.0), 4.0f - 2.f * static_cast<float>(kPi), 1e-5f);
    EXPECT_EQ(victimHeading(1.f, std::nan("")), 0.f);
}

// ---- the state machine --------------------------------------------------------------------------------------------

TEST(SkyrimKillMove, disabled_or_ineligible_or_unchosen_blows_do_nothing)
{
    Rig rig;
    rig.mMoves.setEnabled(false);
    EXPECT_FALSE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    rig.mMoves.setEnabled(true);
    Candidate c = lethalSwordBlow();
    c.mWeaponAnimType = 3;
    EXPECT_FALSE(rig.mMoves.offer(c, victimHandle()));
    rig.mMoves.setChance(0.f);
    EXPECT_FALSE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    EXPECT_FALSE(rig.mMoves.offer(lethalSwordBlow(), ActorHandle()));
    EXPECT_FALSE(rig.mMoves.active());
    EXPECT_TRUE(rig.mHolds.empty());
    EXPECT_EQ(rig.mMoves.started(), 0u);
}

TEST(SkyrimKillMove, a_chosen_blow_holds_the_victim_and_plays_both_sides_when_the_clip_is_ready)
{
    Rig rig;
    ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    EXPECT_EQ(rig.mMoves.phase(), Phase::Asking);
    ASSERT_EQ(rig.mHolds.size(), 1u);
    EXPECT_EQ(rig.mHolds[0], std::make_pair(WorldId(55), true));
    // a second offer while one runs is refused
    EXPECT_FALSE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    rig.mMoves.update(0.1f, rig.mCore);
    EXPECT_EQ(rig.mMoves.phase(), Phase::Playing);
    EXPECT_EQ(rig.mMoves.playerClip(), kKillerClip);
    ASSERT_EQ(rig.mCore.mPlays.size(), 1u);
    EXPECT_EQ(rig.mCore.mPlays[0].first, WorldId(55));
    EXPECT_EQ(rig.mCore.mPlays[0].second, std::string(kVictimClip));
    // the player stands north-facing (the victim is north of him), the victim's node 60 in front of the anchor
    EXPECT_NEAR(rig.mCore.mCarry.mHeading, 0.f, 1e-6f);
    EXPECT_NEAR(rig.mCore.mCarry.mY, 100.0 - 60.0, 1e-6);
    EXPECT_NEAR(rig.mCore.mCarry.mZ, 5.0, 1e-9);
    EXPECT_GE(rig.mCore.mMovements, 1);
    EXPECT_NEAR(std::fabs(rig.mCore.mHeading), static_cast<float>(kPi), 1e-5f); // victim faces back at the killer
}

TEST(SkyrimKillMove, the_victim_dies_and_the_player_is_let_go_at_the_clips_end_pose)
{
    Rig rig;
    ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    rig.mMoves.update(0.1f, rig.mCore);
    rig.mMoves.update(1.0f, rig.mCore);
    EXPECT_TRUE(rig.mMoves.active());
    EXPECT_TRUE(rig.mFinished.empty());
    EXPECT_EQ(rig.mCore.mReleases, 0);
    rig.mMoves.update(1.0f, rig.mCore); // 2.1 s of a 2.0 s clip
    EXPECT_FALSE(rig.mMoves.active());
    EXPECT_TRUE(rig.mMoves.playerClip().empty());
    ASSERT_EQ(rig.mFinished.size(), 1u);
    EXPECT_EQ(rig.mFinished[0], victimHandle());
    ASSERT_EQ(rig.mCore.mReleases, 1);
    // released at the anchor composed with the clip's end pose (10 right, 20 forward of a north-facing killer)
    EXPECT_NEAR(rig.mCore.mRelease.mX, 10.0, 1e-6);
    EXPECT_NEAR(rig.mCore.mRelease.mY, 40.0 + 20.0, 1e-6);
    EXPECT_EQ(rig.mMoves.started(), 1u);
    EXPECT_EQ(rig.mMoves.finished(), 1u);
    // and a new one can start
    EXPECT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
}

TEST(SkyrimKillMove, a_clip_that_is_not_ready_is_waited_for_and_then_the_victim_just_dies)
{
    Rig rig;
    rig.mCore.mStatus = PairedStatus::NotReady;
    ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    for (int i = 0; i < 5; ++i)
        rig.mMoves.update(0.5f, rig.mCore);
    EXPECT_EQ(rig.mMoves.phase(), Phase::Asking);
    EXPECT_TRUE(rig.mFinished.empty());
    for (int i = 0; i < 2; ++i)
        rig.mMoves.update(0.5f, rig.mCore);
    EXPECT_FALSE(rig.mMoves.active());
    EXPECT_EQ(rig.mFinished.size(), 1u);
    EXPECT_EQ(rig.mCore.mCarries, 0) << "the player was never taken";
    EXPECT_EQ(rig.mCore.mReleases, 0);
    EXPECT_TRUE(rig.mCore.mPlays.empty());
}

TEST(SkyrimKillMove, a_failed_clip_or_one_with_no_second_root_finishes_at_once)
{
    {
        Rig rig;
        rig.mCore.mStatus = PairedStatus::Failed;
        ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
        rig.mMoves.update(0.1f, rig.mCore);
        EXPECT_FALSE(rig.mMoves.active());
        EXPECT_EQ(rig.mFinished.size(), 1u);
    }
    {
        Rig rig;
        rig.mCore.mInfo.hasSecond = false;
        ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
        rig.mMoves.update(0.1f, rig.mCore);
        EXPECT_FALSE(rig.mMoves.active());
        EXPECT_EQ(rig.mFinished.size(), 1u);
        EXPECT_TRUE(rig.mCore.mPlays.empty());
    }
}

TEST(SkyrimKillMove, a_victim_that_vanishes_ends_the_move_and_lets_the_player_go)
{
    Rig rig;
    ASSERT_TRUE(rig.mMoves.offer(lethalSwordBlow(), victimHandle()));
    rig.mMoves.update(0.1f, rig.mCore);
    ASSERT_EQ(rig.mMoves.phase(), Phase::Playing);
    rig.mCore.mVictimThere = false;
    rig.mMoves.update(0.1f, rig.mCore);
    EXPECT_FALSE(rig.mMoves.active());
    EXPECT_EQ(rig.mCore.mReleases, 1);
    EXPECT_EQ(rig.mFinished.size(), 1u);
}

TEST(SkyrimKillMove, the_same_seed_makes_the_same_choices_and_a_chance_of_a_quarter_picks_about_a_quarter)
{
    int chosenCount = 0;
    std::vector<bool> first;
    for (int run = 0; run < 2; ++run)
    {
        Rig rig;
        rig.mMoves.setChance(0.25f);
        rig.mMoves.setSeed(99);
        std::vector<bool> got;
        for (int i = 0; i < 400; ++i)
        {
            const bool yes = rig.mMoves.offer(lethalSwordBlow(), victimHandle());
            got.push_back(yes);
            if (yes)
            {
                rig.mMoves.update(0.1f, rig.mCore);
                rig.mMoves.update(3.0f, rig.mCore); // ends it
            }
        }
        if (run == 0)
        {
            first = got;
            for (bool b : got)
                chosenCount += b ? 1 : 0;
        }
        else
        {
            EXPECT_EQ(got, first);
        }
    }
    EXPECT_NEAR(chosenCount / 400.0, 0.25, 0.07);
}

// ---- the combat hook ----------------------------------------------------------------------------------------------

TEST_F(KillMoveCombat, a_lethal_blow_the_hook_accepts_leaves_the_victim_at_one_health_and_holds_it)
{
    mData.mProfiles[kFighterBase].mHealthMax = 3.f;
    mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
    int asked = 0;
    SkyrimCombat::KillMoveQuery seen;
    mCombat.setKillMoveHook([&](const SkyrimCombat::KillMoveQuery& q) {
        ++asked;
        seen = q;
        return true;
    });
    run(5);
    playerHits();
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(seen.mVictim, mA);
    EXPECT_EQ(seen.mAnimType, static_cast<std::uint8_t>(WeaponAnimType::OneHandSword));
    EXPECT_FALSE(seen.mCreature);
    EXPECT_TRUE(mCombat.killMoveActive());
    EXPECT_TRUE(mWriter.mDeaths.empty());
    ASSERT_FALSE(mWriter.mDamage.empty());
    EXPECT_FLOAT_EQ(mWriter.mDamage.back().healthAfter, 1.f);
    SkyrimCombat::FighterView view;
    ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
    EXPECT_FALSE(view.mDead);
    EXPECT_FLOAT_EQ(view.mHealth, 1.f);
}

TEST_F(KillMoveCombat, neither_the_victim_nor_the_player_takes_a_blow_while_the_move_plays_and_the_victim_dies_at_its_end)
{
    mData.mProfiles[kFighterBase].mHealthMax = 3.f;
    mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
    mCombat.setKillMoveHook([](const SkyrimCombat::KillMoveQuery&) { return true; });
    run(5);
    playerHits();
    ASSERT_TRUE(mCombat.killMoveActive());
    const std::size_t damaged = mWriter.mDamage.size();
    run(30); // the player's swing recovers
    mWriter.mSweeps.clear();
    playerHits(); // a second blow on the held victim: nothing happens
    EXPECT_EQ(mWriter.mDamage.size(), damaged);
    EXPECT_TRUE(mWriter.mDeaths.empty());
    mCombat.killMoveFinish(mA);
    run(1);
    EXPECT_FALSE(mCombat.killMoveActive());
    ASSERT_EQ(mWriter.mDeaths.size(), 1u);
    EXPECT_EQ(mWriter.mDeaths[0], mA);
}

TEST_F(KillMoveCombat, a_hook_that_declines_or_is_missing_changes_nothing)
{
    mData.mProfiles[kFighterBase].mHealthMax = 3.f;
    mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
    mCombat.setKillMoveHook([](const SkyrimCombat::KillMoveQuery&) { return false; });
    run(5);
    playerHits();
    EXPECT_FALSE(mCombat.killMoveActive());
    EXPECT_EQ(mWriter.mDeaths.size(), 1u);
}

TEST_F(KillMoveCombat, a_blow_that_does_not_kill_never_asks_the_hook)
{
    mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
    int asked = 0;
    mCombat.setKillMoveHook([&](const SkyrimCombat::KillMoveQuery&) {
        ++asked;
        return true;
    });
    run(5);
    playerHits(); // 100 health, one blow of 7
    EXPECT_EQ(asked, 0);
    EXPECT_FALSE(mCombat.killMoveActive());
}

TEST_F(KillMoveCombat, an_essential_actor_collapses_as_before_and_does_not_ask)
{
    mCombat.setEssentialBleedout(true);
    mData.mProfiles[kFighterBase].mHealthMax = 3.f;
    mData.mProfiles[kFighterBase].mEssential = true;
    mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
    int asked = 0;
    mCombat.setKillMoveHook([&](const SkyrimCombat::KillMoveQuery&) {
        ++asked;
        return true;
    });
    run(5);
    playerHits();
    EXPECT_EQ(asked, 0);
    EXPECT_EQ(mBleedouts.size(), 1u);
}

TEST_F(KillMoveCombat, finishing_a_move_that_is_not_running_does_nothing)
{
    mCombat.killMoveFinish(mA);
    run(2);
    EXPECT_FALSE(mCombat.killMoveActive());
    EXPECT_TRUE(mWriter.mDeaths.empty());
}
