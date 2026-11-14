// Task 0164: the engine-side per-frame hook of the `openeden.ui` library. Written from the spec
// (docs/tasks/0164-ui-same-frame-geometry-and-update-hook.md) and run red against a stub registry first.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using UiTest::UiLuaFixture;

    struct UiUpdateHook : UiLuaFixture
    {
        std::shared_ptr<UiLua::UpdateRegistry> registry = std::make_shared<UiLua::UpdateRegistry>();
        std::vector<std::string> errors;

        UiUpdateHook()
        {
            lua["package"]["loaded"]["openeden.uibackend"] = UiLua::makeBackendPackage(
                lua, [this] { return std::shared_ptr<Ui::Backend>(&backend, [](Ui::Backend*) {}); }, registry);
        }

        void runHook()
        {
            registry->runAll([this](const std::string& message) { errors.push_back(message); });
        }
    };

    TEST_F(UiUpdateHook, RequiringTheLibraryRegistersOneUpdater)
    {
        EXPECT_EQ(registry->size(), 0u);
        ASSERT_TRUE(run("ui = require('openeden.ui')"));
        EXPECT_EQ(registry->size(), 1u);
    }

    TEST_F(UiUpdateHook, TheHookFlushesAChangeWithoutAScriptCallingUpdate)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            v = ui.Value('a')
            ui.Hud{ name = 'h', build = function() return ui.Text{ text = v } end }
        )"));
        runHook();
        const std::size_t before = backend.callCount();
        ASSERT_TRUE(run("v:set('b')"));
        runHook();
        EXPECT_GT(backend.callCount(), before) << "the changed text was not written by the hook";
        bool seen = false;
        for (const auto& [id, node] : backend.nodes())
            seen = seen || node.mText == "b";
        EXPECT_TRUE(seen);
        EXPECT_TRUE(errors.empty());
    }

    TEST_F(UiUpdateHook, AManualUpdateBesideTheHookIsHarmless)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            v = ui.Value('a')
            ui.Hud{ name = 'h', build = function() return ui.Text{ text = v } end }
            v:set('b')
            ui.update()
        )"));
        const std::size_t calls = backend.callCount();
        const std::size_t commits = backend.commitCount();
        runHook();
        EXPECT_EQ(backend.callCount(), calls) << "the hook after a manual update must write nothing";
        EXPECT_EQ(backend.commitCount(), commits);
        EXPECT_TRUE(errors.empty());
    }

    TEST_F(UiUpdateHook, EverySessionThatRegisteredRunsOncePerCallInOrder)
    {
        ASSERT_TRUE(run(R"(
            order = {}
            local b = require('openeden.uibackend')
            for i = 1, 3 do
                local s = b.open()
                s.setUpdater(function() order[#order + 1] = i end)
            end
        )"));
        EXPECT_EQ(registry->size(), 3u);
        runHook();
        EXPECT_EQ(get<std::string>("table.concat(order, ',')"), "1,2,3");
        runHook();
        EXPECT_EQ(get<std::string>("table.concat(order, ',')"), "1,2,3,1,2,3");
    }

    TEST_F(UiUpdateHook, AFailingUpdaterDoesNotStopTheOthersAndIsReportedOnce)
    {
        ASSERT_TRUE(run(R"(
            ran = 0
            local b = require('openeden.uibackend')
            b.open().setUpdater(function() error('boom', 0) end)
            b.open().setUpdater(function() ran = ran + 1 end)
        )"));
        runHook();
        runHook();
        runHook();
        EXPECT_EQ(get<int>("ran"), 3);
        ASSERT_EQ(errors.size(), 1u) << "the same message every frame must be reported once";
        EXPECT_NE(errors[0].find("boom"), std::string::npos);
    }

    TEST_F(UiUpdateHook, ANewMessageFromTheSameUpdaterIsReportedAgain)
    {
        ASSERT_TRUE(run(R"(
            n = 0
            require('openeden.uibackend').open().setUpdater(function()
                n = n + 1
                error('e' .. (n > 1 and 2 or 1), 0)
            end)
        )"));
        runHook();
        runHook();
        runHook();
        EXPECT_EQ(errors.size(), 2u);
    }

    TEST_F(UiUpdateHook, ClearForgetsEveryUpdater)
    {
        ASSERT_TRUE(run("require('openeden.ui')"));
        registry->clear();
        EXPECT_EQ(registry->size(), 0u);
        const std::size_t calls = backend.callCount();
        runHook();
        EXPECT_EQ(backend.callCount(), calls);
    }

    TEST_F(UiUpdateHook, ASessionWithoutARegistryStillLoadsTheLibrary)
    {
        // This fixture's own package has no registry; a fresh package that never heard of one is the same case.
        lua["package"]["loaded"]["openeden.ui"] = sol::nil;
        lua["package"]["loaded"]["openeden.uibackend"] = UiLua::makeBackendPackage(
            lua, [this] { return std::shared_ptr<Ui::Backend>(&backend, [](Ui::Backend*) {}); });
        ASSERT_TRUE(run("ui = require('openeden.ui'); ui.update()"));
        EXPECT_EQ(registry->size(), 0u);
    }

    TEST_F(UiUpdateHook, AnUpdaterThatIsNotAFunctionIsALuaError)
    {
        sol::protected_function_result result
            = lua.safe_script("require('openeden.uibackend').open().setUpdater(5)", sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
    }
}
