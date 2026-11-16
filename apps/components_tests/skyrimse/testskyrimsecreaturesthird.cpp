// Task 0246 (creatures, third slice): seven more rows of the profile table (draugr, falmer, troll, frostbite spider,
// chaurus, hagraven, spriggan), the capsule radius of every row, and the appearance that carries it. Hand-built records;
// no plugin bytes, no game data. The numbers are the ones of docs/tasks/0246-skyrim-creatures-third.md section 2: strides
// and durations are the root-motion blocks of animationdata/boundanims/anims_<project>.txt, capsule heights and radii the
// bounds of the skin models.
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/creatures.hpp>

#include <openeden/game/actor_appearance.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    struct ThirdRow
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
        float mRadius;
        const char* mAttack;
        const char* mPower;
        const char* mRecoil;
        const char* mStagger;
        const char* mDeath;
    };

    // The numbers of the sidecars and the listing of Skyrim - Animations.bsa (see the task document).
    const ThirdRow kThird[] = {
        { "draugr", "Actors\\Draugr\\Character Assets\\Skeleton.nif", "draugrproject", "mtidle", "mtforward", "mtfastforward",
            76.9733f, 1.f, 180.921f, 0.533333f, 145.f, 18.f, "1hmattacka", "1hmattackpowerslash", "1hmrecoil",
            "mtstaggermedium", "special_deathbackward" },
        { "falmer", "Actors\\Falmer\\Character Assets\\Skeleton.nif", "falmerproject", "mtidle", "mtwalkforward",
            "mtrunforward", 167.438f, 1.666667f, 191.458f, 0.533333f, 145.f, 21.f, "h2hattack1", "h2hattack2",
            "h2hrecoilright", "1hmstaggerbackmid", "1hmstaggerbackheavy" },
        { "troll", "Actors\\Troll\\Character Assets\\skeleton.nif", "trollproject", "mtidle_standing", "mtwalkforward",
            "mtrunforward", 116.63f, 1.166667f, 179.669f, 0.666667f, 173.f, 20.f, "h2hattackrighta",
            "h2hattackrightforwardpower", "h2hrecoilrighta", "staggerbacka", "staggerbackc" },
        { "frostbitespider", "Actors\\FrostbiteSpider\\Character Assets\\skeleton.nif", "frostbitespiderproject",
            "mainidle", "forward_walk", "forward_run", 85.09f, 1.f, 300.2f, 1.f, 98.f, 44.f, "attack_rightchop",
            "attack_combochop", "recoil_bite", "stagger_small", "death" },
        { "chaurus", "Actors\\Chaurus\\Character Assets\\skeleton.nif", "chaurusproject", "mainidle", "forwardwalk",
            "forwardrun", 142.513f, 1.666667f, 350.265f, 1.f, 124.f, 46.f, "attackrcbite", "attackrheadbash",
            "recoilrbite", "staggersmall", "death" },
        { "hagraven", "Actors\\Hagraven\\Character Assets\\skeleton.nif", "hagravenproject", "idle", "walkforward",
            "runforward", 90.9832f, 1.333333f, 89.1586f, 0.766667f, 128.f, 17.f, "attackr", "attackl", "recoilr",
            "staggersmall", "staggerlarge" },
        { "spriggan", "Actors\\Spriggan\\Character Assets\\skeleton.nif", "spriggan", "mt_idle", "walkforward",
            "runforward", 95.7992f, 1.466667f, 263.173f, 0.733333f, 156.f, 16.f, "attackright1",
            "powerattackforwardx2", "recoilr", "staggerback_small", "death" },
    };

    // The radius of each of the ten earlier rows: half the smaller horizontal extent of the mid-body, at least 16.
    struct EarlierRadius
    {
        const char* mName;
        float mRadius;
    };
    const EarlierRadius kEarlier[] = { { "wolf", 16.f }, { "dog", 16.f }, { "bear", 68.f }, { "skeever", 16.f },
        { "horse", 32.f }, { "sabrecat", 32.f }, { "deer", 34.f }, { "goat", 16.f }, { "mammoth", 100.f },
        { "giant", 32.f } };

    const CreatureProfile* rowOf(std::string_view name)
    {
        for (const CreatureProfile& p : creatureProfilesAll())
            if (p.mName == name)
                return &p;
        return nullptr;
    }

    // ---- the table ------------------------------------------------------------------------------------------------

    TEST(SkyrimCreatureThirdTable, theFirstTenAreStillTheEarlierSliceAndTheAllTableAddsSevenAndTheDragon)
    {
        EXPECT_EQ(creatureProfiles().size(), 10u);
        ASSERT_EQ(creatureProfilesAll().size(), 10u + std::size(kThird) + 1u /* the dragon, task 0262 */);
        for (std::size_t i = 0; i < creatureProfiles().size(); ++i)
            EXPECT_EQ(&creatureProfiles()[i], &creatureProfilesAll()[i]) << i;
        EXPECT_EQ(&creatureProfilesAll()[0], &wolfProfile());
        std::set<std::string> names;
        for (const CreatureProfile& p : creatureProfilesAll())
            names.insert(std::string(p.mName));
        EXPECT_EQ(names.size(), creatureProfilesAll().size()) << "every name once";
        for (const ThirdRow& e : kThird)
            EXPECT_EQ(names.count(e.mName), 1u) << e.mName;
    }

    TEST(SkyrimCreatureThirdTable, everyNewRowIsComplete)
    {
        for (const ThirdRow& e : kThird)
        {
            SCOPED_TRACE(e.mName);
            const CreatureProfile* p = rowOf(e.mName);
            ASSERT_NE(p, nullptr);
            EXPECT_TRUE(p->mSkeletonFolder.starts_with("actors/"));
            EXPECT_FALSE(p->mProject.empty());
            for (const std::string_view clip :
                { p->mIdle, p->mWalk, p->mRun, p->mAttack, p->mPower, p->mRecoil, p->mStagger, p->mDeath })
                EXPECT_FALSE(clip.empty());
            EXPECT_GT(p->mWalkStride, 0.f);
            EXPECT_GT(p->mRunStride, 0.f);
            EXPECT_GT(p->mWalkSpeed, 0.f);
            EXPECT_GT(p->mRunSpeed, p->mWalkSpeed) << "a run is faster than a walk";
            EXPECT_GE(p->mCapsuleHeight, 48.f);
            EXPECT_LE(p->mCapsuleHeight, 640.f);
        }
    }

    TEST(SkyrimCreatureThirdTable, theFoldersAreUniqueAcrossAllRows)
    {
        std::set<std::string_view> folders;
        for (const CreatureProfile& p : creatureProfilesAll())
            EXPECT_TRUE(folders.insert(p.mSkeletonFolder).second) << p.mName;
    }

    TEST(SkyrimCreatureThirdTable, theNumbersAreTheDatasForEveryNewCreature)
    {
        for (const ThirdRow& e : kThird)
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

    TEST(SkyrimCreatureThirdTable, theClipSlotsNameTheRowsClips)
    {
        for (const ThirdRow& e : kThird)
        {
            SCOPED_TRACE(e.mName);
            const CreatureProfile* p = rowOf(e.mName);
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(creatureClipFor(*p, ClipSlot::Attack), e.mAttack);
            EXPECT_EQ(creatureClipFor(*p, ClipSlot::PowerAttack), e.mPower);
            EXPECT_EQ(creatureClipFor(*p, ClipSlot::Recoil), e.mRecoil);
            EXPECT_EQ(creatureClipFor(*p, ClipSlot::Stagger), e.mStagger);
            EXPECT_EQ(creatureClipFor(*p, ClipSlot::Death), e.mDeath);
            EXPECT_TRUE(creatureClipFor(*p, ClipSlot::BlockStart).empty());
            EXPECT_TRUE(creatureClipFor(*p, ClipSlot::Bash).empty());
        }
    }

    // ---- the lookup -----------------------------------------------------------------------------------------------

    TEST(SkyrimCreatureThirdLookup, everyNewCreatureIsFoundByItsRacesSkeleton)
    {
        for (const ThirdRow& e : kThird)
        {
            const CreatureProfile* p = creatureProfileFor(e.mSkeleton);
            ASSERT_NE(p, nullptr) << e.mName;
            EXPECT_EQ(p->mName, e.mName);
        }
        EXPECT_NE(creatureProfileFor("meshes\\ACTORS\\TROLL\\CHARACTER ASSETS\\SKELETON.NIF"), nullptr);
    }

    TEST(SkyrimCreatureThirdLookup, theDraugrRowTakesTheMaleFemaleAndSkeletonRaceSkeletons)
    {
        for (const char* path : { "Actors\\Draugr\\Character Assets\\Skeleton.nif",
                 "Actors\\Draugr\\Character Assets\\SkeletonF.nif", "Actors\\Draugr\\Character Assets\\SkeletonS.nif" })
        {
            const CreatureProfile* p = creatureProfileFor(path);
            ASSERT_NE(p, nullptr) << path;
            EXPECT_EQ(p->mName, "draugr") << path;
        }
        // A skin, or a skeleton of a DLC folder below, is not a skeleton of this row.
        EXPECT_EQ(creatureProfileFor("Actors\\Draugr\\Character Assets\\DLC01\\SkeletonWarrior.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("Actors\\Draugr\\Character Assets\\draugrmale.nif"), nullptr);
    }

    TEST(SkyrimCreatureThirdLookup, creaturesNoRowKnowsAndPeopleStillHaveNoProfile)
    {
        for (const char* path : { "Actors\\DwarvenSpider\\Character Assets\\skeleton.nif",
                 "Actors\\WerewolfBeast\\Character Assets\\skeleton.nif",
                 "Actors\\FrostbiteSpiderX\\Character Assets\\skeleton.nif",
                 "Actors\\Character\\Character Assets\\skeleton.nif",
                 "Actors\\Character\\Character Assets Female\\skeleton_female.nif", "mods\\actors\\troll\\character assets\\skeleton.nif",
                 "" })
            EXPECT_EQ(creatureProfileFor(path), nullptr) << path;
    }

    // ---- the capsule radius ---------------------------------------------------------------------------------------

    TEST(SkyrimCreatureThirdRadius, everyRowHasItsMeasuredRadius)
    {
        for (const EarlierRadius& e : kEarlier)
        {
            const CreatureProfile* p = rowOf(e.mName);
            ASSERT_NE(p, nullptr) << e.mName;
            EXPECT_FLOAT_EQ(p->mCapsuleRadius, e.mRadius) << e.mName;
        }
        for (const ThirdRow& e : kThird)
        {
            const CreatureProfile* p = rowOf(e.mName);
            ASSERT_NE(p, nullptr) << e.mName;
            EXPECT_FLOAT_EQ(p->mCapsuleRadius, e.mRadius) << e.mName;
        }
    }

    TEST(SkyrimCreatureThirdRadius, aRadiusIsNeverBelowTheMinimumOrBeyondWhatTheCoreAllows)
    {
        EXPECT_FLOAT_EQ(kCreatureMinRadius, 16.f);
        for (const CreatureProfile& p : creatureProfilesAll())
        {
            SCOPED_TRACE(std::string(p.mName));
            EXPECT_TRUE(std::isfinite(p.mCapsuleRadius));
            EXPECT_GE(p.mCapsuleRadius, kCreatureMinRadius);
            EXPECT_LE(p.mCapsuleRadius, p.mCapsuleHeight * 0.45f) << "the core would cut it";
        }
    }

    TEST(SkyrimCreatureThirdRadius, theBigOnesAreWiderThanThePersonsCapsuleAndTheSmallOnesAreNot)
    {
        EXPECT_GT(rowOf("mammoth")->mCapsuleRadius, 22.f);
        EXPECT_GT(rowOf("bear")->mCapsuleRadius, 22.f);
        EXPECT_GT(rowOf("frostbitespider")->mCapsuleRadius, 22.f);
        EXPECT_GT(rowOf("chaurus")->mCapsuleRadius, 22.f);
        EXPECT_LE(rowOf("wolf")->mCapsuleRadius, 22.f);
        EXPECT_LE(rowOf("skeever")->mCapsuleRadius, 22.f);
    }

    // ---- the appearance -------------------------------------------------------------------------------------------

    TEST(SkyrimCreatureThirdAppearance, theSdkVersionIsZeroPointSix)
    {
        EXPECT_EQ(kActorAppearanceVersion, (openeden::sdk::Version{ 0, 6 }));
        const ActorAppearance a;
        EXPECT_EQ(a.capsuleRadius, 0.f) << "a person, and a pack that does not say, get the core's radius";
    }

    ActorIndex worldOfThirdCreatures()
    {
        ActorRecords r;
        FormId id = 0x1000;
        for (const ThirdRow& e : kThird)
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

    TEST(SkyrimCreatureThirdAppearance, everyNewCreatureGetsItsRowsFieldsCapsuleAndRadius)
    {
        const ActorIndex index = worldOfThirdCreatures();
        AppearanceLimits limits;
        limits.mCreatures = true;
        FormId id = 0x1000;
        for (const ThirdRow& e : kThird)
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
            EXPECT_FLOAT_EQ(a.heightScale, e.mHeight / kCreatureReferenceHeight);
            EXPECT_FLOAT_EQ(a.capsuleRadius, e.mRadius);
            id += 0x10;
        }
    }

    TEST(SkyrimCreatureThirdAppearance, withTheSettingOffNoCreatureGetsARadius)
    {
        const ActorIndex index = worldOfThirdCreatures();
        FormId id = 0x1000;
        for (const ThirdRow& e : kThird)
        {
            SCOPED_TRACE(e.mName);
            ActorAppearance a;
            std::string error;
            ASSERT_TRUE(makeActorAppearance(index, id + 3, 0, a, error)) << error;
            EXPECT_EQ(a.capsuleRadius, 0.f);
            EXPECT_TRUE(a.animationProject.empty());
            id += 0x10;
        }
    }

    TEST(SkyrimCreatureThirdAppearance, aPersonOfTheCharacterRaceGetsNoRadiusEvenWithCreaturesOn)
    {
        ActorRecords r;
        RaceRecord race;
        race.mCommon.mId = 0x100;
        race.mSkeleton[0] = "Actors\\Character\\Character Assets\\skeleton.nif";
        race.mSkeleton[1] = "Actors\\Character\\Character Assets Female\\skeleton_female.nif";
        race.mSkin = 0x101;
        race.mHeight = { 1.f, 1.f };
        r.mRaces.push_back(race);
        ArmatureRecord addon;
        addon.mCommon.mId = 0x102;
        addon.mRace = 0x100;
        addon.mSlotMask = 4;
        addon.mMaleModel = "skin\\man.nif";
        r.mArmatures.push_back(addon);
        ArmorExtra skin;
        skin.mCommon.mId = 0x101;
        skin.mArmatures = { 0x102 };
        r.mArmors.push_back(skin);
        ActorNpc npc;
        npc.mCommon.mId = 0x103;
        npc.mRace = 0x100;
        npc.mHasConfig = true;
        r.mNpcs.push_back(npc);
        ActorIndex index;
        index.add(r);
        AppearanceLimits limits;
        limits.mCreatures = true;
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makeActorAppearance(index, 0x103, 0, a, error, limits)) << error;
        EXPECT_EQ(a.capsuleRadius, 0.f);
        EXPECT_TRUE(a.animationProject.empty());
    }
}
