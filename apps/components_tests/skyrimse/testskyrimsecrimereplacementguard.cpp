// Task 0273b (second review pass): when the guard that walks to the player dies on the way, another guard takes over,
// and the offer is made BY THAT GUARD: the guard's base (the global script activates the nearest object of that base)
// and the conversation probe must be about the replacement, not about the dead guard.
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorSpeed;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::SubjectState;

    constexpr FormId kHold = 0x01000100;
    constexpr FormId kBaseA = 0x01000201;
    constexpr FormId kBaseB = 0x01000202;

    class FakeWorld final : public crime::CrimeWorld
    {
    public:
        void actors(std::vector<crime::CrimeActor>& out) override
        {
            for (const auto& [ref, a] : mActors)
                out.push_back(a);
        }
        bool actor(ActorHandle handle, crime::CrimeActor& out) override
        {
            const auto it = mActors.find(static_cast<std::uint32_t>(handle.reference));
            if (it == mActors.end())
                return false;
            out = it->second;
            return true;
        }
        const CrimeData* crimeData(FormId) override { return &mData; }
        std::string factionName(FormId) override { return "Whiterun"; }
        std::int32_t itemValue(FormId) override { return 100; }
        std::int32_t playerGold() override { return 500; }
        bool takeGold(std::int32_t) override { return true; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}

        std::map<std::uint32_t, crime::CrimeActor> mActors;
        CrimeData mData;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = mX[static_cast<std::uint32_t>(subject.actor.reference)];
            return true;
        }
        std::map<std::uint32_t, double> mX;
    };

    TEST(SkyrimCrimeReplacementGuard, theOfferIsMadeByTheGuardThatTookOverNotTheDeadOne)
    {
        crime::Ledger ledger;
        FakeWorld world;
        FakeWriter writer;
        world.mData.mPresent = true;
        world.mData.mAssault = 40;
        const std::pair<std::uint32_t, FormId> guards[] = { { 10, kBaseA }, { 11, kBaseB } };
        for (const auto& [ref, base] : guards)
        {
            crime::CrimeActor guard;
            guard.mHandle = { ref, 1 };
            guard.mBase = base;
            guard.mGuard = true;
            guard.mCrimeFaction = kHold;
            guard.mName = "guard";
            world.mActors[ref] = guard;
        }
        writer.mX[10] = 1000.0; // the nearer one starts to walk
        writer.mX[11] = 3000.0;
        crime::CrimeSystem system(ledger, world);
        FormId probed = kNullFormId;
        system.setDialogueProbe([&](const crime::CrimeActor& guard) {
            probed = guard.mBase;
            return true;
        });
        ledger.addBounty(kHold, 100);
        system.update(0.5f, writer);
        ASSERT_EQ(system.state(), crime::State::Approach);

        world.mActors[10].mDead = true; // killed on the way
        writer.mX[11] = 100.0; // the other one is at the player
        system.update(0.5f, writer);
        ASSERT_EQ(system.state(), crime::State::Offer);
        EXPECT_EQ(system.guardReference(), 11u);
        EXPECT_EQ(system.guardBase(), kBaseB) << "the script activates the nearest object of this base";
        EXPECT_EQ(probed, kBaseB) << "the conversation probe is about the guard that stands there";
    }
}
