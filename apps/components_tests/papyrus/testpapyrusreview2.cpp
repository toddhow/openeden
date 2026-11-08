// Task 0093, fixes of the second fresh review (2026-11-08, findings 1 to 6, 8 and 9): inheritance cycles, hostile type
// strings, whole-runtime bounds on the permanent tables, the queue of waiting stacks, the cost of a collection near the
// heap cap, the reentrancy rules of natives and world callbacks, a script that failed to prepare, and the lifetime of
// array handles that cross the contract. Written before the fixes; none of the existing tests is touched.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include <components/script_papyrus/papyrusruntime.hpp>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using ScriptPapyrus::ErrorKind;

    // A world whose three callbacks are hooks of the test.
    class HookWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId form, std::string& className) const override
        {
            if (mFormClass)
            {
                return mFormClass(form, className);
            }
            return false;
        }
        void report(std::string_view) override {}
        void log(Severity, std::string_view message) override
        {
            mLogs.emplace_back(message);
            if (mLog)
            {
                mLog(message);
            }
        }
        bool logged(std::string_view part) const
        {
            for (const std::string& line : mLogs)
            {
                if (line.find(part) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        std::function<bool(FormId, std::string&)> mFormClass;
        std::function<void(std::string_view)> mLog;
        std::vector<std::string> mLogs;
    };

    // One runtime over a HookWorld.
    class Fx
    {
    public:
        explicit Fx(const ScriptPapyrus::RuntimeOptions& options = {},
            UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant, Target target = Target::SkyrimSE)
            : mTarget(target)
            , mRuntime(ScriptPapyrus::createRuntime(mWorld, policy, options))
        {
        }

        LoadResult load(const ScriptBuilder& script)
        {
            return mRuntime->loadScript(script.build(optionsFor(mTarget)).mBytes, script.name());
        }
        bool add(const ScriptBuilder& script)
        {
            const LoadResult result = load(script);
            EXPECT_TRUE(result.ok) << script.name() << ": "
                                   << (result.reports.empty() ? "no report" : result.reports[0].format());
            return result.ok;
        }
        InstanceId attach(std::string_view name)
        {
            AttachKey key;
            key.kind = AttachKind::Form;
            key.formId = 0x20000u + mNextForm++;
            return mRuntime->attach(key, name);
        }
        ScriptPapyrus::PapyrusRuntime& rt() { return *mRuntime; }
        CallResult call(InstanceId id, std::string_view function, std::vector<PapyrusValue> arguments = {})
        {
            return mRuntime->call(id, function, arguments);
        }
        std::int32_t varInt(InstanceId id, std::string_view name)
        {
            PapyrusValue value;
            EXPECT_TRUE(mRuntime->getVariable(id, name, value)) << name;
            return value.integer;
        }

        HookWorld mWorld;

    private:
        Target mTarget;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        std::uint32_t mNextForm = 0;
    };

    bool anyReportHas(const LoadResult& result, std::string_view part)
    {
        for (const ErrorReport& report : result.reports)
        {
            if (report.format().find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    ScriptBuilder withVar(const std::string& name, const std::string& parent = {})
    {
        ScriptBuilder script(name);
        if (!parent.empty())
        {
            script.parent(parent);
        }
        script.var("v" + name, "Int", Int(3));
        script.function("Ping", "Int").ret(Int(7));
        return script;
    }

    // ---- finding 1: inheritance cycles --------------------------------------------------------------------------

    TEST(PapyrusReview2, a_script_that_extends_itself_is_refused_at_load_with_a_located_error)
    {
        Fx fx;
        const LoadResult result = fx.load(withVar("Selfy", "Selfy"));
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(anyReportHas(result, "cycle"))
            << (result.reports.empty() ? "no report" : result.reports[0].format());
        EXPECT_TRUE(anyReportHas(result, "Selfy"));
    }

    TEST(PapyrusReview2, two_scripts_that_extend_each_other_are_refused_whichever_is_loaded_first)
    {
        for (const bool aFirst : { true, false })
        {
            Fx fx;
            const ScriptBuilder a = withVar("CycA", "CycB");
            const ScriptBuilder b = withVar("CycB", "CycA");
            const LoadResult first = fx.load(aFirst ? a : b);
            EXPECT_TRUE(first.ok) << "the first of the two has a parent that is not loaded yet";
            const LoadResult second = fx.load(aFirst ? b : a);
            EXPECT_FALSE(second.ok) << "the second closes a loop";
            EXPECT_TRUE(anyReportHas(second, "cycle"));
            // The first still runs, without what it cannot inherit, and its variables are sized by its own chain.
            const InstanceId id = fx.attach(aFirst ? "CycA" : "CycB");
            ASSERT_NE(id, 0u);
            EXPECT_EQ(fx.call(id, "Ping").returned.integer, 7);
        }
    }

    TEST(PapyrusReview2, a_longer_loop_is_refused_at_the_script_that_closes_it)
    {
        Fx fx;
        ASSERT_TRUE(fx.load(withVar("LoopA", "LoopB")).ok);
        ASSERT_TRUE(fx.load(withVar("LoopB", "LoopC")).ok);
        const LoadResult closing = fx.load(withVar("LoopC", "LoopA"));
        EXPECT_FALSE(closing.ok);
        EXPECT_TRUE(anyReportHas(closing, "cycle"));
    }

    TEST(PapyrusReview2, a_long_chain_without_a_loop_still_loads_and_attaches_in_any_order)
    {
        Fx fx;
        ASSERT_TRUE(fx.add(withVar("Chain3", "Chain2")));
        ASSERT_TRUE(fx.add(withVar("Chain1")));
        ASSERT_TRUE(fx.add(withVar("Chain2", "Chain1")));
        const InstanceId id = fx.attach("Chain3");
        ASSERT_NE(id, 0u);
        EXPECT_EQ(fx.varInt(id, "vChain1"), 3);
        EXPECT_EQ(fx.varInt(id, "vChain3"), 3);
    }

    // ---- finding 2: type strings --------------------------------------------------------------------------------

    TEST(PapyrusReview2, an_array_of_arrays_is_refused_by_the_reader_in_every_place_a_type_is_written)
    {
        const char* places[] = { "variable", "parameter", "local", "return", "property" };
        for (const char* place : places)
        {
            ScriptBuilder script("Nested");
            const std::string place_ = place;
            if (place_ == "variable")
            {
                script.var("v", "Int[][]");
            }
            else if (place_ == "property")
            {
                script.autoProperty("P", "Int[][]");
            }
            else
            {
                auto& f = script.function("F", place_ == "return" ? "Int[][]" : "None");
                if (place_ == "parameter")
                {
                    f.param("p", "Int[][]");
                }
                if (place_ == "local")
                {
                    f.local("l", "Int[][]");
                }
            }
            const Built built = script.build(optionsFor(Target::SkyrimSE));
            const ScriptPapyrus::Pex::ParseResult parsed = PapyrusTest::parseBytes(built.mBytes);
            EXPECT_FALSE(parsed.mOk) << place;
            EXPECT_TRUE(PapyrusTest::contains(PapyrusTest::messages(parsed), "[]"))
                << place << ": " << PapyrusTest::messages(parsed);
        }
    }

    TEST(PapyrusReview2, a_type_string_of_thirty_thousand_brackets_is_refused_at_load)
    {
        std::string type = "Int";
        for (int i = 0; i < 30000; ++i)
        {
            type += "[]";
        }
        ScriptBuilder script("DeepType");
        script.var("v", type);
        Fx fx;
        const LoadResult result = fx.load(script);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(anyReportHas(result, "[]"));
    }

    TEST(PapyrusReview2, one_array_level_and_a_struct_array_still_load)
    {
        for (const Target target : kBothTargets)
        {
            Fx fx({}, UnknownFunctionPolicy::Tolerant, target);
            ScriptBuilder script("Fine");
            script.var("ints", "Int[]").var("objects", "Actor[]");
            script.function("F", "Float[]").param("p", "String[]").local("l", "Bool[]");
            EXPECT_TRUE(fx.add(script));
        }
    }

    // ---- finding 3: the permanent tables are bounded ------------------------------------------------------------

    ScriptBuilder stateSpammer()
    {
        ScriptBuilder s("Spammer");
        s.function("Spam").local("i", "Int").local("s", "String");
        s.iadd(Id("i"), Id("i"), Int(1));
        s.op(Op::StrCat, { Id("s"), Str("state"), Id("i") });
        s.assign(Id("::State"), Id("s"));
        s.jmp(-3);
        s.function("Ping", "Int").ret(Int(7));
        return s;
    }

    TEST(PapyrusReview2, a_script_that_names_a_new_state_in_a_loop_dies_at_the_cap_not_at_the_budget)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxInternedStrings = 3000;
        options.maxNames = 100000;
        Fx fx(options);
        ASSERT_TRUE(fx.add(stateSpammer()));
        const InstanceId id = fx.attach("Spammer");
        ASSERT_NE(id, 0u);
        const CallResult result = fx.call(id, "Spam");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "full")) << result.error.format();
        EXPECT_TRUE(hasText(result.error, "Spammer.Spam@")) << "the error is located: " << result.error.format();
        EXPECT_LT(result.instructionsExecuted, 100000u) << "it stopped long before the instruction budget";
        EXPECT_EQ(fx.call(id, "Ping").returned.integer, 7) << "the runtime runs on";
    }

    TEST(PapyrusReview2, the_table_of_names_has_a_cap_too)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxNames = 2500;
        options.maxInternedStrings = 100000;
        Fx fx(options);
        ASSERT_TRUE(fx.add(stateSpammer()));
        const InstanceId id = fx.attach("Spammer");
        ASSERT_NE(id, 0u);
        const CallResult result = fx.call(id, "Spam");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "full")) << result.error.format();
        EXPECT_EQ(fx.call(id, "Ping").returned.integer, 7);
    }

    TEST(PapyrusReview2, the_bytes_of_the_permanent_strings_have_a_cap_too)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxInternedBytes = 20000;
        Fx fx(options);
        ASSERT_TRUE(fx.add(stateSpammer()));
        const InstanceId id = fx.attach("Spammer");
        ASSERT_NE(id, 0u);
        const CallResult result = fx.call(id, "Spam");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "full")) << result.error.format();
    }

    TEST(PapyrusReview2, event_names_from_the_host_count_against_the_cap_and_the_refusal_says_why)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxNames = 600;
        Fx fx(options);
        ASSERT_TRUE(fx.add(stateSpammer()));
        const InstanceId id = fx.attach("Spammer");
        ASSERT_NE(id, 0u);
        bool refused = false;
        for (int i = 0; i < 2000 && !refused; ++i)
        {
            const CallResult result = fx.rt().send(id, "OnEvent" + std::to_string(i), {});
            if (!result.ok)
            {
                refused = true;
                EXPECT_TRUE(hasText(result.error, "full")) << result.error.format();
            }
        }
        EXPECT_TRUE(refused);
    }

    // ---- finding 4: the queue of waiting stacks -----------------------------------------------------------------

    TEST(PapyrusReview2, a_queue_that_never_drains_does_not_keep_every_stack_it_ever_ran)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxInstructionsPerTick = 3000;
        Fx fx(options);
        ScriptBuilder s("Spinner");
        s.function("OnSpin").op(Op::Jmp, { Int(0) }).ret(Nil());
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Spinner");
        ASSERT_NE(id, 0u);
        Budget budget;
        budget.maxInstructions = 1000;
        TickResult last;
        for (int round = 0; round < 400; ++round)
        {
            for (int i = 0; i < 4; ++i)
            {
                ASSERT_TRUE(fx.rt().send(id, "OnSpin", {}).ok);
            }
            last = fx.rt().tick(0.0, budget);
        }
        ASSERT_GT(last.stacksWaiting, 100u) << "the queue really did not drain";
        EXPECT_LE(fx.rt().stats().readyQueueSlots, 2u * last.stacksWaiting + 256u)
            << "consumed slots are given back, not kept for the life of the session";
    }

    TEST(PapyrusReview2, a_world_that_throws_inside_a_stack_ends_that_stack_with_an_error_and_the_queue_goes_on)
    {
        Fx fx({}, UnknownFunctionPolicy::Tolerant, Target::Fallout4);
        ScriptBuilder s("Checker");
        s.var("f", "Actor");
        s.var("hits", "Int");
        s.function("OnCheck")
            .local("b", "Bool")
            .op(Op::Is, { Id("b"), Id("f"), Str("Actor") })
            .iadd(Id("hits"), Id("hits"), Int(1))
            .ret(Nil());
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Checker");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        ASSERT_TRUE(fx.rt().setVariable(id, "f", PapyrusValue::makeForm("", 0x14)));
        int calls = 0;
        fx.mWorld.mFormClass = [&calls](FormId, std::string& className) -> bool {
            if (++calls == 1)
            {
                throw std::bad_alloc();
            }
            className = "Actor";
            return true;
        };
        ASSERT_TRUE(fx.rt().send(id, "OnCheck", {}).ok);
        ASSERT_TRUE(fx.rt().send(id, "OnCheck", {}).ok);
        const TickResult first = fx.rt().tick(0.0);
        EXPECT_EQ(first.errors.size(), 1u) << "the stack that met the exception ended with an error";
        EXPECT_EQ(first.stacksWaiting, 0u);
        EXPECT_EQ(fx.varInt(id, "hits"), 1) << "the second event ran";
        // The stack that died went back to the pool: a long run of such events does not grow anything.
        for (int i = 0; i < 50; ++i)
        {
            fx.rt().send(id, "OnCheck", {});
            fx.rt().tick(0.0);
        }
        EXPECT_EQ(fx.varInt(id, "hits"), 51);
    }

    // ---- finding 5: the cost of a collection ---------------------------------------------------------------------

    TEST(PapyrusReview2, a_collection_near_the_cap_costs_the_live_objects_not_the_high_water_mark)
    {
        ScriptPapyrus::RuntimeOptions options;
        // 12 strings of 65,536 bytes, the array that holds them, and room for a few small strings.
        options.maxHeapBytes = 12ull * (65536 + 32) + (12 * 12 + 32) + 32768 + 2000;
        Fx fx(options);
        ScriptBuilder s("Churn");
        s.var("held", "String[]");
        s.function("Spray").local("i", "Int").local("s", "String").local("c", "Bool");
        s.assign(Id("i"), Int(0));
        s.op(Op::CompareLt, { Id("c"), Id("i"), Int(12000) });
        s.jmpf(Id("c"), 4);
        s.op(Op::StrCat, { Id("s"), Str("garbage"), Id("i") });
        s.iadd(Id("i"), Id("i"), Int(1));
        s.jmp(-4);
        s.ret(Nil());
        s.function("Fill").local("base", "String").local("t", "String").local("i", "Int").local("c", "Bool");
        s.assign(Id("base"), Str("abcdefgh"));
        for (int d = 0; d < 12; ++d)
        {
            s.op(Op::StrCat, { Id("base"), Id("base"), Id("base") });
        }
        s.op(Op::ArrayCreate, { Id("held"), Int(12) });
        s.assign(Id("i"), Int(0));
        s.op(Op::CompareLt, { Id("c"), Id("i"), Int(12) });
        s.jmpf(Id("c"), 5);
        s.op(Op::StrCat, { Id("t"), Id("base"), Id("base") });
        s.op(Op::ArraySetElement, { Id("held"), Id("i"), Id("t") });
        s.iadd(Id("i"), Id("i"), Int(1));
        s.jmp(-5);
        s.assign(Id("base"), Str(""));
        s.ret(Nil());
        s.function("Churn").local("i", "Int").local("s", "String").local("c", "Bool");
        s.assign(Id("i"), Int(0));
        s.op(Op::CompareLt, { Id("c"), Id("i"), Int(3000) });
        s.jmpf(Id("c"), 4);
        s.op(Op::StrCat, { Id("s"), Str("churn"), Id("i") });
        s.iadd(Id("i"), Id("i"), Int(1));
        s.jmp(-4);
        s.ret(Nil());
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Churn");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        ASSERT_TRUE(fx.call(id, "Spray").ok);
        const CallResult fill = fx.call(id, "Fill");
        ASSERT_TRUE(fill.ok) << fill.error.format();
        const std::uint64_t collectionsBefore = fx.rt().stats().collections;
        const std::uint64_t visitsBefore = fx.rt().stats().collectionVisits;
        const CallResult churn = fx.call(id, "Churn");
        ASSERT_TRUE(churn.ok) << churn.error.format();
        const std::uint64_t collections = fx.rt().stats().collections - collectionsBefore;
        const std::uint64_t visits = fx.rt().stats().collectionVisits - visitsBefore;
        ASSERT_GT(collections, 0u) << "the churn really ran into the cap";
        // A collection looks at what was allocated since the last one (an amortised cost of about one visit per
        // allocation) and never at the slots of the 12,000 strings that died before: those cost the old collector
        // about 12,000 visits each time.
        EXPECT_LT(visits, 3u * 3000u + 200u) << collections << " collections looked at " << visits << " entries";
    }

    // ---- finding 6: reentrancy, exceptions, a script that failed to prepare ------------------------------------

    ScriptBuilder pokeScript()
    {
        ScriptBuilder g("Hook");
        g.function("Poke").global().native();
        g.function("Note", "Int").global().native().param("n", "Int");
        ScriptBuilder s("Poker");
        s.var("x", "Int");
        s.function("Run", "Int")
            .local("::NoneVar", "None")
            .assign(Id("x"), Int(1))
            .callStatic("Hook", "Poke", Id("::NoneVar"))
            .iadd(Id("x"), Id("x"), Int(1))
            .ret(Id("x"));
        s.function("OnRun").local("::NoneVar", "None").callStatic("Hook", "Poke", Id("::NoneVar")).ret(Nil());
        s.function("OnA").local("r", "Int").callStatic("Hook", "Note", Id("r"), { Int(1) }).ret(Nil());
        s.function("OnB").local("r", "Int").callStatic("Hook", "Note", Id("r"), { Int(2) }).ret(Nil());
        return s;
    }

    NativeDescriptor global(std::string className, std::string functionName)
    {
        NativeDescriptor d;
        d.className = std::move(className);
        d.functionName = std::move(functionName);
        d.global = true;
        return d;
    }

    TEST(PapyrusReview2, a_native_cannot_call_tick_load_or_register_and_the_run_continues)
    {
        ScriptBuilder hook("Hook");
        hook.function("Poke").global().native();
        Fx fx2;
        ASSERT_TRUE(fx2.add(hook));
        ASSERT_TRUE(fx2.add(pokeScript()));
        const InstanceId id = fx2.attach("Poker");
        ASSERT_NE(id, 0u);
        fx2.rt().tick(0.0);
        bool callRefused = false;
        bool tickRefused = false;
        bool loadRefused = false;
        bool registerRefused = false;
        ErrorReport registerError;
        ASSERT_TRUE(fx2.rt().registerNative(
            global("Hook", "Poke"),
            [&](const NativeCall&) {
                callRefused = !fx2.call(id, "Ping").ok;
                const TickResult tick = fx2.rt().tick(0.0);
                tickRefused = !tick.errors.empty();
                const ScriptBuilder other = withVar("LoadedInside");
                loadRefused = !fx2.load(other).ok;
                registerRefused = !fx2.rt().registerNative(
                    global("Hook", "Poke"), [](const NativeCall&) { return NativeResult{}; }, registerError);
                return NativeResult{};
            },
            registerError));
        const CallResult result = fx2.call(id, "Run");
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_EQ(result.returned.integer, 2) << "the run continued after the native";
        EXPECT_TRUE(callRefused);
        EXPECT_TRUE(tickRefused);
        EXPECT_TRUE(loadRefused);
        EXPECT_TRUE(registerRefused);
    }

    TEST(PapyrusReview2, a_native_may_send_twice_and_both_events_run_in_order_in_the_same_tick)
    {
        Fx fx;
        ScriptBuilder hook("Hook");
        hook.function("Poke").global().native();
        hook.function("Note", "Int").global().native().param("n", "Int");
        ASSERT_TRUE(fx.add(hook));
        ASSERT_TRUE(fx.add(pokeScript()));
        const InstanceId id = fx.attach("Poker");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        std::vector<int> order;
        ErrorReport error;
        ASSERT_TRUE(fx.rt().registerNative(
            global("Hook", "Poke"),
            [&](const NativeCall&) {
                order.push_back(0);
                EXPECT_TRUE(fx.rt().send(id, "OnA", {}).ok);
                EXPECT_TRUE(fx.rt().send(id, "OnB", {}).ok);
                return NativeResult{};
            },
            error));
        ASSERT_TRUE(fx.rt().registerNative(
            global("Hook", "Note"),
            [&](const NativeCall& call) {
                order.push_back(call.arguments[0].integer);
                return NativeResult{ PapyrusValue::makeInt(0) };
            },
            error));
        ASSERT_TRUE(fx.rt().send(id, "OnRun", {}).ok);
        const TickResult tick = fx.rt().tick(0.0);
        EXPECT_EQ(tick.stacksRun, 3u);
        EXPECT_EQ(tick.stacksWaiting, 0u);
        EXPECT_EQ(order, (std::vector<int>{ 0, 1, 2 }));
    }

    TEST(PapyrusReview2, a_native_may_attach_many_instances_and_the_running_script_keeps_its_variables)
    {
        Fx fx;
        ScriptBuilder hook("Hook");
        hook.function("Poke").global().native();
        ASSERT_TRUE(fx.add(hook));
        ASSERT_TRUE(fx.add(pokeScript()));
        ASSERT_TRUE(fx.add(withVar("Spare")));
        const InstanceId id = fx.attach("Poker");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        ErrorReport error;
        std::uint32_t attached = 0;
        ASSERT_TRUE(fx.rt().registerNative(
            global("Hook", "Poke"),
            [&](const NativeCall&) {
                for (std::uint32_t i = 0; i < 3000; ++i)
                {
                    AttachKey key;
                    key.kind = AttachKind::Form;
                    key.formId = 0x90000u + i;
                    if (fx.rt().attach(key, "Spare") != 0)
                    {
                        ++attached;
                    }
                }
                return NativeResult{};
            },
            error));
        const CallResult result = fx.call(id, "Run");
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_EQ(result.returned.integer, 2);
        EXPECT_EQ(attached, 3000u);
    }

    TEST(PapyrusReview2, a_log_line_cannot_start_a_call_or_a_tick_from_inside_the_runtime)
    {
        Fx fx;
        ScriptBuilder hook("Hook");
        hook.function("Missing").global().native();
        ASSERT_TRUE(fx.add(hook));
        ScriptBuilder s("Logger");
        s.function("Run", "Int").local("::NoneVar", "None").callStatic("Hook", "Missing", Id("::NoneVar")).ret(Int(5));
        s.function("Ping", "Int").ret(Int(7));
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Logger");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        bool called = false;
        bool callOk = true;
        bool tickRefused = false;
        fx.mWorld.mLog = [&](std::string_view message) {
            if (message.find("not registered") != std::string_view::npos && !called)
            {
                called = true;
                callOk = fx.call(id, "Ping").ok;
                tickRefused = !fx.rt().tick(0.0).errors.empty();
            }
        };
        const CallResult result = fx.call(id, "Run");
        ASSERT_TRUE(result.ok) << result.error.format();
        ASSERT_TRUE(called) << "the unregistered native logged";
        EXPECT_FALSE(callOk) << "a call from inside a world callback is refused";
        EXPECT_TRUE(tickRefused);
    }

    TEST(PapyrusReview2, a_form_class_lookup_cannot_start_a_call_from_inside_the_runtime)
    {
        Fx fx({}, UnknownFunctionPolicy::Tolerant, Target::Fallout4);
        ScriptBuilder s("Looker");
        s.var("f", "Actor");
        s.function("Run", "Int").local("b", "Bool").op(Op::Is, { Id("b"), Id("f"), Str("Actor") }).ret(Int(5));
        s.function("Ping", "Int").ret(Int(7));
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Looker");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        ASSERT_TRUE(fx.rt().setVariable(id, "f", PapyrusValue::makeForm("", 0x14)));
        bool callOk = true;
        fx.mWorld.mFormClass = [&](FormId, std::string& className) -> bool {
            callOk = fx.call(id, "Ping").ok;
            className = "Actor";
            return true;
        };
        const CallResult result = fx.call(id, "Run");
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_FALSE(callOk);
    }

    TEST(PapyrusReview2, an_error_left_pending_by_a_throwing_log_does_not_fail_the_next_unrelated_call)
    {
        ScriptPapyrus::RuntimeOptions options;
        Fx fx(options);
        ScriptBuilder s("Takes");
        s.function("OnBig").param("s", "String").ret(Nil());
        s.function("Ping", "Int").ret(Int(7));
        ASSERT_TRUE(fx.add(s));
        const InstanceId id = fx.attach("Takes");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        // The first error message the runtime logs makes the world throw (a full disk, an out of memory).
        bool thrown = false;
        fx.mWorld.mLog = [&thrown](std::string_view message) {
            if (!thrown && message.find("[limit exceeded]") != std::string_view::npos)
            {
                thrown = true;
                throw std::runtime_error("log failed");
            }
        };
        const std::string huge(70000, 'x');
        const CallResult refused
            = fx.rt().send(id, "OnBig", std::vector<PapyrusValue>{ PapyrusValue::makeString(huge) });
        EXPECT_FALSE(refused.ok);
        const CallResult next = fx.call(id, "Ping");
        EXPECT_TRUE(next.ok) << next.error.format();
        EXPECT_EQ(next.returned.integer, 7);
    }

    TEST(PapyrusReview2, a_script_whose_preparation_threw_is_never_reported_as_ready)
    {
        Fx fx;
        ASSERT_TRUE(fx.add(withVar("Orphan", "MissingParent")));
        fx.mWorld.mLog = [](std::string_view message) {
            if (message.find("is not loaded") != std::string_view::npos)
            {
                throw std::bad_alloc();
            }
        };
        EXPECT_EQ(fx.attach("Orphan"), 0u) << "the first attach met the exception";
        fx.mWorld.mLog = nullptr;
        EXPECT_EQ(fx.attach("Orphan"), 0u) << "a half-built script is not treated as ready";
    }

    // ---- finding 8: a parent that is loaded after its child was prepared ----------------------------------------

    TEST(PapyrusReview2, a_parent_loaded_after_its_child_was_prepared_is_reported_not_silently_ignored)
    {
        Fx fx;
        ASSERT_TRUE(fx.add(withVar("LateChild", "LateParent")));
        const InstanceId id = fx.attach("LateChild");
        ASSERT_NE(id, 0u);
        EXPECT_FALSE(fx.mWorld.logged("loaded after"));
        ASSERT_TRUE(fx.add(withVar("LateParent")));
        EXPECT_TRUE(fx.mWorld.logged("loaded after"))
            << "the child keeps running without what it inherits, and says so";
        EXPECT_EQ(fx.call(id, "Ping").returned.integer, 7);
        // A child attached from now on, before it was ever prepared, does get the parent.
        ASSERT_TRUE(fx.add(withVar("FreshChild", "LateParent")));
        const InstanceId fresh = fx.attach("FreshChild");
        ASSERT_NE(fresh, 0u);
        EXPECT_EQ(fx.varInt(fresh, "vLateParent"), 3);
    }

    // ---- finding 9: array handles that cross the contract ---------------------------------------------------------

    ScriptBuilder handleScript()
    {
        ScriptBuilder s("Handles");
        s.var("ints", "Int[]");
        s.var("floats", "Float[]");
        s.function("Fill").op(Op::ArrayCreate, { Id("ints"), Int(3) }).ret(Nil());
        s.function("MakeArr", "Int[]").local("a", "Int[]").op(Op::ArrayCreate, { Id("a"), Int(10) }).ret(Id("a"));
        s.function("Garbage").local("s", "String").op(Op::StrCat, { Id("s"), Str("g"), Str("g") }).ret(Nil());
        s.function("Eat").param("s", "String").ret(Nil());
        s.function("TakeBoth", "Int")
            .param("s", "String")
            .param("a", "Int[]")
            .local("n", "Int")
            .op(Op::ArrayLength, { Id("n"), Id("a") })
            .ret(Id("n"));
        return s;
    }

    TEST(PapyrusReview2, an_array_handle_of_another_element_kind_is_not_stored_in_a_variable)
    {
        Fx fx;
        ASSERT_TRUE(fx.add(handleScript()));
        const InstanceId id = fx.attach("Handles");
        ASSERT_NE(id, 0u);
        ASSERT_TRUE(fx.call(id, "Fill").ok);
        PapyrusValue ints;
        ASSERT_TRUE(fx.rt().getVariable(id, "ints", ints));
        ASSERT_EQ(ints.kind, PapyrusKind::Array);
        EXPECT_FALSE(fx.rt().setVariable(id, "floats", ints)) << "an Int[] is not a Float[]";
        EXPECT_TRUE(fx.rt().setVariable(id, "ints", ints)) << "its own kind is fine";
    }

    TEST(PapyrusReview2, an_array_handle_among_the_arguments_survives_a_collection_that_a_string_argument_starts)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxHeapBytes = 1000;
        Fx fx(options);
        ASSERT_TRUE(fx.add(handleScript()));
        const InstanceId id = fx.attach("Handles");
        ASSERT_NE(id, 0u);
        fx.rt().tick(0.0);
        // 10 ints: 152 bytes held by nobody after this call; then 700 bytes of garbage strings.
        const CallResult made = fx.call(id, "MakeArr");
        ASSERT_TRUE(made.ok) << made.error.format();
        ASSERT_EQ(made.returned.kind, PapyrusKind::Array);
        const std::string big(300, 'q');
        const std::string garbage(640, 'z');
        // The garbage is a string argument of an earlier call that nobody keeps.
        ASSERT_TRUE(fx.call(id, "Eat", { PapyrusValue::makeString(garbage) }).ok);
        const CallResult both = fx.call(id, "TakeBoth", { PapyrusValue::makeString(big), made.returned });
        ASSERT_TRUE(both.ok) << both.error.format();
        EXPECT_EQ(both.returned.integer, 10) << "the array was still there when the string argument made room";
    }
}
