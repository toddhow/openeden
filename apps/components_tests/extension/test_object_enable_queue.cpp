// Task 0181 (Papyrus slice 2, part 2): WorldSourceService 1.2, setObjectEnabled. A game pack asks the host to enable or
// disable one placement of its neutral world; the host queues the change and the engine takes it on the main thread.
// Written first and run red against a stub that refuses and queues nothing.
#include <components/extension/game_services.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using openeden::extension::ObjectEnableChange;
    using openeden::extension::WorldSourceServiceImpl;

    class NamedSource final : public WorldCellSource
    {
    public:
        explicit NamedSource(std::string game)
            : mGame(std::move(game))
        {
        }
        std::string_view gameId() const override { return mGame; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }

    private:
        std::string mGame;
    };

    struct Registered
    {
        Registered()
            : mSource("skyrimse")
        {
            std::string error;
            EXPECT_TRUE(mService.registerSource(mSource, error)) << error;
        }
        WorldSourceServiceImpl mService;
        NamedSource mSource;
    };

    TEST(ObjectEnableQueue, theServiceVersionIsOnePointThree)
    {
        EXPECT_EQ(kWorldSourceServiceVersion.major, 1);
        EXPECT_EQ(kWorldSourceServiceVersion.minor, 3);
    }

    TEST(ObjectEnableQueue, aServiceThatDoesNotOverrideItRefuses)
    {
        // The base class default: "not supported" (a host built before 1.2).
        struct Plain final : WorldSourceService
        {
            bool registerSource(WorldCellSource&, std::string&) override { return true; }
            void withdrawSource(WorldCellSource&) override {}
            bool consumerEnabled() const override { return false; }
        } plain;
        NamedSource source("skyrimse");
        EXPECT_FALSE(plain.setObjectEnabled(source, 1, 2, false));
    }

    TEST(ObjectEnableQueue, aChangeFromAnUnregisteredSourceIsRefused)
    {
        WorldSourceServiceImpl service;
        NamedSource source("skyrimse");
        EXPECT_FALSE(service.setObjectEnabled(source, 1, 2, false));
        std::vector<ObjectEnableChange> out;
        EXPECT_EQ(service.takeObjectEnableChanges(out), 0u);
    }

    TEST(ObjectEnableQueue, changesAreTakenInOrderWithTheGameId)
    {
        Registered r;
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 0x10, 0x100, false));
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 0x10, 0x101, true));
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 0x20, 0x200, false));
        std::vector<ObjectEnableChange> out;
        ASSERT_EQ(r.mService.takeObjectEnableChanges(out), 3u);
        EXPECT_EQ(out[0].gameId, "skyrimse");
        EXPECT_EQ(out[0].cell, 0x10u);
        EXPECT_EQ(out[0].placement, 0x100u);
        EXPECT_FALSE(out[0].enabled);
        EXPECT_EQ(out[1].placement, 0x101u);
        EXPECT_TRUE(out[1].enabled);
        EXPECT_EQ(out[2].cell, 0x20u);
        std::vector<ObjectEnableChange> again;
        EXPECT_EQ(r.mService.takeObjectEnableChanges(again), 0u) << "taking empties the queue";
    }

    TEST(ObjectEnableQueue, theLastChangeOfAPlacementWinsAndKeepsItsPlaceInTheQueue)
    {
        Registered r;
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x100, false));
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x101, false));
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x100, true));
        std::vector<ObjectEnableChange> out;
        ASSERT_EQ(r.mService.takeObjectEnableChanges(out), 2u);
        EXPECT_EQ(out[0].placement, 0x100u);
        EXPECT_TRUE(out[0].enabled);
        EXPECT_EQ(out[1].placement, 0x101u);
    }

    TEST(ObjectEnableQueue, theQueueIsBoundedAndCountsWhatItDrops)
    {
        Registered r;
        for (std::uint32_t i = 0; i < WorldSourceServiceImpl::kMaxPendingObjectChanges; ++i)
            ASSERT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x1000 + i, false));
        EXPECT_FALSE(r.mService.setObjectEnabled(r.mSource, 1, 0x9000, false)) << "full";
        EXPECT_EQ(r.mService.droppedObjectChanges(), 1u);
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x1000, true)) << "a placement already queued still updates";
        std::vector<ObjectEnableChange> out;
        EXPECT_EQ(r.mService.takeObjectEnableChanges(out), WorldSourceServiceImpl::kMaxPendingObjectChanges);
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x9000, false)) << "room again after a take";
    }

    TEST(ObjectEnableQueue, withdrawingTheSourceDropsItsPendingChanges)
    {
        Registered r;
        EXPECT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 0x100, false));
        r.mService.withdrawSource(r.mSource);
        std::vector<ObjectEnableChange> out;
        EXPECT_EQ(r.mService.takeObjectEnableChanges(out), 0u);
        EXPECT_FALSE(r.mService.setObjectEnabled(r.mSource, 1, 0x100, false));
    }
}
