// Task 0162: the reactive model of `openeden.ui` (Value, Computed, Effect, Scope, one flush per frame). Written from
// the spec (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against a stub library first.
#include "uiluafixture.hpp"

namespace
{
    using UiTest::UiLuaFixture;
    using UiReactive = UiLuaFixture;

    TEST_F(UiReactive, AnEffectRunsOnceAtCreationAndOnceMoreForManyWritesInAFrame)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(1)
            runs, seen = 0, nil
            ui.withScope(scope, function()
                ui.Effect(function() runs = runs + 1; seen = v:get() end)
            end)
            assert(runs == 1 and seen == 1)
            v:set(2); v:set(3); v:set(4)
            assert(runs == 1, 'effects must wait for the flush')
            ui.flush()
            assert(runs == 2 and seen == 4, runs .. ' ' .. tostring(seen))
            ui.flush()
            assert(runs == 2, 'a flush with nothing changed runs nothing')
        )"));
    }

    TEST_F(UiReactive, SettingTheSameValueIsNotAChange)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value('a')
            runs = 0
            ui.withScope(scope, function() ui.Effect(function() v:get(); runs = runs + 1 end) end)
            v:set('a')
            ui.flush()
            assert(runs == 1)
            v:set('b'); v:set('a')   -- back to the starting value inside one frame is still a change
            ui.flush()
            assert(runs == 2, runs)
        )"));
    }

    TEST_F(UiReactive, ReadingOutsideAnEffectTracksNothing)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local v = ui.Value(5)
            assert(v:get() == 5 and v:peek() == 5)
            ui.flush()
        )"));
    }

    TEST_F(UiReactive, AComputedIsLazyAndCached)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local v = ui.Value(2)
            computes = 0
            c = ui.Computed(function() computes = computes + 1; return v:get() * 10 end)
            assert(computes == 0, 'lazy')
            assert(c:get() == 20 and c:get() == 20)
            assert(computes == 1, 'cached')
            v:set(3)
            assert(computes == 1, 'still lazy after a write')
            assert(c:get() == 30 and computes == 2)
        )"));
    }

    TEST_F(UiReactive, AComputedWithAnUnchangedResultDoesNotWakeItsReaders)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(1)
            parity = ui.Computed(function() return v:get() % 2 end)
            runs = 0
            ui.withScope(scope, function() ui.Effect(function() parity:get(); runs = runs + 1 end) end)
            v:set(3)
            ui.flush()
            assert(runs == 1, 'parity stayed 1: ' .. runs)
            v:set(4)
            ui.flush()
            assert(runs == 2)
        )"));
    }

    TEST_F(UiReactive, EffectsRunInCreationOrderWithinAFlush)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(0)
            order = {}
            ui.withScope(scope, function()
                for _, name in ipairs({ 'a', 'b', 'c' }) do
                    ui.Effect(function() v:get(); order[#order + 1] = name end)
                end
            end)
            order = {}
            v:set(1)
            ui.flush()
            assert(table.concat(order) == 'abc', table.concat(order))
        )"));
    }

    TEST_F(UiReactive, AnEffectThatReadsADifferentValueAfterAChangeDropsTheOldDependency)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            flag, a, b = ui.Value(true), ui.Value(1), ui.Value(1)
            runs = 0
            ui.withScope(scope, function()
                ui.Effect(function() runs = runs + 1; if flag:get() then a:get() else b:get() end end)
            end)
            flag:set(false); ui.flush()
            assert(runs == 2)
            a:set(2); ui.flush()
            assert(runs == 2, 'a is no longer read')
            b:set(2); ui.flush()
            assert(runs == 3)
        )"));
    }

    TEST_F(UiReactive, DestroyingAScopeStopsItsEffectsAndRunsCleanupsInReverse)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(1)
            runs, log = 0, {}
            ui.withScope(scope, function() ui.Effect(function() v:get(); runs = runs + 1 end) end)
            scope:onCleanup(function() log[#log + 1] = 'first' end)
            scope:onCleanup(function() log[#log + 1] = 'second' end)
            scope:destroy()
            assert(table.concat(log, ',') == 'second,first', table.concat(log, ','))
            v:set(2); ui.flush()
            assert(runs == 1, 'a destroyed scope runs nothing')
            scope:destroy() -- twice is harmless
            assert(#log == 2)
        )"));
    }

    TEST_F(UiReactive, NestedScopesAreDestroyedWithTheirParent)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local parent = ui.Scope()
            local child = ui.Scope(parent)
            local grandchild = ui.Scope(child)
            v = ui.Value(1)
            runs, cleaned = 0, 0
            for _, s in ipairs({ parent, child, grandchild }) do
                ui.withScope(s, function() ui.Effect(function() v:get(); runs = runs + 1 end) end)
                s:onCleanup(function() cleaned = cleaned + 1 end)
            end
            assert(runs == 3)
            parent:destroy()
            assert(cleaned == 3)
            v:set(2); ui.flush()
            assert(runs == 3)
        )"));
    }

    TEST_F(UiReactive, ACleanupMayDestroyAnotherScopeWithoutTrouble)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local a, b = ui.Scope(), ui.Scope()
            a:onCleanup(function() b:destroy() end)
            b:onCleanup(function() a:destroy() end)
            a:destroy()
        )"));
    }

    TEST_F(UiReactive, AnEffectCannotBeCreatedWithoutAScope)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local ok, err = pcall(ui.Effect, function() end)
            assert(not ok and tostring(err):find('scope'), tostring(err))
        )"));
    }

    TEST_F(UiReactive, AnEffectThatKeepsDirtyingItselfIsALocatedErrorNotAHang)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(0)
            ui.withScope(scope, function()
                ui.Effect(function() local x = v:get(); if x > 0 then v:set(x + 1) end end)
            end)
            v:set(1)
            local ok, err = pcall(ui.flush)
            assert(not ok and tostring(err):find('cycle'), tostring(err))
        )"));
    }

    TEST_F(UiReactive, AnEffectMayWriteAnotherValueAndTheFlushSettles)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            a, b = ui.Value(1), ui.Value(0)
            seen = nil
            ui.withScope(scope, function()
                ui.Effect(function() b:set(a:get() * 2) end)
                ui.Effect(function() seen = b:get() end)
            end)
            a:set(5)
            ui.flush()
            assert(seen == 10, tostring(seen))
        )"));
    }

    TEST_F(UiReactive, AnErrorInAnEffectNamesItAndTheFlushKeepsGoing)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(0)
            after = 0
            ui.withScope(scope, function()
                ui.Effect(function() if v:get() == 1 then error('boom') end end)
                ui.Effect(function() v:get(); after = after + 1 end)
            end)
            v:set(1)
            local ok, err = pcall(ui.flush)
            assert(not ok and tostring(err):find('boom'), tostring(err))
            assert(after == 2, 'the second effect still ran: ' .. after)
        )"));
    }

    TEST_F(UiReactive, StatsCountEffectAndComputeRuns)
    {
        EXPECT_TRUE(run(R"(
            ui = require('openeden.ui')
            local scope = ui.Scope()
            v = ui.Value(1)
            local c = ui.Computed(function() return v:get() + 1 end)
            ui.withScope(scope, function() ui.Effect(function() c:get() end) end)
            local s = ui.stats()
            assert(s.effectRuns == 1 and s.computeRuns == 1, s.effectRuns .. ' ' .. s.computeRuns)
            v:set(2); ui.flush()
            s = ui.stats()
            assert(s.effectRuns == 2 and s.computeRuns == 2)
        )"));
    }
}
