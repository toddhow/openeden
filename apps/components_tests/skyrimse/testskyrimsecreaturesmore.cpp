// Task 0216 (creatures, second slice): the profile table of ten creatures, the clip of each combat slot, the appearance
// of each, and combat for a creature by placement. Hand-built records; no plugin bytes, no game data. The numbers are
// the ones in docs/tasks/0216-skyrim-creatures-more.md section 2: strides and durations are the root-motion blocks of
// animationdata/boundanims/anims_<project>.txt, capsule heights the bounds of the skin models.
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/creatures.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    struct Expect
    {
        const char* mName;
        const char* mSkeleton;
        const char* mProject;
        const char* mIdle;
        const char* mWalk;
        const char* mRun;
        float mWalkStride;
        float mWalkSeconds;
        float mRunStride;
        float mRunSeconds;
        float mHeight;
        const char* mAttack;
        const char* mPower;
        const char* mRecoil;
        const char* mStagger;
        const char* mDeath;
    };

    // The numbers of the sidecars and the listing of Skyrim - Animations.bsa (see the task document).
    const Expect kExpected[] = {
        { "wolf", "Actors\\Canine\\Character Assets Wolf\\skeleton.nif", "wolfproject", "mt_idle_wolf",
            "walkforward_wolf", "runforward", 89.448f, 1.2f, 300.081f, 0.533333f, 80.f, "Attack1",
            "AttackPowerStanding", "Recoil", "StaggerBackSmall", "Death" },
        { "dog", "Actors\\Canine\\Character Assets Dog\\skeleton.nif", "dogproject", "mt_idle", "walkforward",
            "runforward", 89.448f, 1.2f, 300.081f, 0.533333f, 76.f, "attack1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "death" },
        { "bear", "Actors\\Bear\\Character Assets\\skeleton.nif", "bearproject", "mt_idle", "walkforward",
            "runforward", 99.7136f, 1.666667f, 382.846f, 0.6f, 192.f, "attack1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "death" },
        { "skeever", "Actors\\Skeever\\Character Assets\\skeleton.nif", "skeeverproject", "mt_idle", "walkforward",
            "runforward", 37.3473f, 1.f, 260.128f, 0.533333f, 57.f, "attack1", "attackpowerforward", "recoil",
            "staggerbacksmall", "staggerbacklarge" },
        { "horse", "Actors\\Horse\\Character Assets\\skeleton.nif", "horseproject", "idle", "walkforward",
            "runforward", 137.624f, 1.1f, 212.32f, 0.466667f, 169.f, "attack2", "attack1", "recoil2",
            "staggerbacksmall", "staggerbacklarge" },
        { "sabrecat", "Actors\\SabreCat\\Character Assets\\Skeleton.nif", "sabrecatproject", "mt_idle",
            "walkforward", "runforward", 162.094f, 1.433333f, 359.399f, 0.733333f, 117.f, "attack1",
            "attackpowerforward", "recoil", "staggerbacksmall", "staggerbacklarge" },
        { "deer", "Actors\\Deer\\Character Assets\\Skeleton.nif", "deerproject", "mt_idle", "walkforward",
            "runforward", 169.802f, 1.f, 500.f, 0.6f, 214.f, "attack1", "attack2", "recoil", "staggerbacksmall",
            "staggerbacklarge" },
        { "goat", "Actors\\Goat\\Character Assets\\skeleton.nif", "goatproject", "mt_idle", "walkforward",
            "runforward", 62.1303f, 1.f, 360.f, 1.f, 82.f, "attack1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "staggerbacklarge" },
        { "mammoth", "Actors\\Mammoth\\Character Assets\\skeleton.nif", "mammothproject", "mt_idle",
            "walkforward", "runforward", 247.36f, 4.f, 400.f, 1.f, 376.f, "attack1", "attackpowerstanding",
            "recoil", "staggerbacksmall", "deathpose" },
        { "giant", "Actors\\Giant\\Character Assets\\skeleton.nif", "giantproject", "idle", "walkforward",
            "runforward", 164.134f, 2.666667f, 415.f, 1.f, 267.f, "attack1x2", "attack_stomp", "stagger_small",
            "stagger_large", "deathpose" },
    };

    // ---- the table ------------------------------------------------------------------------------------------------

    TEST(SkyrimCreatureTable, itHasTheTenCreaturesOfTheSliceAndTheWolfIsFirst)
    {
        const std::span<const CreatureProfile> all = creatureProfiles();
        ASSERT_EQ(all.size(), std::size(kExpected));
        EXPECT_EQ(&all[0], &wolfProfile());
        std::set<std::string> names;
        for (const CreatureProfile& p : all)
            names.insert(std::string(p.mName));
        for (const Expect& e : kExpected)
            EXPECT_EQ(names.count(e.mName), 1u) << e.mName;
    }

    TEST(SkyrimCreatureTable, everyRowIsComplete)
    {
        std::set<std::string_view> folders;
        for (const CreatureProfile& p : creatureProfiles())
        {
            SCOPED_TRACE(std::string(p.mName));
            EXPECT_FALSE(p.mName.empty());
            EXPECT_TRUE(p.mSkeletonFolder.starts_with("actors/"));
            EXPECT_TRUE(p.mSkeletonFolder.ends_with("/")) << "a folder, so a prefix test cannot match a longer name";
            EXPECT_FALSE(p.mProject.empty());
            EXPECT_TRUE(p.mProject.ends_with("project"));
            for (const std::string_view clip :
                { p.mIdle, p.mWalk, p.mRun, p.mAttack, p.mPower, p.mRecoil, p.mStagger, p.mDeath })
                EXPECT_FALSE(clip.empty());
            EXPECT_GT(p.mWalkStride, 0.f);
            EXPECT_GT(p.mRunStride, 0.f);
            EXPECT_GT(p.mWalkSpeed, 0.f);
            EXPECT_GT(p.mRunSpeed, p.mWalkSpeed) << "a run is faster than a walk";
            EXPECT_GE(p.mCapsuleHeight, 48.f) << "the core's minimum capsule";
            EXPECT_LE(p.mCapsuleHeight, 640.f) << "the core's maximum capsule";
            EXPECT_TRUE(folders.insert(p.mSkeletonFolder).second) << "one row per skeleton folder";
        }
    }

    TEST(SkyrimCreatureTable, theNumbersAreTheDatasForEveryCreature)
    {
        for (const Expect& e : kExpected)
        {
            SCOPED_TRACE(e.mName);
            const CreatureProfile* p = creatureProfileFor(e.mSkeleton);
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(p->mName, e.mName);
            EXPECT_EQ(p->mProject, e.mProject);
            EXPECT_EQ(p->mIdle, e.mIdle);
            EXPECT_EQ(p->mWalk, e.mWalk);
            EXPECT_EQ(p->mRun, e.mRun);
            EXPECT_FLOAT_EQ(p->mWalkStride, e.mWalkStride);
            EXPECT_FLOAT_EQ(p->mRunStride, e.mRunStride);
            EXPECT_NEAR(p->mWalkSpeed, e.mWalkStride / e.mWalkSeconds, e.mWalkStride / e.mWalkSeconds * 1e-3f);
            EXPECT_NEAR(p->mRunSpeed, e.mRunStride / e.mRunSeconds, e.mRunStride / e.mRunSeconds * 1e-3f);
            EXPECT_FLOAT_EQ(p->mCapsuleHeight, e.mHeight);
            EXPECT_EQ(p->mAttack, e.mAttack);
            EXPECT_EQ(p->mPower, e.mPower);
            EXPECT_EQ(p->mRecoil, e.mRecoil);
            EXPECT_EQ(p->mStagger, e.mStagger);
            EXPECT_EQ(p->mDeath, e.mDeath);
        }
    }

    TEST(SkyrimCreatureTable, theLookupSeesThroughCaseSlashesAndTheMeshRoot)
    {
        ASSERT_NE(creatureProfileFor("meshes\\ACTORS\\BEAR\\CHARACTER ASSETS\\SKELETON.NIF"), nullptr);
        EXPECT_EQ(creatureProfileFor("meshes\\ACTORS\\BEAR\\CHARACTER ASSETS\\SKELETON.NIF")->mName, "bear");
        ASSERT_NE(creatureProfileFor("actors/horse/character assets/skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("actors/horse/character assets/skeleton.nif")->mName, "horse");
        ASSERT_NE(creatureProfileFor("Actors\\SabreCat\\Character Assets\\Skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("Actors\\SabreCat\\Character Assets\\Skeleton.nif")->mName, "sabrecat");
    }

    TEST(SkyrimCreatureTable, theDogAndTheWolfShareAFolderButNotARow)
    {
        const CreatureProfile* wolf = creatureProfileFor("Actors\\Canine\\Character Assets Wolf\\skeleton.nif");
        const CreatureProfile* dog = creatureProfileFor("Actors\\Canine\\Character Assets Dog\\skeleton.nif");
        ASSERT_NE(wolf, nullptr);
        ASSERT_NE(dog, nullptr);
        EXPECT_NE(wolf, dog);
        EXPECT_EQ(wolf->mProject, "wolfproject");
        EXPECT_EQ(dog->mProject, "dogproject");
    }

    TEST(SkyrimCreatureTable, creaturesThisSliceDoesNotKnowAndPeopleHaveNoProfile)
    {
        for (const char* path : { "Actors\\Draugr\\Character Assets\\Skeleton.nif",
                 "Actors\\Dragon\\Character Assets\\Skeleton.nif", "Actors\\Hagraven\\Character Assets\\skeleton.nif",
                 "Actors\\Ambient\\Hare\\Character Assets\\skeleton.nif",
                 "Actors\\Ambient\\Chicken\\Character Assets\\Skeleton.nif",
                 "Actors\\Character\\Character Assets\\skeleton.nif",
                 "Actors\\Character\\Character Assets Female\\skeleton_female.nif",
                 "Actors\\Character\\Character Assets\\skeletonBeast.nif", "" })
            EXPECT_EQ(creatureProfileFor(path), nullptr) << path;
    }

    TEST(SkyrimCreatureTable, aFolderThatOnlyStartsLikeARowIsNotThatRow)
    {
        EXPECT_EQ(creatureProfileFor("actors\\bearcub\\character assets\\skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("actors\\canine\\character assets wolfpup\\skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("mods\\actors\\bear\\character assets\\skeleton.nif"), nullptr);
    }

    // ---- the clip of each slot ------------------------------------------------------------------------------------

    TEST(SkyrimCreatureClips, eachSlotNamesTheRowsClip)
    {
        for (const CreatureProfile& p : creatureProfiles())
        {
            SCOPED_TRACE(std::string(p.mName));
            EXPECT_EQ(creatureClipFor(p, ClipSlot::Attack), p.mAttack);
            EXPECT_EQ(creatureClipFor(p, ClipSlot::PowerAttack), p.mPower);
            EXPECT_EQ(creatureClipFor(p, ClipSlot::Recoil), p.mRecoil);
            EXPECT_EQ(creatureClipFor(p, ClipSlot::Stagger), p.mStagger);
            EXPECT_EQ(creatureClipFor(p, ClipSlot::Death), p.mDeath);
        }
    }

    TEST(SkyrimCreatureClips, aCreatureHasNoBlockBashCastOrBowClips)
    {
        for (const CreatureProfile& p : creatureProfiles())
            for (const ClipSlot slot : { ClipSlot::None, ClipSlot::AttackLeft, ClipSlot::BlockStart,
                     ClipSlot::BlockHit, ClipSlot::BlockStop, ClipSlot::Bash, ClipSlot::BowDraw, ClipSlot::BowRelease,
                     ClipSlot::Cast })
                EXPECT_TRUE(creatureClipFor(p, slot).empty()) << p.mName << " slot " << int(slot);
    }

    TEST(SkyrimCreatureClips, theWolfsClipsEqualTheOldWolfTable)
    {
        const SlotTable table = SlotTable::wolf();
        for (const ClipSlot slot :
            { ClipSlot::Attack, ClipSlot::PowerAttack, ClipSlot::Recoil, ClipSlot::Stagger, ClipSlot::Death })
            EXPECT_EQ(creatureClipFor(wolfProfile(), slot), table.clipFor(slot, kWeaponAnimHandToHand));
    }

    // ---- the appearance -------------------------------------------------------------------------------------------

    // One race per row, named by the row's skeleton, with a skin model that has only the male path (as the wolf's).
    ActorIndex worldOfAllCreatures()
    {
        ActorRecords r;
        FormId id = 0x1000;
        for (const Expect& e : kExpected)
        {
            RaceRecord race;
            race.mCommon.mId = id;
            race.mSkeleton[0] = e.mSkeleton;
            race.mSkeleton[1] = e.mSkeleton;
            race.mSkin = id + 1;
            race.mHeight = { 1.f, 1.f };
            r.mRaces.push_back(race);
            ArmatureRecord addon;
            addon.mCommon.mId = id + 2;
            addon.mRace = id;
            addon.mSlotMask = 4;
            addon.mMaleModel = std::string("skin\\") + e.mName + ".nif";
            r.mArmatures.push_back(addon);
            ArmorExtra skin;
            skin.mCommon.mId = id + 1;
            skin.mArmatures = { id + 2 };
            r.mArmors.push_back(skin);
            ActorNpc npc;
            npc.mCommon.mId = id + 3;
            npc.mRace = id;
            npc.mHasConfig = true;
            r.mNpcs.push_back(npc);
            id += 0x10;
        }
        ActorIndex index;
        index.add(r);
        return index;
    }

    TEST(SkyrimCreatureAppearanceMore, everyCreatureGetsItsRowsFieldsAndCapsule)
    {
        const ActorIndex index = worldOfAllCreatures();
        AppearanceLimits limits;
        limits.mCreatures = true;
        FormId id = 0x1000;
        for (const Expect& e : kExpected)
        {
            SCOPED_TRACE(e.mName);
            ActorAppearance a;
            std::string error;
            ASSERT_TRUE(makeActorAppearance(index, id + 3, 0, a, error, limits)) << error;
            EXPECT_EQ(a.skeletonPath, e.mSkeleton);
            EXPECT_EQ(a.animationProject, e.mProject);
            EXPECT_EQ(a.idleClip, e.mIdle);
            EXPECT_EQ(a.walkClip, e.mWalk);
            EXPECT_EQ(a.runClip, e.mRun);
            EXPECT_FLOAT_EQ(a.walkStride, e.mWalkStride);
            EXPECT_FLOAT_EQ(a.runStride, e.mRunStride);
            EXPECT_FLOAT_EQ(a.heightScale, e.mHeight / kCreatureReferenceHeight);
            ASSERT_EQ(a.parts.size(), 1u);
            EXPECT_EQ(a.parts[0].kind, ActorPartKind::Skin);
            id += 0x10;
        }
    }

    TEST(SkyrimCreatureAppearanceMore, withTheSettingOffNoCreatureGetsAnimationFields)
    {
        const ActorIndex index = worldOfAllCreatures();
        FormId id = 0x1000;
        for (const Expect& e : kExpected)
        {
            SCOPED_TRACE(e.mName);
            ActorAppearance a;
            std::string error;
            ASSERT_TRUE(makeActorAppearance(index, id + 3, 0, a, error)) << error;
            EXPECT_TRUE(a.animationProject.empty());
            EXPECT_TRUE(a.idleClip.empty());
            EXPECT_EQ(a.walkSpeed, 0.f);
            EXPECT_FLOAT_EQ(a.heightScale, 1.f);
            id += 0x10;
        }
    }

    // ---- combat for a creature ------------------------------------------------------------------------------------

    constexpr WorldId kBearBase = 400;
    constexpr WorldId kGiantBase = 401;
    constexpr WorldId kHorseBase = 402;
    constexpr WorldId kPlainBase = 403;
    constexpr WorldId kManBase = 404;

    const CreatureProfile& rowNamed(std::string_view name)
    {
        for (const CreatureProfile& p : creatureProfiles())
            if (p.mName == name)
                return p;
        return wolfProfile(); // never reached for a name of the table
    }

    FighterProfile creatureFighter(const CreatureProfile* creature, float health)
    {
        FighterProfile f;
        f.mHealthMax = health;
        f.mStaminaMax = 100.f;
        f.mUnarmedDamage = 8.f;
        f.mUnarmedReach = 80.f;
        f.mCreature = creature;
        return f;
    }

    class MoreData final : public CombatData
    {
    public:
        MoreData()
        {
            mProfiles[kBearBase] = creatureFighter(&rowNamed("bear"), 50.f);
            mProfiles[kGiantBase] = creatureFighter(&rowNamed("giant"), 3.f);
            mProfiles[kHorseBase] = creatureFighter(&rowNamed("horse"), 3.f);
            mProfiles[kManBase] = creatureFighter(nullptr, 100.f);
            mProfiles[kManBase].mUnarmedDamage = 4.f;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            ++mPlainCalls;
            return find(base, out);
        }
        bool fighterFor(WorldId base, std::uint32_t variation, FighterProfile& out) override
        {
            mVariations.push_back(variation);
            if (base == kPlainBase) // a stand-in whose template is picked by the variation
            {
                out = creatureFighter(variation % 2 == 0 ? &rowNamed("bear") : &rowNamed("horse"), 50.f);
                return true;
            }
            return find(base, out);
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return {}; }
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        std::vector<std::uint32_t> mVariations;
        int mPlainCalls = 0;

    private:
        bool find(WorldId base, FighterProfile& out)
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
    };

    class DefaultOnlyData final : public CombatData
    {
    public:
        bool fighter(WorldId base, FighterProfile& out) override
        {
            mBases.push_back(base);
            out = creatureFighter(nullptr, 10.f);
            return true;
        }
        FighterProfile playerFighter() override { return {}; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return {}; }
        std::vector<WorldId> mBases;
    };

    class MoreWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            ClipSlot mSlot;
            std::string mName;
            WorldId mReference;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ request.slot, std::string(request.clip), subject.actor.reference });
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport&) override { return true; }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
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
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        return i;
    }

    struct MoreCreatureCombat : ::testing::Test
    {
        MoreData mData;
        SkyrimCombat mCombat;
        MoreWriter mWriter;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        void blow(ActorHandle attacker, ActorHandle victim)
        {
            ClipEvent e;
            e.subject = CombatSubject::of(attacker);
            e.slot = ClipSlot::Attack;
            e.token = 5;
            e.kind = ClipEventKind::Hit;
            mCombat.onClipEvent(e);
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit h;
            h.target = SweepTarget::Actor;
            h.actor = victim;
            h.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::of(attacker), 5, &h, 1);
            mCombat.combatUpdate(0.016f, mWriter);
        }
    };

    TEST_F(MoreCreatureCombat, aBearThatIsHitRecoilsWithTheBearsClip)
    {
        mCombat.onActorAdded(infoOf(10, kBearBase));
        mCombat.onActorAdded(infoOf(20, kManBase));
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil);
        EXPECT_EQ(mWriter.mClips[0].mName, "recoil");
    }

    TEST_F(MoreCreatureCombat, aGiantThatDiesPlaysItsOwnDeathClip)
    {
        mData.mProfiles[kManBase].mUnarmedDamage = 40.f;
        mCombat.onActorAdded(infoOf(10, kGiantBase));
        mCombat.onActorAdded(infoOf(20, kManBase));
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Death);
        EXPECT_EQ(mWriter.mClips[0].mName, "deathpose");
    }

    TEST_F(MoreCreatureCombat, aHorseThatDiesFallsWithTheLargeStaggerBecauseItHasNoDeathClip)
    {
        mData.mProfiles[kManBase].mUnarmedDamage = 40.f;
        mCombat.onActorAdded(infoOf(10, kHorseBase));
        mCombat.onActorAdded(infoOf(20, kManBase));
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Death);
        EXPECT_EQ(mWriter.mClips[0].mName, "staggerbacklarge");
    }

    TEST_F(MoreCreatureCombat, theFighterOfAPlacementIsMadeWithThePlacementsOwnNumber)
    {
        mCombat.onActorAdded(infoOf(7, kPlainBase));
        mCombat.onActorAdded(infoOf(8, kPlainBase));
        mCombat.onActorAdded(infoOf(20, kManBase));
        mWriter.mStates[7] = { 100.0, 200.0, 5.0, 0.f, 0.f, 0.f };
        mWriter.mStates[8] = { 100.0, 220.0, 5.0, 0.f, 0.f, 0.f };
        blow({ 20, 1 }, { 7, 1 });
        blow({ 20, 1 }, { 8, 1 });
        EXPECT_NE(std::find(mData.mVariations.begin(), mData.mVariations.end(), 7u), mData.mVariations.end());
        EXPECT_NE(std::find(mData.mVariations.begin(), mData.mVariations.end(), 8u), mData.mVariations.end());
        EXPECT_EQ(mData.mPlainCalls, 0) << "the creature-aware call is the one made";
    }

    TEST_F(MoreCreatureCombat, twoPlacementsOfOneStandInGetTheirOwnCreaturesClips)
    {
        mCombat.onActorAdded(infoOf(6, kPlainBase)); // even: a bear
        mCombat.onActorAdded(infoOf(7, kPlainBase)); // odd: a horse
        mCombat.onActorAdded(infoOf(20, kManBase));
        mWriter.mStates[6] = { 100.0, 200.0, 5.0, 0.f, 0.f, 0.f };
        mWriter.mStates[7] = { 100.0, 220.0, 5.0, 0.f, 0.f, 0.f };
        blow({ 20, 1 }, { 6, 1 });
        blow({ 20, 1 }, { 7, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 2u);
        EXPECT_EQ(mWriter.mClips[0].mName, "recoil");
        EXPECT_EQ(mWriter.mClips[1].mName, "recoil2");
    }

    TEST(SkyrimCreatureCombatDefault, theCreatureAwareCallFallsBackToThePlainOne)
    {
        DefaultOnlyData data;
        FighterProfile out;
        EXPECT_TRUE(data.fighterFor(77, 123u, out));
        ASSERT_EQ(data.mBases.size(), 1u);
        EXPECT_EQ(data.mBases[0], 77u);
        EXPECT_FLOAT_EQ(out.mHealthMax, 10.f);
    }

    TEST_F(MoreCreatureCombat, aPersonStillUsesTheHumanoidClips)
    {
        mCombat.onActorAdded(infoOf(10, kManBase));
        mCombat.onActorAdded(infoOf(20, kBearBase));
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_RecoilRight");
    }
}
