// Task 0299 (dungeon traps): a script's Weapon.Fire reaches the combat as a trap shot (game_skyrimse/combat.hpp,
// SkyrimCombat::fireTrapShot). Tested through the contract with a fake data source and a recording writer. The numbers
// are worked out by hand: the dart weapon "TrapDartWeapon" has damage 4 and the dart "TrapDartAmmo" damage 6, so a dart
// does 10 before armour (no skill, no draw: a trap has neither); the victim NPC has 20 percent armour, which makes that
// 8. The PROJ has speed 3000, range 2000 and gravity 0 (a straight shot).
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kVictimBase = 200;
    constexpr FormId kWeapon = 0x1000;
    constexpr FormId kAmmo = 0x1001;
    constexpr FormId kAmmoNoProjectile = 0x1002;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mWeapon.mCommon.mId = kWeapon;
            mWeapon.mCommon.mEditorId = "TrapDartWeapon";
            mWeapon.mHasData = true;
            mWeapon.mDamage = 4;
            mWeapon.mHasDnam = true;
            mWeapon.mAnimType = WeaponAnimType::Crossbow;

            mAmmo.mCommon.mId = kAmmo;
            mAmmo.mHasData = true;
            mAmmo.mDamage = 6.f;
            mAmmo.mProjectile = 77;
            mBare.mCommon.mId = kAmmoNoProjectile;
            mBare.mHasData = true;
            mBare.mDamage = 6.f;
            mBare.mProjectile = 0;

            mProj.mHasData = true;
            mProj.mType = ProjectileRecord::kTypeArrow;
            mProj.mGravity = 0.f;
            mProj.mSpeed = 3000.f;
            mProj.mRange = 2000.f;
            mProj.mModel = "Traps\\DartProjectile.nif";

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
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
        const WeaponRecord* weaponById(FormId id) override { return id == kWeapon ? &mWeapon : nullptr; }
        const AmmoRecord* ammoById(FormId id) override
        {
            return id == kAmmo ? &mAmmo : id == kAmmoNoProjectile ? &mBare : nullptr;
        }
        const ProjectileRecord* projectileOf(const AmmoRecord& ammo) override
        {
            return ammo.mProjectile == 77 ? &mProj : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }

        WeaponRecord mWeapon;
        AmmoRecord mAmmo, mBare;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Launch
        {
            CombatSubject mOwner;
            ProjectileRequest mRequest;
            std::string mModel;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, std::string(request.clip) });
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
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            return false;
        }

        std::vector<std::pair<CombatSubject, std::string>> mClips;
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        SubjectState mPlayerState;
        bool mRefuseLaunch = false;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatTraps : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
            update(); // the profiles are read
        }
        void update(float seconds = 0.016f) { mCombat.combatUpdate(seconds, mWriter); }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        static SkyrimCombat::TrapShot shot()
        {
            SkyrimCombat::TrapShot s;
            s.mWeapon = kWeapon;
            s.mAmmo = kAmmo;
            s.x = 100.0;
            s.y = 200.0;
            s.z = 300.0;
            s.heading = kPi / 2.f;
            s.pitch = 0.25f;
            return s;
        }
        // Fires one trap shot and tells the combat how it ended.
        void fireAndEnd(ProjectileFate fate, ActorHandle actor = {})
        {
            ASSERT_TRUE(mCombat.fireTrapShot(mWriter, shot()));
            const FakeWriter::Launch launch = mWriter.mLaunches.back();
            ProjectileResult r;
            r.owner = launch.mOwner;
            r.token = launch.mRequest.token;
            r.fate = fate;
            r.actor = actor;
            r.x = 900.0;
            mCombat.onProjectileResult(r);
            update();
        }
    };

    TEST_F(CombatTraps, aTrapShotLeavesTheSourceOwnerlessWithTheProjectilesNumbers)
    {
        ASSERT_TRUE(mCombat.fireTrapShot(mWriter, shot()));
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        const FakeWriter::Launch& launch = mWriter.mLaunches[0];
        EXPECT_FALSE(launch.mOwner.valid()) << "nobody fired it";
        const ProjectileRequest& r = launch.mRequest;
        EXPECT_TRUE(r.ownerless);
        EXPECT_FALSE(r.visualOnly);
        EXPECT_NE(r.token, 0u);
        EXPECT_NEAR(r.x, 100.0, 1e-9);
        EXPECT_NEAR(r.y, 200.0, 1e-9);
        EXPECT_NEAR(r.z, 300.0, 1e-9);
        EXPECT_NEAR(r.heading, kPi / 2.f, 1e-6f);
        EXPECT_NEAR(r.pitch, 0.25f, 1e-6f);
        EXPECT_FLOAT_EQ(r.speed, 3000.f);
        EXPECT_FLOAT_EQ(r.range, 2000.f);
        EXPECT_FLOAT_EQ(r.gravity, 0.f);
        EXPECT_EQ(launch.mModel, "meshes/Traps/DartProjectile.nif");
        EXPECT_EQ(mCombat.trapShotsInFlight(), 1u);
        EXPECT_TRUE(traceHas("COMBAT trap shot launch"));
    }

    TEST_F(CombatTraps, anUnknownWeaponAnUnknownAmmoOrAnAmmoWithNoProjectileFiresNothing)
    {
        SkyrimCombat::TrapShot s = shot();
        s.mWeapon = 0x9999;
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, s));
        s = shot();
        s.mAmmo = 0x9998;
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, s));
        s = shot();
        s.mAmmo = kAmmoNoProjectile;
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, s));
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_EQ(mCombat.trapShotsInFlight(), 0u);
    }

    TEST_F(CombatTraps, aNonFiniteAimOrPlaceFiresNothingAndACoreThatRefusesLeavesNoShotBehind)
    {
        SkyrimCombat::TrapShot s = shot();
        s.heading = std::nanf("");
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, s));
        s = shot();
        s.z = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, s));
        mWriter.mRefuseLaunch = true;
        EXPECT_FALSE(mCombat.fireTrapShot(mWriter, shot()));
        EXPECT_EQ(mCombat.trapShotsInFlight(), 0u);
    }

    TEST_F(CombatTraps, aDartThatHitsThePlayerDoesTheWeaponsAndTheAmmosDamageFromNobody)
    {
        fireAndEnd(ProjectileFate::HitPlayer);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_FALSE(d.attackerIsPlayer);
        EXPECT_FALSE(d.attacker.valid());
        EXPECT_TRUE(d.victimIsPlayer);
        EXPECT_NEAR(d.amount, 10.f, 1e-4f); // 4 + 6, the stand-in has no armour
        EXPECT_NEAR(d.healthAfter, 90.f, 1e-4f);
        EXPECT_FLOAT_EQ(d.healthMax, 100.f);
        EXPECT_NEAR(d.x, 900.0, 1e-6);
        EXPECT_EQ(mCombat.trapShotsInFlight(), 0u) << "told once: forgotten";
        EXPECT_TRUE(traceHas("COMBAT trap damage"));
    }

    TEST_F(CombatTraps, aDartThatHitsAnActorIsReducedByItsArmourAndMakesItRecoil)
    {
        fireAndEnd(ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_FALSE(d.victimIsPlayer);
        EXPECT_EQ(d.victim, mB);
        EXPECT_NEAR(d.amount, 8.f, 1e-4f); // 10 x (1 - 20 / 100)
        EXPECT_NEAR(d.healthAfter, 42.f, 1e-4f);
        EXPECT_FALSE(mWriter.mClips.empty()) << "a hit reaction plays";
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), view));
        EXPECT_NEAR(view.mHealth, 42.f, 1e-4f);
        EXPECT_TRUE(mWriter.mDeaths.empty());
    }

    TEST_F(CombatTraps, aDartThatKillsAnActorReportsTheDeathOnce)
    {
        mData.mProfiles[kVictimBase].mHealthMax = 5.f;
        mCombat.setData(&mData);
        mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
        update();
        fireAndEnd(ProjectileFate::HitActor, mB);
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths[0], mB);
        // a second dart at the dead body hurts nobody again
        fireAndEnd(ProjectileFate::HitActor, mB);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
    }

    TEST_F(CombatTraps, aDartThatHitsTheWorldOrExpiresDoesNoDamage)
    {
        fireAndEnd(ProjectileFate::HitWorld);
        fireAndEnd(ProjectileFate::Expired);
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.trapShotsInFlight(), 0u);
    }

    TEST_F(CombatTraps, aResultForATokenThatIsNoTrapShotIsIgnored)
    {
        ProjectileResult r;
        r.owner = CombatSubject{};
        r.token = 424242;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatTraps, theShotsInFlightAreBounded)
    {
        for (std::size_t i = 0; i < kMaxShotsRemembered + 10; ++i)
            ASSERT_TRUE(mCombat.fireTrapShot(mWriter, shot()));
        EXPECT_EQ(mCombat.trapShotsInFlight(), kMaxShotsRemembered);
    }

    // ---- the aim of a placed object -----------------------------------------------------------------------------

    TEST(TrapAim, anObjectWithNoRotationFacesPlusY)
    {
        const TrapAim a = trapAimOf(0.0, 0.0, 0.0);
        EXPECT_NEAR(a.heading, 0.f, 1e-6f);
        EXPECT_NEAR(a.pitch, 0.f, 1e-6f);
    }

    TEST(TrapAim, aPositiveYawTurnsItClockwiseFromAboveTowardPlusX)
    {
        const TrapAim a = trapAimOf(0.0, 0.0, kPi / 2.0);
        EXPECT_NEAR(a.heading, kPi / 2.f, 1e-5f);
        EXPECT_NEAR(a.pitch, 0.f, 1e-5f);
        const TrapAim b = trapAimOf(0.0, 0.0, kPi);
        EXPECT_NEAR(std::abs(b.heading), kPi, 1e-5f);
    }

    TEST(TrapAim, aPositiveStoredXAngleTiltsItDownAsEulerToQuatDoes)
    {
        const TrapAim a = trapAimOf(0.5, 0.0, 0.0);
        EXPECT_NEAR(a.heading, 0.f, 1e-5f);
        EXPECT_NEAR(a.pitch, -0.5f, 1e-5f);
    }

    TEST(TrapAim, theMuzzleIsSixteenUnitsAlongTheFacing)
    {
        const double from[3] = { 100.0, 200.0, 300.0 };
        double out[3];
        TrapAim north;
        trapMuzzle(from, north, out);
        EXPECT_NEAR(out[0], 100.0, 1e-9);
        EXPECT_NEAR(out[1], 216.0, 1e-9);
        EXPECT_NEAR(out[2], 300.0, 1e-9);
        TrapAim eastUp;
        eastUp.heading = kPi / 2.f;
        eastUp.pitch = kPi / 2.f - 1e-4f;
        trapMuzzle(from, eastUp, out);
        EXPECT_NEAR(out[2], 316.0, 1e-2);
    }

    TEST(TrapAim, theYawIsAppliedBeforeTheTilt)
    {
        // Rz first: +Y goes to +X; the tilt about the world X axis then leaves +X alone, so the heading stays pi / 2
        // and the pitch is zero for any X angle (R = Rx * Ry * Rz acts on the vector from the right).
        const TrapAim a = trapAimOf(0.7, 0.0, kPi / 2.0);
        EXPECT_NEAR(a.heading, kPi / 2.f, 1e-5f);
        EXPECT_NEAR(a.pitch, 0.f, 1e-5f);
    }
}
