// Task 0279 (vampirism, second slice): the cure and the vampire races. Numbers by hand:
//  - the game's own cure is PlayerVampireQuestScript.VampireCure(player), run by the Falion quest VC01 (its fragment) and
//    by the Companions' cure; a port that has no `cure` refuses it;
//  - the vampire race of each playable race: Argonian 0x13740 -> 0x8883A, Breton 0x13741 -> 0x8883C, Dark Elf 0x13742 ->
//    0x8883D, High Elf 0x13743 -> 0x88840, Imperial 0x13744 -> 0x88844, Khajiit 0x13745 -> 0x88845, Nord 0x13746 ->
//    0x88794, Orc 0x13747 -> 0xA82B9, Redguard 0x13748 -> 0x88846, Wood Elf 0x13749 -> 0x88884 (the quest script's own
//    race properties, read from the record's RACE ids).
#include <game_skyrimse/vampirism.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::vampirism;

    class CurePort : public ScriptPort
    {
    public:
        bool ready() const override { return true; }
        bool change() override
        {
            mStatus = 1;
            return true;
        }
        bool feed() override { return true; }
        bool tick() override { return true; }
        bool progress(int) override { return true; }
        int status() const override { return mStatus; }
        bool setStage(int stage, double) override
        {
            mStatus = stage;
            return true;
        }
        void setDays(double) override {}
        bool cure() override
        {
            ++mCures;
            mStatus = 0;
            return true;
        }
        int mStatus = 0, mCures = 0;
    };

    // A port written before the cure existed: it does not override `cure`.
    class OldPort final : public CurePort
    {
    public:
        bool cure() override { return ScriptPort::cure(); }
    };

    template <class Port>
    struct Rig
    {
        Rig()
        {
            VampirismService::Hooks hooks;
            hooks.mDays = [this] { return days; };
            hooks.mSanguinare = [this] { return sanguinare; };
            hooks.mCureSanguinare = [this] { sanguinare = false; };
            hooks.mLog = [this](const std::string& line) { logs.push_back(line); };
            service = std::make_unique<VampirismService>(port, std::move(hooks));
        }
        void turn()
        {
            sanguinare = true;
            service->poll();
            days += 3.0;
            service->poll();
            service->poll();
        }
        bool logged(const std::string& text) const
        {
            for (const std::string& line : logs)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
        Port port;
        double days = 10.0;
        bool sanguinare = false;
        std::vector<std::string> logs;
        std::unique_ptr<VampirismService> service;
    };

    TEST(SkyrimVampirismCure, aVampireIsCuredThroughTheGamesOwnFunction)
    {
        Rig<CurePort> rig;
        rig.turn();
        ASSERT_TRUE(rig.service->vampire());
        EXPECT_TRUE(rig.service->cure());
        EXPECT_EQ(rig.port.mCures, 1);
        rig.service->poll();
        EXPECT_FALSE(rig.service->vampire());
        EXPECT_EQ(rig.service->stage(), 0);
        EXPECT_TRUE(rig.logged("VAMPIRE cured"));
    }

    TEST(SkyrimVampirismCure, noVampireNoCure)
    {
        Rig<CurePort> rig;
        rig.service->poll();
        EXPECT_FALSE(rig.service->cure());
        EXPECT_EQ(rig.port.mCures, 0);
    }

    TEST(SkyrimVampirismCure, aPortWithNoCureRefusesAndNothingChanges)
    {
        Rig<OldPort> rig;
        rig.turn();
        ASSERT_TRUE(rig.service->vampire());
        EXPECT_FALSE(rig.service->cure());
        rig.service->poll();
        EXPECT_TRUE(rig.service->vampire());
    }

    TEST(SkyrimVampirismCure, aCureTheGameScriptsDoThemselvesIsSeen)
    {
        Rig<CurePort> rig;
        rig.turn();
        ASSERT_TRUE(rig.service->vampire());
        rig.port.mStatus = 0; // the quest VC01's fragment called VampireCure
        rig.service->poll();
        EXPECT_FALSE(rig.service->vampire());
        EXPECT_TRUE(rig.logged("VAMPIRE cured"));
    }

    TEST(SkyrimVampirismCure, aScheduledCureRunsOnceWhenItsDayComes)
    {
        Rig<CurePort> rig;
        rig.turn();
        rig.service->scheduleCure(rig.days + 2.0);
        rig.service->poll();
        EXPECT_EQ(rig.port.mCures, 0);
        rig.days += 2.5;
        rig.service->poll();
        EXPECT_EQ(rig.port.mCures, 1);
        rig.service->poll();
        rig.service->poll();
        EXPECT_EQ(rig.port.mCures, 1);
        EXPECT_FALSE(rig.service->vampire());
    }

    TEST(SkyrimVampirismCure, curingBeforeTheTurnCostsNothingAndTheDiseasesTimerIsForgotten)
    {
        Rig<CurePort> rig;
        rig.sanguinare = true;
        rig.service->poll();
        EXPECT_TRUE(rig.service->machine().infected());
        rig.sanguinare = false; // a Cure Disease potion or a shrine took the disease away
        rig.service->poll();
        EXPECT_FALSE(rig.service->machine().infected());
        rig.days += 5.0;
        rig.service->poll();
        EXPECT_FALSE(rig.service->vampire()) << "the three days no longer run";
        EXPECT_EQ(rig.port.mStatus, 0);
    }

    TEST(SkyrimVampirismCure, aCuredPlayerCanBeInfectedAndTurnAgain)
    {
        Rig<CurePort> rig;
        rig.turn();
        ASSERT_TRUE(rig.service->cure());
        rig.service->poll();
        ASSERT_FALSE(rig.service->vampire());
        rig.turn();
        EXPECT_TRUE(rig.service->vampire());
    }

    TEST(SkyrimVampirismRaces, eachPlayableRaceHasItsVampireRaceAndBack)
    {
        struct Pair
        {
            FormId mHuman, mVampire;
        };
        const Pair pairs[] = { { 0x13740, 0x8883A }, { 0x13741, 0x8883C }, { 0x13742, 0x8883D }, { 0x13743, 0x88840 },
            { 0x13744, 0x88844 }, { 0x13745, 0x88845 }, { 0x13746, 0x88794 }, { 0x13747, 0xA82B9 },
            { 0x13748, 0x88846 }, { 0x13749, 0x88884 } };
        for (const Pair& p : pairs)
        {
            EXPECT_EQ(vampireRaceOf(p.mHuman), p.mVampire) << std::hex << p.mHuman;
            EXPECT_EQ(humanRaceOf(p.mVampire), p.mHuman) << std::hex << p.mVampire;
            EXPECT_EQ(vampireRaceOf(p.mVampire), p.mVampire) << "a vampire stays a vampire";
            EXPECT_EQ(humanRaceOf(p.mHuman), p.mHuman);
        }
    }

    TEST(SkyrimVampirismRaces, anUnknownRaceHasNoVampireRace)
    {
        EXPECT_EQ(vampireRaceOf(0xCDD84), 0xCDD84u) << "the beast race is not swapped";
        EXPECT_EQ(vampireRaceOf(0), 0u);
        EXPECT_EQ(humanRaceOf(0x1234), 0x1234u);
    }
}
