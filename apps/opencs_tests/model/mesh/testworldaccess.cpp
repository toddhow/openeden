// Task 0173 (stage d): the world the cell viewer reads (apps/opencs/model/mesh/worldaccess.hpp), the neutral world
// store of the session's game pack and the game id its source is registered under. Written from the header's comments
// before the code.
#include "apps/opencs/model/mesh/worldaccess.hpp"

#include <components/worldmodel/store.hpp>

#include <openeden/game/world_source.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        class NamedSource final : public openeden::sdk::game::WorldCellSource
        {
        public:
            explicit NamedSource(std::string game)
                : mGame(std::move(game))
            {
            }
            std::string_view gameId() const override { return mGame; }
            std::size_t cellCount() const override { return 0; }
            std::size_t listCells(std::size_t, std::size_t, std::vector<openeden::sdk::game::WorldId>&) const override
            {
                return 0;
            }
            bool buildCell(openeden::sdk::game::WorldId, openeden::sdk::game::WorldCell&, std::string& error) override
            {
                error = "none";
                return false;
            }

        private:
            std::string mGame;
        };

        // The active world is process-wide: every test starts and ends with none.
        class MeshWorldAccessTest : public ::testing::Test
        {
        protected:
            void SetUp() override { setActiveWorld({}); }
            void TearDown() override { setActiveWorld({}); }

            static bool add(NeutralWorld::NeutralWorldStore& store, NamedSource& source)
            {
                std::string error;
                return store.addSource(source, error);
            }
        };

        TEST_F(MeshWorldAccessTest, thereIsNoActiveWorldAtFirst)
        {
            EXPECT_EQ(activeWorld().mStore, nullptr);
            EXPECT_TRUE(activeWorld().mGameId.empty());
            EXPECT_FALSE(hasActiveWorld());
        }

        TEST_F(MeshWorldAccessTest, aWorldFromANullStoreHasNoGameId)
        {
            const ActiveWorld world = worldFromStore(nullptr, "Some Game");
            EXPECT_EQ(world.mStore, nullptr);
            EXPECT_TRUE(world.mGameId.empty());
        }

        TEST_F(MeshWorldAccessTest, aWorldFromAStoreWithNoSourceHasNoGameId)
        {
            NeutralWorld::NeutralWorldStore store;
            EXPECT_TRUE(worldFromStore(&store, "Some Game").mGameId.empty());
        }

        TEST_F(MeshWorldAccessTest, aWorldFromAStoreTakesItsFirstGameIdAndTheGivenName)
        {
            NeutralWorld::NeutralWorldStore store;
            NamedSource zeta("zeta");
            NamedSource alpha("alpha");
            ASSERT_TRUE(add(store, zeta));
            ASSERT_TRUE(add(store, alpha));
            const ActiveWorld world = worldFromStore(&store, "The Game");
            EXPECT_EQ(world.mStore, &store);
            EXPECT_EQ(world.mGameId, store.gameIds().front());
            EXPECT_EQ(world.mGameId, "alpha");
            EXPECT_EQ(world.mGameName, "The Game");
        }

        TEST_F(MeshWorldAccessTest, settingTheActiveWorldMakesItReadableAndUsable)
        {
            NeutralWorld::NeutralWorldStore store;
            NamedSource source("fakegame");
            ASSERT_TRUE(add(store, source));
            setActiveWorld(worldFromStore(&store, "Fake Game"));
            EXPECT_EQ(activeWorld().mStore, &store);
            EXPECT_EQ(activeWorld().mGameId, "fakegame");
            EXPECT_EQ(activeWorld().mGameName, "Fake Game");
            EXPECT_TRUE(hasActiveWorld());
        }

        TEST_F(MeshWorldAccessTest, clearingTheActiveWorldRemovesIt)
        {
            NeutralWorld::NeutralWorldStore store;
            NamedSource source("fakegame");
            ASSERT_TRUE(add(store, source));
            setActiveWorld(worldFromStore(&store, "Fake Game"));
            ASSERT_TRUE(hasActiveWorld());
            setActiveWorld({});
            EXPECT_EQ(activeWorld().mStore, nullptr);
            EXPECT_FALSE(hasActiveWorld());
        }

        TEST_F(MeshWorldAccessTest, settingAnotherWorldReplacesTheFirst)
        {
            NeutralWorld::NeutralWorldStore first;
            NeutralWorld::NeutralWorldStore second;
            NamedSource a("gamea");
            NamedSource b("gameb");
            ASSERT_TRUE(add(first, a));
            ASSERT_TRUE(add(second, b));
            setActiveWorld(worldFromStore(&first, "A"));
            setActiveWorld(worldFromStore(&second, "B"));
            EXPECT_EQ(activeWorld().mStore, &second);
            EXPECT_EQ(activeWorld().mGameId, "gameb");
            EXPECT_EQ(activeWorld().mGameName, "B");
        }

        TEST_F(MeshWorldAccessTest, aStoreWithoutASourceForTheGameIdIsNotAWorld)
        {
            NeutralWorld::NeutralWorldStore store;
            NamedSource source("fakegame");
            ASSERT_TRUE(add(store, source));

            ActiveWorld world;
            world.mStore = &store;
            world.mGameId = "othergame";
            setActiveWorld(world);
            EXPECT_FALSE(hasActiveWorld());

            ActiveWorld noId;
            noId.mStore = &store;
            setActiveWorld(noId);
            EXPECT_FALSE(hasActiveWorld());
        }

        TEST_F(MeshWorldAccessTest, aSourceThatLeftTheStoreEndsTheWorld)
        {
            NeutralWorld::NeutralWorldStore store;
            NamedSource source("fakegame");
            ASSERT_TRUE(add(store, source));
            setActiveWorld(worldFromStore(&store, "Fake Game"));
            ASSERT_TRUE(hasActiveWorld());
            store.removeSource("fakegame");
            EXPECT_FALSE(hasActiveWorld());
        }
    }
}
