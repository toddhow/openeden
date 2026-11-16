// Task 0223: the world map indoors. `openeden.skyrimse.mapplace` (the place the map window shows when the player stands in
// an interior: the last exterior world, else the world of the interior's location) and the window itself drawing no
// player marker when the place has no position. Over the headless UI backend and fake packages written in Lua.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using UiTest::UiLuaFixture;

    struct SkyrimMapPlaceLua : UiLuaFixture
    {
        void SetUp() override
        {
            UiLuaFixture::SetUp();
            ASSERT_TRUE(run(R"(
                P = require('openeden.skyrimse.mapplace')
                own = { x = 10, y = 20, yaw = 0.5, world = 5 }
                cellName = 'TheMare'
                asked = {}
                map = { interior_world = function(name) asked[#asked + 1] = name; if name == 'TheMare' then return 7 end return 0 end }
                place = P.create{ place = function() return own end, cellName = function() return cellName end, map = map }
            )"));
        }
    };

    TEST_F(SkyrimMapPlaceLua, anExteriorPlaceIsPassedOnUnchanged)
    {
        ASSERT_TRUE(run("p = place()"));
        EXPECT_EQ(get<int>("p.world"), 5);
        EXPECT_EQ(get<int>("p.x"), 10);
        EXPECT_EQ(get<int>("p.y"), 20);
        EXPECT_FALSE(get<bool>("p.indoors == true"));
        EXPECT_EQ(get<int>("#asked"), 0);
    }

    TEST_F(SkyrimMapPlaceLua, noPlayerIsNoPlace)
    {
        ASSERT_TRUE(run("own = nil; p = place()"));
        EXPECT_TRUE(get<bool>("p == nil"));
    }

    TEST_F(SkyrimMapPlaceLua, indoorsAfterAnExteriorTheLastExteriorWorldAndPlaceAreShown)
    {
        ASSERT_TRUE(run("place()")); // stood outside
        ASSERT_TRUE(run("own = { x = 999, y = 888, yaw = 1.5, world = nil }; p = place()"));
        EXPECT_EQ(get<int>("p.world"), 5);
        EXPECT_EQ(get<int>("p.x"), 10) << "the position is where the player went in, not the interior's";
        EXPECT_EQ(get<int>("p.y"), 20);
        EXPECT_NEAR(get<double>("p.yaw"), 1.5, 1e-9) << "the player still turns";
        EXPECT_TRUE(get<bool>("p.indoors == true"));
        EXPECT_EQ(get<int>("#asked"), 0) << "the location is only asked when there was no exterior";
    }

    TEST_F(SkyrimMapPlaceLua, theLastExteriorWorldFollowsTheLatestExterior)
    {
        ASSERT_TRUE(run("place(); own = { x = 1, y = 2, yaw = 0, world = 9 }; place()"));
        ASSERT_TRUE(run("own = { x = 0, y = 0, yaw = 0, world = nil }; p = place()"));
        EXPECT_EQ(get<int>("p.world"), 9);
        EXPECT_EQ(get<int>("p.x"), 1);
    }

    TEST_F(SkyrimMapPlaceLua, indoorsFromTheStartTheWorldComesFromTheInteriorsLocationAndHasNoPosition)
    {
        ASSERT_TRUE(run("own = { x = 999, y = 888, yaw = 1.5, world = nil }; p = place()"));
        EXPECT_EQ(get<int>("p.world"), 7);
        EXPECT_TRUE(get<bool>("p.x == nil and p.y == nil"));
        EXPECT_TRUE(get<bool>("p.indoors == true"));
        EXPECT_EQ(get<std::string>("asked[1]"), "TheMare");
    }

    TEST_F(SkyrimMapPlaceLua, anInteriorWithNoWorldKeepsTheInteriorPlaceSoTheMapStaysClosed)
    {
        ASSERT_TRUE(run("cellName = 'Nowhere'; own = { x = 999, y = 888, yaw = 1.5, world = nil }; p = place()"));
        EXPECT_TRUE(get<bool>("p.world == nil"));
    }

    TEST_F(SkyrimMapPlaceLua, aPackWithoutTheFunctionIsTolerated)
    {
        ASSERT_TRUE(run("map.interior_world = nil; own = { x = 1, y = 2, yaw = 0, world = nil }; p = place()"));
        EXPECT_TRUE(get<bool>("p.world == nil"));
    }

    struct SkyrimMapIndoorsLua : UiLuaFixture
    {
        std::string property(ElementId id, const std::string& name) const
        {
            const auto* node = backend.node(id);
            const auto it = node->mProperties.find(name);
            return it == node->mProperties.end() ? std::string() : it->second;
        }
        void SetUp() override
        {
            UiLuaFixture::SetUp();
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.worldmap')
                map = {
                  ready = function() return true end,
                  revision = function() return 1 end,
                  marker_x = function() return { 0 } end, marker_y = function() return { 1000 } end,
                  marker_type = function() return { 1 } end, marker_discovered = function() return { true } end,
                  marker_name = function() return { 'Whiterun' } end,
                  target_x = function() return {} end, target_y = function() return {} end,
                  target_world = function() return {} end, target_text = function() return {} end,
                  raster_info = function(world) if world ~= 7 then return {} end return { -3, -2, 6, 4 } end,
                  raster_row = function(world, row) local px = {}; for i = 1, 6 do px[i] = 0x3040 end return px end,
                }
                player = { yaw = 0, world = 7, indoors = true }
                wmap = W.create({ ui = ui, map = map, player = function() return player end, close = function() end })
            )"));
        }
    };

    TEST_F(SkyrimMapIndoorsLua, theMapOpensForAPlaceWithAWorldButNoPositionAndDrawsNoPlayer)
    {
        ASSERT_TRUE(run("opened = wmap.open(); ui.update()"));
        EXPECT_TRUE(get<bool>("opened"));
        EXPECT_TRUE(get<bool>("wmap.isOpen()"));
        const auto players = withClass("map-player");
        ASSERT_EQ(players.size(), 1u);
        EXPECT_EQ(property(players[0], "display"), "none");
        ASSERT_TRUE(run("ui.update(); wmap.update(); ui.update()"));
        EXPECT_EQ(property(withClass("map-player")[0], "display"), "none");
    }

    TEST_F(SkyrimMapIndoorsLua, thePlayerMarkerAppearsWhenAPositionComes)
    {
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(run("player.x = 0; player.y = 0; wmap.update(); ui.update()"));
        EXPECT_NE(property(withClass("map-player")[0], "display"), "none");
    }

    TEST_F(SkyrimMapIndoorsLua, aPlaceWithNoWorldStillDoesNotOpenTheMap)
    {
        ASSERT_TRUE(run("player.world = nil; opened = wmap.open()"));
        EXPECT_FALSE(get<bool>("opened"));
    }
}
