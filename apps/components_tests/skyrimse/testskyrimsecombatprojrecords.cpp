// Task 0178 (milestone M-S5, stage S5-6): the PROJ and AMMO decoders, the arrow GMSTs, the bow hit, the draw speed, the
// launch pitch, the roll and the model path, on generated data. No game data. Every expected number is derived by hand
// in a comment. The formulas are RECALLED-VERIFY (plausible and tunable); these tests pin THIS implementation.
#include <game_skyrimse/combat_stats.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kProj = 0x01000D00, kAmmo = 0x01000E00, kGmst = 0x01000C00;
    constexpr float kPi = 3.14159265358979f;

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x4321;
        return r;
    }

    // PROJ DATA, 92 bytes: flags u16 +0, type u16 +2, gravity f32 +4, speed f32 +8, range f32 +12, the rest zeros.
    Bytes projData(
        std::uint16_t flags, std::uint16_t type, float gravity, float speed, float range, std::size_t size = 92)
    {
        Bytes d;
        d.u16(flags).u16(type).f32(gravity).f32(speed).f32(range);
        while (d.str().size() < size)
            d.u8(0);
        return d;
    }

    // AMMO DATA, 20 bytes (Skyrim.esm): projectile form id +0, flags u32 +4, damage f32 +8, value u32 +12, weight f32
    // +16.
    Bytes ammoData(std::uint32_t projectile, std::uint32_t flags, float damage, std::uint32_t value)
    {
        return Bytes().u32(projectile).u32(flags).f32(damage).u32(value).f32(0.1f);
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), 0, records);
    }

    // ---- PROJ ------------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatProjRecords, aProjectileDecodesItsNumbersAndModel)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronArrowProjectile")));
        subs.append(sub4("MODL", Bytes().zstring("Weapons\\Iron\\IronArrowProjectile.nif")));
        subs.append(sub4("DATA", projData(0x0123, 64, 0.35f, 6000.f, 15000.f)));
        ProjectileRecord p;
        std::string why;
        ASSERT_TRUE(decodeProjectile(view(subs), raw("PROJ", kProj), f.context(), p, why)) << why;
        EXPECT_EQ(p.mCommon.mEditorId, "IronArrowProjectile");
        EXPECT_TRUE(p.mHasData);
        EXPECT_EQ(p.mFlags, 0x0123);
        EXPECT_EQ(p.mType, 64);
        EXPECT_TRUE(p.isArrow());
        EXPECT_FLOAT_EQ(p.mGravity, 0.35f);
        EXPECT_FLOAT_EQ(p.mSpeed, 6000.f);
        EXPECT_FLOAT_EQ(p.mRange, 15000.f);
        EXPECT_EQ(p.mModel, "Weapons\\Iron\\IronArrowProjectile.nif");
    }

    TEST(SkyrimCombatProjRecords, aSpellProjectileIsNotAnArrowAndASmallerDataBlockStillGivesTheFirstFields)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("FireboltProjectile")));
        subs.append(sub4("DATA", projData(0, 1, 0.f, 1500.f, 8000.f, 84))); // 84 bytes: one of the six shorter ones
        ProjectileRecord p;
        std::string why;
        ASSERT_TRUE(decodeProjectile(view(subs), raw("PROJ", kProj), f.context(), p, why)) << why;
        EXPECT_TRUE(p.mHasData);
        EXPECT_FALSE(p.isArrow());
        EXPECT_FLOAT_EQ(p.mSpeed, 1500.f);
    }

    TEST(SkyrimCombatProjRecords, aDataBlockTooShortOrWithABadFloatIsOddAndDecodesToNothing)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        // 20 bytes is shorter than any block in the data (84, 88 or 92); the other two carry a float that is not finite.
        const std::vector<Bytes> blocks = { projData(0, 64, 0.f, 6000.f, 15000.f, 20),
            projData(0, 64, 0.f, nan, 15000.f), projData(0, 64, inf, 6000.f, 15000.f) };
        std::uint64_t odd = 0;
        for (const Bytes& data : blocks)
        {
            Fixture f;
            Bytes subs;
            subs.append(sub4("EDID", Bytes().zstring("X")));
            subs.append(sub4("DATA", data));
            ProjectileRecord p;
            std::string why;
            ASSERT_TRUE(decodeProjectile(view(subs), raw("PROJ", kProj), f.context(), p, why)) << why;
            EXPECT_FALSE(p.mHasData);
            odd += f.mNotes.mOddSubrecords;
        }
        EXPECT_EQ(odd, 3u);
    }

    TEST(SkyrimCombatProjRecords, anOverlongModelNameIsAnErrorNotAnAllocationBomb)
    {
        Fixture f;
        f.mLimits.mMaxStringBytes = 64;
        Bytes subs;
        subs.append(sub4("MODL", Bytes().zstring(std::string(200, 'a'))));
        ProjectileRecord p;
        std::string why;
        EXPECT_FALSE(decodeProjectile(view(subs), raw("PROJ", kProj), f.context(), p, why));
        EXPECT_FALSE(why.empty());
    }

    // ---- AMMO ------------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatProjRecords, anAmmoDecodesItsProjectileDamageAndFlags)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronArrow")));
        subs.append(sub4("DATA", ammoData(kProj, 4, 8.f, 1)));
        AmmoRecord a;
        std::string why;
        ASSERT_TRUE(decodeAmmo(view(subs), raw("AMMO", kAmmo), f.context(), a, why)) << why;
        EXPECT_EQ(a.mCommon.mEditorId, "IronArrow");
        EXPECT_TRUE(a.mHasData);
        EXPECT_NE(a.mProjectile, 0u);
        EXPECT_EQ(a.mFlags, 4u);
        EXPECT_FLOAT_EQ(a.mDamage, 8.f);
        EXPECT_EQ(a.mValue, 1u);
    }

    TEST(SkyrimCombatProjRecords, anAmmoDataBlockOfAnotherSizeOrWithANanDamageIsOdd)
    {
        Fixture f;
        {
            Bytes subs;
            subs.append(sub4("DATA", Bytes().u32(kProj).u32(4).f32(8.f).u32(1))); // 16 bytes: not what the data has
            AmmoRecord a;
            std::string why;
            ASSERT_TRUE(decodeAmmo(view(subs), raw("AMMO", kAmmo), f.context(), a, why)) << why;
            EXPECT_FALSE(a.mHasData);
        }
        {
            Bytes subs;
            subs.append(sub4("DATA", ammoData(kProj, 4, std::numeric_limits<float>::quiet_NaN(), 1)));
            AmmoRecord a;
            std::string why;
            ASSERT_TRUE(decodeAmmo(view(subs), raw("AMMO", kAmmo), f.context(), a, why)) << why;
            EXPECT_FALSE(a.mHasData);
        }
        EXPECT_GE(f.mNotes.mOddSubrecords, 2u);
    }

    // ---- the walk and the index ----------------------------------------------------------------------------------

    TEST(SkyrimCombatProjRecords, theWalkKeepsProjectilesAndAmmoAndTheIndexLinksThem)
    {
        Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes proj;
        proj.append(sub4("EDID", Bytes().zstring("IronArrowProjectile")));
        proj.append(sub4("MODL", Bytes().zstring("Weapons\\Iron\\IronArrowProjectile.nif")));
        proj.append(sub4("DATA", projData(0, 64, 0.35f, 6000.f, 15000.f)));
        plugin.append(topGroup("PROJ", record4("PROJ", 0, kProj, proj)));
        Bytes ammo;
        ammo.append(sub4("EDID", Bytes().zstring("IronArrow")));
        ammo.append(sub4("DATA", ammoData(kProj, 4, 8.f, 1)));
        Bytes ammo2;
        ammo2.append(sub4("EDID", Bytes().zstring("SteelArrow")));
        ammo2.append(sub4("DATA", ammoData(kProj, 4, 12.f, 1)));
        Bytes ammos = record4("AMMO", 0, kAmmo, ammo);
        ammos.append(record4("AMMO", 0, kAmmo + 1, ammo2));
        plugin.append(topGroup("AMMO", ammos));
        Bytes g;
        g.append(sub4("EDID", Bytes().zstring("iArrowInventoryChance")));
        g.append(sub4("DATA", Bytes().u32(50)));
        plugin.append(topGroup("GMST", record4("GMST", 0, kGmst, g)));

        Content::Error error;
        const std::unique_ptr<Content::Plugin> file = Content::Plugin::open(
            std::make_unique<std::istringstream>(plugin.str(), std::ios::binary), "test.esp", error);
        ASSERT_TRUE(file != nullptr) << error.mMessage;
        CombatRecords records;
        ASSERT_TRUE(parseCombatRecords(*file, oneMaster(), ExteriorLimits(), records));
        EXPECT_EQ(records.mProjectiles.size(), 1u);
        EXPECT_EQ(records.mAmmo.size(), 2u);
        EXPECT_TRUE(records.mErrors.empty());

        CombatIndex index;
        index.add(records);
        const FormId projId = records.mProjectiles.front().mCommon.mId;
        ASSERT_NE(index.projectile(projId), nullptr);
        EXPECT_FLOAT_EQ(index.projectile(projId)->mSpeed, 6000.f);
        const std::vector<FormId> ammoIds = index.ammoIds();
        ASSERT_EQ(ammoIds.size(), 2u);
        EXPECT_LT(ammoIds[0], ammoIds[1]);
        ASSERT_NE(index.ammo(ammoIds[0]), nullptr);
        EXPECT_EQ(index.ammo(ammoIds[0])->mProjectile, projId); // the AMMO's projectile id resolved to the PROJ's
        EXPECT_EQ(index.projectile(projId + 12345), nullptr);
        EXPECT_EQ(index.ammo(0), nullptr);
        // The GMST: iArrowInventoryChance 50 replaces 33; the others stay.
        const ProjectileSettings settings = index.settings().projectileSettings();
        EXPECT_FLOAT_EQ(settings.mArrowInventoryChance, 50.f);
        EXPECT_FLOAT_EQ(settings.mMissileStickDepth, 20.f);
        EXPECT_FLOAT_EQ(settings.mAttachedArrowAgeMax, 10.f);
    }

    TEST(SkyrimCombatProjRecords, theArrowSettingsDefaultToTheValuesTheDataStores)
    {
        const ProjectileSettings settings = SettingsTable().projectileSettings();
        EXPECT_FLOAT_EQ(settings.mArrowInventoryChance, 33.f);
        EXPECT_FLOAT_EQ(settings.mMissileStickDepth, 20.f);
        EXPECT_FLOAT_EQ(settings.mAttachedArrowAgeMax, 10.f);
    }

    TEST(SkyrimCombatProjRecords, aWeaponsRangesAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronBow")));
        subs.append(sub4("DATA", Bytes().u32(30).f32(7.f).u16(10)));
        Bytes dnam;
        dnam.u8(7).u8(0).u8(0).u8(0).f32(1.f).f32(1.f).u16(0);
        while (dnam.str().size() < 28)
            dnam.u8(0);
        dnam.f32(500.f).f32(2500.f);
        while (dnam.str().size() < 76)
            dnam.u8(0);
        dnam.u32(8);
        while (dnam.str().size() < 100)
            dnam.u8(0);
        subs.append(sub4("DNAM", dnam));
        WeaponRecord w;
        std::string why;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", 0x01000A00), f.context(), w, why)) << why;
        EXPECT_EQ(w.mAnimType, WeaponAnimType::Bow);
        EXPECT_FLOAT_EQ(w.mMinRange, 500.f);
        EXPECT_FLOAT_EQ(w.mMaxRange, 2500.f);
        EXPECT_EQ(w.skillSlot(), 2); // Archery: actor value 8
    }

    // ---- the bow hit -----------------------------------------------------------------------------------------------

    WeaponRecord bow(std::uint16_t damage = 10)
    {
        WeaponRecord w;
        w.mHasData = true;
        w.mDamage = damage;
        w.mHasDnam = true;
        w.mAnimType = WeaponAnimType::Bow;
        w.mStagger = 0.5f;
        return w;
    }

    AmmoRecord arrow(float damage = 8.f)
    {
        AmmoRecord a;
        a.mHasData = true;
        a.mDamage = damage;
        return a;
    }

    TEST(SkyrimCombatBowHit, bowAndArrowDamageAddAndTheSkillAndTheDrawScaleThem)
    {
        const WeaponRecord w = bow();
        const AmmoRecord a = arrow();
        BowInput in;
        in.mBow = &w;
        in.mAmmo = &a;
        in.mArcherySkill = 50.f; // multiplier 1 + 50 / 200 = 1.25
        const BowHit full = computeBowHit(in, CombatSettings());
        EXPECT_NEAR(full.mDamageBefore, 22.5f, 1e-4f); // (10 + 8) * 1.25
        EXPECT_NEAR(full.mDamageAfter, 22.5f, 1e-4f);
        EXPECT_FLOAT_EQ(full.mStagger, 0.5f);
        in.mDrawFraction = 0.5f;
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageBefore, 11.25f, 1e-4f);
        in.mDrawFraction = 2.f; // clamped to the full draw
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageBefore, 22.5f, 1e-4f);
        in.mDrawFraction = -1.f;
        EXPECT_EQ(computeBowHit(in, CombatSettings()).mDamageBefore, 0.f);
        in.mDrawFraction = 1.f;
        in.mArcherySkill = 100.f; // 1.5, the cap fDamagePCSkillMax
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageBefore, 27.f, 1e-4f);
        in.mArcherySkill = 400.f; // clamped to 100
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageBefore, 27.f, 1e-4f);
    }

    TEST(SkyrimCombatBowHit, armourTakesItsPercentOffAndNothingBadComesOut)
    {
        const WeaponRecord w = bow();
        const AmmoRecord a = arrow();
        BowInput in;
        in.mBow = &w;
        in.mAmmo = &a;
        in.mArcherySkill = 50.f;
        in.mTargetMitigationPercent = 20.f;
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageAfter, 18.f, 1e-4f); // 22.5 * 0.8
        in.mTargetMitigationPercent = 95.f; // capped at fMaxArmorRating 80
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageAfter, 4.5f, 1e-4f);
        in.mTargetMitigationPercent = -50.f; // a negative mitigation is data: never more than the blow
        EXPECT_NEAR(computeBowHit(in, CombatSettings()).mDamageAfter, 22.5f, 1e-4f);
        in.mArcherySkill = std::numeric_limits<float>::quiet_NaN();
        const BowHit nanSkill = computeBowHit(in, CombatSettings());
        EXPECT_TRUE(std::isfinite(nanSkill.mDamageAfter));
        in.mBow = nullptr;
        EXPECT_EQ(computeBowHit(in, CombatSettings()).mDamageBefore, 0.f);
        in.mBow = &w;
        in.mAmmo = nullptr;
        EXPECT_EQ(computeBowHit(in, CombatSettings()).mDamageBefore, 0.f);
    }

    // ---- the draw speed ------------------------------------------------------------------------------------------

    TEST(SkyrimCombatBowDraw, theDrawSpeedIsTheWeaponSpeedTimesOnePlusTheBonus)
    {
        EXPECT_FLOAT_EQ(bowDrawSpeed(1.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(bowDrawSpeed(1.f, 0.5f), 1.5f);
        EXPECT_NEAR(bowDrawSpeed(0.7f, 0.5f), 1.05f, 1e-6f);
        EXPECT_FLOAT_EQ(bowDrawSpeed(1.f, -0.95f), 0.1f); // clamped
        EXPECT_FLOAT_EQ(bowDrawSpeed(1.f, 20.f), 10.f);
        EXPECT_FLOAT_EQ(bowDrawSpeed(std::numeric_limits<float>::quiet_NaN(), 0.f), 1.f);
        EXPECT_FLOAT_EQ(bowDrawSpeed(1.f, std::numeric_limits<float>::infinity()), 1.f);
        EXPECT_FLOAT_EQ(bowDrawSpeed(0.f, 0.f), 0.1f); // a weapon with no speed: the slowest
    }

    // ---- the launch pitch ------------------------------------------------------------------------------------------

    TEST(SkyrimCombatBowPitch, noGravityIsTheStraightLine)
    {
        const LaunchSolution flat = solveLaunchPitch(1000.f, 0.f, 6000.f, 0.f);
        EXPECT_TRUE(flat.mReachable);
        EXPECT_NEAR(flat.mPitch, 0.f, 1e-6f);
        EXPECT_NEAR(solveLaunchPitch(1000.f, 1000.f, 6000.f, 0.f).mPitch, kPi / 4.f, 1e-6f);
        EXPECT_NEAR(solveLaunchPitch(1000.f, -500.f, 6000.f, 0.f).mPitch, std::atan2(-500.f, 1000.f), 1e-6f);
    }

    TEST(SkyrimCombatBowPitch, gravityRaisesTheAimByTheDropAndMatchesTheClosedForm)
    {
        // The drop over 1000 units at 6000 u/s is g d^2 / (2 v^2) = 686.38e6 / 7.2e7 = 9.533 units: tan(pitch) about
        // 0.009533 (the exact solution is slightly larger).
        const LaunchSolution s = solveLaunchPitch(1000.f, 0.f, 6000.f, 686.38f);
        EXPECT_TRUE(s.mReachable);
        EXPECT_NEAR(s.mPitch, 0.0095333f, 2e-6f);
        // Fly it: the arc of that pitch at that speed reaches the same height at d = 1000 (within a unit).
        const double v = 6000.0, g = 686.38, pitch = s.mPitch;
        const double t = 1000.0 / (v * std::cos(pitch));
        const double z = v * std::sin(pitch) * t - 0.5 * g * t * t;
        EXPECT_NEAR(z, 0.0, 0.5);
    }

    TEST(SkyrimCombatBowPitch, aTargetOutOfReachIsSaidSoAndAimedAtFortyFiveDegrees)
    {
        const LaunchSolution s = solveLaunchPitch(1000.f, 0.f, 100.f, 686.38f);
        EXPECT_FALSE(s.mReachable);
        EXPECT_NEAR(s.mPitch, kPi / 4.f, 1e-6f);
        const LaunchSolution bad = solveLaunchPitch(std::numeric_limits<float>::quiet_NaN(), 0.f, 6000.f, 0.f);
        EXPECT_FALSE(bad.mReachable);
    }

    TEST(SkyrimCombatBowPitch, aTargetStraightAboveIsClampedNotNan)
    {
        const LaunchSolution s = solveLaunchPitch(0.f, 100.f, 6000.f, 686.38f);
        EXPECT_TRUE(std::isfinite(s.mPitch));
        EXPECT_NEAR(s.mPitch, 1.5f, 1e-6f);
    }

    // ---- the roll and the model path -----------------------------------------------------------------------------

    TEST(SkyrimCombatBowRoll, theRollIsDeterministicInRangeAndEvenlySpread)
    {
        EXPECT_EQ(rollPercent(12345), rollPercent(12345));
        EXPECT_NE(rollPercent(1), rollPercent(2));
        double sum = 0.0;
        int below = 0;
        for (std::uint64_t seed = 0; seed < 20000; ++seed)
        {
            const float r = rollPercent(seed);
            ASSERT_GE(r, 0.f);
            ASSERT_LT(r, 100.f);
            sum += r;
            below += r < 33.f ? 1 : 0;
        }
        EXPECT_NEAR(sum / 20000.0, 50.0, 1.0);
        EXPECT_NEAR(static_cast<double>(below) / 20000.0, 0.33, 0.015);
    }

    TEST(SkyrimCombatBowPath, aStoredModelPathBecomesAVfsPath)
    {
        EXPECT_EQ(projectileModelPath("Weapons\\Iron\\IronArrowProjectile.nif"),
            "meshes/Weapons/Iron/IronArrowProjectile.nif");
        EXPECT_EQ(projectileModelPath("meshes\\weapons\\x.nif"), "meshes/weapons/x.nif");
        EXPECT_EQ(projectileModelPath("Meshes/Weapons/x.nif"), "Meshes/Weapons/x.nif");
        EXPECT_EQ(projectileModelPath(""), "");
        EXPECT_EQ(projectileModelPath("..\\x.nif"), "");
        EXPECT_EQ(projectileModelPath("weapons\\..\\..\\x.nif"), "");
        EXPECT_EQ(projectileModelPath("C:\\x.nif"), "");
        EXPECT_EQ(projectileModelPath("/etc/passwd"), "");
        EXPECT_EQ(projectileModelPath(std::string(257, 'a')), "");
    }
}
