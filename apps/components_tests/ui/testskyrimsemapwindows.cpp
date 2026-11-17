// Task 0201: the Skyrim SE compass (files/data/openeden/skyrimse/compass.lua) and world map window (worldmap.lua) over
// the headless UI backend and a fake `core.skyrimse.map` package written in Lua.
#include "uiluafixture.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;
    constexpr double kPi = 3.14159265358979323846;

    struct SkyrimMapLua : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                C = require('openeden.skyrimse.compass')
                W = require('openeden.skyrimse.worldmap')
                revision = 1
                calls = { marker_x = 0, target_x = 0, raster_row = 0 }
                data = {
                  markers = {
                    { x = 0, y = 1000, discovered = true, name = 'Whiterun' },   -- straight north
                    { x = 1000, y = 0, discovered = false, name = '' },          -- east, not found
                    { x = 0, y = -1000, discovered = true, name = 'Behind' },    -- south
                  },
                  targets = { { x = 0, y = 2000, world = 5, text = 'Reach the keep' },
                              { x = 50, y = 50, world = 0, text = 'In a cave' } },
                  rows = 4, minX = -3, minY = -2, width = 6,
                }
                local function col(f) local t = {}; for i, m in ipairs(data.markers) do t[i] = f(m) end; return t end
                map = {
                  ready = function() return true end,
                  revision = function() return revision end,
                  marker_x = function() calls.marker_x = calls.marker_x + 1; return col(function(m) return m.x end) end,
                  marker_y = function() return col(function(m) return m.y end) end,
                  marker_type = function() return col(function() return 1 end) end,
                  marker_discovered = function() return col(function(m) return m.discovered end) end,
                  marker_name = function() return col(function(m) return m.name end) end,
                  target_x = function() calls.target_x = calls.target_x + 1; local t = {}; for i, x in ipairs(data.targets) do t[i] = x.x end; return t end,
                  target_y = function() local t = {}; for i, x in ipairs(data.targets) do t[i] = x.y end; return t end,
                  target_world = function() local t = {}; for i, x in ipairs(data.targets) do t[i] = x.world end; return t end,
                  target_text = function() local t = {}; for i, x in ipairs(data.targets) do t[i] = x.text end; return t end,
                  raster_info = function(world)
                    if world ~= 5 then return {} end
                    return { data.minX, data.minY, data.width, data.rows }
                  end,
                  raster_row = function(world, row)
                    calls.raster_row = calls.raster_row + 1
                    local px = {}
                    for i = 1, data.width do px[i] = (i == 1) and -1 or (row * 65536 + 0x3040) end
                    return px
                  end,
                }
                player = { x = 0, y = 0, yaw = 0, world = 5 }
                function getPlayer() return player end
                compass = C.create({ ui = ui, map = map, player = function() return player end })
                closes = 0
                wmap = W.create({ ui = ui, map = map, player = function() return player end,
                                  close = function() closes = closes + 1 end })
            )"));
        }

        std::string property(ElementId id, const std::string& name) const
        {
            const auto* node = backend.node(id);
            const auto it = node->mProperties.find(name);
            return it == node->mProperties.end() ? std::string() : it->second;
        }

        /// The visible elements (display not none) whose text is `text`.
        std::vector<ElementId> shown(const std::string& text) const
        {
            std::vector<ElementId> out;
            for (const auto& [id, node] : backend.nodes())
            {
                if (node.mText != text)
                    continue;
                const auto it = node.mProperties.find("display");
                if (it == node.mProperties.end() || it->second != "none")
                    out.push_back(id);
            }
            return out;
        }

        std::size_t documents(const std::string& name) const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == name)
                    ++n;
            return n;
        }

        void frame() { ASSERT_TRUE(run("compass.update(); ui.update()")); }
    };

    // ---- the maths ----------------------------------------------------------------------------------------------

    TEST_F(SkyrimMapLua, aThingAheadIsAtZeroAndTheAngleGrowsClockwise)
    {
        EXPECT_NEAR(get<double>("C.relativeAngle(0, 10, 0)"), 0.0, 1e-9); // north, facing north
        EXPECT_NEAR(get<double>("C.relativeAngle(10, 0, 0)"), kPi / 2, 1e-9); // east is to the right
        EXPECT_NEAR(get<double>("C.relativeAngle(-10, 0, 0)"), -kPi / 2, 1e-9);
    }

    TEST_F(SkyrimMapLua, theHeadingTurnsTheWorldTheOtherWay)
    {
        // facing east (yaw = pi/2), north is to the left
        EXPECT_NEAR(get<double>("C.relativeAngle(0, 10, math.pi / 2)"), -kPi / 2, 1e-9);
        // facing east, an eastern thing is ahead
        EXPECT_NEAR(get<double>("C.relativeAngle(10, 0, math.pi / 2)"), 0.0, 1e-9);
    }

    TEST_F(SkyrimMapLua, theAngleWrapsIntoMinusPiToPi)
    {
        EXPECT_NEAR(get<double>("C.relativeAngle(0, -10, 0)"), kPi, 1e-9); // directly behind is +pi, not -pi
        EXPECT_NEAR(get<double>("C.relativeAngle(0, 10, 3 * math.pi)"), kPi, 1e-9);
        const double a = get<double>("C.relativeAngle(1, 1, -5 * math.pi)");
        EXPECT_GT(a, -kPi);
        EXPECT_LE(a, kPi);
    }

    TEST_F(SkyrimMapLua, theStripPlacesAnAngleAndHidesWhatIsOutside)
    {
        EXPECT_EQ(get<int>("C.stripX(0)"), 260);
        EXPECT_EQ(get<int>("C.stripX(-math.pi / 2)"), 0);
        EXPECT_EQ(get<int>("C.stripX(math.pi / 2)"), 520);
        EXPECT_TRUE(get<bool>("C.stripX(math.pi / 2 + 0.01) == nil"));
        EXPECT_TRUE(get<bool>("C.stripX(math.pi) == nil"));
        EXPECT_EQ(get<int>("C.stripX(0, 100, math.pi)"), 50);
    }

    // ---- the compass ----------------------------------------------------------------------------------------------

    TEST_F(SkyrimMapLua, theStripAppearsWithAPlayerAndGoesWithout)
    {
        EXPECT_EQ(documents("skyrim-compass"), 0u);
        frame();
        EXPECT_EQ(documents("skyrim-compass"), 1u);
        ASSERT_TRUE(run("player = nil"));
        frame();
        EXPECT_EQ(documents("skyrim-compass"), 0u);
        ASSERT_TRUE(run("player = { x = 0, y = 0, yaw = 0, world = 5 }"));
        frame();
        EXPECT_EQ(documents("skyrim-compass"), 1u);
    }

    TEST_F(SkyrimMapLua, theCardinalPointsFollowTheHeading)
    {
        frame();
        // facing north: N in the middle, E and W on the edges, S behind
        ASSERT_EQ(shown("N").size(), 1u);
        EXPECT_EQ(property(shown("N")[0], "left"), "260px");
        ASSERT_EQ(shown("E").size(), 1u);
        EXPECT_EQ(property(shown("E")[0], "left"), "520px");
        ASSERT_EQ(shown("W").size(), 1u);
        EXPECT_EQ(property(shown("W")[0], "left"), "0px");
        EXPECT_TRUE(shown("S").empty());
        // facing east: E in the middle, N on the left edge
        ASSERT_TRUE(run("player.yaw = math.pi / 2"));
        frame();
        EXPECT_EQ(property(shown("E")[0], "left"), "260px");
        EXPECT_EQ(property(shown("N")[0], "left"), "0px");
        EXPECT_TRUE(shown("W").empty());
        // a little right of north: N slides left
        ASSERT_TRUE(run("player.yaw = 0.2"));
        frame();
        ASSERT_EQ(shown("N").size(), 1u);
        const int left = std::stoi(property(shown("N")[0], "left"));
        EXPECT_LT(left, 260);
        EXPECT_GT(left, 220);
    }

    TEST_F(SkyrimMapLua, aDiscoveredMarkerAheadIsAStarAndAnUnfoundOneAQuestionMark)
    {
        frame();
        ASSERT_EQ(shown("O").size(), 1u); // Whiterun; the one behind is hidden
        EXPECT_EQ(property(shown("O")[0], "left"), "260px");
        ASSERT_EQ(shown("?").size(), 1u); // east: on the right edge
        EXPECT_EQ(property(shown("?")[0], "left"), "520px");
    }

    TEST_F(SkyrimMapLua, theNameOfTheMarkerInFrontShowsBelowTheStrip)
    {
        ASSERT_TRUE(run("data.targets = {}")); // a quest target in front would name itself instead
        frame();
        EXPECT_FALSE(shown("Whiterun").empty());
        ASSERT_TRUE(run("player.yaw = math.pi / 2")); // looking east: nothing named in front
        frame();
        EXPECT_TRUE(shown("Whiterun").empty());
        ASSERT_TRUE(run("player.yaw = math.pi")); // looking south: 'Behind' is in front
        frame();
        EXPECT_FALSE(shown("Behind").empty());
    }

    TEST_F(SkyrimMapLua, aQuestTargetInThePlayersWorldIsAnExclamationMarkAndNamesItself)
    {
        frame();
        ASSERT_EQ(shown("!").size(), 1u); // the cave target has world 0: not shown
        EXPECT_EQ(property(shown("!")[0], "left"), "260px");
        EXPECT_FALSE(shown("Reach the keep").empty());
    }

    TEST_F(SkyrimMapLua, anInteriorShowsTheCardinalPointsOnly)
    {
        ASSERT_TRUE(run("player.world = nil"));
        frame();
        EXPECT_EQ(shown("N").size(), 1u);
        EXPECT_TRUE(shown("O").empty());
        EXPECT_TRUE(shown("?").empty());
        EXPECT_TRUE(shown("!").empty());
        EXPECT_EQ(get<int>("calls.marker_x"), 0);
    }

    TEST_F(SkyrimMapLua, thePackIsAskedAgainOnlyWhenThePlayerMovedFarOrTheRevisionMoved)
    {
        frame();
        frame();
        ASSERT_TRUE(run("player.yaw = 0.5"));
        frame();
        EXPECT_EQ(get<int>("calls.marker_x"), 1); // turning asks nothing
        ASSERT_TRUE(run("player.x = 100"));
        frame();
        EXPECT_EQ(get<int>("calls.marker_x"), 1);
        ASSERT_TRUE(run("player.x = 400"));
        frame();
        EXPECT_EQ(get<int>("calls.marker_x"), 2);
        ASSERT_TRUE(run("revision = 2"));
        frame();
        EXPECT_EQ(get<int>("calls.marker_x"), 3);
        EXPECT_EQ(get<int>("calls.target_x"), 3);
    }

    TEST_F(SkyrimMapLua, aFrameWhereNothingChangedMakesNoProtocolCall)
    {
        frame();
        ASSERT_TRUE(run("ui.update()"));
        const std::size_t before = backend.nodes().size();
        for (int i = 0; i < 3; ++i)
            frame();
        EXPECT_EQ(backend.nodes().size(), before);
    }

    TEST_F(SkyrimMapLua, nothingIsAskedBeforeThePackIsReady)
    {
        ASSERT_TRUE(run("map.ready = function() return false end"));
        frame();
        EXPECT_EQ(get<int>("calls.marker_x"), 0);
        EXPECT_EQ(documents("skyrim-compass"), 1u);
    }

    // ---- the world map --------------------------------------------------------------------------------------------

    TEST_F(SkyrimMapLua, runsMergeEqualPixelsAndSkipTheCellsWithNoLand)
    {
        ASSERT_TRUE(run("r = W.runs({ -1, 7, 7, 3, -1, -1, 3, 3 })"));
        EXPECT_EQ(get<int>("#r"), 3);
        EXPECT_EQ(get<int>("r[1].x"), 1);
        EXPECT_EQ(get<int>("r[1].n"), 2);
        EXPECT_EQ(get<int>("r[1].rgb"), 7);
        EXPECT_EQ(get<int>("r[2].x"), 3);
        EXPECT_EQ(get<int>("r[2].n"), 1);
        EXPECT_EQ(get<int>("r[3].x"), 6);
        EXPECT_EQ(get<int>("r[3].n"), 2);
        ASSERT_TRUE(run("e = W.runs({})"));
        EXPECT_EQ(get<int>("#e"), 0);
    }

    TEST_F(SkyrimMapLua, aWorldPositionIsAColumnAndARowWithNorthUp)
    {
        // cells -3..2 east, -2..1 north: the cell (0,0) starts at column 3; y 0 is a cell above the bottom of cell row
        // 0
        ASSERT_TRUE(run("c, r = W.toMap(0, 0, -3, 1)"));
        EXPECT_NEAR(get<double>("c"), 3.0, 1e-9);
        EXPECT_NEAR(get<double>("r"), 2.0, 1e-9); // top edge of cell row 1 is row 0; the line y = 0 is 2 cells below it
        ASSERT_TRUE(run("c, r = W.toMap(4096, 4096, -3, 1)"));
        EXPECT_NEAR(get<double>("c"), 4.0, 1e-9);
        EXPECT_NEAR(get<double>("r"), 1.0, 1e-9);
    }

    TEST_F(SkyrimMapLua, theMapDoesNotOpenWithoutAWorldOrLand)
    {
        ASSERT_TRUE(run("player.world = nil"));
        EXPECT_FALSE(get<bool>("wmap.open()"));
        ASSERT_TRUE(run("player.world = 9"));
        EXPECT_FALSE(get<bool>("wmap.open()")); // raster_info answers nothing for world 9
        EXPECT_EQ(documents("skyrim-worldmap"), 0u);
        EXPECT_FALSE(get<bool>("wmap.isOpen()"));
    }

    TEST_F(SkyrimMapLua, theMapOpensAndReadsOneRasterRowAFrame)
    {
        ASSERT_TRUE(run("ok = wmap.open()"));
        EXPECT_TRUE(get<bool>("ok"));
        EXPECT_EQ(documents("skyrim-worldmap"), 1u);
        EXPECT_EQ(get<int>("wmap.rowsRead()"), 0);
        for (int i = 1; i <= 4; ++i)
        {
            ASSERT_TRUE(run("wmap.update(); ui.update()"));
            EXPECT_EQ(get<int>("wmap.rowsRead()"), i);
        }
        ASSERT_TRUE(run("wmap.update(); ui.update()"));
        EXPECT_EQ(get<int>("wmap.rowsRead()"), 4); // complete: no more reads
        EXPECT_EQ(get<int>("calls.raster_row"), 4);
    }

    TEST_F(SkyrimMapLua, theScaleFitsTheMapInTheWindow)
    {
        ASSERT_TRUE(run("wmap.open()"));
        EXPECT_EQ(get<int>("wmap.scale()"), 125); // 780 / 6 = 130, 500 / 4 = 125
    }

    TEST_F(SkyrimMapLua, thePlayerAndTheDiscoveredMarkersAreDrawnOnTheMap)
    {
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        // player at (0, 0): column 3, 2 rows from the top of a 4 row map, scale 125; the marker is 8 wide, centred
        const auto players = withClass("map-player");
        ASSERT_EQ(players.size(), 1u);
        EXPECT_EQ(property(players[0], "left"), std::to_string(3 * 125 - 4) + "px");
        EXPECT_EQ(property(players[0], "top"), std::to_string(2 * 125 - 4) + "px");
        // two discovered markers (Whiterun, Behind); the unfound one is not offered by the fake as shown
        EXPECT_GE(get<int>("wmap.markersShown()"), 1);
    }

    TEST_F(SkyrimMapLua, aQuestTargetOfThisWorldIsDrawnAndAnotherWorldsIsNot)
    {
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        std::size_t visible = 0;
        for (const ElementId id : withClass("map-target"))
            if (property(id, "display") != "none")
                ++visible;
        EXPECT_EQ(visible, 1u); // the cave target has world 0
    }

    TEST_F(SkyrimMapLua, theCloseButtonLeavesAndDismissTakesTheWindowDown)
    {
        ASSERT_TRUE(run("wmap.open()"));
        std::vector<ElementId> buttons = withClass("button");
        ASSERT_EQ(buttons.size(), 1u);
        ASSERT_TRUE(backend.emit(buttons[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<int>("closes"), 1);
        ASSERT_TRUE(run("wmap.dismiss()"));
        EXPECT_FALSE(get<bool>("wmap.isOpen()"));
        EXPECT_EQ(documents("skyrim-worldmap"), 0u);
    }

    TEST_F(SkyrimMapLua, aSecondOpenReplacesTheFirstWindow)
    {
        ASSERT_TRUE(run("wmap.open()"));
        ASSERT_TRUE(run("wmap.open()"));
        EXPECT_EQ(documents("skyrim-worldmap"), 1u);
    }

    TEST_F(SkyrimMapLua, updateOfAClosedMapDoesNothing)
    {
        ASSERT_TRUE(run("wmap.update()"));
        EXPECT_EQ(get<int>("calls.raster_row"), 0);
    }
}
