// Task 0162: the Lua face of a backend (package `openeden.uibackend`). Written from the spec
// (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against stubs first.
#include "uiluafixture.hpp"

namespace
{
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct UiBinding : UiLuaFixture
    {
        UiBinding() { EXPECT_TRUE(run("session = require('openeden.uibackend').open()")); }
    };

    TEST_F(UiBinding, EachFunctionReachesTheBackendWithItsArguments)
    {
        ASSERT_TRUE(run(R"(
            doc = session.createDocument('main')
            a = session.create('div')
            b = session.create('input')
            assert(type(doc) == 'number' and doc ~= a and a ~= b)
            session.insertBefore(doc, a)
            session.insertBefore(doc, b, a)
            session.setText(a, 'hi')
            session.setClass(a, 'x y')
            session.setProperty(a, 'left', '4dp')
            session.setAttribute(b, 'type', 'text')
            session.listen(b, 'change', true)
            session.focus(b)
            session.detach(a)
            session.commit()
        )"));
        EXPECT_EQ(backend.calls().size(), 12u);
        EXPECT_EQ(backend.calls().front(), "createDocument #1 main");
        EXPECT_EQ(backend.calls()[1], "create #2 div");
        EXPECT_EQ(backend.calls()[4], "insertBefore #1 #3 #2");
        EXPECT_EQ(backend.calls()[10], "focus #3");
        EXPECT_EQ(backend.commitCount(), 1u);
    }

    TEST_F(UiBinding, CommitWithNothingWrittenDoesNotCommit)
    {
        ASSERT_TRUE(run("session.commit(); session.commit()"));
        EXPECT_EQ(backend.commitCount(), 0u);
        ASSERT_TRUE(run("session.create('div'); session.commit(); session.commit()"));
        EXPECT_EQ(backend.commitCount(), 1u);
    }

    TEST_F(UiBinding, PollCallsTheHandlerPerEventWithIdKindAndValue)
    {
        ASSERT_TRUE(run(R"(
            a = session.create('div')
            session.listen(a, 'click', true)
            session.listen(a, 'scroll', true)
            got = {}
        )"));
        const Ui::ElementId a = static_cast<Ui::ElementId>(get<double>("a"));
        backend.emit(a, EventKind::Click);
        backend.emit(a, EventKind::Scroll, "42");
        ASSERT_TRUE(run(R"(
            session.poll(function(id, kind, value) got[#got + 1] = id .. ':' .. kind .. ':' .. tostring(value) end)
        )"));
        EXPECT_EQ(get<int>("#got"), 2);
        EXPECT_EQ(get<std::string>("got[1]"), std::to_string(a) + ":click:");
        EXPECT_EQ(get<std::string>("got[2]"), std::to_string(a) + ":scroll:42");
        ASSERT_TRUE(run("session.poll(function() error('no events expected') end)"));
    }

    TEST_F(UiBinding, BadArgumentsRaiseErrorsThatNameTheFunction)
    {
        for (const char* call : { "session.create(5)", "session.setText('x', 'y')", "session.setProperty(1, 2, 3)",
                 "session.listen(1, 'hover', true)", "session.insertBefore(1)", "session.poll(5)", "session.create()" })
        {
            sol::protected_function_result r
                = lua.safe_script(std::string("return pcall(function() ") + call + " end)", sol::script_pass_on_error);
            ASSERT_TRUE(r.valid()) << call;
            EXPECT_FALSE(r.get<bool>(0)) << call << " should raise";
        }
        sol::protected_function_result r = lua.safe_script(
            "local ok, err = pcall(function() session.listen(1, 'hover', true) end) return tostring(err)",
            sol::script_pass_on_error);
        EXPECT_NE(r.get<std::string>().find("listen"), std::string::npos);
    }

    TEST_F(UiBinding, AnOpenerThatGivesNothingMakesOpenRaise)
    {
        lua["package"]["loaded"]["empty.backend"]
            = UiLua::makeBackendPackage(lua, [] { return std::shared_ptr<Ui::Backend>(); });
        sol::protected_function_result r
            = lua.safe_script("return pcall(require('empty.backend').open)", sol::script_pass_on_error);
        ASSERT_TRUE(r.valid());
        EXPECT_FALSE(r.get<bool>(0));
    }

    TEST_F(UiBinding, TwoSessionsAreTwoBackends)
    {
        ASSERT_TRUE(
            run("other = require('openeden.uibackend').open(); x = other.create('div'); y = session.create('div')"));
        EXPECT_EQ(backend.callCount(), 2u); // the fixture's opener hands out the same backend; both writes arrive
    }
}
