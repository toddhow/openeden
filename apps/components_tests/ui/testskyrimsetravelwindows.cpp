// Task 0218: fast travel from the Skyrim SE world map window (files/data/openeden/skyrimse/worldmap.lua) over the
// headless UI backend, a fake `core.skyrimse.map` package and a fake `core.skyrimse.travel` package written in Lua.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimTravelLua : UiLuaFixture
    {
        /// `withTravel` false builds the window the way a session without the travel setting gets it.
        void build(bool withTravel)
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.worldmap')
                data = {
                  markers = {
                    { x = 0, y = 1000, discovered = true, name = 'Whiterun', ref = '01000200' },
                    { x = 1000, y = 0, discovered = false, name = '', ref = '01000201' },
                    { x = 0, y = -1000, discovered = true, name = 'Riften', ref = '01000202' },
                  },
                  rows = 4, minX = -3, minY = -2, width = 6,
                }
                local function col(f) local t = {}; for i, m in ipairs(data.markers) do t[i] = f(m) end; return t end
                map = {
                  ready = function() return true end,
                  revision = function() return 1 end,
                  marker_x = function() return col(function(m) return m.x end) end,
                  marker_y = function() return col(function(m) return m.y end) end,
                  marker_type = function() return col(function() return 1 end) end,
                  marker_discovered = function() return col(function(m) return m.discovered end) end,
                  marker_name = function() return col(function(m) return m.name end) end,
                  target_x = function() return {} end, target_y = function() return {} end,
                  target_world = function() return {} end, target_text = function() return {} end,
                  raster_info = function(world) if world ~= 5 then return {} end return { data.minX, data.minY, data.width, data.rows } end,
                  raster_row = function(world, row) local px = {}; for i = 1, data.width do px[i] = 0x3040 end return px end,
                }
                travel = {
                  marker_ref = function() return col(function(m) return m.ref end) end,
                }
                asked = {}
                player = { x = 0, y = 0, yaw = 0, world = 5 }
                closes = 0
                env = { ui = ui, map = map, player = function() return player end,
                              close = function() closes = closes + 1 end }
            )"));
            if (withTravel)
            {
                ASSERT_TRUE(run(R"(
                    env.travel = travel
                    env.requestTravel = function(ref, name) asked[#asked + 1] = ref .. ':' .. name end
                )"));
            }
            ASSERT_TRUE(run("wmap = W.create(env)"));
        }

        std::vector<ElementId> shownWithText(const std::string& text) const
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

        /// The visible marker frames in the order of the slots.
        std::vector<ElementId> shownMarkers() const
        {
            std::vector<ElementId> out;
            for (const ElementId id : withClass("map-marker"))
            {
                const auto* node = backend.node(id);
                const auto it = node->mProperties.find("display");
                if (it == node->mProperties.end() || it->second != "none")
                    out.push_back(id);
            }
            return out;
        }
    };

    TEST_F(SkyrimTravelLua, clickingADiscoveredMarkerAsksBeforeTravelling)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        const std::vector<ElementId> markers = shownMarkers();
        ASSERT_EQ(markers.size(), 3u);
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
        ASSERT_TRUE(backend.emit(markers[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(documents("skyrim-travel-prompt"), 1u);
        EXPECT_EQ(shownWithText("Travel to Whiterun?").size(), 1u);
        EXPECT_EQ(shownWithText("Yes").size(), 1u);
        EXPECT_EQ(shownWithText("No").size(), 1u);
        EXPECT_TRUE(get<bool>("wmap.asking()"));
        EXPECT_EQ(get<int>("#asked"), 0); // nothing is requested until the player says yes
    }

    TEST_F(SkyrimTravelLua, yesRequestsTheTravelOnceAndTakesThePromptDown)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(backend.emit(shownMarkers()[2], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        const std::vector<ElementId> yes = shownWithText("Yes");
        ASSERT_EQ(yes.size(), 1u);
        ASSERT_TRUE(backend.emit(yes[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_EQ(get<int>("#asked"), 1);
        EXPECT_EQ(get<std::string>("asked[1]"), "01000202:Riften");
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
        EXPECT_FALSE(get<bool>("wmap.asking()"));
        EXPECT_TRUE(get<bool>("wmap.isOpen()")); // the map stays until the global script answers
    }

    TEST_F(SkyrimTravelLua, noTakesThePromptDownAndRequestsNothing)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(backend.emit(shownMarkers()[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        const std::vector<ElementId> no = shownWithText("No");
        ASSERT_EQ(no.size(), 1u);
        ASSERT_TRUE(backend.emit(no[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<int>("#asked"), 0);
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
    }

    TEST_F(SkyrimTravelLua, aMarkerThePlayerHasNotFoundCannotBeTravelledTo)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(backend.emit(shownMarkers()[1], EventKind::Click)); // the undiscovered one: no name
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
        EXPECT_FALSE(get<bool>("wmap.asking()"));
    }

    TEST_F(SkyrimTravelLua, aSecondClickReplacesThePromptAndDismissingTheMapTakesItDown)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(backend.emit(shownMarkers()[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        ASSERT_TRUE(backend.emit(shownMarkers()[2], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(documents("skyrim-travel-prompt"), 1u);
        EXPECT_EQ(shownWithText("Travel to Riften?").size(), 1u);
        EXPECT_EQ(shownWithText("Travel to Whiterun?").size(), 0u);
        ASSERT_TRUE(run("wmap.dismiss()"));
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
    }

    TEST_F(SkyrimTravelLua, aNoticeIsShownInTheStatusLine)
    {
        build(true);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_TRUE(run("wmap.notice('You cannot fast travel while in combat.'); ui.update()"));
        EXPECT_EQ(shownWithText("You cannot fast travel while in combat.").size(), 1u);
    }

    TEST_F(SkyrimTravelLua, withoutTheTravelPackageNothingIsClickable)
    {
        build(false);
        ASSERT_TRUE(run("wmap.open(); ui.update()"));
        ASSERT_EQ(shownMarkers().size(), 3u);
        (void)backend.emit(shownMarkers()[0], EventKind::Click); // no listener: the event goes nowhere
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(documents("skyrim-travel-prompt"), 0u);
        EXPECT_FALSE(get<bool>("wmap.asking()"));
    }

    TEST_F(SkyrimTravelLua, theTextsOfTheRefusalsAreOnesAPlayerCanRead)
    {
        build(true);
        for (const char* reason :
            { "combat", "enemies", "interior", "undiscovered", "unknown", "no_destination", "not_ready" })
        {
            const std::string text = get<std::string>(std::string("W.refusalText('") + reason + "')");
            EXPECT_GT(text.size(), 10u) << reason;
            EXPECT_NE(text.find("travel"), std::string::npos) << reason;
        }
        EXPECT_EQ(get<std::string>("W.refusalText('combat')"), "You cannot fast travel while in combat.");
        EXPECT_GT(
            get<std::string>("W.refusalText('something new')").size(), 10u); // an unknown reason still says something
    }
}
