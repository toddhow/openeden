// Task 0219: the Skyrim SE service windows of files/data/openeden/skyrimse/services_windows.lua (training, renting a
// room and gifts) over the headless UI backend and fakes of the `core.skyrimse.inventory` and
// `core.skyrimse.services` packages written in Lua. Written after the module (the window logic was drafted with the
// pack's numbers); the pack's own rules are tested in testskyrimseservices.cpp.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimServicesWindows : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.services_windows')
                GOLD = '0000000f'
                store = { player = { [GOLD] = 100, ['00000b02'] = 2, ['00000a01'] = 3 }, ['700'] = {} }
                info = { [GOLD] = 'Gold', ['00000a01'] = 'Pebble', ['00000b02'] = 'Sword' }
                invRevision = 1
                inv = {
                  revision = function() return invRevision end,
                  items = function(o)
                    local ids = {}
                    for id in pairs(store[o] or {}) do ids[#ids + 1] = id end
                    table.sort(ids, function(a, b) return info[a] < info[b] end)
                    return ids
                  end,
                  count = function(o, id) return (store[o] or {})[id] or 0 end,
                  name = function(id) return info[id] end,
                  gold_item = function() return GOLD end,
                }
                st = { state = 'training', reason = '', name = 'Njada', revision = 1, gold = 1000, skill = 'Block',
                       level = 15, max = 75, cost = 150, left = 5, can = '', rentCost = 10, rentHours = 24,
                       rentState = 'available', hoursLeft = 0, owner = '700', last = '' }
                services = {
                  state = function() return st.state end,
                  reason = function() return st.reason end,
                  name = function() return st.name end,
                  revision = function() return st.revision end,
                  gold = function() return st.gold end,
                  skill_name = function() return st.skill end,
                  skill_level = function() return st.level end,
                  max_level = function() return st.max end,
                  cost = function() return st.cost end,
                  trains_left = function() return st.left end,
                  can_train = function() return st.can end,
                  rent_cost = function() return st.rentCost end,
                  rent_hours = function() return st.rentHours end,
                  rent_state = function() return st.rentState end,
                  rent_hours_left = function() return st.hoursLeft end,
                  owner = function() return st.owner end,
                  last_result = function() return st.last end,
                }
                trains, rents, gives, closes = 0, 0, {}, 0
                env = { ui = ui, inv = inv, services = services,
                  train = function() trains = trains + 1 end,
                  rent = function() rents = rents + 1 end,
                  give = function(owner, id, n) gives[#gives + 1] = { owner, id, n } end,
                  close = function() closes = closes + 1 end }
                win = W.create(env)
            )"));
        }

        std::vector<ElementId> buttonsWith(const std::string& prefix) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass("button"))
                if (backend.node(id)->mText.rfind(prefix, 0) == 0)
                    out.push_back(id);
            return out;
        }

        std::vector<std::string> documents() const
        {
            std::vector<std::string> names;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument)
                    names.push_back(node.mName);
            std::sort(names.begin(), names.end());
            return names;
        }

        bool textExists(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }
    };

    TEST_F(SkyrimServicesWindows, trainingShowsTheSkillTheLevelTheCostAndTheGold)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-training" }));
        EXPECT_TRUE(textExists("Njada"));
        EXPECT_TRUE(textExists("Skill: Block"));
        EXPECT_TRUE(textExists("Level 15 (this trainer teaches up to 75)"));
        EXPECT_TRUE(textExists("Cost 150 gold   Your gold 1000"));
        EXPECT_TRUE(textExists("Trainings left at this level: 5"));
    }

    TEST_F(SkyrimServicesWindows, theTrainButtonAsksThePackAndTheNumbersFollowTheRevision)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        click(buttonsWith("Train").front());
        EXPECT_EQ(get<int>("trains"), 1);
        ASSERT_TRUE(
            run("st.level = 16; st.cost = 160; st.gold = 850; st.left = 4; st.revision = 2; "
                "st.last = 'trained Block 15 -> 16'; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Level 16 (this trainer teaches up to 75)"));
        EXPECT_TRUE(textExists("Cost 160 gold   Your gold 850"));
        EXPECT_TRUE(textExists("Trainings left at this level: 4"));
        EXPECT_TRUE(textExists("trained Block 15 -> 16"));
    }

    TEST_F(SkyrimServicesWindows, aReasonTheTrainerRefusesForIsShownInWords)
    {
        ASSERT_TRUE(run("st.can = 'gold'; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("You do not have enough gold."));
    }

    TEST_F(SkyrimServicesWindows, anIdleFrameChangesNothing)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        const std::size_t before = backend.nodes().size();
        ASSERT_TRUE(run("win.update(); win.update(); ui.update()"));
        EXPECT_EQ(backend.nodes().size(), before);
    }

    TEST_F(SkyrimServicesWindows, rentShowsTheCostAndHoursAndTheRentButtonAsksThePack)
    {
        ASSERT_TRUE(run("st.state = 'rent'; st.name = 'Hulda'; win.open(); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-rent" }));
        EXPECT_TRUE(textExists("Rent a room"));
        EXPECT_TRUE(textExists("10 gold for 24 hours"));
        click(buttonsWith("Rent").front());
        EXPECT_EQ(get<int>("rents"), 1);
        ASSERT_TRUE(run("st.rentState = 'active'; st.hoursLeft = 24; st.revision = 2; win.update(); ui.update()"));
        EXPECT_TRUE(textExists("Your room is rented: 24.0 hours left"));
    }

    TEST_F(SkyrimServicesWindows, aGiftListsThePlayersItemsWithoutGoldAndGivesToTheNpcOwner)
    {
        ASSERT_TRUE(run("st.state = 'gift'; st.name = 'Hulda'; win.open(); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-gift" }));
        EXPECT_EQ(buttonsWith("Gold").size(), 0u);
        EXPECT_EQ(buttonsWith("Pebble  x3").size(), 1u);
        click(buttonsWith("Sword  x2").front());
        ASSERT_EQ(get<int>("#gives"), 1);
        EXPECT_EQ(get<std::string>("gives[1][1]"), "700");
        EXPECT_EQ(get<std::string>("gives[1][2]"), "00000b02");
        EXPECT_EQ(get<int>("gives[1][3]"), 1);
    }

    TEST_F(SkyrimServicesWindows, aGiftListFollowsTheInventoryRevision)
    {
        ASSERT_TRUE(run("st.state = 'gift'; win.open(); ui.update()"));
        ASSERT_TRUE(run("store.player['00000b02'] = 1; invRevision = 2; win.update(); ui.update()"));
        EXPECT_EQ(buttonsWith("Sword").size(), 1u);
        EXPECT_EQ(buttonsWith("Sword  x2").size(), 0u);
    }

    TEST_F(SkyrimServicesWindows, aPendingSessionShowsOpeningAndBuildsThePanelWhenItSettles)
    {
        ASSERT_TRUE(run("st.state = 'pending'; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("Opening..."));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-service" }));
        ASSERT_TRUE(run("st.state = 'training'; st.revision = 2; win.update(); ui.update()"));
        EXPECT_EQ(documents(), (std::vector<std::string>{ "skyrim-training" }));
    }

    TEST_F(SkyrimServicesWindows, aRefusedSessionShowsTheReasonAndOnlyClose)
    {
        ASSERT_TRUE(run("st.state = 'refused'; st.reason = 'this is not a trainer'; win.open(); ui.update()"));
        EXPECT_TRUE(textExists("this is not a trainer"));
        EXPECT_EQ(buttonsWith("Train").size(), 0u);
        EXPECT_EQ(buttonsWith("Close").size(), 1u);
    }

    TEST_F(SkyrimServicesWindows, closeCallsTheHookAndDismissTakesTheWindowDown)
    {
        ASSERT_TRUE(run("win.open(); ui.update()"));
        click(buttonsWith("Close").front());
        EXPECT_EQ(get<int>("closes"), 1);
        ASSERT_TRUE(run("win.dismiss(); ui.update()"));
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        EXPECT_TRUE(documents().empty());
    }
}
