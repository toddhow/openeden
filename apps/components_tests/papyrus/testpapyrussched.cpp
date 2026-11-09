// Task 0093 stage d: the scheduler. Latent natives park a stack, tick() wakes it when script time reaches its wake
// time, in (wake time, creation sequence) order, other stacks keep running meanwhile, the host can complete or cancel a
// parked stack, and the caps and the per-tick budget hold. Written from the task's spec before the scheduler and run
// red against a stub (the stage c behaviour) first. No game asset anywhere: every script is generated.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using ScriptPapyrus::ErrorKind;

    ScriptPapyrus::RuntimeOptions latentOptions()
    {
        ScriptPapyrus::RuntimeOptions options;
        options.runLatentNatives = true;
        return options;
    }

    ScriptBuilder gameWithLatents()
    {
        ScriptBuilder g("Game");
        g.function("Wait").global().native().param("seconds", "Float");
        g.function("Park").global().native();
        g.function("Mark").global().native().param("what", "Int");
        g.function("Plain").global().native().param("seconds", "Float");
        return g;
    }

    /// The scripts that log what they do through Game.Mark and wait through Game.Wait.
    ScriptBuilder sleeper()
    {
        ScriptBuilder s("Sleeper");
        s.var("counter", "Int");
        s.var("kept", "String");
        s.function("OnGo")
            .param("w", "Float")
            .param("base", "Int")
            .local("n", "None")
            .local("t", "Int")
            .callStatic("Game", "Mark", Id("n"), { Id("base") })
            .callStatic("Game", "Wait", Id("n"), { Id("w") })
            .iadd(Id("t"), Id("base"), Int(1))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .ret(Nil());
        s.function("OnChain")
            .param("w1", "Float")
            .param("w2", "Float")
            .param("w3", "Float")
            .param("base", "Int")
            .local("n", "None")
            .local("t", "Int")
            .callStatic("Game", "Mark", Id("n"), { Id("base") })
            .callStatic("Game", "Wait", Id("n"), { Id("w1") })
            .iadd(Id("t"), Id("base"), Int(1))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .callStatic("Game", "Wait", Id("n"), { Id("w2") })
            .iadd(Id("t"), Id("base"), Int(2))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .callStatic("Game", "Wait", Id("n"), { Id("w3") })
            .iadd(Id("t"), Id("base"), Int(3))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .ret(Nil());
        // A local computed before the wait is used after it.
        s.function("OnLocals", "None")
            .local("n", "None")
            .local("a", "Int")
            .local("b", "Int")
            .assign(Id("a"), Int(40))
            .callStatic("Game", "Wait", Id("n"), { Flt(2.0f) })
            .iadd(Id("b"), Id("a"), Int(2))
            .assign(Id("counter"), Id("b"))
            .ret(Nil());
        s.function("Helper").local("n", "None").callStatic("Game", "Wait", Id("n"), { Flt(3.0f) }).ret(Nil());
        // A wait inside a called function: the caller continues after the callee returns.
        s.function("OnNested")
            .local("n", "None")
            .assign(Id("counter"), Int(1))
            .callMethod("Helper", Id("self"), Id("n"))
            .assign(Id("counter"), Int(2))
            .ret(Nil());
        // call() runs it until the wait.
        s.function("Loop", "Int")
            .local("n", "None")
            .assign(Id("counter"), Int(1))
            .callStatic("Game", "Wait", Id("n"), { Flt(1.0f) })
            .assign(Id("counter"), Int(2))
            .ret(Int(7));
        // Zero waits in an endless loop: one pass per tick, never more.
        s.function("OnZero")
            .local("n", "None")
            .iadd(Id("counter"), Id("counter"), Int(1))
            .callStatic("Game", "Wait", Id("n"), { Flt(0.0f) })
            .jmp(-2)
            .ret(Nil());
        s.function("OnPark")
            .local("n", "None")
            .callStatic("Game", "Park", Id("n"))
            .iadd(Id("counter"), Id("counter"), Int(1))
            .ret(Nil());
        s.function("OnPlain")
            .local("n", "None")
            .callStatic("Game", "Plain", Id("n"), { Flt(5.0f) })
            .assign(Id("counter"), Int(9))
            .ret(Nil());
        s.function("OnBadWait")
            .local("n", "None")
            .callStatic("Game", "Wait", Id("n"), { Flt(1.0f) })
            .assign(Id("counter"), Int(5))
            .ret(Nil());
        // After waking it spins: its budget is used up, nothing else suffers.
        s.function("OnWakeAndSpin")
            .local("n", "None")
            .callStatic("Game", "Wait", Id("n"), { Flt(1.0f) })
            .nop()
            .jmp(-1)
            .ret(Nil());
        // A string made before the wait must still be there after a collection.
        s.function("OnHoldString")
            .local("n", "None")
            .local("s", "String")
            .op(Op::StrCat, { Id("s"), Str("held-"), Str("string") })
            .callStatic("Game", "Wait", Id("n"), { Flt(1.0f) })
            .assign(Id("kept"), Id("s"))
            .ret(Nil());
        s.function("OnChurn", "None")
            .local("i", "Int")
            .local("s", "String")
            .local("c", "Bool")
            .assign(Id("i"), Int(0))
            .op(Op::StrCat, { Id("s"), Str("churn-"), Str("churn-churn-churn-churn-churn-churn") })
            .iadd(Id("i"), Id("i"), Int(1))
            .op(Op::CompareLt, { Id("c"), Id("i"), Int(300) })
            .jmpt(Id("c"), -3)
            .ret(Nil());
        return s;
    }

    /// The host side: the marks in the order they were made, and the natives.
    struct Host
    {
        std::vector<std::int32_t> mMarks;
        std::vector<std::uint64_t> mTokens; ///< Tokens seen by Park.
        std::vector<std::uint64_t> mWaitTokens; ///< Tokens seen by Wait.
        std::function<void()> mOnPlain; ///< Runs inside the Plain native.
        double mLastWait = 0.0;
        bool mWaitCalledWithNaN = false;
        double mNextWait = std::numeric_limits<double>::quiet_NaN(); ///< When set (not NaN), Wait returns this.
    };

    void bindLatents(Vm& vm, Host& host)
    {
        ErrorReport error;
        NativeDescriptor wait;
        wait.className = "Game";
        wait.functionName = "Wait";
        wait.global = true;
        wait.latent = true;
        ASSERT_TRUE(vm.rt().registerNative(
            wait,
            [&host, &vm](const NativeCall& call) {
                host.mWaitTokens.push_back(vm.rt().latentToken());
                host.mLastWait = call.arguments[0].number;
                NativeResult result;
                result.waitSeconds
                    = std::isnan(host.mNextWait) ? static_cast<double>(call.arguments[0].number) : host.mNextWait;
                return result;
            },
            error))
            << error.format();
        NativeDescriptor park;
        park.className = "Game";
        park.functionName = "Park";
        park.global = true;
        park.latent = true;
        ASSERT_TRUE(vm.rt().registerNative(
            park,
            [&host, &vm](const NativeCall&) {
                host.mTokens.push_back(vm.rt().latentToken());
                NativeResult result;
                result.waitSeconds = -1.0;
                return result;
            },
            error))
            << error.format();
        NativeDescriptor mark;
        mark.className = "Game";
        mark.functionName = "Mark";
        mark.global = true;
        ASSERT_TRUE(vm.rt().registerNative(
            mark,
            [&host](const NativeCall& call) {
                host.mMarks.push_back(call.arguments[0].integer);
                return NativeResult{};
            },
            error))
            << error.format();
        NativeDescriptor plain;
        plain.className = "Game";
        plain.functionName = "Plain";
        plain.global = true;
        ASSERT_TRUE(vm.rt().registerNative(
            plain,
            [&host](const NativeCall& call) {
                if (host.mOnPlain)
                {
                    host.mOnPlain();
                }
                NativeResult result;
                result.waitSeconds = call.arguments[0].number; // ignored: the native is not latent
                return result;
            },
            error))
            << error.format();
    }

    struct Rig
    {
        explicit Rig(Target target = Target::SkyrimSE, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant,
            const ScriptPapyrus::RuntimeOptions& options = latentOptions())
            : mVm(target, policy, options)
        {
            EXPECT_TRUE(mVm.add(gameWithLatents()));
            EXPECT_TRUE(mVm.add(sleeper()));
            bindLatents(mVm, mHost);
        }

        InstanceId make()
        {
            const InstanceId id = mVm.makeAndInit("Sleeper");
            return id;
        }

        CallResult send(InstanceId id, const char* event, std::vector<PapyrusValue> args)
        {
            return mVm.rt().send(id, event, args);
        }

        Vm mVm;
        Host mHost;
    };

    PapyrusValue F(float v)
    {
        return PapyrusValue::makeFloat(v);
    }
    PapyrusValue I(std::int32_t v)
    {
        return PapyrusValue::makeInt(v);
    }

    // ---- parking and waking ----------------------------------------------------------------------------------------

    TEST(PapyrusSched, a_wait_parks_the_stack_while_other_stacks_keep_running)
    {
        for (const Target target : kBothTargets)
        {
            Rig rig(target);
            const InstanceId a = rig.make();
            const InstanceId b = rig.make();
            rig.send(a, "OnGo", { F(5.0f), I(100) });
            rig.send(b, "OnGo", { F(0.0f), I(200) });
            const TickResult tick = rig.mVm.tick(0.0);
            // a marked 100 and parked; b marked 200 and (zero wait) is parked too until the next tick.
            EXPECT_EQ(tick.stacksRun, 2u);
            EXPECT_EQ(tick.stacksFinished, 0u);
            EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 2u);
            EXPECT_EQ((std::vector<std::int32_t>{ 100, 200 }), rig.mHost.mMarks);
            EXPECT_TRUE(tick.errors.empty());
        }
    }

    TEST(PapyrusSched, a_stack_wakes_when_the_time_has_come_and_not_before)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnGo", { F(5.0f), I(10) });
        rig.mVm.tick(1.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 10 }), rig.mHost.mMarks);
        rig.mVm.tick(5.9);
        EXPECT_EQ(1u, rig.mHost.mMarks.size()) << "parked at 1.0 for 5.0 seconds: due at 6.0";
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        const TickResult woke = rig.mVm.tick(6.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 10, 11 }), rig.mHost.mMarks) << "at exactly the wake time";
        EXPECT_EQ(woke.stacksRun, 1u);
        EXPECT_EQ(woke.stacksFinished, 1u);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
        EXPECT_EQ(rig.mVm.rt().stats().stacksWoken, 1u);
    }

    TEST(PapyrusSched, stacks_wake_in_wake_time_order)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        const InstanceId c = rig.make();
        rig.send(a, "OnGo", { F(3.0f), I(10) });
        rig.send(b, "OnGo", { F(1.0f), I(20) });
        rig.send(c, "OnGo", { F(2.0f), I(30) });
        rig.mVm.tick(0.0);
        rig.mHost.mMarks.clear();
        rig.mVm.tick(10.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 21, 31, 11 }), rig.mHost.mMarks);
    }

    TEST(PapyrusSched, equal_wake_times_wake_in_creation_order)
    {
        Rig rig;
        std::vector<InstanceId> ids;
        for (int i = 0; i < 6; ++i)
        {
            ids.push_back(rig.make());
        }
        for (int i = 0; i < 6; ++i)
        {
            rig.send(ids[static_cast<std::size_t>(i)], "OnGo", { F(2.0f), I(100 * (i + 1)) });
        }
        rig.mVm.tick(0.0);
        rig.mHost.mMarks.clear();
        rig.mVm.tick(2.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 101, 201, 301, 401, 501, 601 }), rig.mHost.mMarks);
    }

    TEST(PapyrusSched, woken_stacks_run_before_the_events_queued_for_the_same_tick)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        rig.send(a, "OnGo", { F(1.0f), I(10) });
        rig.mVm.tick(0.0);
        rig.mHost.mMarks.clear();
        rig.send(b, "OnGo", { F(100.0f), I(20) });
        rig.mVm.tick(1.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 11, 20 }), rig.mHost.mMarks);
    }

    TEST(PapyrusSched, a_local_computed_before_the_wait_is_there_after_it)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnLocals", {});
        rig.mVm.tick(0.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 0);
        rig.mVm.tick(2.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 42);
    }

    TEST(PapyrusSched, a_wait_inside_a_called_function_resumes_the_whole_call_chain)
    {
        for (const Target target : kBothTargets)
        {
            Rig rig(target);
            const InstanceId a = rig.make();
            rig.send(a, "OnNested", {});
            rig.mVm.tick(0.0);
            EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
            rig.mVm.tick(2.9);
            EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
            const TickResult tick = rig.mVm.tick(3.0);
            EXPECT_EQ(rig.mVm.varInt(a, "counter"), 2);
            EXPECT_EQ(tick.stacksFinished, 1u);
        }
    }

    TEST(PapyrusSched, call_returns_suspended_and_the_rest_runs_at_the_wake_time)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const CallResult result = rig.mVm.call(a, "Loop");
        EXPECT_TRUE(result.ok) << result.error.format();
        EXPECT_TRUE(result.suspended);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        const TickResult before = rig.mVm.tick(0.5);
        EXPECT_EQ(before.stacksRun, 0u);
        const TickResult after = rig.mVm.tick(1.0);
        EXPECT_EQ(after.stacksFinished, 1u);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 2);
    }

    TEST(PapyrusSched, a_wait_of_zero_never_loops_inside_one_tick)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnZero", {});
        for (int i = 1; i <= 5; ++i)
        {
            rig.mVm.tick(0.1 * i);
            EXPECT_EQ(rig.mVm.varInt(a, "counter"), i) << "one pass of the loop per tick";
        }
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
    }

    TEST(PapyrusSched, the_same_inputs_give_the_same_marks_twice)
    {
        auto run = [] {
            Rig rig;
            const InstanceId a = rig.make();
            const InstanceId b = rig.make();
            rig.send(a, "OnChain", { F(0.5f), F(0.25f), F(1.0f), I(10) });
            rig.send(b, "OnChain", { F(0.25f), F(0.5f), F(0.0f), I(20) });
            for (int i = 0; i < 30; ++i)
            {
                rig.mVm.tick(0.1 * i);
            }
            return rig.mHost.mMarks;
        };
        const std::vector<std::int32_t> first = run();
        EXPECT_EQ(first.size(), 8u);
        EXPECT_EQ(first, run());
    }

    // ---- what is and is not latent ---------------------------------------------------------------------------------

    TEST(PapyrusSched, a_native_that_is_not_latent_ignores_a_wait_in_its_result)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnPlain", {});
        const TickResult tick = rig.mVm.tick(0.0);
        EXPECT_EQ(tick.stacksFinished, 1u);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 9);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
    }

    TEST(PapyrusSched, a_latent_native_nobody_registered_is_an_unregistered_native)
    {
        for (const UnknownFunctionPolicy policy : { UnknownFunctionPolicy::Tolerant, UnknownFunctionPolicy::Strict })
        {
            Vm vm(Target::SkyrimSE, policy, latentOptions());
            ASSERT_TRUE(vm.add(gameWithLatents()));
            ASSERT_TRUE(vm.add(sleeper()));
            const InstanceId a = vm.makeAndInit("Sleeper");
            vm.rt().send(a, "OnBadWait", {});
            const TickResult tick = vm.tick(0.0);
            EXPECT_EQ(vm.rt().suspendedStacks(), 0u);
            if (policy == UnknownFunctionPolicy::Tolerant)
            {
                EXPECT_TRUE(tick.errors.empty());
                EXPECT_EQ(vm.varInt(a, "counter"), 5)
                    << "the call returned the zero of its type and the script went on";
            }
            else
            {
                ASSERT_EQ(tick.errors.size(), 1u);
                EXPECT_TRUE(hasText(tick.errors[0], "[unregistered native]")) << tick.errors[0].format();
            }
        }
    }

    TEST(PapyrusSched, with_the_option_off_a_latent_native_is_still_skipped)
    {
        ScriptPapyrus::RuntimeOptions off;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, off);
        const InstanceId a = rig.make();
        rig.send(a, "OnLocals", {});
        const TickResult tick = rig.mVm.tick(0.0);
        EXPECT_EQ(tick.stacksFinished, 1u) << "the stage c behaviour";
        EXPECT_EQ(rig.mVm.rt().stats().latentSkipped, 1u);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
    }

    // ---- the host completes a wait ---------------------------------------------------------------------------------

    TEST(PapyrusSched, a_negative_wait_parks_until_the_host_completes_it)
    {
        Rig rig;
        const InstanceId a = rig.make();
        EXPECT_EQ(rig.mVm.rt().latentToken(), 0u) << "outside a handler there is no token";
        rig.send(a, "OnPark", {});
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mHost.mTokens.size(), 1u);
        EXPECT_NE(rig.mHost.mTokens[0], 0u);
        rig.mVm.tick(1.0e9);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 0) << "no timer wakes it";
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        EXPECT_FALSE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[0] + 1000));
        EXPECT_TRUE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[0]));
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 0) << "it resumes at the next tick, not inside completeLatent";
        const TickResult tick = rig.mVm.tick(1.0e9);
        EXPECT_EQ(tick.stacksFinished, 1u);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
        EXPECT_FALSE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[0])) << "a token works once";
    }

    TEST(PapyrusSched, completing_a_timed_wait_wakes_it_early)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnLocals", {});
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mHost.mWaitTokens.size(), 1u);
        EXPECT_NE(rig.mHost.mWaitTokens[0], 0u);
        EXPECT_TRUE(rig.mVm.rt().completeLatent(rig.mHost.mWaitTokens[0]));
        rig.mVm.tick(0.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 42) << "due at 2.0, woken at 0.0 by the host";
    }

    TEST(PapyrusSched, completing_one_wait_leaves_the_others_alone)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnPark", {});
        rig.mVm.tick(0.0);
        const std::uint64_t token = rig.mHost.mTokens.at(0);
        const InstanceId b = rig.make();
        rig.send(b, "OnLocals", {});
        rig.mVm.tick(0.0);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 2u);
        EXPECT_TRUE(rig.mVm.rt().completeLatent(token));
        rig.mVm.tick(0.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
        EXPECT_EQ(rig.mVm.varInt(b, "counter"), 0) << "the other timer is untouched";
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
    }

    // ---- bad waits, caps, time ------------------------------------------------------------------------------------

    TEST(PapyrusSched, a_wait_of_nan_ends_that_stack_with_a_native_error)
    {
        Rig rig;
        const InstanceId a = rig.make();
        // mNextWait is NaN, which means "use the argument", and the argument itself is NaN.
        rig.send(a, "OnGo", { F(std::numeric_limits<float>::quiet_NaN()), I(1) });
        const TickResult tick = rig.mVm.tick(0.0);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[native failed]")) << tick.errors[0].format();
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
    }

    TEST(PapyrusSched, an_infinite_wait_is_clamped_to_the_longest_wait)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxWaitSeconds = 100.0;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        rig.send(a, "OnGo", { F(std::numeric_limits<float>::infinity()), I(1) });
        rig.mVm.tick(0.0);
        rig.mVm.tick(99.0);
        EXPECT_EQ(rig.mHost.mMarks.size(), 1u);
        rig.mVm.tick(100.0);
        EXPECT_EQ(rig.mHost.mMarks.size(), 2u);
    }

    TEST(PapyrusSched, a_latent_call_over_the_cap_ends_that_stack_only)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxSuspendedStacks = 2;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        const InstanceId c = rig.make();
        rig.send(a, "OnGo", { F(5.0f), I(10) });
        rig.send(b, "OnGo", { F(5.0f), I(20) });
        rig.send(c, "OnGo", { F(5.0f), I(30) });
        const TickResult tick = rig.mVm.tick(0.0);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[limit exceeded]")) << tick.errors[0].format();
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 2u);
        rig.mVm.tick(5.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 10, 20, 30, 11, 21 }), rig.mHost.mMarks);
        // The room comes back when they finish.
        rig.send(c, "OnGo", { F(1.0f), I(40) });
        const TickResult again = rig.mVm.tick(5.0);
        EXPECT_TRUE(again.errors.empty());
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
    }

    TEST(PapyrusSched, time_that_goes_backwards_is_clamped_and_counted)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.mVm.tick(10.0);
        rig.send(a, "OnGo", { F(5.0f), I(10) });
        rig.mVm.tick(4.0); // clamped to 10.0: parked at 10.0, due at 15.0
        EXPECT_EQ(rig.mVm.rt().stats().timeMovedBackwards, 1u);
        rig.mVm.tick(14.9);
        EXPECT_EQ(rig.mHost.mMarks.size(), 1u);
        rig.mVm.tick(15.0);
        EXPECT_EQ(rig.mHost.mMarks.size(), 2u);
        rig.mVm.tick(std::numeric_limits<double>::quiet_NaN());
        EXPECT_EQ(rig.mVm.rt().stats().timeMovedBackwards, 2u) << "a time that is not a number counts as going back";
    }

    // ---- budgets ---------------------------------------------------------------------------------------------------

    TEST(PapyrusSched, a_woken_stack_has_a_fresh_instruction_budget_and_a_runaway_one_ends_alone)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        rig.send(a, "OnWakeAndSpin", {});
        rig.send(b, "OnGo", { F(1.0f), I(20) });
        rig.mVm.tick(0.0);
        Budget budget;
        budget.maxInstructions = 500;
        const TickResult tick = rig.mVm.rt().tick(1.0, budget);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[budget exceeded]")) << tick.errors[0].format();
        EXPECT_EQ((std::vector<std::int32_t>{ 20, 21 }), rig.mHost.mMarks) << "the other stack was not starved";
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
    }

    TEST(PapyrusSched, the_tick_budget_defers_due_stacks_in_order_and_stacksWaiting_counts_them)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxInstructionsPerTick = 1; // every stack run uses more, so one is run per tick
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        std::vector<InstanceId> ids;
        for (int i = 0; i < 4; ++i)
        {
            ids.push_back(rig.make());
        }
        for (int i = 0; i < 4; ++i)
        {
            rig.send(
                ids[static_cast<std::size_t>(i)], "OnGo", { F(1.0f + 0.1f * static_cast<float>(i)), I(100 * (i + 1)) });
        }
        // Each tick runs one stack: the four events start one by one.
        for (int i = 0; i < 4; ++i)
        {
            rig.mVm.tick(0.0);
        }
        EXPECT_EQ((std::vector<std::int32_t>{ 100, 200, 300, 400 }), rig.mHost.mMarks);
        rig.mHost.mMarks.clear();
        TickResult tick = rig.mVm.tick(10.0);
        EXPECT_EQ(tick.stacksRun, 1u);
        EXPECT_EQ(tick.stacksWaiting, 3u) << "three are due and wait their turn";
        tick = rig.mVm.tick(10.0);
        tick = rig.mVm.tick(10.0);
        tick = rig.mVm.tick(10.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 101, 201, 301, 401 }), rig.mHost.mMarks) << "still in wake order";
        EXPECT_EQ(tick.stacksWaiting, 0u);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
    }

    // ---- cancelling ------------------------------------------------------------------------------------------------

    TEST(PapyrusSched, cancelWaiting_drops_the_parked_stacks_of_an_instance_for_good)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        rig.send(a, "OnGo", { F(1.0f), I(10) });
        rig.send(a, "OnGo", { F(2.0f), I(20) });
        rig.send(b, "OnGo", { F(1.0f), I(30) });
        rig.mVm.tick(0.0);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 3u);
        EXPECT_EQ(rig.mVm.rt().cancelWaiting(a), 2u);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        rig.mHost.mMarks.clear();
        rig.mVm.tick(100.0);
        EXPECT_EQ((std::vector<std::int32_t>{ 31 }), rig.mHost.mMarks);
        EXPECT_EQ(rig.mVm.rt().cancelWaiting(a), 0u);
    }

    TEST(PapyrusSched, cancelWaiting_of_zero_drops_everything_and_the_room_is_reused)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxSuspendedStacks = 4;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        for (int round = 0; round < 50; ++round)
        {
            for (int i = 0; i < 4; ++i)
            {
                rig.send(a, "OnGo", { F(1000.0f), I(1) });
            }
            const TickResult tick = rig.mVm.tick(round);
            EXPECT_TRUE(tick.errors.empty()) << "round " << round;
            EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 4u);
            EXPECT_EQ(rig.mVm.rt().cancelWaiting(0), 4u);
            EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
        }
    }

    TEST(PapyrusSched, cancelWaiting_and_completeLatent_are_refused_inside_a_native)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        rig.send(a, "OnGo", { F(100.0f), I(10) });
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        std::uint32_t cancelled = 99;
        bool completed = true;
        rig.mHost.mOnPlain = [&] {
            cancelled = rig.mVm.rt().cancelWaiting(0);
            completed = rig.mVm.rt().completeLatent(rig.mHost.mWaitTokens.at(0));
        };
        rig.send(b, "OnPlain", {});
        rig.mVm.tick(0.0);
        EXPECT_EQ(cancelled, 0u);
        EXPECT_FALSE(completed);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u) << "the parked stack was not touched";
    }

    // ---- the heap -------------------------------------------------------------------------------------------------

    TEST(PapyrusSched, a_parked_stack_keeps_what_it_holds_through_a_collection)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxHeapBytes = 4096; // small, so that the churn below collects often
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        rig.send(a, "OnHoldString", {});
        rig.mVm.tick(0.0);
        const std::uint64_t before = rig.mVm.rt().stats().collections;
        rig.send(b, "OnChurn", {});
        rig.mVm.tick(0.5);
        EXPECT_GT(rig.mVm.rt().stats().collections, before) << "the churn collected";
        rig.mVm.tick(1.0);
        PapyrusValue kept;
        ASSERT_TRUE(rig.mVm.rt().getVariable(a, "kept", kept));
        EXPECT_EQ(kept.kind, PapyrusKind::String);
        EXPECT_EQ(std::string(kept.text), "held-string");
    }

    // ---- a property test: the order is the one a model of the scheduler gives --------------------------------------

    // The model: at each tick, the parked stacks that are due (wake <= time) at its start run first in (wake, creation)
    // order, then the events sent since the last tick in sending order; a stack that parks again in this tick is not
    // woken in it, even for a wait of zero.
    TEST(PapyrusSched, random_programs_run_in_the_order_the_model_gives_and_twice_the_same)
    {
        for (std::uint32_t seed = 1; seed <= 40; ++seed)
        {
            auto simulate = [seed](bool useRuntime) {
                std::mt19937 random(seed);
                const int stacks = 3 + static_cast<int>(random() % 10);
                struct Plan
                {
                    float mWait[3];
                    int mSendTick;
                };
                std::vector<Plan> plans;
                for (int i = 0; i < stacks; ++i)
                {
                    Plan plan{};
                    for (float& w : plan.mWait)
                    {
                        // Multiples of 0.25 (exact in binary) including 0.
                        w = 0.25f * static_cast<float>(random() % 9);
                    }
                    plan.mSendTick = static_cast<int>(random() % 6);
                    plans.push_back(plan);
                }
                const int ticks = 40;
                std::vector<double> times;
                double time = 0.0;
                for (int t = 0; t < ticks; ++t)
                {
                    time += 0.25 * static_cast<double>(random() % 4); // may stay equal, never back
                    times.push_back(time);
                }

                std::vector<std::int32_t> marks;
                if (useRuntime)
                {
                    Rig rig;
                    std::vector<InstanceId> ids;
                    for (int i = 0; i < stacks; ++i)
                    {
                        ids.push_back(rig.make());
                    }
                    for (int t = 0; t < ticks; ++t)
                    {
                        for (int i = 0; i < stacks; ++i)
                        {
                            if (plans[static_cast<std::size_t>(i)].mSendTick == t)
                            {
                                const Plan& p = plans[static_cast<std::size_t>(i)];
                                rig.send(ids[static_cast<std::size_t>(i)], "OnChain",
                                    { F(p.mWait[0]), F(p.mWait[1]), F(p.mWait[2]), I(10 * (i + 1)) });
                            }
                        }
                        const TickResult result = rig.mVm.tick(times[static_cast<std::size_t>(t)]);
                        EXPECT_TRUE(result.errors.empty());
                    }
                    marks = rig.mHost.mMarks;
                }
                else
                {
                    struct Parked
                    {
                        double mWake;
                        int mSeq;
                        int mId;
                        int mStep;
                    };
                    std::vector<Parked> parked;
                    int nextSeq = 0; // the order the events were sent in
                    for (int t = 0; t < ticks; ++t)
                    {
                        const double now = times[static_cast<std::size_t>(t)];
                        std::vector<Parked> due;
                        for (auto it = parked.begin(); it != parked.end();)
                        {
                            if (it->mWake <= now)
                            {
                                due.push_back(*it);
                                it = parked.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        std::sort(due.begin(), due.end(), [](const Parked& a, const Parked& b) {
                            return a.mWake != b.mWake ? a.mWake < b.mWake : a.mSeq < b.mSeq;
                        });
                        for (const Parked& p : due)
                        {
                            marks.push_back(10 * (p.mId + 1) + p.mStep);
                            if (p.mStep < 3)
                            {
                                parked.push_back(
                                    { now + static_cast<double>(plans[static_cast<std::size_t>(p.mId)].mWait[p.mStep]),
                                        p.mSeq, p.mId, p.mStep + 1 });
                            }
                        }
                        for (int i = 0; i < stacks; ++i)
                        {
                            if (plans[static_cast<std::size_t>(i)].mSendTick == t)
                            {
                                marks.push_back(10 * (i + 1));
                                parked.push_back(
                                    { now + static_cast<double>(plans[static_cast<std::size_t>(i)].mWait[0]), nextSeq++,
                                        i, 1 });
                            }
                        }
                    }
                }
                return marks;
            };
            const std::vector<std::int32_t> model = simulate(false);
            const std::vector<std::int32_t> actual = simulate(true);
            EXPECT_EQ(model, actual) << "seed " << seed;
            EXPECT_EQ(actual, simulate(true)) << "seed " << seed << ": a second run differs";
        }
    }

    // ---- review of 2026-11-08 (round 5): growth, tokens, host waits, option checks
    // ------------------------------------ In plain words: the lists of parked stacks grow by doubling (not by one slot
    // per park), every park hands out a token of its own (a token kept from an earlier wait must not wake a later one),
    // completing many waits is not quadratic, a host wait can be given an expiry, bad option values are replaced, and
    // with latent natives off the time given to tick() is not looked at.

    TEST(PapyrusSchedReview5, parking_many_stacks_grows_the_lists_geometrically)
    {
        Rig rig;
        const InstanceId a = rig.make();
        for (int i = 0; i < 3000; ++i)
        {
            rig.send(a, "OnGo", { F(100.0f), I(i) });
        }
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mVm.rt().suspendedStacks(), 3000u);
        // Doubling needs about log2(3000) = 12 growths of the waiting list and a few of the due list; one slot per
        // park needs 3000.
        EXPECT_LE(rig.mVm.rt().stats().schedulerGrowths, 40u);
        rig.mVm.tick(100.0);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u);
        EXPECT_LE(rig.mVm.rt().stats().schedulerGrowths, 60u);
    }

    TEST(PapyrusSchedReview5, a_token_kept_from_an_earlier_wait_does_not_wake_a_later_one)
    {
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnChain", { F(1.0f), F(5.0f), F(5.0f), I(10) });
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mHost.mWaitTokens.size(), 1u);
        const std::uint64_t first = rig.mHost.mWaitTokens[0];
        rig.mVm.tick(1.0); // the timer woke it; it is parked on its second wait
        ASSERT_EQ(rig.mHost.mWaitTokens.size(), 2u);
        const std::uint64_t second = rig.mHost.mWaitTokens[1];
        EXPECT_NE(first, second) << "every park has a token of its own";
        EXPECT_FALSE(rig.mVm.rt().completeLatent(first)) << "the first wait is over";
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        const TickResult early = rig.mVm.tick(2.0);
        EXPECT_EQ(early.stacksRun, 0u) << "the stale token woke the second wait";
        EXPECT_TRUE(rig.mVm.rt().completeLatent(second));
        EXPECT_EQ(rig.mVm.tick(2.0).stacksRun, 1u);
    }

    TEST(PapyrusSchedReview5, completing_tens_of_thousands_of_waits_is_not_quadratic)
    {
        Rig rig;
        const InstanceId a = rig.make();
        constexpr int kCount = 50000;
        for (int i = 0; i < kCount; ++i)
        {
            rig.send(a, "OnPark", {});
        }
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mHost.mTokens.size(), static_cast<std::size_t>(kCount));
        ASSERT_EQ(rig.mVm.rt().suspendedStacks(), static_cast<std::uint32_t>(kCount));
        const auto begin = std::chrono::steady_clock::now();
        for (int i = kCount - 1; i >= 0; --i)
        {
            ASSERT_TRUE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[static_cast<std::size_t>(i)])) << i;
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        // A scan and a rebuild of the heap per completion take many seconds here; a token index and one sift take
        // milliseconds. The bound is loose on purpose (a slow or shared machine): this is a complexity check.
        EXPECT_LT(seconds, 3.0);
        const TickResult tick = rig.mVm.tick(0.0);
        EXPECT_EQ(tick.stacksFinished, static_cast<std::uint32_t>(kCount));
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), kCount);
    }

    TEST(PapyrusSchedReview5, a_host_wait_has_no_expiry_by_default)
    {
        EXPECT_TRUE(std::isinf(ScriptPapyrus::RuntimeOptions{}.maxHostWaitSeconds));
        Rig rig;
        const InstanceId a = rig.make();
        rig.send(a, "OnPark", {});
        rig.mVm.tick(0.0);
        rig.mVm.tick(1.0e12);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u);
        EXPECT_EQ(rig.mVm.rt().stats().hostWaitsExpired, 0u);
    }

    TEST(PapyrusSchedReview5, a_host_wait_that_is_never_completed_expires_when_the_option_says_so)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxHostWaitSeconds = 10.0;
        options.maxSuspendedStacks = 3;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        rig.send(a, "OnPark", {});
        rig.send(a, "OnPark", {});
        rig.send(a, "OnPark", {});
        rig.mVm.tick(0.0);
        ASSERT_EQ(rig.mVm.rt().suspendedStacks(), 3u);
        ASSERT_EQ(rig.mHost.mTokens.size(), 3u);
        EXPECT_TRUE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[0])) << "completed in time: not an expiry";
        rig.mVm.tick(9.9);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 2u);
        const TickResult tick = rig.mVm.tick(10.0);
        EXPECT_EQ(tick.stacksFinished, 2u) << "the two forgotten waits time out";
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 3);
        EXPECT_EQ(rig.mVm.rt().stats().hostWaitsExpired, 2u);
        EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 0u) << "the room is free again";
        EXPECT_FALSE(rig.mVm.rt().completeLatent(rig.mHost.mTokens[1])) << "a late completion is refused";
    }

    TEST(PapyrusSchedReview5, an_expiry_is_counted_from_the_time_the_stack_parked)
    {
        ScriptPapyrus::RuntimeOptions options = latentOptions();
        options.maxHostWaitSeconds = 10.0;
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const InstanceId a = rig.make();
        rig.mVm.tick(100.0);
        rig.send(a, "OnPark", {});
        rig.mVm.tick(100.0);
        rig.mVm.tick(109.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 0);
        rig.mVm.tick(110.0);
        EXPECT_EQ(rig.mVm.varInt(a, "counter"), 1);
    }

    TEST(PapyrusSchedReview5, a_max_wait_that_is_not_a_usable_number_is_replaced_by_the_default)
    {
        for (const double bad :
            { std::numeric_limits<double>::quiet_NaN(), -5.0, 0.0, std::numeric_limits<double>::infinity() })
        {
            ScriptPapyrus::RuntimeOptions options = latentOptions();
            options.maxWaitSeconds = bad;
            Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
            const InstanceId a = rig.make();
            rig.send(a, "OnGo", { F(3.0f), I(10) });
            rig.mVm.tick(0.0);
            rig.mVm.tick(2.9);
            EXPECT_EQ(rig.mHost.mMarks.size(), 1u) << "max wait " << bad << ": woke early";
            rig.mVm.tick(3.0);
            EXPECT_EQ(rig.mHost.mMarks.size(), 2u) << "max wait " << bad << ": did not wake at its time";
            // A very long wait is cut to the default longest wait, not left as it was.
            rig.mHost.mNextWait = 1.0e9;
            rig.send(a, "OnGo", { F(1.0f), I(20) });
            rig.mVm.tick(3.0);
            rig.mVm.tick(3.0 + ScriptPapyrus::RuntimeOptions{}.maxWaitSeconds);
            EXPECT_EQ(rig.mHost.mMarks.size(), 4u) << "max wait " << bad << ": a long wait was not cut";
            EXPECT_TRUE(rig.mVm.mWorld.logged("maxWaitSeconds")) << "one warning names the option";
        }
    }

    TEST(PapyrusSchedReview5, a_host_wait_limit_that_is_not_a_usable_number_is_replaced_by_the_default)
    {
        for (const double bad : { std::numeric_limits<double>::quiet_NaN(), -1.0, 0.0 })
        {
            ScriptPapyrus::RuntimeOptions options = latentOptions();
            options.maxHostWaitSeconds = bad;
            Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
            const InstanceId a = rig.make();
            rig.send(a, "OnPark", {});
            rig.mVm.tick(0.0);
            rig.mVm.tick(1.0e6);
            EXPECT_EQ(rig.mVm.rt().suspendedStacks(), 1u) << "limit " << bad << ": expired at once";
            EXPECT_TRUE(rig.mVm.mWorld.logged("maxHostWaitSeconds"));
        }
    }

    TEST(PapyrusSchedReview5, with_latent_natives_off_the_time_given_to_tick_is_not_looked_at)
    {
        ScriptPapyrus::RuntimeOptions options; // runLatentNatives false
        Rig rig(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        const std::size_t logsBefore = rig.mVm.mWorld.mLogs.size();
        rig.mVm.tick(5.0);
        rig.mVm.tick(1.0);
        rig.mVm.tick(std::numeric_limits<double>::quiet_NaN());
        EXPECT_EQ(rig.mVm.rt().stats().timeMovedBackwards, 0u);
        EXPECT_EQ(rig.mVm.mWorld.mLogs.size(), logsBefore) << "nothing is logged for a time nobody uses";
    }

    TEST(PapyrusSchedReview5, call_parks_relative_to_the_time_of_the_last_tick)
    {
        // Documented limit (components/script_papyrus/CLAUDE.md): call() has no time of its own, so a wait started by
        // call() is due at the last tick's time plus the wait.
        Rig rig;
        const InstanceId a = rig.make();
        rig.mVm.tick(100.0);
        const CallResult result = rig.mVm.call(a, "Loop");
        ASSERT_TRUE(result.suspended);
        EXPECT_EQ(rig.mVm.tick(100.5).stacksRun, 0u);
        EXPECT_EQ(rig.mVm.tick(101.0).stacksFinished, 1u);
    }

    TEST(PapyrusSchedReview5, cancelling_with_many_parked_stacks_keeps_the_others_in_order)
    {
        Rig rig;
        const InstanceId a = rig.make();
        const InstanceId b = rig.make();
        for (int i = 0; i < 200; ++i)
        {
            rig.send((i % 2) ? a : b, "OnGo", { F(1.0f + static_cast<float>(i % 7)), I(1000 * (i + 1)) });
        }
        rig.mVm.tick(0.0);
        rig.mHost.mMarks.clear();
        EXPECT_EQ(rig.mVm.rt().cancelWaiting(a), 100u);
        rig.mVm.tick(100.0);
        ASSERT_EQ(rig.mHost.mMarks.size(), 100u);
        // Every survivor belongs to b (even i), and they wake by wait time then creation order.
        std::vector<std::pair<int, int>> expected;
        for (int i = 0; i < 200; i += 2)
        {
            expected.push_back({ 1 + i % 7, i });
        }
        std::stable_sort(
            expected.begin(), expected.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
        for (std::size_t k = 0; k < expected.size(); ++k)
        {
            EXPECT_EQ(rig.mHost.mMarks[k], 1000 * (expected[k].second + 1) + 1) << k;
        }
    }
}
