// Task 0212 (creatures, first slice: the wolf): the profile of a creature race, its appearance, and its combat clips.
// Hand-built records; no plugin bytes, no game data. The numbers asserted are the ones in
// docs/tasks/0212-skyrim-creatures-wolf.md section 2 (read from the installed data): the stride of WalkForward00_Wolf
// is 89.448 units in 1.2 s, of RunForward 300.081 units in 0.533333 s.
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/creatures.hpp>
#include <game_skyrimse/neutral_world.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr const char* kWolfSkeleton = "Actors\\Canine\\Character Assets Wolf\\skeleton.nif";

    // ---- the profile --------------------------------------------------------------------------------------------

    TEST(SkyrimCreatureProfile, theWolfSkeletonIsFoundInAnyCaseWithEitherSlashAndAnOptionalMeshRoot)
    {
        EXPECT_EQ(creatureProfileFor(kWolfSkeleton), &wolfProfile());
        EXPECT_EQ(creatureProfileFor("actors/canine/character assets wolf/skeleton.nif"), &wolfProfile());
        EXPECT_EQ(creatureProfileFor("meshes\\ACTORS\\CANINE\\CHARACTER ASSETS WOLF\\SKELETON.NIF"), &wolfProfile());
    }

    TEST(SkyrimCreatureProfile, peopleAndUnknownCreaturesHaveNoProfile)
    {
        EXPECT_EQ(creatureProfileFor("actors\\character\\character assets\\skeleton.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor("actors\\character\\character assets female\\skeleton_female.nif"), nullptr);
        EXPECT_EQ(creatureProfileFor(""), nullptr);
        // Not a prefix of some other path: a folder of the same name deeper down does not count.
        EXPECT_EQ(creatureProfileFor("dlc\\actors\\canine\\character assets wolf\\skeleton.nif"), nullptr);
    }

    TEST(SkyrimCreatureProfile, theWolfNumbersAreTheDatas)
    {
        const CreatureProfile& wolf = wolfProfile();
        EXPECT_EQ(wolf.mProject, "wolfproject");
        EXPECT_EQ(wolf.mIdle, "mt_idle_wolf");
        EXPECT_EQ(wolf.mWalk, "walkforward_wolf");
        EXPECT_EQ(wolf.mRun, "runforward");
        EXPECT_FLOAT_EQ(wolf.mWalkStride, 89.448f);
        EXPECT_FLOAT_EQ(wolf.mRunStride, 300.081f);
        EXPECT_NEAR(wolf.mWalkSpeed, 89.448f / 1.2f, 1e-3f);
        EXPECT_NEAR(wolf.mRunSpeed, 300.081f / 0.533333f, 0.05f);
        EXPECT_EQ(wolf.mAttack, "Attack1");
        EXPECT_EQ(wolf.mPower, "AttackPowerStanding");
        EXPECT_EQ(wolf.mRecoil, "Recoil");
        EXPECT_EQ(wolf.mStagger, "StaggerBackSmall");
        EXPECT_EQ(wolf.mDeath, "Death");
        EXPECT_GT(wolf.mCapsuleHeight, 48.f); // the core's minimum capsule
        EXPECT_LT(wolf.mCapsuleHeight, kCreatureReferenceHeight);
    }

    TEST(SkyrimCreatureProfile, theSettingKeyIsCreatures)
    {
        EXPECT_EQ(kCreaturesKey, "creatures");
    }

    // ---- the slot table -----------------------------------------------------------------------------------------

    TEST(SkyrimCreatureSlots, theWolfTableHasFiveClipsForNoWeapon)
    {
        const SlotTable table = SlotTable::wolf();
        EXPECT_EQ(table.size(), 5u);
        EXPECT_EQ(table.clipFor(ClipSlot::Attack, kWeaponAnimHandToHand), "Attack1");
        EXPECT_EQ(table.clipFor(ClipSlot::PowerAttack, kWeaponAnimHandToHand), "AttackPowerStanding");
        EXPECT_EQ(table.clipFor(ClipSlot::Recoil, kWeaponAnimHandToHand), "Recoil");
        EXPECT_EQ(table.clipFor(ClipSlot::Stagger, kWeaponAnimHandToHand), "StaggerBackSmall");
        EXPECT_EQ(table.clipFor(ClipSlot::Death, kWeaponAnimHandToHand), "Death");
        EXPECT_TRUE(table.clipFor(ClipSlot::Attack, kWeaponAnim1HSword).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::Bash, kWeaponAnimHandToHand).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::BlockHit, kWeaponAnimHandToHand).empty());
    }

    TEST(SkyrimCreatureSlots, theHumanoidTableIsUnchanged)
    {
        const SlotTable table = SlotTable::humanoid();
        EXPECT_EQ(table.size(), 7u);
        EXPECT_TRUE(table.clipFor(ClipSlot::Death, kWeaponAnim1HSword).empty());
    }

    // ---- the appearance -----------------------------------------------------------------------------------------

    constexpr FormId kRaceWolf = 0x10;
    constexpr FormId kRaceNord = 0x11;
    constexpr FormId kWolfNpc = 0x100;
    constexpr FormId kWolfNpcFemale = 0x101;
    constexpr FormId kNordNpc = 0x102;
    constexpr FormId kWolfSkin = 0x200;
    constexpr FormId kNordSkin = 0x201;
    constexpr FormId kWolfAddon = 0x500;
    constexpr FormId kNordAddon = 0x501;

    ActorIndex wolfWorld()
    {
        ActorRecords r;
        RaceRecord wolf;
        wolf.mCommon.mId = kRaceWolf;
        wolf.mSkeleton[0] = kWolfSkeleton; // the female field is empty, as in many creature races
        wolf.mSkin = kWolfSkin;
        wolf.mHeight = { 1.2f, 1.2f };
        r.mRaces.push_back(wolf);
        RaceRecord nord;
        nord.mCommon.mId = kRaceNord;
        nord.mSkeleton[0] = "actors\\character\\character assets\\skeleton.nif";
        nord.mSkeleton[1] = "actors\\character\\character assets female\\skeleton_female.nif";
        nord.mSkin = kNordSkin;
        nord.mHeight = { 1.03f, 1.0f };
        r.mRaces.push_back(nord);

        ArmatureRecord wolfAddon;
        wolfAddon.mCommon.mId = kWolfAddon;
        wolfAddon.mRace = kRaceWolf;
        wolfAddon.mSlotMask = 4;
        wolfAddon.mMaleModel = "Actors\\Canine\\Character Assets Wolf\\wolf.nif"; // no female model
        r.mArmatures.push_back(wolfAddon);
        ArmatureRecord nordAddon;
        nordAddon.mCommon.mId = kNordAddon;
        nordAddon.mRace = kRaceNord;
        nordAddon.mSlotMask = 4;
        nordAddon.mMaleModel = "m\\body.nif";
        nordAddon.mFemaleModel = "f\\body.nif";
        r.mArmatures.push_back(nordAddon);
        ArmorExtra wolfSkin;
        wolfSkin.mCommon.mId = kWolfSkin;
        wolfSkin.mArmatures = { kWolfAddon };
        r.mArmors.push_back(wolfSkin);
        ArmorExtra nordSkin;
        nordSkin.mCommon.mId = kNordSkin;
        nordSkin.mArmatures = { kNordAddon };
        r.mArmors.push_back(nordSkin);

        for (const auto& [id, race, female] : { std::tuple{ kWolfNpc, kRaceWolf, false },
                 std::tuple{ kWolfNpcFemale, kRaceWolf, true }, std::tuple{ kNordNpc, kRaceNord, false } })
        {
            ActorNpc n;
            n.mCommon.mId = id;
            n.mRace = race;
            n.mHasConfig = true;
            n.mFlags = female ? NpcFlags::Female : 0u;
            r.mNpcs.push_back(n);
        }
        ActorIndex index;
        index.add(r);
        return index;
    }

    AppearanceLimits creaturesOn()
    {
        AppearanceLimits limits;
        limits.mCreatures = true;
        return limits;
    }

    TEST(SkyrimCreatureAppearance, withTheSettingOnAWolfGetsItsAnimationFieldsAndItsOwnCapsule)
    {
        const ActorIndex index = wolfWorld();
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makeActorAppearance(index, kWolfNpc, 0, a, error, creaturesOn())) << error;
        EXPECT_EQ(a.skeletonPath, kWolfSkeleton);
        EXPECT_FALSE(a.female);
        ASSERT_EQ(a.parts.size(), 1u);
        EXPECT_EQ(a.parts[0].kind, ActorPartKind::Skin);
        EXPECT_EQ(a.parts[0].modelPath, "Actors\\Canine\\Character Assets Wolf\\wolf.nif");
        EXPECT_EQ(a.animationProject, "wolfproject");
        EXPECT_EQ(a.idleClip, "mt_idle_wolf");
        EXPECT_EQ(a.walkClip, "walkforward_wolf");
        EXPECT_EQ(a.runClip, "runforward");
        EXPECT_NEAR(a.walkSpeed, 74.54f, 0.01f);
        EXPECT_NEAR(a.runSpeed, 562.65f, 0.1f);
        EXPECT_FLOAT_EQ(a.walkStride, 89.448f);
        EXPECT_FLOAT_EQ(a.runStride, 300.081f);
        EXPECT_FLOAT_EQ(a.heightScale, wolfProfile().mCapsuleHeight / kCreatureReferenceHeight);
    }

    TEST(SkyrimCreatureAppearance, aFemaleWolfTakesTheMalesSkeletonAndModelButStaysFemale)
    {
        const ActorIndex index = wolfWorld();
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makeActorAppearance(index, kWolfNpcFemale, 0, a, error, creaturesOn())) << error;
        EXPECT_EQ(a.skeletonPath, kWolfSkeleton);
        EXPECT_TRUE(a.female);
        ASSERT_EQ(a.parts.size(), 1u);
        EXPECT_EQ(a.parts[0].modelPath, "Actors\\Canine\\Character Assets Wolf\\wolf.nif");
        EXPECT_EQ(a.animationProject, "wolfproject");
    }

    TEST(SkyrimCreatureAppearance, withTheSettingOffAWolfIsDescribedAsBefore)
    {
        const ActorIndex index = wolfWorld();
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makeActorAppearance(index, kWolfNpc, 0, a, error)) << error;
        EXPECT_TRUE(a.animationProject.empty());
        EXPECT_TRUE(a.idleClip.empty());
        EXPECT_TRUE(a.walkClip.empty());
        EXPECT_TRUE(a.runClip.empty());
        EXPECT_EQ(a.walkSpeed, 0.f);
        EXPECT_EQ(a.runSpeed, 0.f);
        EXPECT_EQ(a.walkStride, 0.f);
        EXPECT_EQ(a.runStride, 0.f);
        EXPECT_FLOAT_EQ(a.heightScale, 1.2f); // the race's own height, as before
        // And a female wolf, whose race has no female skeleton, is still refused.
        EXPECT_FALSE(makeActorAppearance(index, kWolfNpcFemale, 0, a, error));
        EXPECT_NE(error.find("no skeleton"), std::string::npos);
    }

    TEST(SkyrimCreatureAppearance, aPersonIsUnchangedByTheSetting)
    {
        const ActorIndex index = wolfWorld();
        ActorAppearance on, off;
        std::string error;
        ASSERT_TRUE(makeActorAppearance(index, kNordNpc, 0, on, error, creaturesOn())) << error;
        ASSERT_TRUE(makeActorAppearance(index, kNordNpc, 0, off, error)) << error;
        EXPECT_TRUE(on == off);
        EXPECT_TRUE(on.animationProject.empty());
        EXPECT_FLOAT_EQ(on.heightScale, 1.03f);
    }

    // ---- combat ---------------------------------------------------------------------------------------------------

    constexpr WorldId kWolfBase = 300;
    constexpr WorldId kManBase = 301;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            FighterProfile wolf;
            wolf.mHealthMax = 12.f;
            wolf.mStaminaMax = 200.f;
            wolf.mUnarmedDamage = 8.f;
            wolf.mUnarmedReach = 80.f;
            wolf.mCreature = &wolfProfile();
            mProfiles[kWolfBase] = wolf;
            FighterProfile man;
            man.mHealthMax = 100.f;
            man.mStaminaMax = 100.f;
            man.mUnarmedDamage = 4.f;
            man.mUnarmedReach = 96.f;
            mProfiles[kManBase] = man;
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
        CombatSettings settings() override { return {}; }
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
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip) });
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
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
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CreatureCombat : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(10, kWolfBase, "EncWolf"));
            mCombat.onActorAdded(info(20, kManBase, "Man"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        // The attacker's hit event, a frame to turn it into a sweep, the result, a frame to resolve it.
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

    TEST_F(CreatureCombat, aWolfThatIsHitRecoilsWithItsOwnClip)
    {
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_TRUE(mWriter.mClips[0].mSubject == CombatSubject::of(ActorHandle{ 10, 1 }));
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil);
        EXPECT_EQ(mWriter.mClips[0].mName, "Recoil");
    }

    TEST_F(CreatureCombat, aPersonThatIsHitStillRecoilsWithTheHumanoidClip)
    {
        blow({ 10, 1 }, { 20, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_RecoilRight");
    }

    TEST_F(CreatureCombat, aWolfThatDiesPlaysItsDeathClipAndIsReportedOnce)
    {
        mData.mProfiles[kWolfBase].mHealthMax = 3.f; // a hit of a man's unarmed 4 takes it to 0
        mCombat.onActorRemoved({ 10, 1 });
        mCombat.onActorAdded(info(10, kWolfBase, "EncWolf"));
        blow({ 20, 1 }, { 10, 1 });
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Death);
        EXPECT_EQ(mWriter.mClips[0].mName, "Death");
    }

    TEST_F(CreatureCombat, aPersonThatDiesStillPlaysTheStaggerClipAsItsDeath)
    {
        mData.mProfiles[kManBase].mHealthMax = 3.f;
        mCombat.onActorRemoved({ 20, 1 });
        mCombat.onActorAdded(info(20, kManBase, "Man"));
        blow({ 10, 1 }, { 20, 1 });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Death);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_StaggerBackMedium");
    }

    TEST_F(CreatureCombat, aHostileWolfInReachSwingsItsOwnAttackClipUnarmed)
    {
        ASSERT_TRUE(mCombat.setTestHostile("EncWolf"));
        mCombat.onActorAdded(info(10, kWolfBase, "EncWolf")); // seen again: now a hostile
        mWriter.mStates[10] = { 0.0, 60.0, 5.0, 0.f, 0.f, 0.f }; // 60 units from the player: inside the wolf's reach 80
        mCombat.combatUpdate(1.f, mWriter);
        ASSERT_FALSE(mWriter.mClips.empty());
        EXPECT_TRUE(mWriter.mClips[0].mSubject == CombatSubject::of(ActorHandle{ 10, 1 }));
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Attack);
        EXPECT_EQ(mWriter.mClips[0].mName, "Attack1");
    }
}
