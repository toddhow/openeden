#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <vector>

// Task 0274 (the Helgen cart ride) and the package brain: a list that belongs to ONE placement (an alias filled with a
// specific reference), a list that is read again when a quest moves on, and an actor that sits on a cart.

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorClock;
    using openeden::sdk::game::ActorGoal;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorInfo;
    using openeden::sdk::game::ActorRequestWriter;
    using openeden::sdk::game::WorldId;

    constexpr FormId kBase = 0xB9E1D;
    constexpr WorldId kHorse1 = 0xB9DF2, kHorse2 = 0xBB971;

    class RefSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mBaseList;
            return true;
        }
        bool packagesForReference(FormId reference, FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mBaseList;
            const auto found = mOfReference.find(reference);
            if (found != mOfReference.end())
                out.insert(out.begin(), found->second.begin(), found->second.end());
            return true;
        }
        std::uint64_t packageListRevision() const override { return mRevision; }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }

        std::vector<const PackageRecord*> mBaseList;
        std::map<FormId, std::vector<const PackageRecord*>> mOfReference;
        std::uint64_t mRevision = 0;
    };

    // The base class' default: no reference list of its own
    class BaseOnlySource final : public PackageSource
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
        std::vector<const PackageRecord*> mList;
    };

    class QuietWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal&) override
        {
            ++mGoals;
            return true;
        }
        bool cancelGoal(ActorHandle) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
        int mGoals = 0;
    };

    PackageRecord package(FormId id)
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mHasPkdt = true;
        return p;
    }

    void addHorse(PackageBrain& brain, WorldId reference, std::uint32_t session)
    {
        ActorInfo info;
        info.handle = { reference, session };
        info.base = kBase;
        info.editorId = "MQ101CartHorse";
        brain.onActorAdded(info);
    }

    TEST(SkyrimCartBrain, twoPlacementsOfOneBaseGetTheirOwnAliasPackages)
    {
        std::vector<PackageRecord> packs{ package(1), package(2), package(3) };
        RefSource source;
        source.mBaseList = { &packs[0] };
        source.mOfReference[kHorse1] = { &packs[1] };
        source.mOfReference[kHorse2] = { &packs[2], &packs[1] };
        PackageBrain brain(source);
        QuietWriter writer;
        addHorse(brain, kHorse1, 1);
        addHorse(brain, kHorse2, 2);
        brain.update(1.1f, writer);
        ASSERT_TRUE(brain.view(kHorse1).has_value());
        ASSERT_TRUE(brain.view(kHorse2).has_value());
        EXPECT_EQ(brain.view(kHorse1)->mPackages, 2u);
        EXPECT_EQ(brain.view(kHorse2)->mPackages, 3u);
    }

    TEST(SkyrimCartBrain, aSourceWithNoReferenceListsGivesTheBasesListToEveryPlacement)
    {
        std::vector<PackageRecord> packs{ package(1), package(2) };
        BaseOnlySource source;
        source.mList = { &packs[0], &packs[1] };
        PackageBrain brain(source);
        QuietWriter writer;
        addHorse(brain, kHorse1, 1);
        brain.update(1.1f, writer);
        ASSERT_TRUE(brain.view(kHorse1).has_value());
        EXPECT_EQ(brain.view(kHorse1)->mPackages, 2u);
        EXPECT_EQ(source.packageListRevision(), 0u);
    }

    TEST(SkyrimCartBrain, aQuestThatMovesOnGivesTheActorItsNewListAtTheNextUpdate)
    {
        std::vector<PackageRecord> packs{ package(1), package(2) };
        RefSource source;
        source.mBaseList = { &packs[0] };
        PackageBrain brain(source);
        QuietWriter writer;
        addHorse(brain, kHorse1, 1);
        brain.update(1.1f, writer);
        ASSERT_TRUE(brain.view(kHorse1).has_value());
        EXPECT_EQ(brain.view(kHorse1)->mPackages, 1u);
        source.mOfReference[kHorse1] = { &packs[1] }; // the quest started: the alias gives a package
        brain.update(1.1f, writer);
        EXPECT_EQ(brain.view(kHorse1)->mPackages, 1u); // no revision change: not looked at again
        source.mRevision = 1;
        brain.update(1.1f, writer);
        EXPECT_EQ(brain.view(kHorse1)->mPackages, 2u);
    }

    TEST(SkyrimCartBrain, aRiderIsNotEvaluatedAndIsFreedWhenItGetsOff)
    {
        std::vector<PackageRecord> packs{ package(1) };
        RefSource source;
        source.mBaseList = { &packs[0] };
        PackageBrain brain(source);
        QuietWriter writer;
        addHorse(brain, kHorse1, 1);
        EXPECT_TRUE(brain.setVehicleRider(kHorse1, true));
        EXPECT_TRUE(brain.isVehicleRider(kHorse1));
        EXPECT_FALSE(brain.setVehicleRider(kHorse1, true)); // nothing changed
        for (int i = 0; i < 4; ++i)
            brain.update(1.1f, writer);
        ASSERT_TRUE(brain.view(kHorse1).has_value());
        EXPECT_EQ(brain.view(kHorse1)->mActive, nullptr);
        EXPECT_TRUE(brain.setVehicleRider(kHorse1, false));
        brain.update(1.1f, writer);
        EXPECT_NE(brain.view(kHorse1)->mActive, nullptr);
    }

    TEST(SkyrimCartBrain, aRiderMarkedBeforeTheActorAppearsStaysARiderAfterADoor)
    {
        std::vector<PackageRecord> packs{ package(1) };
        RefSource source;
        source.mBaseList = { &packs[0] };
        PackageBrain brain(source);
        QuietWriter writer;
        EXPECT_TRUE(brain.setVehicleRider(kHorse1, true)); // the actor is not here yet
        addHorse(brain, kHorse1, 1);
        for (int i = 0; i < 3; ++i)
            brain.update(1.1f, writer);
        EXPECT_EQ(brain.view(kHorse1)->mActive, nullptr);
        brain.onActorRemoved({ kHorse1, 1 });
        addHorse(brain, kHorse1, 2); // a door made the core add it again
        for (int i = 0; i < 3; ++i)
            brain.update(1.1f, writer);
        EXPECT_EQ(brain.view(kHorse1)->mActive, nullptr);
    }
}
