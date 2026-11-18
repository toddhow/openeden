// Task 0253 (dragons): the dragon in the combat rules (game_skyrimse/combat_dragon.cpp), through a fake data source and
// a recording writer. Numbers by hand:
//  - the breath spell is the shout DragonFireBreathShout01's: concentration, aimed, cost 5 a second, cast duration 2.4
//  s,
//    one health damage effect of 20 a second (VoiceDragonFire01 in the data), projectile range 2000; the shout's
//    recovery is 5 s and kBreathMinGap 6 s, the clip is 3 s long and the spell goes 0.3333 s after it starts;
//  - so one breath on a target in the cone costs it 20 x 2.4 = 48 health, a second breath starts no sooner than the
//    clip's end (3 s) plus 6 s after the first began;
//  - the dragon's unarmed reach is 130 units; a breath needs the target farther than reach + 22 - 4 + 250 = 398;
//  - the flight (ground 2 s, air 5 s) is checked in dragons.hpp's own tests; here only what the combat does with it.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/dragons.hpp>

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

    constexpr WorldId kDragonBase = 100;
    constexpr WorldId kOtherBase = 200;
    constexpr FormId kShout = 0x252C0, kSpell = 0x252C2, kEffect = 0x252C1, kProj = 0x252C6, kWord = 0x252C3;

    class DragonData final : public CombatData
    {
    public:
        DragonData()
        {
            mProj.mCommon.mId = kProj;
            mProj.mHasData = true;
            mProj.mType = 8;
            mProj.mSpeed = 1000.f;
            mProj.mRange = 2000.f;
            MagicEffectRecord e;
            e.mCommon.mId = kEffect;
            e.mCommon.mEditorId = "VoiceDragonFireBreathEffect1";
            e.mHasData = true;
            e.mArchetype = 0;
            e.mPrimaryValue = 24;
            e.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;
            e.mProjectile = kProj;
            mEffects[kEffect] = e;
            SpellRecord s;
            s.mCommon.mId = kSpell;
            s.mCommon.mEditorId = "VoiceDragonFire01";
            s.mHasData = true;
            s.mType = 11;
            s.mCost = 5;
            s.mCastType = MagicCast::Concentration;
            s.mDelivery = MagicDelivery::Aimed;
            s.mCastDuration = 2.4f;
            SpellEffect se;
            se.mEffect = kEffect;
            se.mMagnitude = 20.f;
            s.mEffects.push_back(se);
            mSpell = s;
            mShout.mCommon.mId = kShout;
            mShout.mCommon.mEditorId = "DragonFireBreathShout01";
            mShout.mHasData = true;
            mShout.mWordCount = 3;
            for (std::size_t i = 0; i < 3; ++i)
                mShout.mWords[i] = { kWord, kSpell, 5.f };
            FighterProfile dragon;
            dragon.mHealthMax = 1000.f;
            dragon.mStaminaMax = 100.f;
            dragon.mMagickaMax = 0.f; // a dragon has no magicka: its breath is a shout and costs none
            dragon.mUnarmedDamage = 30.f;
            dragon.mUnarmedReach = 130.f;
            dragon.mAi.mKnown = true;
            dragon.mAi.mConfidence = 4;
            dragon.mCreature = &dragons::dragonProfile();
            dragon.mSpells.push_back(kShout);
            mProfiles[kDragonBase] = dragon;
            FighterProfile other;
            other.mHealthMax = 100.f;
            other.mStaminaMax = 100.f;
            other.mUnarmedReach = 96.f;
            other.mAi.mKnown = true;
            mProfiles[kOtherBase] = other;
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
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override { return id == kSpell ? &mSpell : nullptr; }
        const SpellRecord* spellByEditorId(std::string_view) override { return nullptr; }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProj ? &mProj : nullptr; }
        const ShoutRecord* shoutById(FormId id) override { return id == kShout ? &mShout : nullptr; }

        SpellRecord mSpell;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        ShoutRecord mShout;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class DragonWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            float mAt;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), mNow });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            else
                mHealth[report.victim.reference] = report.healthAfter;
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
            out = { true, 0.5f, 3.f };
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
        std::size_t clipsNamed(const std::string& name) const
        {
            return static_cast<std::size_t>(
                std::count_if(mClips.begin(), mClips.end(), [&](const Clip& c) { return c.mName == name; }));
        }
        std::size_t clipsOf(ClipSlot slot) const
        {
            return static_cast<std::size_t>(
                std::count_if(mClips.begin(), mClips.end(), [&](const Clip& c) { return c.mSlot == slot; }));
        }
        float mNow = 0.f;
        std::vector<Clip> mClips;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        std::map<WorldId, float> mHealth;
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

    using Kind = SkyrimCombat::AiMoveIntent::Kind;

    struct DragonRig
    {
        DragonData mData;
        SkyrimCombat mCombat;
        DragonWriter mWriter;
        shouts::ShoutState mShouts;
        ActorHandle mD{ 10, 1 };
        std::vector<std::string> mTrace;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        float mClock = 0.f;

        void setUpRig()
        {
            mCombat.setData(&mData);
            mCombat.setCombatAi(true);
            mCombat.setShoutState(&mShouts);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 0.f, 100.f, 100.f };
        }
        void addDragon(double distance, bool on = true, bool flight = false)
        {
            dragons::Settings s;
            s.mOn = on;
            s.mFlight.mEnabled = flight;
            s.mFlight.mGroundSeconds = 2.f;
            s.mFlight.mAirSeconds = 5.f;
            mCombat.setDragons(s);
            ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
            mCombat.onActorAdded(infoOf(10, kDragonBase, "DragonX"));
            mWriter.mStates[10] = { 0.0, -distance, 0.0, 0.f, 0.f, 0.f };
        }
        void addBystander(WorldId ref, double x, double y, double z)
        {
            mCombat.onActorAdded(infoOf(ref, kOtherBase, "Bystander"));
            mWriter.mStates[ref] = { x, y, z, 0.f, 0.f, 0.f };
        }
        // One update of `dt`; the airborne dragon is moved where the Fly intents say, as the core would.
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mClock += dt;
                mWriter.mNow = mClock;
                mCombat.combatUpdate(dt, mWriter);
                std::vector<SkyrimCombat::AiMoveIntent> fresh;
                mCombat.drainAiMoves(fresh);
                for (const auto& m : fresh)
                {
                    if (m.mActor == mD && (m.mKind == Kind::Fly || m.mKind == Kind::Land))
                    {
                        mWriter.mStates[10].x = m.mX;
                        mWriter.mStates[10].y = m.mY;
                        mWriter.mStates[10].z = m.mZ;
                        if (m.mKind == Kind::Fly)
                            mWriter.mStates[10].heading = m.mHeading;
                    }
                    mMoves.push_back(m);
                }
            }
        }
        std::size_t movesOf(Kind kind) const
        {
            return static_cast<std::size_t>(std::count_if(
                mMoves.begin(), mMoves.end(), [&](const SkyrimCombat::AiMoveIntent& m) { return m.mKind == kind; }));
        }
        bool traced(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
    };

    struct CombatDragon : DragonRig, ::testing::Test
    {
        void SetUp() override { setUpRig(); }
    };

    // ---- off -------------------------------------------------------------------------------------------------------

    TEST_F(CombatDragon, withTheDragonsSettingOffNothingBreathesOrFlies)
    {
        addDragon(900.0, false, true);
        run(200);
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 0u);
        EXPECT_EQ(movesOf(Kind::Fly), 0u);
        EXPECT_EQ(movesOf(Kind::Land), 0u);
        EXPECT_EQ(mCombat.dragonCount(), 0u);
        EXPECT_EQ(mCombat.dragonBreaths(), 0u);
    }

    TEST_F(CombatDragon, aDragonsWalkingOrdersAreDirectAndAnotherActorsAreNot)
    {
        addDragon(1500.0, true, false); // beyond the breath's 2000? no: inside it, so it breathes first
        addBystander(30, 0.0, -2500.0, 0.0);
        ASSERT_TRUE(mCombat.setTestHostile("bystander"));
        mCombat.onActorAdded(infoOf(30, kOtherBase, "Bystander"));
        run(60);
        std::size_t direct = 0, nav = 0;
        for (const auto& m : mMoves)
            if (m.mKind == Kind::GoTo)
                ++(m.mDirect ? direct : nav);
        EXPECT_GE(direct, 1u);
        for (const auto& m : mMoves)
            if (m.mKind == Kind::GoTo && m.mActor.reference == 30)
            {
                EXPECT_FALSE(m.mDirect);
            }
        EXPECT_GE(nav, 1u);
    }

    // ---- the breath -------------------------------------------------------------------------------------------------

    TEST_F(CombatDragon, anEngagedDragonBreathesOnATargetBeyondItsMeleeReach)
    {
        addDragon(900.0);
        run(30);
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 1u);
        EXPECT_EQ(mCombat.dragonBreaths(), 1u);
        EXPECT_EQ(mCombat.dragonCount(), 1u);
        EXPECT_TRUE(traced("dragon breath"));
        ASSERT_FALSE(mWriter.mClips.empty());
        bool cast = false;
        for (const auto& c : mWriter.mClips)
            cast = cast || (c.mName == "ground_exhalefirebreath" && c.mSlot == ClipSlot::Cast);
        EXPECT_TRUE(cast);
    }

    TEST_F(CombatDragon, aTargetInsideMeleeReachGetsTheBiteNotTheBreath)
    {
        addDragon(120.0);
        run(60);
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 0u);
        EXPECT_GE(mWriter.clipsOf(ClipSlot::Attack) + mWriter.clipsOf(ClipSlot::PowerAttack), 1u);
    }

    TEST_F(CombatDragon, aTargetBeyondTheBreathsRangeGetsNoBreath)
    {
        addDragon(2600.0);
        run(60);
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 0u);
    }

    TEST_F(CombatDragon, theBreathCostsTheTargetItsCastDurationTimesTheEffectAndNoMagicka)
    {
        addDragon(900.0);
        run(70); // one breath: 0.33 s to the spell, 2.4 s of fire, the clip ends at 3 s
        EXPECT_NEAR(100.f - mWriter.mPlayerState.health, 48.f, 4.f);
        EXPECT_EQ(mCombat.dragonBreaths(), 1u);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mD), view));
        EXPECT_NEAR(view.mMagicka, 0.f, 1e-4f);
    }

    TEST_F(CombatDragon, theConeHurtsWhatIsInItAndNothingOutsideIt)
    {
        addDragon(900.0);
        addBystander(20, 0.0, -300.0, 120.0); // between the dragon's mouth and the player: in the cone
        addBystander(21, 900.0, -450.0, 0.0); // to the side: out of it
        addBystander(22, 0.0, -2000.0, 0.0); // behind the dragon: out of it
        run(70);
        EXPECT_NEAR(100.f - mWriter.mHealth[20], 48.f, 4.f);
        EXPECT_EQ(mWriter.mHealth.count(21), 0u);
        EXPECT_EQ(mWriter.mHealth.count(22), 0u);
        EXPECT_NEAR(100.f - mWriter.mPlayerState.health, 48.f, 4.f);
    }

    TEST_F(CombatDragon, aSecondBreathWaitsForTheClipTheRecoveryAndTheMinimumGap)
    {
        addDragon(900.0);
        run(85); // 8.5 s: the first breath began about 0.3 s in, a second may start at 3.3 + 6.0
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 1u);
        run(60); // 14.5 s
        EXPECT_EQ(mWriter.clipsNamed("ground_exhalefirebreath"), 2u);
        EXPECT_EQ(mCombat.dragonBreaths(), 2u);
    }

    TEST_F(CombatDragon, whileItBreathesTheAiMakesNoAttackOrMove)
    {
        addDragon(900.0);
        run(5);
        const std::size_t before = mWriter.clipsOf(ClipSlot::Attack) + mWriter.clipsOf(ClipSlot::PowerAttack);
        run(25);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Attack) + mWriter.clipsOf(ClipSlot::PowerAttack), before);
    }

    TEST_F(CombatDragon, theSameRunTwiceGivesTheSameTrace)
    {
        std::vector<std::string> first, second;
        {
            addDragon(900.0);
            run(100);
            first = mTrace;
        }
        DragonRig again;
        again.setUpRig();
        again.addDragon(900.0);
        again.run(100);
        second = again.mTrace;
        EXPECT_EQ(first, second);
        EXPECT_FALSE(first.empty());
    }

    // ---- the flight in the combat -----------------------------------------------------------------------------------

    TEST_F(CombatDragon, anEngagedDragonTakesOffCirclesLandsAndTheAiThenResumes)
    {
        // 300 units: beyond the bite (148), short of the breath (398), so it only walks at the player, which the fake
        // writer never lets it reach: nothing makes it busy on the ground.
        addDragon(300.0, true, true);
        run(10);
        EXPECT_EQ(movesOf(Kind::Fly), 0u) << "2 s of fighting first";
        run(15);
        ASSERT_GE(movesOf(Kind::Fly), 1u);
        EXPECT_EQ(mCombat.dragonPhase(mD), dragons::Phase::Takeoff);
        const SkyrimCombat::AiMoveIntent* firstFly = nullptr;
        for (const auto& m : mMoves)
            if (m.mKind == Kind::Fly)
            {
                firstFly = &m;
                break;
            }
        ASSERT_NE(firstFly, nullptr);
        EXPECT_TRUE(firstFly->mStart);
        EXPECT_EQ(firstFly->mClip, dragons::kClipTakeoff);
        // airborne: the AI gives it no walking orders
        const std::size_t walks = movesOf(Kind::GoTo);
        run(40);
        EXPECT_NE(mCombat.dragonPhase(mD), dragons::Phase::Ground);
        EXPECT_EQ(movesOf(Kind::GoTo), walks);
        // the cruise breathes from the air
        run(80);
        EXPECT_GE(mWriter.clipsNamed("shout_flightexhaleflame"), 1u);
        // and lands
        for (int i = 0; i < 400 && movesOf(Kind::Land) == 0; ++i)
            run(1);
        EXPECT_EQ(mCombat.dragonPhase(mD), dragons::Phase::Ground);
        EXPECT_EQ(movesOf(Kind::Land), 1u);
        run(45); // the dragon landed 900 units off: it breathes first (3 s), then the AI walks it at the player
        EXPECT_GT(movesOf(Kind::GoTo), walks) << "the AI sends it at the player again after the landing";
    }

    TEST_F(CombatDragon, withFlightOffTheDragonStaysOnTheGround)
    {
        addDragon(300.0, true, false);
        run(400);
        EXPECT_EQ(movesOf(Kind::Fly), 0u);
        EXPECT_EQ(mCombat.dragonPhase(mD), dragons::Phase::Ground);
    }

    TEST_F(CombatDragon, aDragonThatDiesInTheAirIsTakenOffItsPoseWhereItWas)
    {
        addDragon(300.0, true, true);
        run(60);
        ASSERT_NE(mCombat.dragonPhase(mD), dragons::Phase::Ground);
        const double z = mWriter.mStates[10].z;
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(2);
        ASSERT_EQ(movesOf(Kind::Land), 1u);
        const SkyrimCombat::AiMoveIntent* land = nullptr;
        for (const auto& m : mMoves)
            if (m.mKind == Kind::Land)
                land = &m;
        ASSERT_NE(land, nullptr);
        EXPECT_NEAR(land->mZ, z, 1.0);
        EXPECT_EQ(mCombat.dragonPhase(mD), dragons::Phase::Ground);
    }

    TEST_F(CombatDragon, theTestDamageScaleScalesTheBreathAndZeroMakesItHarmless)
    {
        dragons::Settings s;
        s.mOn = true;
        s.mTestDamageScale = 0.5f;
        mCombat.setDragons(s);
        ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
        mCombat.onActorAdded(infoOf(10, kDragonBase, "DragonX"));
        mWriter.mStates[10] = { 0.0, -900.0, 0.0, 0.f, 0.f, 0.f };
        run(70);
        EXPECT_NEAR(100.f - mWriter.mPlayerState.health, 24.f, 3.f);
        EXPECT_EQ(mCombat.dragonBreaths(), 1u);
    }

    TEST_F(CombatDragon, theTestHealthSettingStartsTheDragonWeak)
    {
        dragons::Settings s;
        s.mOn = true;
        s.mTestHealth = 40.f;
        mCombat.setDragons(s);
        ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
        mCombat.onActorAdded(infoOf(10, kDragonBase, "DragonX"));
        mWriter.mStates[10] = { 0.0, -900.0, 0.0, 0.f, 0.f, 0.f };
        run(10);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mD), view));
        EXPECT_NEAR(view.mHealth, 40.f, 0.5f);
        EXPECT_FALSE(view.mDead);
    }

    TEST_F(CombatDragon, aDeadDragonPlaysItsOwnDeathClipOnceWhateverKilledIt)
    {
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(20);
        EXPECT_EQ(mWriter.clipsNamed("mtdeathintro"), 1u);
        std::size_t death = 0;
        for (const auto& c : mWriter.mClips)
            if (c.mSlot == ClipSlot::Death)
                ++death;
        EXPECT_EQ(death, 1u);
    }

    TEST_F(CombatDragon, theTestKillSettingKillsTheDragonAtItsTimeAndReportsTheDeathOnce)
    {
        dragons::Settings s;
        s.mOn = true;
        s.mTestKillAt = 2.f;
        mCombat.setDragons(s);
        ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
        mCombat.onActorAdded(infoOf(10, kDragonBase, "DragonX"));
        mWriter.mStates[10] = { 0.0, -900.0, 0.0, 0.f, 0.f, 0.f };
        run(15);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mD), view));
        EXPECT_FALSE(view.mDead);
        EXPECT_TRUE(mWriter.mDeaths.empty());
        run(10);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mD), view));
        EXPECT_TRUE(view.mDead);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
        run(60);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.clipsNamed("mtdeathintro"), 1u);
        EXPECT_EQ(mShouts.souls(), 1u) << "and its soul comes to the player";
    }

    // ---- the soul ---------------------------------------------------------------------------------------------------

    TEST_F(CombatDragon, aDeadDragonsSoulComesToThePlayerNearAfterTheDelayOnce)
    {
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(30); // 3 s
        EXPECT_EQ(mShouts.souls(), 0u);
        run(15); // 4.5 s since it died
        EXPECT_EQ(mShouts.souls(), 1u);
        EXPECT_EQ(mCombat.dragonSouls(), 1u);
        EXPECT_EQ(mShouts.noticeCount(), 1u);
        EXPECT_EQ(mShouts.noticeAt(1), "Dragon soul absorbed");
        run(100);
        EXPECT_EQ(mShouts.souls(), 1u) << "never twice";
        EXPECT_TRUE(traced("dragon soul"));
    }

    TEST_F(CombatDragon, aSoulWaitsUntilThePlayerIsWithinRange)
    {
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        mWriter.mPlayerState.x = 5000.0;
        run(80);
        EXPECT_EQ(mShouts.souls(), 0u);
        mWriter.mPlayerState.x = 100.0;
        run(5);
        EXPECT_EQ(mShouts.souls(), 1u);
    }

    TEST_F(CombatDragon, withNoShoutStateTheSoulIsCountedAndTheMessageIsNotLost)
    {
        mCombat.setShoutState(nullptr);
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(60);
        EXPECT_EQ(mCombat.dragonSouls(), 0u) << "nothing keeps it";
        EXPECT_TRUE(traced("no shout state"));
    }

    TEST(DragonShoutNotices, theShoutStateKeepsAnnouncementsInOrderAndASmallRing)
    {
        shouts::ShoutState state;
        EXPECT_EQ(state.noticeCount(), 0u);
        EXPECT_EQ(state.noticeAt(1), "");
        state.announce("one");
        state.announce("two");
        EXPECT_EQ(state.noticeCount(), 2u);
        EXPECT_EQ(state.noticeAt(1), "one");
        EXPECT_EQ(state.noticeAt(2), "two");
        EXPECT_EQ(state.noticeAt(0), "");
        EXPECT_EQ(state.noticeAt(3), "");
        for (int i = 0; i < 40; ++i)
            state.announce("n" + std::to_string(i));
        EXPECT_EQ(state.noticeCount(), 42u);
        EXPECT_EQ(state.noticeAt(1), "") << "fell out of the ring";
        EXPECT_EQ(state.noticeAt(42), "n39");
        state.announce(std::string(5000, 'x'));
        EXPECT_LE(state.noticeAt(43).size(), 256u);
        state.announce("");
        EXPECT_EQ(state.noticeCount(), 43u) << "an empty text is not an announcement";
    }

    TEST_F(CombatDragon, whenTheDragonsOwnScriptTakesTheSoulTheHandWrittenAbsorptionStepsAside)
    {
        // task 0313: the game's death sequence ends in Actor.ModActorValue("dragonsouls"); two souls would be one too many
        std::vector<openeden::sdk::game::WorldId> asked;
        mCombat.setDragonSoulByScript([&asked](openeden::sdk::game::WorldId reference) {
            asked.push_back(reference);
            return true;
        });
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(60);
        EXPECT_EQ(mShouts.souls(), 0u);
        EXPECT_EQ(mCombat.dragonSouls(), 0u);
        EXPECT_EQ(mShouts.noticeCount(), 0u);
        ASSERT_EQ(asked.size(), 1u) << "asked once, then it is settled";
        EXPECT_EQ(asked[0], mD.reference);
        EXPECT_TRUE(traced("left to its death script"));
    }

    TEST_F(CombatDragon, whenNoScriptTakesTheSoulTheOlderAbsorptionStillRuns)
    {
        mCombat.setDragonSoulByScript([](openeden::sdk::game::WorldId) { return false; });
        addDragon(900.0);
        run(5);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mD), 0.f));
        run(60);
        EXPECT_EQ(mShouts.souls(), 1u);
        EXPECT_EQ(mCombat.dragonSouls(), 1u);
    }
}
