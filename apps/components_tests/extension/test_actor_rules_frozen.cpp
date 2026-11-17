// Task 0273b (review finding 2): the virtual function order of ActorRequestWriter.
//
// In plain words: a pack built against an older header calls a function of the core's writer by its place in the
// table of virtual functions. Adding a function in the MIDDLE moves every function after it, so the old pack calls the
// wrong one. New functions go at the END. This test calls the functions through their table places, in the order the
// 0.5 header declared them (clock last of that set) followed by the two later additions in the order they came, so a
// reordered, removed or inserted function changes what is called and fails here. (The same technique as
// worldmodel/testfrozen.cpp.) Places: the destructor (Itanium two, MSVC one), setMovement, goTo, cancelGoal,
// setHeading, useFurniture, leaveFurniture, placeFurniture, actorPosition, clock, playerPosition, placeCorpse.
#include <openeden/game/actor_rules.hpp>

#include <gtest/gtest.h>

#include <cstddef>

namespace
{
    using namespace openeden::sdk::game;

    class Probe final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return false; }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            x = 1.0;
            y = 2.0;
            z = 3.0;
            return true;
        }
        [[nodiscard]] ActorClock clock() const override
        {
            ActorClock c;
            c.hour = 6.5f;
            c.day = 9;
            c.valid = true;
            return c;
        }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = 11.0;
            y = 12.0;
            z = 13.0;
            return true;
        }
        bool placeCorpse(ActorHandle, double x, double y, double z, float heading) override
        {
            mCorpse = x + y + z + heading;
            return true;
        }
        double mCorpse = 0.0;
    };

    TEST(ActorRulesFrozen, theVirtualFunctionOrderKeepsClockWhereTheOlderHeaderHadIt)
    {
        Probe real;
        ActorRequestWriter* base = &real;
#if defined(_MSC_VER)
        constexpr std::size_t destructorSlots = 1;
#else
        constexpr std::size_t destructorSlots = 2;
#endif
        void** vtable = *reinterpret_cast<void***>(base);
        const auto slot = [&](std::size_t index) { return vtable[destructorSlots + index]; };

        using ActorPosition = bool (*)(const void*, ActorHandle, double&, double&, double&);
        double x = 0.0, y = 0.0, z = 0.0;
        ASSERT_TRUE(reinterpret_cast<ActorPosition>(slot(7))(base, ActorHandle{}, x, y, z));
        EXPECT_EQ(x, 1.0);
        EXPECT_EQ(z, 3.0);

#if defined(_MSC_VER)
        // MSVC passes the address of a returned class right after `this`.
        using Clock = ActorClock* (*)(const void*, ActorClock*);
        ActorClock clock;
        reinterpret_cast<Clock>(slot(8))(base, &clock);
#else
        using Clock = ActorClock (*)(const void*);
        const ActorClock clock = reinterpret_cast<Clock>(slot(8))(base);
#endif
        EXPECT_TRUE(clock.valid) << "slot 8 is clock(): it was the last function of the 0.5 header";
        EXPECT_EQ(clock.day, 9);

        using PlayerPosition = bool (*)(const void*, double&, double&, double&);
        ASSERT_TRUE(reinterpret_cast<PlayerPosition>(slot(9))(base, x, y, z));
        EXPECT_EQ(x, 11.0);

        using PlaceCorpse = bool (*)(void*, ActorHandle, double, double, double, float);
        ASSERT_TRUE(reinterpret_cast<PlaceCorpse>(slot(10))(base, ActorHandle{}, 1.0, 2.0, 3.0, 4.0f));
        EXPECT_EQ(real.mCorpse, 10.0);
    }

    TEST(ActorRulesFrozen, theVersionIsBumpedForTheLateAdditions)
    {
        EXPECT_GE(kActorRulesVersion.minor, 7u);
    }
}
