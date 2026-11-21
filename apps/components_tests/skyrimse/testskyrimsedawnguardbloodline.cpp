#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/quest_priority.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Task 0334 (Dawnguard, after "Awakening"): Serana's follow packages come from DLC1NPCMentalModel (priority 80, an
// alias filled with her placement) and `DLC1VQ02SeranaAbandoned` from DLC1VQ02 (priority 70, an external alias): the
// quest with the higher priority is tried first whatever kind of alias gives the package.

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorClock;
    using openeden::sdk::game::ActorGoal;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorInfo;
    using openeden::sdk::game::ActorRequestWriter;

    PackageRecord package(FormId id, const char* name)
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mCommon.mEditorId = name;
        p.mHasPkdt = true;
        return p;
    }

    std::vector<FormId> ids(const std::vector<const PackageRecord*>& list)
    {
        std::vector<FormId> out;
        for (const PackageRecord* p : list)
            out.push_back(p->mCommon.mId);
        return out;
    }

    TEST(SkyrimAliasPackageOrder, aQuestOfHigherPriorityComesFirstWhateverKindOfAliasGivesThePackage)
    {
        const PackageRecord abandoned = package(0x10396, "DLC1VQ02SeranaAbandoned");
        const PackageRecord react = package(0x76C3, "DLC1VQ02SeranaReactToHarkon");
        const PackageRecord followFar = package(0x2BBB, "DLC1NPCFollowFar");
        const PackageRecord followClose = package(0x2BB9, "DLC1NPCFollowClose");
        std::vector<AliasPackageGroup> groups;
        groups.push_back({ 0x2F65, 70, true, { &abandoned, &react } }); // DLC1VQ02, external alias (a forced fill)
        groups.push_back({ 0x2B6E, 80, false, { &followFar, &followClose } }); // DLC1NPCMentalModel, her placement
        EXPECT_EQ(ids(orderAliasPackageGroups(groups)), (std::vector<FormId>{ 0x2BBB, 0x2BB9, 0x10396, 0x76C3 }));
    }

    TEST(SkyrimAliasPackageOrder, atTheSamePriorityAForcedAliasIsFirstThenTheQuestId)
    {
        const PackageRecord a = package(1, "A"), b = package(2, "B"), c = package(3, "C"), d = package(4, "D");
        std::vector<AliasPackageGroup> groups;
        groups.push_back({ 0x300, 70, false, { &a } });
        groups.push_back({ 0x200, 70, false, { &b } });
        groups.push_back({ 0x400, 70, true, { &c } });
        groups.push_back({ 0x100, 71, false, { &d } });
        EXPECT_EQ(ids(orderAliasPackageGroups(groups)), (std::vector<FormId>{ 4, 3, 2, 1 }));
    }

    TEST(SkyrimAliasPackageOrder, theOrderInsideAGroupIsKeptAndNothingIsLostOrDoubled)
    {
        const PackageRecord a = package(1, "A"), b = package(2, "B"), c = package(3, "C");
        std::vector<AliasPackageGroup> groups;
        groups.push_back({ 0x100, 70, false, { &c, &a } });
        groups.push_back({ 0x100, 70, true, { &b } });
        EXPECT_EQ(ids(orderAliasPackageGroups(groups)), (std::vector<FormId>{ 2, 3, 1 }));
        EXPECT_TRUE(orderAliasPackageGroups({}).empty());
    }

    // the brain tells its source which package it switched to (a developer trace)
    class TraceSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        bool tracesPackages() const override { return true; }
        void packageChosen(
            FormId reference, FormId base, const PackageRecord* chosen, const std::string&) const override
        {
            mChosen.push_back({ reference, base, chosen != nullptr ? chosen->mCommon.mId : 0 });
        }
        std::vector<const PackageRecord*> mList;
        struct Entry
        {
            FormId mReference, mBase, mPackage;
        };
        mutable std::vector<Entry> mChosen;
    };

    class QuietWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal&) override { return true; }
        bool cancelGoal(ActorHandle) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
    };

    TEST(SkyrimAliasPackageOrder, theBrainTellsItsSourceWhichPackageItSwitchedTo)
    {
        const PackageRecord first = package(7, "First");
        TraceSource source;
        source.mList = { &first };
        PackageBrain brain(source);
        QuietWriter writer;
        ActorInfo info;
        info.handle = { 0x2B74, 1 };
        info.base = 0x2B6C;
        brain.onActorAdded(info);
        brain.update(1.1f, writer);
        ASSERT_FALSE(source.mChosen.empty());
        EXPECT_EQ(source.mChosen.front().mReference, 0x2B74u);
        EXPECT_EQ(source.mChosen.front().mBase, 0x2B6Cu);
        EXPECT_EQ(source.mChosen.front().mPackage, 7u);
    }

    // A package that follows the player (Serana's DLC1NPCFollowMedium): the player's doors carry the actor and it takes
    // his side, although it is no brain follower (its packages still decide).
    class FollowSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        PackageActivity activityOf(const PackageRecord& p) const override
        {
            return p.mCommon.mId == 0x2BBA ? PackageActivity::Follow : PackageActivity::None;
        }
        bool targetOf(const PackageRecord&, PackageTarget& out) const override
        {
            out.mType = PackageTargetType::SpecificReference;
            out.mValue = 0x14; // the player
            return true;
        }
        bool conditionsPass(const PackageRecord& p, FormId, FormId) override { return p.mCommon.mId != mFailing; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        std::vector<const PackageRecord*> mList;
        FormId mFailing = 0;
    };

    TEST(SkyrimPackageFollowers, anActorWhoseChosenPackageFollowsThePlayerIsCarriedAndTakesHisSide)
    {
        const PackageRecord follow = package(0x2BBA, "DLC1NPCFollowMedium");
        const PackageRecord stay = package(0x2BBC, "Stay");
        FollowSource source;
        source.mList = { &follow, &stay };
        PackageBrain brain(source);
        brain.setFollowPlayerPackages(true);
        QuietWriter writer;
        ActorInfo info;
        info.handle = { 0x2B74, 1 };
        info.base = 0x2B6C;
        brain.onActorAdded(info);
        brain.update(1.1f, writer);
        EXPECT_TRUE(brain.followsByPackage(0x2B74));
        EXPECT_EQ(brain.packageFollowerRefs().size(), 1u);
        EXPECT_FALSE(brain.followsPlayer(0x2B74)); // no brain follower: not saved, the packages decide
        EXPECT_TRUE(brain.followerRefs().empty());
    }

    TEST(SkyrimPackageFollowers, theActorIsNoPackageFollowerWhenThePackageFailsItsConditionsOrTheSettingIsOff)
    {
        const PackageRecord follow = package(0x2BBA, "DLC1NPCFollowMedium");
        const PackageRecord stay = package(0x2BBC, "Stay");
        FollowSource source;
        source.mList = { &follow, &stay };
        source.mFailing = 0x2BBA;
        PackageBrain brain(source);
        brain.setFollowPlayerPackages(true);
        QuietWriter writer;
        ActorInfo info;
        info.handle = { 0x2B74, 1 };
        info.base = 0x2B6C;
        brain.onActorAdded(info);
        brain.update(1.1f, writer);
        EXPECT_FALSE(brain.followsByPackage(0x2B74));
        source.mFailing = 0;
        PackageBrain off(source);
        off.onActorAdded(info);
        off.update(1.1f, writer);
        EXPECT_FALSE(off.followsByPackage(0x2B74)); // the setting is off by default
        brain.onActorRemoved(info.handle);
        EXPECT_TRUE(brain.packageFollowerRefs().empty());
    }

    // The FollowPlayer template (0x0750BE) has two branches: Follow while WaitingForPlayer is 0, Sandbox (stay near)
    // while it is 1. Its names hold both, and a package that is a Follow with a sandbox branch is a Follow.
    TEST(SkyrimFollowTemplate, aFollowWithASandboxBranchIsAFollowAndNotASandbox)
    {
        const std::vector<std::string> followPlayer{ "Follow", "Sandbox", "Sandbox" };
        EXPECT_EQ(classifyActivity(followPlayer), PackageActivity::Follow);
        EXPECT_NE(classifyProcedures(followPlayer), PackageBehavior::Sandbox);
        const std::vector<std::string> followTo{ "FollowTo", "Travel", "Sandbox" };
        EXPECT_EQ(classifyActivity(followTo), PackageActivity::Follow);
    }

    TEST(SkyrimFollowTemplate, theOtherMixesKeepTheirAnswers)
    {
        // a sandbox package sandboxes, a patrol with a follow and a sandbox stays no activity, sleeping wins
        EXPECT_EQ(
            classifyActivity(std::vector<std::string>{ "Travel", "UnlockDoors", "Sandbox" }), PackageActivity::None);
        EXPECT_EQ(classifyActivity(std::vector<std::string>{ "Patrol", "Follow", "Sandbox", "Guard" }),
            PackageActivity::None);
        EXPECT_EQ(classifyActivity(std::vector<std::string>{ "Follow", "Sleep" }), PackageActivity::None);
        EXPECT_EQ(classifyProcedures(std::vector<std::string>{ "Travel", "UnlockDoors", "Sandbox" }),
            PackageBehavior::Sandbox);
        EXPECT_EQ(classifyProcedures(std::vector<std::string>{ "Follow", "Sleep" }), PackageBehavior::Sleep);
        EXPECT_EQ(classifyProcedures(std::vector<std::string>{ "Sandbox" }), PackageBehavior::Sandbox);
        EXPECT_EQ(classifyProcedures(std::vector<std::string>{ "Follow" }), PackageBehavior::Unsupported);
    }
}
