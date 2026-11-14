// Task 0162: "a frame in which nothing changed allocates nothing" on the C++ side of the UI library: the script
// session over the command queue, polling for events, committing nothing. (The Lua side's memory is checked in
// components-tests, testuiframe.cpp.) Counts operator new of the calling thread with the counter of
// apps/openmw_tests/support/allocationcounter.hpp. Written after the code (the counter proves the claim, a deliberately
// allocating poll was not tried).
#include <components/ui/commandqueue.hpp>
#include <components/ui/headless.hpp>
#include <components/ui_lua/backendpackage.hpp>

#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include <filesystem>
#include <memory>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    TEST(UiAllocation, IdleFramesAllocateNothingThroughTheQueueSession)
    {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);
        lua["package"]["path"]
            = (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data").generic_string() + "/?.lua";
        auto queue = std::make_shared<Ui::CommandQueue>();
        lua["package"]["loaded"]["openeden.uibackend"]
            = UiLua::makeBackendPackage(lua, [queue] { return std::shared_ptr<Ui::Backend>(queue->openSession()); });
        sol::protected_function_result built = lua.safe_script(R"(
            ui = require('openeden.ui')
            v = ui.Value('a')
            doc = ui.Hud{ name = 'h', build = function() return ui.Column{ ui.Text{ text = v }, ui.Button{ text = 'b', onClick = function() end } } end }
            update = ui.update
            ui.update(); ui.update()
        )",
            sol::script_pass_on_error);
        ASSERT_TRUE(built.valid()) << built.get<sol::error>().what();
        Ui::HeadlessBackend engine{ 0 };
        queue->drain(engine);

        sol::protected_function update = lua["update"];
        // The calls themselves go through sol: warm one call so that its own caches exist.
        ASSERT_TRUE(update().valid());
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 200; ++i)
                update();
            allocations = counter.count();
        }
        // sol's protected call of a Lua function takes no heap memory in the steady state; what is left would be the
        // queue session (poll, commit), which is what this test is about.
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(queue->pendingBatches(), 0u);
    }
}
