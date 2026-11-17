// Task 0178 (milestone M-S5, stage S5-6): the Skyrim pack's bow rules (game_skyrimse/combat.hpp), tested through the
// contract 0.3 with a fake data source and a recording writer. The numbers are worked out by hand: the bow ("IronBow")
// has damage 10 and the arrow ("IronArrow", projectile "IronArrowProjectile") damage 8, so an arrow does (10 + 8) x
// min(1.5, 1 + skill / 200): skill 50 gives 22.5, skill 15 gives 19.35; armour then takes its percent off. The PROJ
// has speed 6000, range 15000 and gravity 0 (a straight shot) unless a test says otherwise.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

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

    constexpr WorldId kArcherBase = 100;
    constexpr WorldId kVictimBase = 200;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mBow.mCommon.mEditorId = "IronBow";
            mBow.mHasData = true;
            mBow.mDamage = 10;
            mBow.mWeight = 5.f;
            mBow.mHasDnam = true;
            mBow.mAnimType = WeaponAnimType::Bow;
            mBow.mSpeed = 1.f;
            mBow.mReach = 1.f;
            mBow.mSkillValue = kFirstSkillActorValue + 2; // Archery: skill slot 2
            mBow.mStagger = 0.5f;
            mBow.mMinRange = 500.f;
            mBow.mMaxRange = 2500.f;

            mAmmo.mCommon.mEditorId = "IronArrow";
            mAmmo.mHasData = true;
            mAmmo.mDamage = 8.f;
            mAmmo.mProjectile = 77;

            mProj.mCommon.mEditorId = "IronArrowProjectile";
            mProj.mHasData = true;
            mProj.mType = ProjectileRecord::kTypeArrow;
            mProj.mGravity = 0.f;
            mProj.mSpeed = 6000.f;
            mProj.mRange = 15000.f;
            mProj.mModel = "Weapons\\Iron\\IronArrowProjectile.nif";

            FighterProfile archer;
            archer.mHealthMax = 100.f;
            archer.mStaminaMax = 100.f;
            archer.mSkills[2] = 50; // multiplier 1.25
            mProfiles[kArcherBase] = archer;

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15); // multiplier 1.075
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
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironbow" ? &mBow : nullptr;
        }
        const AmmoRecord* ammoByEditorId(std::string_view editorId) override
        {
            return editorId == "ironarrow" ? &mAmmo : nullptr;
        }
        const ProjectileRecord* projectileOf(const AmmoRecord& ammo) override
        {
            return ammo.mProjectile == 77 ? &mProj : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }

        WeaponRecord mBow;
        AmmoRecord mAmmo;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        struct Launch
        {
            CombatSubject mOwner;
            ProjectileRequest mRequest;
            std::string mModel;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool setClipSpeed(CombatSubject subject, float multiplier) override
        {
            mSpeeds.push_back({ subject, multiplier });
            return true;
        }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (!mTimesReady || kind != ClipEventKind::BowDrawn || clip != "Bow_DrawLight")
                return false;
            out = mDrawnTime;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            if (mRefuseLaunch)
                return false;
            mLaunches.push_back({ owner, request, std::string(request.model) });
            mLaunches.back().mRequest.model = mLaunches.back().mModel;
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override
        {
            ++mSweeps;
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

        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, float>> mSpeeds;
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::vector<std::pair<CombatSubject, float>> mFacings;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        bool mTimesReady = true;
        float mDrawnTime = 1.66667f;
        bool mRefuseLaunch = false;
        int mSweeps = 0;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent bowEvent(CombatSubject subject, std::uint32_t token, ClipSlot slot, ClipEventKind kind)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = kind;
        return e;
    }

    // The archer "Hulda" (handle 10) at the origin facing +X; the victim NPC (handle 20) and the player at x = 800.
    struct CombatBow : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setData(&mData);
            ASSERT_TRUE(mCombat.setTestWeapon("IronBow"));
            ASSERT_TRUE(mCombat.setTestAmmo("IronArrow"));
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
            mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
            SubjectState archer;
            archer.x = 0.0;
            archer.y = 0.0;
            archer.z = 0.0;
            archer.heading = kPi / 2.f;
            mWriter.mStates[10] = archer;
            SubjectState victim = archer;
            victim.x = 800.0;
            mWriter.mStates[20] = victim;
            mWriter.mPlayerState = victim;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }

        void update(float seconds = 0.016f) { mCombat.combatUpdate(seconds, mWriter); }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }

        // The hostile archer's whole cycle up to and including the launch; returns the launch (the last one).
        const FakeWriter::Launch* shoot(float waitBefore = 3.f)
        {
            const std::size_t clipsBefore = mWriter.mClips.size();
            const std::size_t launchesBefore = mWriter.mLaunches.size();
            update(waitBefore); // the draw starts
            if (mWriter.mClips.size() != clipsBefore + 1)
                return nullptr;
            const std::uint32_t drawToken = mWriter.mClips.back().mToken;
            mCombat.onClipEvent(bowEvent(CombatSubject::of(mA), drawToken, ClipSlot::BowDraw, ClipEventKind::BowDrawn));
            update();
            if (mWriter.mClips.size() != clipsBefore + 2)
                return nullptr;
            const std::uint32_t releaseToken = mWriter.mClips.back().mToken;
            mCombat.onClipEvent(
                bowEvent(CombatSubject::of(mA), releaseToken, ClipSlot::BowRelease, ClipEventKind::BowRelease));
            update();
            return mWriter.mLaunches.size() == launchesBefore + 1 ? &mWriter.mLaunches.back() : nullptr;
        }

        void result(const FakeWriter::Launch& launch, ProjectileFate fate, ActorHandle actor = {})
        {
            ProjectileResult r;
            r.owner = launch.mOwner;
            r.token = launch.mRequest.token;
            r.fate = fate;
            r.actor = actor;
            r.x = 800.0;
            r.flightSeconds = 0.13f;
            r.distance = 770.f;
            r.speed = 6000.f;
            mCombat.onProjectileResult(r);
            update();
        }
    };

    TEST_F(CombatBow, aHostileArcherInRangeDrawsItsBowAtTheDataSpeedAndFacesTheTarget)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda")); // names the actor again: the setter came after
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::BowDraw);
        EXPECT_EQ(mWriter.mClips[0].mName, "Bow_DrawLight");
        EXPECT_EQ(mWriter.mClips[0].mSubject, CombatSubject::of(mA));
        ASSERT_EQ(mWriter.mSpeeds.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mSpeeds[0].second, 1.f); // the bow's speed 1.0 x (1 + bonus 0)
        ASSERT_FALSE(mWriter.mFacings.empty());
        EXPECT_NEAR(mWriter.mFacings[0].second, kPi / 2.f, 1e-5f); // toward +X
        EXPECT_EQ(mWriter.mSweeps, 0); // a bow is not a sweep
    }

    TEST_F(CombatBow, theBowSpeedBonusMakesTheDrawFaster)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        mCombat.setTestBowSpeedBonus(0.5f);
        update(3.f);
        ASSERT_EQ(mWriter.mSpeeds.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mSpeeds[0].second, 1.5f);
    }

    TEST_F(CombatBow, noShootingOutOfRangeWithoutAmmoOrWithAnUnknownAmmo)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        mWriter.mPlayerState.x = 3000.0; // past the bow's 2500
        update(3.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        mWriter.mPlayerState.x = 800.0;
        ASSERT_TRUE(mCombat.setTestAmmo("NoSuchArrow"));
        update(3.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        ASSERT_TRUE(mCombat.setTestAmmo(""));
        update(3.f);
        EXPECT_TRUE(mWriter.mClips.empty());
    }

    TEST_F(CombatBow, aTooLongAmmoNameIsRefused)
    {
        EXPECT_FALSE(mCombat.setTestAmmo(std::string(kMaxCombatTestSwingName + 1, 'a')));
    }

    TEST_F(CombatBow, theDrawnEventStartsTheReleaseClipAndTheReleaseEventLaunchesTheArrow)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        const FakeWriter::Launch* launch = shoot();
        ASSERT_NE(launch, nullptr);
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[1].mSlot, ClipSlot::BowRelease);
        EXPECT_EQ(mWriter.mClips[1].mName, "Bow_Release");
        EXPECT_NE(mWriter.mClips[1].mToken, mWriter.mClips[0].mToken);
        const ProjectileRequest& r = launch->mRequest;
        EXPECT_EQ(launch->mOwner, CombatSubject::of(mA));
        EXPECT_NE(r.token, 0u);
        // From 100 above the feet and 30 ahead (+X): (30, 0, 100); aimed at the player's chest 80 above its feet.
        EXPECT_NEAR(r.x, 30.0, 1e-4);
        EXPECT_NEAR(r.y, 0.0, 1e-4);
        EXPECT_NEAR(r.z, 100.0, 1e-4);
        EXPECT_NEAR(r.heading, kPi / 2.f, 1e-5f);
        EXPECT_NEAR(r.pitch, std::atan2(-20.f, 770.f), 1e-5f); // no gravity: the straight line
        EXPECT_FLOAT_EQ(r.speed, 6000.f);
        EXPECT_FLOAT_EQ(r.gravity, 0.f);
        EXPECT_FLOAT_EQ(r.range, 15000.f);
        EXPECT_FLOAT_EQ(r.stickDepth, 20.f);
        EXPECT_FLOAT_EQ(r.stickSeconds, 10.f);
        EXPECT_EQ(launch->mModel, "meshes/Weapons/Iron/IronArrowProjectile.nif");
        EXPECT_EQ(mCombat.arrowsLaunched(), 1u);
        EXPECT_TRUE(traceHas("COMBAT arrow launch"));
    }

    TEST_F(CombatBow, aDrawClipThatEndsHasDrawnEvenIfItsLastEventWasPastItsEnd)
    {
        // Measured in game: Bow_DrawLight's bowDrawn event (1.66667 s) lies a hair past the clip's own length, so the
        // core never delivers it; the clip's completion is the same moment.
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips[0].mToken, ClipEnd::Completed);
        update();
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[1].mSlot, ClipSlot::BowRelease);
    }

    TEST_F(CombatBow, aDrawClipThatIsInterruptedOrFailsFreesTheArcherWithoutReleasing)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips[0].mToken, ClipEnd::Interrupted);
        update();
        EXPECT_EQ(mWriter.mClips.size(), 1u);
        update(3.f); // free again: it draws anew
        EXPECT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[1].mSlot, ClipSlot::BowDraw);
    }

    TEST_F(CombatBow, aProjectileWithGravityIsAimedHigherAndLaunchedWithTheGravityInUnits)
    {
        mData.mProj.mGravity = 0.35f;
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        const FakeWriter::Launch* launch = shoot();
        ASSERT_NE(launch, nullptr);
        EXPECT_NEAR(launch->mRequest.gravity, 0.35f * 686.38f, 1e-3f); // 240.233
        EXPECT_GT(launch->mRequest.pitch, std::atan2(-20.f, 770.f)); // above the straight line
        const LaunchSolution expected = solveLaunchPitch(770.f, -20.f, 6000.f, 0.35f * kProjectileGravityUnits);
        EXPECT_NEAR(launch->mRequest.pitch, expected.mPitch, 1e-6f);
    }

    TEST_F(CombatBow, anArrowThatHitsThePlayerDealsBowPlusAmmoDamageWithTheArchersSkill)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        const FakeWriter::Launch* launch = shoot();
        ASSERT_NE(launch, nullptr);
        const FakeWriter::Launch copy = *launch;
        result(copy, ProjectileFate::HitPlayer);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_FALSE(d.attackerIsPlayer);
        EXPECT_EQ(d.attacker, mA);
        EXPECT_TRUE(d.victimIsPlayer);
        EXPECT_NEAR(d.amount, 22.5f, 1e-4f); // (10 + 8) x 1.25, the player stand-in has no armour
        EXPECT_NEAR(d.healthAfter, 77.5f, 1e-4f);
        EXPECT_FLOAT_EQ(d.healthMax, 100.f);
        EXPECT_EQ(mCombat.hitsResolved(), 1u);
        EXPECT_EQ(mCombat.projectileResultsSeen(), 1u);
        EXPECT_EQ(mCombat.shotsRemembered(), 0u); // told once: forgotten
    }

    TEST_F(CombatBow, anArrowThatHitsAnActorIsReducedByItsArmourAndMakesItRecoil)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        const FakeWriter::Launch* launch = shoot();
        ASSERT_NE(launch, nullptr);
        const FakeWriter::Launch copy = *launch;
        const std::size_t clips = mWriter.mClips.size();
        result(copy, ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_FALSE(d.victimIsPlayer);
        EXPECT_EQ(d.victim, mB);
        EXPECT_NEAR(d.amount, 18.f, 1e-4f); // 22.5 x (1 - 20 / 100)
        EXPECT_NEAR(d.healthAfter, 32.f, 1e-4f); // 50 - 18
        EXPECT_GT(mWriter.mClips.size(), clips); // a hit reaction plays
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), view));
        EXPECT_NEAR(view.mHealth, 32.f, 1e-4f);
    }

    TEST_F(CombatBow, anArrowThatKillsReportsTheDeathOnce)
    {
        mData.mProfiles[kVictimBase].mHealthMax = 10.f;
        mCombat.setData(&mData); // the profiles are read again
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
        const FakeWriter::Launch* launch = shoot();
        ASSERT_NE(launch, nullptr);
        const FakeWriter::Launch copy = *launch;
        result(copy, ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths[0], mB);
        EXPECT_TRUE(traceHas("COMBAT death"));
    }

    TEST_F(CombatBow, aShotThatHitsTheWorldOrExpiresDoesNoDamage)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        const FakeWriter::Launch* first = shoot();
        ASSERT_NE(first, nullptr);
        const FakeWriter::Launch a = *first;
        result(a, ProjectileFate::HitWorld);
        const FakeWriter::Launch* second = shoot();
        ASSERT_NE(second, nullptr);
        const FakeWriter::Launch b = *second;
        result(b, ProjectileFate::Expired);
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.projectileResultsSeen(), 2u);
        EXPECT_TRUE(traceHas("COMBAT arrow ended"));
    }

    TEST_F(CombatBow, aResultForAnUnknownTokenOrAnOtherPacksOwnerIsIgnored)
    {
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = 999;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.projectileResultsSeen(), 0u);
    }

    TEST_F(CombatBow, theChanceOfAnArrowEndingInTheTargetsInventoryIsTheGmstRolledDeterministically)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        std::size_t predicted = 0;
        const int shots = 30;
        for (int i = 0; i < shots; ++i)
        {
            const FakeWriter::Launch* launch = shoot();
            ASSERT_NE(launch, nullptr);
            const FakeWriter::Launch copy = *launch;
            // The seed: the token in the high half, the victim's placement id in the low half.
            const std::uint64_t seed = (static_cast<std::uint64_t>(copy.mRequest.token) << 32) ^ mB.reference;
            predicted += rollPercent(seed) < 33.f ? 1 : 0;
            // Brenuin's health: keep him alive so that every shot lands.
            mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
            result(copy, ProjectileFate::HitActor, mB);
        }
        EXPECT_EQ(mCombat.arrowsKept(), predicted);
        EXPECT_GT(predicted, 0u);
        EXPECT_LT(predicted, static_cast<std::size_t>(shots));
    }

    TEST_F(CombatBow, anArcherWhoseBowHandIsGoneNeverReleases)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        mCombat.onClipEvent(
            bowEvent(CombatSubject::of(mA), mWriter.mClips[0].mToken, ClipSlot::BowDraw, ClipEventKind::BowDrawn));
        mCombat.onActorRemoved(mA); // its cell unloads between the draw and the release
        update();
        EXPECT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(CombatBow, aRefusedLaunchLeavesNothingRememberedAndTheArcherShootsAgainLater)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        mWriter.mRefuseLaunch = true;
        EXPECT_EQ(shoot(), nullptr);
        EXPECT_EQ(mCombat.shotsRemembered(), 0u);
        EXPECT_EQ(mCombat.arrowsLaunched(), 0u);
        mWriter.mRefuseLaunch = false;
        EXPECT_NE(shoot(), nullptr); // a later period: it draws and shoots
    }

    TEST_F(CombatBow, theTableOfShotsWaitingForTheirResultNeverGrowsBeyondItsBound)
    {
        mCombat.setTestHostile("Hulda");
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        for (std::size_t i = 0; i < kMaxShotsRemembered + 10; ++i)
            ASSERT_NE(shoot(), nullptr);
        EXPECT_EQ(mCombat.shotsRemembered(), kMaxShotsRemembered);
        EXPECT_EQ(mCombat.arrowsLaunched(), kMaxShotsRemembered + 10);
    }

    // ---- the player's bow ------------------------------------------------------------------------------------------

    struct PlayerBow : CombatBow
    {
        void SetUp() override
        {
            CombatBow::SetUp();
            mWriter.mPlayerState.x = 0.0;
            mWriter.mPlayerState.y = 0.0;
            mWriter.mPlayerState.z = 0.0;
            mWriter.mPlayerState.heading = kPi / 2.f;
            mWriter.mPlayerState.pitch = 0.1f;
        }
        // Presses, holds `seconds` of game time in 0.05 s updates, releases and updates once more.
        void drawAndRelease(float seconds)
        {
            update(0.05f); // the data is read, the draw time asked for
            mCombat.onPlayerAttackEdge(true);
            update(0.05f);
            for (float t = 0.f; t < seconds; t += 0.05f)
                update(0.05f);
            mCombat.onPlayerAttackEdge(false);
            update(0.05f);
        }
    };

    TEST_F(PlayerBow, aFullDrawShootsAtFullSpeedAlongTheLookDirectionFromTheEye)
    {
        drawAndRelease(1.8f); // the draw clip reaches bowDrawn at 1.667
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        const ProjectileRequest& r = mWriter.mLaunches[0].mRequest;
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_NEAR(r.z, 118.0, 1e-4); // the eye
        EXPECT_NEAR(r.x, 30.0, 1e-4);
        EXPECT_NEAR(r.heading, kPi / 2.f, 1e-5f);
        EXPECT_NEAR(r.pitch, 0.1f, 1e-6f); // the player's look, not solved
        EXPECT_FLOAT_EQ(r.speed, 6000.f);
        // A hit on an actor: the player's skill 15 gives 1.075: (10 + 8) x 1.075 = 19.35; armour 20 percent: 15.48.
        result(mWriter.mLaunches[0], ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(mWriter.mDamage[0].attackerIsPlayer);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 15.48f, 1e-3f);
    }

    TEST_F(PlayerBow, aHalfDrawIsSlowerAndWeaker)
    {
        drawAndRelease(0.8333f); // about half of 1.667
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        // The hold counts from the press to the release: the updates add up to 0.05 + 0.8333 rounded to 0.05 steps.
        const float fraction
            = (mWriter.mLaunches[0].mRequest.speed / 6000.f - kMinDrawSpeedFraction) / (1.f - kMinDrawSpeedFraction);
        EXPECT_GT(fraction, 0.4f);
        EXPECT_LT(fraction, 0.65f);
        result(mWriter.mLaunches[0], ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 15.48f * fraction, 0.05f);
    }

    TEST_F(PlayerBow, aPressShorterThanTheMinimumShootsNothing)
    {
        drawAndRelease(0.f); // press, one 0.05 s update, release: about 0.05 s
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("COMBAT player bow cancelled"));
    }

    TEST_F(PlayerBow, withoutTheDrawTimeThePressIsIgnoredAndNothingIsShot)
    {
        mWriter.mTimesReady = false;
        drawAndRelease(1.8f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(PlayerBow, aReleaseWithNoPressShootsNothing)
    {
        update(0.05f);
        mCombat.onPlayerAttackEdge(false);
        update(0.05f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(PlayerBow, theMeleePressDoesNothingWithABow)
    {
        mWriter.mClips.clear();
        update(0.05f);
        mCombat.onPlayerAttack(false);
        update(0.05f);
        update(2.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_EQ(mWriter.mSweeps, 0);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
    }

    TEST_F(PlayerBow, theAutomaticTestPressesAndHoldsAndShootsOnItsOwn)
    {
        mCombat.setTestPlayerBow(1.8f);
        float t = 0.f;
        while (t < 3.f)
        {
            update(0.05f);
            t += 0.05f;
        }
        // The first press at about 1 s, held 1.8 s, released at about 2.8 s: one shot by 3.0 s, none yet by 2.5 s.
        EXPECT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
    }

    // The registered source is what the engine talks to: the two new hooks must reach the combat rules through it (the
    // first in-game run showed the arrow results and the player's attack edges going nowhere).
    TEST(SkyrimCellSourceBow, theNewHooksAreForwardedToTheCombatRules)
    {
        FakeData data;
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        source.combat().setData(&data);
        ASSERT_TRUE(source.combat().setTestWeapon("IronBow"));
        ASSERT_TRUE(source.combat().setTestAmmo("IronArrow"));
        source.combat().onActorAdded(info(20, kVictimBase, "Brenuin"));
        WorldCellSource* asBase = &source;
        auto* rules = dynamic_cast<CombatRules*>(asBase);
        ASSERT_NE(rules, nullptr);
        FakeWriter writer;
        SubjectState player;
        player.heading = kPi / 2.f;
        writer.mPlayerState = player;
        rules->combatUpdate(0.05f, writer);
        rules->onPlayerAttackEdge(true);
        rules->combatUpdate(0.05f, writer);
        for (int i = 0; i < 40; ++i)
            rules->combatUpdate(0.05f, writer);
        rules->onPlayerAttackEdge(false);
        rules->combatUpdate(0.05f, writer);
        ASSERT_EQ(writer.mLaunches.size(), 1u); // the edges arrived
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = writer.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitActor;
        r.actor = { 20, 1 };
        rules->onProjectileResult(r);
        rules->combatUpdate(0.05f, writer);
        EXPECT_EQ(writer.mDamage.size(), 1u); // the result arrived
    }
}
