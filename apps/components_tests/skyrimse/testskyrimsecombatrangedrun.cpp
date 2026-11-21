// Task 0300 (crossbows, real bows, dual wielding, dual casting) in the combat runtime: SkyrimCombat with a fake data
// source and a recording writer, written from the spec (docs/tasks/0300-skyrim-crossbows-dual-wield.md) before the
// code. The numbers are worked out by hand:
//  - the crossbow "DLC1CrossBow" has damage 19, the bolt "IronBolt" damage 10: a bolt does (19 + 10) x min(1.5, 1 +
//  skill
//    / 200); the archer's skill 50 gives 1.25, so 36.25; the player's skill 15 gives 1.075, so 31.175;
//  - the sword has damage 7, the dagger 4, a skill of 0 multiplies by 1: the right hand hits for 7, the left for 4;
//  - a dual cast of a 20 magicka spell costs 20 x 2.8 = 56 and its 10 point effect heals 10 x 2.2 = 22.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_style.hpp>

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
    constexpr FormId kBoltItem = 0x900;
    constexpr FormId kMend = 0x1000, kMendEffect = 0x2000;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mCrossbow = weapon(30, "DLC1CrossBow", WeaponAnimType::Crossbow, 19);
            mDwarven = weapon(31, "DLC1DwarvenCrossBow", WeaponAnimType::Crossbow, 22);
            mBow = weapon(32, "IronBow", WeaponAnimType::Bow, 10);
            mSword = weapon(1, "IronSword", WeaponAnimType::OneHandSword, 7);
            mDagger = weapon(4, "IronDagger", WeaponAnimType::OneHandDagger, 4);
            mBolt.mCommon.mId = 50;
            mBolt.mCommon.mEditorId = "IronBolt";
            mBolt.mHasData = true;
            mBolt.mDamage = 10.f;
            mBolt.mProjectile = 78;
            mBolt.mFlags = 0; // a bolt
            mArrow.mCommon.mId = 51;
            mArrow.mCommon.mEditorId = "IronArrow";
            mArrow.mHasData = true;
            mArrow.mDamage = 8.f;
            mArrow.mProjectile = 77;
            mArrow.mFlags = kAmmoNonBoltFlag;
            mArrowProj.mCommon.mEditorId = "IronArrowProjectile";
            mArrowProj.mHasData = true;
            mArrowProj.mSpeed = 6000.f;
            mArrowProj.mRange = 15000.f;
            mArrowProj.mModel = "Weapons\\Iron\\IronArrowProjectile.nif";
            mBoltProj = mArrowProj;
            mBoltProj.mCommon.mEditorId = "IronBoltProjectile";
            mBoltProj.mSpeed = 7000.f;
            mBoltProj.mModel = "DLC01\\Weapons\\Crossbow\\Bolt.nif";

            MagicEffectRecord effect;
            effect.mCommon.mId = kMendEffect;
            effect.mCommon.mEditorId = "MendEffect";
            effect.mHasData = true;
            effect.mArchetype = 0;
            effect.mPrimaryValue = 24;
            effect.mFlags = 0x201000;
            mEffects[kMendEffect] = effect;
            SpellRecord spell;
            spell.mCommon.mId = kMend;
            spell.mCommon.mEditorId = "Mend";
            spell.mHasData = true;
            spell.mCost = 20;
            spell.mCastType = MagicCast::FireAndForget;
            spell.mDelivery = MagicDelivery::Self;
            SpellEffect se;
            se.mEffect = kMendEffect;
            se.mMagnitude = 10.f;
            spell.mEffects.push_back(se);
            mSpells[kMend] = spell;

            FighterProfile archer;
            archer.mHealthMax = 100.f;
            archer.mStaminaMax = 100.f;
            archer.mMagickaMax = 100.f;
            archer.mSkills[2] = 50;
            mProfiles[kArcherBase] = archer;
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 200.f;
            mPlayer.mSkills.fill(15);
            mPlayer.mSkills[0] = 0;
        }
        static WeaponRecord weapon(FormId id, const char* editorId, std::uint8_t type, std::uint16_t damage)
        {
            WeaponRecord w;
            w.mCommon.mId = id;
            w.mCommon.mEditorId = editorId;
            w.mHasData = true;
            w.mHasDnam = true;
            w.mAnimType = type;
            w.mDamage = damage;
            w.mWeight = 5.f;
            w.mSpeed = 1.f;
            w.mReach = 1.f;
            const bool ranged = type == WeaponAnimType::Bow || type == WeaponAnimType::Crossbow;
            w.mSkillValue = kFirstSkillActorValue + (ranged ? 2u : 0u);
            w.mMinRange = ranged ? 500.f : 0.f;
            w.mMaxRange = ranged ? 2500.f : 0.f;
            return w;
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
            for (const WeaponRecord* w : { &mSword, &mDagger, &mBow, &mCrossbow })
            {
                std::string lower = w->mCommon.mEditorId;
                for (char& c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == id)
                    return w;
            }
            return nullptr;
        }
        const AmmoRecord* ammoByEditorId(std::string_view id) override
        {
            return id == "ironarrow" ? &mArrow : (id == "ironbolt" ? &mBolt : nullptr);
        }
        const ProjectileRecord* projectileOf(const AmmoRecord& ammo) override
        {
            return ammo.mProjectile == 77 ? &mArrowProj : (ammo.mProjectile == 78 ? &mBoltProj : nullptr);
        }
        CombatSettings settings() override { return CombatSettings(); }
        bool fighterGear(WorldId base, const std::vector<ArmourPiece>&, FighterGear& out) override
        {
            const auto it = mGear.find(base);
            if (it == mGear.end())
                return false;
            out = it->second;
            out.mKnown = true;
            return true;
        }
        const SpellRecord* spellByEditorId(std::string_view id) override
        {
            return id == "mend" ? &mSpells[kMend] : nullptr;
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
        DualCastSettings dualCastSettings() override { return mDualCast; }

        WeaponRecord mCrossbow, mDwarven, mBow, mSword, mDagger;
        AmmoRecord mBolt, mArrow;
        ProjectileRecord mArrowProj, mBoltProj;
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        std::map<WorldId, FighterProfile> mProfiles;
        std::map<WorldId, FighterGear> mGear;
        FighterProfile mPlayer;
        DualCastSettings mDualCast;
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
        bool setClipSpeed(CombatSubject, float) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind == ClipEventKind::BowDrawn && clip == "Bow_DrawLight")
                out = 1.66667f;
            else if (kind == ClipEventKind::SpellFire && clip == "DualMagic_SelfRelease")
                out = 0.367f;
            else if (kind == ClipEventKind::SpellFire && clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
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
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view clip, bool, ClipTiming& out) override
        {
            mTimingClips.emplace_back(clip);
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
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<std::string> mTimingClips;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
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

    // The archer "Hulda" (handle 10) at the origin facing +X; the player 800 units east.
    struct RangedRun : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<std::string> mTrace;
        std::vector<FormId> mSpent;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.setAmmoSpentHook([this](FormId item) { mSpent.push_back(item); });
            mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
            SubjectState archer;
            archer.heading = kPi / 2.f;
            mWriter.mStates[10] = archer;
            mWriter.mPlayerState = archer;
            mWriter.mPlayerState.x = 800.0;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void update(float seconds = 0.016f) { mCombat.combatUpdate(seconds, mWriter); }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        FighterGear gear(const WeaponRecord* bow, const AmmoRecord* ammo)
        {
            FighterGear g;
            g.mBow = bow;
            g.mAmmo = ammo;
            return g;
        }
        void finish(const FakeWriter::Clip& clip, ClipEnd how = ClipEnd::Completed)
        {
            mCombat.onClipFinished(clip.mSubject, clip.mToken, how);
        }
        std::size_t clipsNamed(const std::string& name) const
        {
            return static_cast<std::size_t>(std::count_if(mWriter.mClips.begin(), mWriter.mClips.end(),
                [&](const FakeWriter::Clip& c) { return c.mName == name; }));
        }
        void realBowsOn()
        {
            mCombat.setRealGear(true);
            mCombat.setRangedGear(true);
            ASSERT_TRUE(mCombat.setTestHostile("Hulda"));
            mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        }
    };

    // ---- an NPC with a crossbow ----------------------------------------------------------------------------------

    TEST_F(RangedRun, anNpcCrossbowAimsFiresWhenTheReleaseBeginsAndReloads)
    {
        mData.mGear[kArcherBase] = gear(&mData.mCrossbow, &mData.mBolt);
        realBowsOn();
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::BowDraw);
        EXPECT_EQ(mWriter.mClips[0].mName, "dlc01/crossbow_aim");
        EXPECT_TRUE(mWriter.mLaunches.empty());
        finish(mWriter.mClips[0]); // the aim clip ended: the draw is done
        update();
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[1].mSlot, ClipSlot::BowRelease);
        EXPECT_EQ(mWriter.mClips[1].mName, "dlc01/crossbow_release");
        ASSERT_EQ(mWriter.mLaunches.size(), 1u); // the bolt went with the release: the clip has no event
        EXPECT_EQ(mWriter.mLaunches[0].mModel, "meshes/DLC01/Weapons/Crossbow/Bolt.nif");
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mRequest.speed, 7000.f); // the bolt's projectile, a full draw
        finish(mWriter.mClips[1]); // the release clip ended: the reload follows
        update();
        ASSERT_EQ(mWriter.mClips.size(), 3u);
        EXPECT_EQ(mWriter.mClips[2].mName, "dlc01/crossbow_reload");
        EXPECT_EQ(mWriter.mLaunches.size(), 1u); // no second bolt
        finish(mWriter.mClips[2]); // loaded again: the next aim comes after the hostile period
        update(3.f);
        EXPECT_EQ(clipsNamed("dlc01/crossbow_aim"), 2u);
    }

    TEST_F(RangedRun, aDwarvenCrossbowUsesTheDwarvenClips)
    {
        mData.mGear[kArcherBase] = gear(&mData.mDwarven, &mData.mBolt);
        realBowsOn();
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "dlc01/crossbow_aimdwarven");
        finish(mWriter.mClips[0]);
        update();
        EXPECT_EQ(mWriter.mClips.back().mName, "dlc01/crossbow_dwarvenrelease");
        finish(mWriter.mClips.back());
        update();
        EXPECT_EQ(mWriter.mClips.back().mName, "dlc01/crossbow_reloaddwarven");
    }

    TEST_F(RangedRun, aBoltDoesTheCrossbowPlusTheBoltDamageWithTheArchersSkill)
    {
        mData.mGear[kArcherBase] = gear(&mData.mCrossbow, &mData.mBolt);
        realBowsOn();
        update(3.f);
        finish(mWriter.mClips[0]);
        update();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        ProjectileResult r;
        r.owner = mWriter.mLaunches[0].mOwner;
        r.token = mWriter.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitPlayer;
        r.x = 800.0;
        mCombat.onProjectileResult(r);
        update();
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 36.25f, 1e-3f); // (19 + 10) x 1.25
    }

    TEST_F(RangedRun, aShotRemembersTheCrossbowThatFiredItNotTheTestSetting)
    {
        mData.mGear[kArcherBase] = gear(&mData.mCrossbow, &mData.mBolt);
        realBowsOn();
        update(3.f);
        finish(mWriter.mClips[0]);
        update();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        // the gear changes in flight (another weapon in the gear table); the bolt still does its own damage
        mData.mGear[kArcherBase] = gear(&mData.mBow, &mData.mArrow);
        ProjectileResult r;
        r.owner = mWriter.mLaunches[0].mOwner;
        r.token = mWriter.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 36.25f, 1e-3f);
    }

    TEST_F(RangedRun, aRealBowOfAnNpcDrawsTheBowClipAndShootsArrows)
    {
        mData.mGear[kArcherBase] = gear(&mData.mBow, &mData.mArrow);
        realBowsOn();
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "Bow_DrawLight");
        finish(mWriter.mClips[0]);
        update();
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[1].mName, "Bow_Release");
        EXPECT_TRUE(mWriter.mLaunches.empty()); // a bow waits for its arrowRelease event
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::BowRelease;
        e.token = mWriter.mClips[1].mToken;
        e.kind = ClipEventKind::BowRelease;
        mCombat.onClipEvent(e);
        update();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mRequest.speed, 6000.f);
    }

    TEST_F(RangedRun, withTheSettingOffTheBowOfTheGearIsNotUsed)
    {
        mData.mGear[kArcherBase] = gear(&mData.mCrossbow, &mData.mBolt);
        mCombat.setRealGear(true); // ranged gear stays off
        ASSERT_TRUE(mCombat.setTestHostile("Hulda"));
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(3.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(RangedRun, aBowWithNoAmmunitionShootsNothing)
    {
        mData.mGear[kArcherBase] = gear(&mData.mBow, nullptr);
        realBowsOn();
        update(3.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(RangedRun, theTestWeaponStillComesFirst)
    {
        mData.mGear[kArcherBase] = gear(&mData.mCrossbow, &mData.mBolt);
        ASSERT_TRUE(mCombat.setTestWeapon("IronBow"));
        ASSERT_TRUE(mCombat.setTestAmmo("IronArrow"));
        realBowsOn();
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "Bow_DrawLight");
    }

    TEST_F(RangedRun, aCrossbowAsTheTestWeaponWorksWithABolt)
    {
        ASSERT_TRUE(mCombat.setTestWeapon("DLC1CrossBow"));
        ASSERT_TRUE(mCombat.setTestAmmo("IronBolt"));
        ASSERT_TRUE(mCombat.setTestHostile("Hulda"));
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(3.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "dlc01/crossbow_aim");
    }

    // ---- the player ----------------------------------------------------------------------------------------------

    struct PlayerRanged : RangedRun
    {
        void SetUp() override
        {
            RangedRun::SetUp();
            mWriter.mPlayerState.x = 0.0;
            mWriter.mPlayerState.heading = kPi / 2.f;
            mWriter.mPlayerState.pitch = 0.1f;
            mCombat.setRangedGear(true);
        }
        void equip(const WeaponRecord* weapon, const AmmoRecord* ammo)
        {
            SkyrimCombat::PlayerEquipment eq;
            eq.mWeapon = weapon;
            eq.mAmmo = ammo;
            eq.mAmmoItem = ammo != nullptr ? kBoltItem : kNullFormId;
            mCombat.setPlayerEquipment(eq);
        }
        void hold(float seconds)
        {
            update(0.05f); // the data is read, the aim time asked for
            mCombat.onPlayerAttackEdge(true);
            update(0.05f);
            for (float t = 0.f; t < seconds; t += 0.05f)
                update(0.05f);
            mCombat.onPlayerAttackEdge(false);
            update(0.05f);
        }
    };

    TEST_F(PlayerRanged, anEquippedCrossbowShootsABoltWhenAimedAllTheWayAndSpendsTheAmmunition)
    {
        equip(&mData.mCrossbow, &mData.mBolt);
        hold(1.4f); // the aim clip is 1.287 s long
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_EQ(mWriter.mLaunches[0].mModel, "meshes/DLC01/Weapons/Crossbow/Bolt.nif");
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mRequest.speed, 7000.f); // a crossbow has no partial draw
        EXPECT_NEAR(mWriter.mLaunches[0].mRequest.pitch, 0.1f, 1e-6f);
        ASSERT_EQ(mSpent.size(), 1u);
        EXPECT_EQ(mSpent[0], kBoltItem);
        EXPECT_NE(std::find(mWriter.mTimingClips.begin(), mWriter.mTimingClips.end(), "dlc01/crossbow_aim"),
            mWriter.mTimingClips.end());
    }

    TEST_F(PlayerRanged, aCrossbowAimedTooShortShootsNothing)
    {
        equip(&mData.mCrossbow, &mData.mBolt);
        hold(0.5f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(mSpent.empty());
        EXPECT_TRUE(traceHas("crossbow cancelled"));
    }

    TEST_F(PlayerRanged, theCrossbowMustBeReloadedBeforeTheNextShot)
    {
        equip(&mData.mCrossbow, &mData.mBolt);
        hold(1.4f);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        mCombat.onPlayerAttackEdge(true); // at once: the reload (1.287 s) is still going
        update(0.05f);
        for (float t = 0.f; t < 1.4f; t += 0.05f)
            update(0.05f);
        mCombat.onPlayerAttackEdge(false);
        update(0.05f);
        EXPECT_EQ(mWriter.mLaunches.size(), 1u);
        hold(1.4f); // loaded again: the next shot
        EXPECT_EQ(mWriter.mLaunches.size(), 2u);
        EXPECT_EQ(mSpent.size(), 2u);
    }

    TEST_F(PlayerRanged, anEquippedBowShootsArrowsLikeTheTestBowDid)
    {
        equip(&mData.mBow, &mData.mArrow);
        hold(1.8f);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mRequest.speed, 6000.f);
        EXPECT_EQ(mWriter.mLaunches[0].mModel, "meshes/Weapons/Iron/IronArrowProjectile.nif");
        ASSERT_EQ(mSpent.size(), 1u);
    }

    TEST_F(PlayerRanged, aBoltFromThePlayersCrossbowDoesTheDamageOfThatCrossbowAndBolt)
    {
        equip(&mData.mCrossbow, &mData.mBolt);
        hold(1.4f);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
        mWriter.mStates[20] = mWriter.mStates[10];
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = mWriter.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitActor;
        r.actor = { 20, 1 };
        mCombat.onProjectileResult(r);
        update();
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 31.175f, 1e-3f); // (19 + 10) x 1.075
    }

    TEST_F(PlayerRanged, aRangedWeaponWithNoAmmunitionShootsNothingAndDoesNotSwingEither)
    {
        equip(&mData.mCrossbow, nullptr);
        hold(1.4f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_EQ(mCombat.playerSwingCount(), 0u);
        mCombat.onPlayerAttack(false);
        update(0.2f);
        EXPECT_EQ(mCombat.playerSwingCount(), 0u); // the melee press does nothing with a ranged weapon in hand
    }

    TEST_F(PlayerRanged, anAmmunitionThatDoesNotFitTheWeaponIsIgnored)
    {
        equip(&mData.mCrossbow, &mData.mArrow); // arrows in a crossbow
        hold(1.4f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(PlayerRanged, withTheSettingOffTheEquippedCrossbowIsNotUsed)
    {
        mCombat.setRangedGear(false);
        equip(&mData.mCrossbow, &mData.mBolt);
        hold(1.4f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(mSpent.empty());
    }

    // ---- dual wielding -------------------------------------------------------------------------------------------

    struct DualRun : RangedRun
    {
        void SetUp() override
        {
            RangedRun::SetUp();
            mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void style(std::uint32_t flags)
        {
            mData.mProfiles[kArcherBase].mAi.mStyle.mFlags = flags;
            mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        }
        void dual(bool on = true)
        {
            FighterGear g;
            g.mWeapon = &mData.mSword;
            g.mOffHand = &mData.mDagger;
            mData.mGear[kArcherBase] = g;
            mCombat.setRealGear(true);
            mCombat.setDualWield(on);
            ASSERT_TRUE(mCombat.setTestHostile("Hulda"));
            mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        }
        // One swing: the clip is played, its hit event comes, the sweep finds the player; the clip then ends.
        void swing()
        {
            const std::size_t before = mWriter.mClips.size();
            update(3.f);
            ASSERT_EQ(mWriter.mClips.size(), before + 1);
            const FakeWriter::Clip clip = mWriter.mClips.back();
            ClipEvent hit;
            hit.subject = clip.mSubject;
            hit.slot = clip.mSlot;
            hit.token = clip.mToken;
            hit.kind = ClipEventKind::Hit;
            mCombat.onClipEvent(hit);
            update();
            SweepHit found;
            found.target = SweepTarget::Player;
            found.distance = 20.f;
            mCombat.onSweepResult(clip.mSubject, clip.mToken, &found, 1);
            update();
            finish(clip);
        }
    };

    TEST_F(DualRun, aDualWielderSwingsItsRightHandThenItsLeftWithEachWeaponsDamage)
    {
        style(CombatStyleFlag::AllowDualWielding);
        dual();
        swing();
        swing();
        swing();
        ASSERT_EQ(mWriter.mClips.size(), 3u);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_AttackRight");
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Attack);
        EXPECT_EQ(mWriter.mClips[1].mName, "1HM_AttackLeft");
        EXPECT_EQ(mWriter.mClips[1].mSlot, ClipSlot::AttackLeft);
        EXPECT_EQ(mWriter.mClips[2].mName, "1HM_AttackRight");
        ASSERT_EQ(mWriter.mDamage.size(), 3u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f, 1e-4f); // the sword
        EXPECT_NEAR(mWriter.mDamage[1].amount, 4.f, 1e-4f); // the dagger
        EXPECT_NEAR(mWriter.mDamage[2].amount, 7.f, 1e-4f);
    }

    TEST_F(DualRun, aCombatStyleThatDoesNotAllowItKeepsTheRightHand)
    {
        style(CombatStyleFlag::Dueling);
        dual();
        swing();
        swing();
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_AttackRight");
        EXPECT_EQ(mWriter.mClips[1].mName, "1HM_AttackRight");
        EXPECT_NEAR(mWriter.mDamage[1].amount, 7.f, 1e-4f);
    }

    TEST_F(DualRun, withTheSettingOffNoLeftHandSwings)
    {
        style(CombatStyleFlag::AllowDualWielding);
        dual(false);
        swing();
        swing();
        EXPECT_EQ(clipsNamed("1HM_AttackLeft"), 0u);
    }

    TEST_F(DualRun, aDualPowerAttackIsTheDualClipAndHitsWithBothWeapons)
    {
        style(CombatStyleFlag::AllowDualWielding);
        dual();
        mCombat.setTestPowerEvery(1);
        swing();
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::PowerAttack);
        EXPECT_EQ(mWriter.mClips[0].mName, "DW_AttackPowerForward");
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        MeleeInput sword;
        sword.mWeapon = &mData.mSword;
        sword.mPowerAttack = true;
        MeleeInput dagger = sword;
        dagger.mWeapon = &mData.mDagger;
        const float both = computeMeleeHit(sword, CombatSettings()).mDamageAfter
            + computeMeleeHit(dagger, CombatSettings()).mDamageAfter;
        EXPECT_NEAR(mWriter.mDamage[0].amount, both, 1e-3f);
        EXPECT_GT(mWriter.mDamage[0].amount, computeMeleeHit(sword, CombatSettings()).mDamageAfter);
    }

    TEST_F(DualRun, aDualWielderHasNoShield)
    {
        style(CombatStyleFlag::AllowDualWielding);
        dual();
        mData.mGear[kArcherBase].mHasShield = true;
        mCombat.onActorAdded(info(10, kArcherBase, "Hulda"));
        update(0.1f);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), v));
        EXPECT_FALSE(v.mHasShield);
    }

    TEST_F(DualRun, thePlayerWithALeftHandTestWeaponAlternatesHands)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestWeapon("IronSword"));
        ASSERT_TRUE(mCombat.setTestPlayerOffHand("IronDagger"));
        update(0.1f);
        mCombat.onPlayerAttack(false);
        update(0.1f);
        EXPECT_EQ(mCombat.playerSwingClip(), "1HM_AttackRight");
        update(2.f); // the swing is over
        mCombat.onPlayerAttack(false);
        update(0.1f);
        EXPECT_EQ(mCombat.playerSwingClip(), "1HM_AttackLeft");
        update(2.f);
        mCombat.onPlayerAttack(false);
        update(0.1f);
        EXPECT_EQ(mCombat.playerSwingClip(), "1HM_AttackRight");
    }

    // ---- dual casting --------------------------------------------------------------------------------------------

    struct DualCastRun : RangedRun
    {
        void cast(bool dual)
        {
            mCombat.setDualCast(dual);
            ASSERT_TRUE(mCombat.setTestPlayerSpell("Mend"));
            mWriter.mPlayerState.health = 40.f;
            mCombat.onPlayerAttackEdge(true);
            for (int i = 0; i < 6; ++i)
                update(0.25f);
        }
    };

    TEST_F(DualCastRun, aSingleCastCostsTheSpellsCostAndHealsItsMagnitude)
    {
        cast(false);
        EXPECT_NEAR(mWriter.mPlayerState.health, 50.f, 1e-3f); // 40 + 10
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_NEAR(v.mMagicka, 180.f, 1e-3f); // 200 - 20
        EXPECT_FALSE(traceHas("with both hands"));
    }

    TEST_F(DualCastRun, aDualCastCostsTwoPointEightTimesAndHealsTwoPointTwoTimes)
    {
        cast(true);
        EXPECT_NEAR(mWriter.mPlayerState.health, 62.f, 1e-3f); // 40 + 10 x 2.2
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_NEAR(v.mMagicka, 144.f, 1e-3f); // 200 - 20 x 2.8
        EXPECT_TRUE(traceHas("with both hands"));
    }

    TEST_F(DualCastRun, theNumbersComeFromTheGmstsTheDataGives)
    {
        mData.mDualCast.mEffectiveness = 3.f;
        mData.mDualCast.mCostMult = 4.f;
        cast(true);
        EXPECT_NEAR(mWriter.mPlayerState.health, 70.f, 1e-3f); // 40 + 10 x 3
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_NEAR(v.mMagicka, 120.f, 1e-3f); // 200 - 20 x 4
    }

    TEST_F(DualCastRun, aCasterThatCannotPayTheDualCostCastsWithOneHand)
    {
        mData.mPlayer.mMagickaMax = 50.f; // 50 < 56 but 50 >= 20
        cast(true);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_NEAR(v.mMagicka, 30.f, 1e-3f); // the single cost
        EXPECT_NEAR(mWriter.mPlayerState.health, 50.f, 1e-3f);
    }
}
