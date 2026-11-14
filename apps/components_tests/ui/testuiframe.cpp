// Task 0162: the cost of a frame in which nothing changed, and the whole path script -> queue -> engine thread -> events
// back. Written from the spec (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against stubs first.
#include "uiluafixture.hpp"

#include <components/ui/commandqueue.hpp>

namespace
{
    using UiTest::UiLuaFixture;
    using Ui::ElementId;
    using Ui::EventKind;

    struct UiFrame : UiLuaFixture
    {
        void buildPanel()
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                fps = ui.Value('FPS: 60')
                sel = ui.Value(1)
                items = {}
                for i = 1, 500 do items[i] = { id = i, name = 'row ' .. i } end
                hud = ui.Hud{ name = 'fps', build = function() return ui.Text{ text = fps } end }
                panel = ui.Document{ name = 'panel', class = 'window', build = function()
                    return ui.Column{
                        ui.Text{ text = 'Title' },
                        ui.Row{ ui.Button{ text = 'A', onClick = function() end }, ui.Button{ text = 'B' } },
                        ui.TextInput{ value = ui.Value('') },
                        ui.List{ items = items, key = function(it) return it.id end, rowHeight = 18, height = 120,
                            render = function(it)
                                return ui.Text{ text = it.name,
                                    class = ui.Computed(function() return sel:get() == it.id and 'selected' or '' end) }
                            end } }
                end }
                ui.update()
            )"));
        }
    };

    TEST_F(UiFrame, AFrameWithNothingChangedMakesNoProtocolCall)
    {
        buildPanel();
        const std::size_t calls = backend.callCount();
        const std::size_t commits = backend.commitCount();
        ASSERT_TRUE(run("for i = 1, 1000 do ui.update() end"));
        EXPECT_EQ(backend.callCount(), calls);
        EXPECT_EQ(backend.commitCount(), commits) << "an empty frame must not even commit";
    }

    TEST_F(UiFrame, AFrameWithNothingChangedAllocatesNoLuaMemory)
    {
        buildPanel();
        // One chunk, so compiling the measuring code is not counted.
        ASSERT_TRUE(run(R"(
            collectgarbage(); collectgarbage(); collectgarbage('stop')
            ui.update(); ui.update()
            local before = collectgarbage('count')
            for i = 1, 1000 do ui.update() end
            idleDelta = collectgarbage('count') - before
            collectgarbage('restart')
        )"));
        EXPECT_EQ(get<double>("idleDelta"), 0.0) << "1000 idle frames allocated Lua memory (KiB)";
    }

    TEST_F(UiFrame, AFrameWithOneChangeCostsOneProtocolCall)
    {
        buildPanel();
        backend.clearCalls();
        ASSERT_TRUE(run("fps:set('FPS: 59'); ui.update()"));
        EXPECT_EQ(backend.callCount(), 1u);
        EXPECT_EQ(backend.commitCount(), 2u);
    }

    TEST_F(UiFrame, ASelectionChangeInABigListTouchesOnlyTheRowsInView)
    {
        buildPanel();
        backend.clearCalls();
        ASSERT_TRUE(run("sel:set(2); ui.update()"));
        // Rows 1 and 2 (indices 0 and 1) both exist; each recomputes, both change class.
        EXPECT_EQ(backend.callCount(), 2u);
    }

    // ---- script -> queue -> engine thread -> events back ----

    TEST(UiPipeline, ScriptWritesGoThroughTheQueueAndEventsComeBack)
    {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);
        lua["package"]["path"]
            = (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data").generic_string() + "/?.lua";
        auto queue = std::make_shared<Ui::CommandQueue>();
        lua["package"]["loaded"]["openeden.uibackend"] = UiLua::makeBackendPackage(lua, [queue] {
            return std::shared_ptr<Ui::Backend>(queue->openSession());
        });
        sol::protected_function_result r = lua.safe_script(R"(
            ui = require('openeden.ui')
            clicks, typed = 0, nil
            v = ui.Value('a')
            doc = ui.Document{ name = 'd', build = function()
                return ui.Column{ ui.Button{ text = 'go', onClick = function() clicks = clicks + 1 end },
                    ui.Text{ text = v }, ui.TextInput{ value = ui.Value(''), onChange = function(s) typed = s end } }
            end }
            ui.update()
        )", sol::script_pass_on_error);
        ASSERT_TRUE(r.valid()) << r.get<sol::error>().what();

        Ui::HeadlessBackend engine{ 0 };
        EXPECT_GT(queue->drain(engine), 5u);
        ElementId button = Ui::kNoElement, input = Ui::kNoElement;
        for (const auto& [id, node] : engine.nodes())
        {
            if (node.mText == "go")
                button = id;
            if (node.mTag == "input")
                input = id;
        }
        ASSERT_NE(button, Ui::kNoElement);
        ASSERT_NE(input, Ui::kNoElement);
        EXPECT_TRUE(engine.node(button)->mListens.count(EventKind::Click));

        // The engine thread reports a click and a change; the script sees them at its next update.
        queue->pushEvent({ button, EventKind::Click, "" });
        queue->pushEvent({ input, EventKind::Change, "hello" });
        r = lua.safe_script("ui.update()", sol::script_pass_on_error);
        ASSERT_TRUE(r.valid()) << r.get<sol::error>().what();
        EXPECT_EQ(lua["clicks"].get<int>(), 1);
        EXPECT_EQ(lua["typed"].get<std::string>(), "hello");

        // A change reaches the engine only after the commit at the end of update.
        engine.clearCalls();
        r = lua.safe_script("v:set('b')", sol::script_pass_on_error);
        EXPECT_EQ(queue->drain(engine), 0u);
        r = lua.safe_script("ui.update()", sol::script_pass_on_error);
        EXPECT_EQ(queue->drain(engine), 1u);

        // Dropping the library's document session ends the documents.
        r = lua.safe_script("doc:destroy(); ui.update()", sol::script_pass_on_error);
        queue->drain(engine);
        EXPECT_EQ(engine.liveElements(), 0u);
    }
}
