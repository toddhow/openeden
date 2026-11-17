// Task 0192 (combat on many actors): the crowd test rules of the Skyrim pack (game_skyrimse/combat_crowd.hpp and the
// `combat test crowd` / `combat test duels` rules of SkyrimCombat). The pure choice is tested on hand-placed actors;
// the rules through the contract with a fake data source and a recording writer, as the melee tests do. Numbers: the
// sword reaches 141 units, a body radius is 22, so a hostile attacks the player from up to 141 + 22 - 4 = 159 units.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_crowd.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kBase = 100;

    game_skyrimse::Crowd::Candidate at(WorldId ref, std::uint32_t session, double x, double y)
    {
        game_skyrimse::Crowd::Candidate c;
        c.mHandle = { ref, session };
        c.mX = x;
        c.mY = y;
        return c;
    }

    using Candidates = std::vector<game_skyrimse::Crowd::Candidate>;

    // ---- the pure choice --------------------------------------------------------------------------------------------

    TEST(CombatCrowdChoose, theNearestActorsToThePlayerAreHostileNearestFirst)
    {
        Candidates c = { at(5, 1, 300, 0), at(1, 1, 100, 0), at(3, 1, 0, 200), at(2, 1, -150, 0) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 3, 0);
        ASSERT_EQ(choice.mHostile.size(), 3u);
        EXPECT_EQ(choice.mHostile[0].reference, 1u);
        EXPECT_EQ(choice.mHostile[1].reference, 2u);
        EXPECT_EQ(choice.mHostile[2].reference, 3u);
        EXPECT_TRUE(choice.mOpponents.empty());
    }

    TEST(CombatCrowdChoose, equalDistancesBreakByPlacementThenSession)
    {
        Candidates c = { at(9, 2, 100, 0), at(9, 1, 0, 100), at(4, 1, -100, 0) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 3, 0);
        ASSERT_EQ(choice.mHostile.size(), 3u);
        EXPECT_EQ(choice.mHostile[0].reference, 4u);
        EXPECT_EQ(choice.mHostile[1].session, 1u);
        EXPECT_EQ(choice.mHostile[2].session, 2u);
    }

    TEST(CombatCrowdChoose, anActorBeyondTheCrowdRadiusNeverJoins)
    {
        Candidates c = { at(1, 1, game_skyrimse::Crowd::kMaxDistance + 1.0, 0),
            at(2, 1, game_skyrimse::Crowd::kMaxDistance - 1.0, 0) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 5, 0);
        ASSERT_EQ(choice.mHostile.size(), 1u);
        EXPECT_EQ(choice.mHostile[0].reference, 2u);
    }

    TEST(CombatCrowdChoose, duelistsArePairedWithTheirNearestNeighbourNotTheNextInTheRanking)
    {
        // a = 1 (100, 0), b = 2 (130, 0), c = 3 (0, 110), d = 4 (0, 140): a and b side by side, c and d side by side.
        // Ranked by distance from the player at the origin: a, c, b, d. Pairing consecutive ones would join a with c.
        Candidates c = { at(1, 1, 100, 0), at(2, 1, 130, 0), at(3, 1, 0, 110), at(4, 1, 0, 140) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 0, 2);
        ASSERT_EQ(choice.mOpponents.size(), 4u); // both directions
        const auto opponentOf = [&](WorldId ref) {
            for (const auto& pair : choice.mOpponents)
                if (pair.first.reference == ref)
                    return pair.second.reference;
            return WorldId{ 0 };
        };
        EXPECT_EQ(opponentOf(1), 2u);
        EXPECT_EQ(opponentOf(2), 1u);
        EXPECT_EQ(opponentOf(3), 4u);
        EXPECT_EQ(opponentOf(4), 3u);
    }

    TEST(CombatCrowdChoose, hostileActorsComeFirstAndDuelistsAreTheNextNearest)
    {
        Candidates c = { at(1, 1, 50, 0), at(2, 1, 400, 0), at(3, 1, 450, 0), at(4, 1, 2000, 0), at(5, 1, 2040, 0) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 1, 1);
        ASSERT_EQ(choice.mHostile.size(), 1u);
        EXPECT_EQ(choice.mHostile[0].reference, 1u);
        ASSERT_EQ(choice.mOpponents.size(), 2u);
        EXPECT_EQ(choice.mOpponents[0].first.reference, 2u);
        EXPECT_EQ(choice.mOpponents[0].second.reference, 3u);
    }

    TEST(CombatCrowdChoose, anOddDuelistHasNoOpponentAndCountsAreCutToWhatExists)
    {
        Candidates c = { at(1, 1, 10, 0), at(2, 1, 20, 0), at(3, 1, 30, 0) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 0, 5);
        EXPECT_EQ(choice.mOpponents.size(), 2u); // one pair; the third actor is left out
        const auto many = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 99, 0);
        EXPECT_EQ(many.mHostile.size(), 3u);
    }

    TEST(CombatCrowdChoose, badInputIsSkipped)
    {
        const double nan = std::nan("");
        Candidates c = { at(1, 1, nan, 0), at(2, 0, 10, 0), at(3, 1, 20, 0), at(4, 1, 0, nan) };
        const auto choice = game_skyrimse::Crowd::choose(c, 0.0, 0.0, 9, 0);
        ASSERT_EQ(choice.mHostile.size(), 1u);
        EXPECT_EQ(choice.mHostile[0].reference, 3u);
        EXPECT_TRUE(game_skyrimse::Crowd::choose(c, nan, 0.0, 9, 9).mHostile.empty());
    }

    TEST(CombatCrowdChoose, theCountsAreBoundedAndTheOrderOfTheInputDoesNotMatter)
    {
        Candidates base;
        for (WorldId i = 1; i <= 400; ++i)
            base.push_back(at(i, 1, static_cast<double>(i % 17) * 40.0, static_cast<double>(i % 23) * 30.0));
        Candidates a = base;
        const auto big = game_skyrimse::Crowd::choose(a, 0.0, 0.0, 1000000, 1000000);
        EXPECT_LE(big.mHostile.size(), game_skyrimse::Crowd::kMaxPicked);
        EXPECT_LE(big.mOpponents.size(), game_skyrimse::Crowd::kMaxPicked);
        Candidates b = base;
        std::mt19937 rng(7);
        std::shuffle(b.begin(), b.end(), rng);
        a = base;
        const auto first = game_skyrimse::Crowd::choose(a, 3.0, 4.0, 20, 5);
        const auto second = game_skyrimse::Crowd::choose(b, 3.0, 4.0, 20, 5);
        ASSERT_EQ(first.mHostile.size(), second.mHostile.size());
        for (std::size_t i = 0; i < first.mHostile.size(); ++i)
            EXPECT_TRUE(first.mHostile[i] == second.mHostile[i]);
        ASSERT_EQ(first.mOpponents.size(), second.mOpponents.size());
        for (std::size_t i = 0; i < first.mOpponents.size(); ++i)
        {
            EXPECT_TRUE(first.mOpponents[i].first == second.mOpponents[i].first);
            EXPECT_TRUE(first.mOpponents[i].second == second.mOpponents[i].second);
        }
    }

    // ---- the rules through the contract -----------------------------------------------------------------------------

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
            mFighter.mHealthMax = 100.f;
            mFighter.mStaminaMax = 100.f;
            mFighter.mSkills[0] = 50; // multiplier 1.25: 7 x 1.25 = 8.75 a hit
            mPlayer.mHealthMax = 1000.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
            mBow.mCommon.mEditorId = "IronBow";
            mBow.mHasData = true;
            mBow.mDamage = 10;
            mBow.mWeight = 5.f;
            mBow.mHasDnam = true;
            mBow.mAnimType = WeaponAnimType::Bow;
            mBow.mSpeed = 1.f;
            mBow.mReach = 1.f;
            mBow.mSkillValue = kFirstSkillActorValue + 2;
            mBow.mMaxRange = 2500.f;
            mAmmo.mCommon.mEditorId = "IronArrow";
            mAmmo.mHasData = true;
            mAmmo.mDamage = 8.f;
            mAmmo.mProjectile = 77;
            mProj.mCommon.mEditorId = "IronArrowProjectile";
            mProj.mHasData = true;
            mProj.mType = ProjectileRecord::kTypeArrow;
            mProj.mSpeed = 6000.f;
            mProj.mRange = 15000.f;
            mProj.mModel = "Weapons\\Iron\\IronArrowProjectile.nif";
        }
        bool fighter(WorldId, FighterProfile& out) override
        {
            out = mFighter;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            if (editorId == "ironsword")
                return &mSword;
            return editorId == "ironbow" ? &mBow : nullptr;
        }
        CombatSettings settings() override { return {}; }

        const AmmoRecord* ammoByEditorId(std::string_view editorId) override
        {
            return editorId == "ironarrow" ? &mAmmo : nullptr;
        }
        const ProjectileRecord* projectileOf(const AmmoRecord&) override { return &mProj; }

        WeaponRecord mSword;
        WeaponRecord mBow;
        AmmoRecord mAmmo;
        ProjectileRecord mProj;
        FighterProfile mFighter;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool setClipSpeed(CombatSubject, float) override { return true; }
        bool clipEventTime(std::string_view, bool, ClipEventKind, float& out) override
        {
            out = 1.66667f;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunches.push_back({ owner, request.token });
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
        bool faceHeading(CombatSubject subject, float heading) override
        {
            mFacings.push_back({ subject, heading });
            return true;
        }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = ClipTiming{ true, 0.367f, 1.287f };
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
            ClipSlot mSlot;
            std::uint32_t mToken;
        };
        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, std::uint32_t>> mLaunches; ///< owner, token
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::vector<std::pair<CombatSubject, float>> mFacings;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;

        [[nodiscard]] std::size_t attackClipsOf(WorldId reference) const
        {
            return static_cast<std::size_t>(std::count_if(mClips.begin(), mClips.end(), [&](const Clip& c) {
                return !c.mSubject.player && c.mSubject.actor.reference == reference && c.mSlot == ClipSlot::Attack;
            }));
        }
    };

    ActorInfo info(WorldId reference, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = kBase;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Attack;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    SweepHit hitOnActor(WorldId reference, float distance)
    {
        SweepHit h;
        h.target = SweepTarget::Actor;
        h.actor = { reference, 1 };
        h.distance = distance;
        return h;
    }

    SweepHit hitOnPlayer(float distance)
    {
        SweepHit h;
        h.target = SweepTarget::Player;
        h.distance = distance;
        return h;
    }

    struct CombatCrowd : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 0.f, 1000.f, 1000.f };
        }
        void add(WorldId reference, double x, double y)
        {
            mCombat.onActorAdded(info(reference, "A" + std::to_string(reference)));
            mWriter.mStates[reference] = { x, y, 0.0, 0.f, 0.f, 0.f };
        }
        void run(int updates, float dt = 0.1f)
        {
            for (int i = 0; i < updates; ++i)
                mCombat.combatUpdate(dt, mWriter);
        }
        // A hit event, one update to turn it into a sweep request, the result, one update to resolve it.
        void swing(CombatSubject attacker, std::uint32_t token, std::vector<SweepHit> hits)
        {
            mCombat.onClipEvent(hitEvent(attacker, token));
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.onSweepResult(attacker, token, hits.data(), hits.size());
            mCombat.combatUpdate(0.016f, mWriter);
        }
    };

    TEST_F(CombatCrowd, theNearestActorsAttackThePlayerAndTheFarOnesDoNot)
    {
        add(1, 50, 0); // 50 from the player: in reach
        add(2, 0, 100); // 100: in reach
        add(3, 150, 0); // 150: in reach (the limit is 159)
        add(4, 500, 0); // too far to swing
        mCombat.setTestCrowd(3, 0, 0.f);
        run(3);
        EXPECT_EQ(mCombat.hostiles(), 3u);
        EXPECT_EQ(mWriter.attackClipsOf(1), 1u);
        EXPECT_EQ(mWriter.attackClipsOf(2), 1u);
        EXPECT_EQ(mWriter.attackClipsOf(3), 1u);
        EXPECT_EQ(mWriter.attackClipsOf(4), 0u);
    }

    TEST_F(CombatCrowd, theCrowdIsChosenOnceAtTheDelayAndLaterActorsDoNotJoin)
    {
        add(1, 50, 0);
        mCombat.setTestCrowd(5, 0, 1.f);
        run(5); // 0.5 s of combat clock: before the delay
        EXPECT_EQ(mCombat.hostiles(), 0u);
        EXPECT_EQ(mWriter.attackClipsOf(1), 0u);
        run(7);
        EXPECT_EQ(mCombat.hostiles(), 1u);
        add(2, 60, 0); // arrives after the choice
        run(40);
        EXPECT_EQ(mCombat.hostiles(), 1u);
        EXPECT_EQ(mWriter.attackClipsOf(2), 0u);
    }

    TEST_F(CombatCrowd, twoDuelistsFarFromThePlayerSwingAtEachOtherAndFaceEachOther)
    {
        add(1, 1000, 0);
        add(2, 1100, 0); // 100 apart: in reach of each other, a kilometre from the player
        mCombat.setTestCrowd(0, 1, 0.f);
        run(3);
        EXPECT_EQ(mCombat.hostiles(), 2u);
        EXPECT_EQ(mCombat.duelists(), 2u);
        EXPECT_EQ(mWriter.attackClipsOf(1), 1u);
        EXPECT_EQ(mWriter.attackClipsOf(2), 1u);
        bool oneFacesEast = false;
        bool twoFacesWest = false;
        for (const auto& facing : mWriter.mFacings)
        {
            if (!facing.first.player && facing.first.actor.reference == 1)
                oneFacesEast = std::fabs(facing.second - 1.5707963f) < 1e-4f; // atan2(dx 100, dy 0)
            if (!facing.first.player && facing.first.actor.reference == 2)
                twoFacesWest = std::fabs(facing.second + 1.5707963f) < 1e-4f;
        }
        EXPECT_TRUE(oneFacesEast);
        EXPECT_TRUE(twoFacesWest);
    }

    TEST_F(CombatCrowd, aDuelistOutOfReachOfItsOpponentDoesNotSwingEvenBesideThePlayer)
    {
        add(1, 50, 0); // beside the player...
        add(2, 400, 0); // ...but its opponent is 350 away
        mCombat.setTestCrowd(0, 1, 0.f);
        run(3);
        EXPECT_EQ(mWriter.attackClipsOf(1), 0u);
        EXPECT_EQ(mWriter.attackClipsOf(2), 0u);
    }

    TEST_F(CombatCrowd, aCrowdMemberHurtsThePlayerNotItsFriendStandingInTheWay)
    {
        add(1, 100, 0); // the attacker
        add(2, 50, 0); // a friend between it and the player: the nearest body in the arc
        mCombat.setTestCrowd(2, 0, 0.f);
        run(2);
        swing(CombatSubject::of({ 1, 1 }), 5, { hitOnActor(2, 20.f), hitOnPlayer(60.f) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(mWriter.mDamage[0].victimIsPlayer);
        SkyrimCombat::FighterView friendView;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of({ 2, 1 }), friendView));
        EXPECT_FLOAT_EQ(friendView.mHealth, friendView.mHealthMax);
    }

    TEST_F(CombatCrowd, aDuelistHurtsItsOpponentNotABystanderInTheArc)
    {
        add(1, 1000, 0);
        add(2, 1100, 0);
        add(3, 1050, 0);
        // The nearest-neighbour rule pairs 1 with 3 (50 apart) and leaves 2 out: who swings tells the pairing, and the
        // one that does not is the bystander the hit list puts first.
        mCombat.setTestCrowd(0, 1, 0.f);
        run(2);
        ASSERT_EQ(mCombat.duelists(), 2u);
        WorldId attacker = 0;
        WorldId opponent = 0;
        WorldId bystander = 0;
        for (const WorldId id : { WorldId{ 1 }, WorldId{ 2 }, WorldId{ 3 } })
        {
            if (mWriter.attackClipsOf(id) == 0)
                bystander = id;
            else if (attacker == 0)
                attacker = id;
            else
                opponent = id;
        }
        ASSERT_NE(attacker, 0u);
        ASSERT_NE(opponent, 0u);
        ASSERT_NE(bystander, 0u);
        swing(CombatSubject::of({ attacker, 1 }), 9, { hitOnActor(bystander, 10.f), hitOnActor(opponent, 50.f) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(mWriter.mDamage[0].victim == ActorHandle(opponent, 1));
    }

    TEST_F(CombatCrowd, withoutACrowdTestTheNearestBodyInTheArcStillTakesTheHit)
    {
        add(1, 100, 0);
        add(2, 50, 0);
        mCombat.setTestHostile("a1"); // the old single test rule
        run(2);
        swing(CombatSubject::of({ 1, 1 }), 5, { hitOnActor(2, 20.f), hitOnPlayer(60.f) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FALSE(mWriter.mDamage[0].victimIsPlayer);
        EXPECT_TRUE(mWriter.mDamage[0].victim == ActorHandle(2, 1));
    }

    // Every fighter of a crowd that swings in the same frame reaches its HitFrame in the same frame, so the number of
    // hits that frame is the number of fighters. The bridge plays 64 clips and holds 64 sweep results at most, so the
    // pack must keep up with 64 (it kept 32 and dropped the rest without a word).
    struct Volley
    {
        std::size_t mSweeps = 0;
        std::size_t mDamage = 0;
        std::size_t mWarnings = 0;
    };

    Volley volley(CombatCrowd& t, int fighters)
    {
        std::vector<std::string> warnings;
        t.mCombat.setWarning([&](const std::string& line) { warnings.push_back(line); });
        for (int i = 0; i < fighters; ++i)
            t.add(static_cast<WorldId>(1000 + i), 60.0 + (i % 5) * 20.0, 0.0);
        t.mCombat.setTestCrowd(fighters, 0, 0.f);
        t.run(2); // everybody starts a swing
        for (int i = 0; i < fighters; ++i)
            t.mCombat.onClipEvent(hitEvent(CombatSubject::of({ static_cast<WorldId>(1000 + i), 1 }), 500 + i));
        t.mCombat.combatUpdate(0.016f, t.mWriter);
        Volley v;
        v.mSweeps = t.mWriter.mSweeps.size();
        for (const auto& sweep : t.mWriter.mSweeps)
        {
            const SweepHit hit = hitOnPlayer(50.f);
            t.mCombat.onSweepResult(sweep.first, sweep.second.token, &hit, 1);
        }
        t.mCombat.combatUpdate(0.016f, t.mWriter);
        v.mDamage = t.mWriter.mDamage.size();
        v.mWarnings = warnings.size();
        return v;
    }

    TEST_F(CombatCrowd, fortyFightersWhoReachTheirHitFrameTogetherAllLand)
    {
        const Volley v = volley(*this, 40);
        EXPECT_EQ(v.mSweeps, 40u);
        EXPECT_EQ(v.mDamage, 40u);
        EXPECT_EQ(v.mWarnings, 0u);
    }

    TEST_F(CombatCrowd, sixtyFourFightersAllLandAndMoreAreCountedAndReportedOnce)
    {
        const Volley v = volley(*this, 64);
        EXPECT_EQ(v.mSweeps, 64u);
        EXPECT_EQ(v.mDamage, 64u);
        EXPECT_EQ(v.mWarnings, 0u);
    }

    TEST_F(CombatCrowd, hitsBeyondTheTableAreDroppedWithOneWarningNotInSilence)
    {
        const Volley v = volley(*this, 80);
        EXPECT_EQ(v.mSweeps, static_cast<std::size_t>(game_skyrimse::kMaxConcurrentAttacks));
        EXPECT_EQ(v.mDamage, static_cast<std::size_t>(game_skyrimse::kMaxConcurrentAttacks));
        EXPECT_EQ(v.mWarnings, 1u);
    }

    // An archer of a crowd shoots at the player; an arrow that ends in a friend standing in the way hurts nobody (it
    // hit a friend in 35 percent of the arrows of a 24 archer crowd in the game, before this rule).
    struct ArrowEnd
    {
        std::size_t mDamageToFriend = 0;
        std::size_t mDamageToPlayer = 0;
    };

    ArrowEnd shootAndEndAt(CombatCrowd& t, ProjectileFate fate)
    {
        t.mCombat.setTestWeapon("IronBow");
        t.mCombat.setTestAmmo("IronArrow");
        t.add(10, 0, 0); // the archer
        t.add(20, 30, 0); // a friend in front of it
        t.mWriter.mPlayerState = { 800.0, 0.0, 0.0, 0.f, 1000.f, 1000.f };
        t.mCombat.setTestCrowd(2, 0, 0.f);
        t.run(1, 3.f); // the draw clips start
        std::uint32_t drawToken = 0;
        for (const auto& clip : t.mWriter.mClips)
            if (!clip.mSubject.player && clip.mSubject.actor.reference == 10 && clip.mSlot == ClipSlot::BowDraw)
                drawToken = clip.mToken;
        ClipEvent drawn;
        drawn.subject = CombatSubject::of({ 10, 1 });
        drawn.slot = ClipSlot::BowDraw;
        drawn.token = drawToken;
        drawn.kind = ClipEventKind::BowDrawn;
        t.mCombat.onClipEvent(drawn);
        t.run(1);
        std::uint32_t releaseToken = 0;
        for (const auto& clip : t.mWriter.mClips)
            if (!clip.mSubject.player && clip.mSubject.actor.reference == 10 && clip.mSlot == ClipSlot::BowRelease)
                releaseToken = clip.mToken;
        ClipEvent release = drawn;
        release.slot = ClipSlot::BowRelease;
        release.token = releaseToken;
        release.kind = ClipEventKind::BowRelease;
        t.mCombat.onClipEvent(release);
        t.run(1);
        ArrowEnd end;
        std::uint32_t shot = 0;
        for (const auto& launch : t.mWriter.mLaunches)
            if (launch.first.actor.reference == 10)
                shot = launch.second;
        if (shot == 0)
            return end;
        ProjectileResult r;
        r.owner = CombatSubject::of({ 10, 1 });
        r.token = shot;
        r.fate = fate;
        r.actor = { 20, 1 };
        r.flightSeconds = 0.01f;
        r.distance = 30.f;
        r.speed = 6000.f;
        t.mCombat.onProjectileResult(r);
        t.run(1);
        for (const DamageReport& d : t.mWriter.mDamage)
        {
            if (d.victimIsPlayer)
                ++end.mDamageToPlayer;
            else if (d.victim == ActorHandle(20, 1))
                ++end.mDamageToFriend;
        }
        return end;
    }

    TEST_F(CombatCrowd, anArrowOfACrowdArcherThatEndsInAFriendHurtsNobody)
    {
        const ArrowEnd end = shootAndEndAt(*this, ProjectileFate::HitActor);
        EXPECT_EQ(end.mDamageToFriend, 0u);
        EXPECT_EQ(end.mDamageToPlayer, 0u);
    }

    TEST_F(CombatCrowd, anArrowOfACrowdArcherThatEndsInThePlayerHurtsIt)
    {
        const ArrowEnd end = shootAndEndAt(*this, ProjectileFate::HitPlayer);
        EXPECT_EQ(end.mDamageToPlayer, 1u);
        EXPECT_EQ(end.mDamageToFriend, 0u);
    }

    TEST_F(CombatCrowd, whenItsOpponentIsGoneADuelistStopsSwinging)
    {
        add(1, 1000, 0);
        add(2, 1100, 0);
        mCombat.setTestCrowd(0, 1, 0.f);
        run(3);
        ASSERT_EQ(mWriter.attackClipsOf(1), 1u);
        mCombat.onActorRemoved({ 2, 1 });
        mWriter.mClips.clear();
        run(60); // 6 s: long past the 2.5 s attack period
        EXPECT_EQ(mWriter.attackClipsOf(1), 0u);
    }
}
