// Task 0093 stage c: the VM core runs generated scripts (no game asset): instances, variables, properties, states,
// inheritance, calls, events, arithmetic, jumps, casts and strings. Written from the task's spec before the VM.

#include <gtest/gtest.h>

#include <limits>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;

    // ---- instances and OnInit ------------------------------------------------------------------------------------

    TEST(PapyrusVmCore, attach_queues_oninit_and_tick_runs_it)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder s("Quest1");
            s.var("counter", "Int", Int(1));
            s.function("OnInit").assign(Id("counter"), Int(42)).ret(Nil());
            ASSERT_TRUE(vm.add(s));
            const InstanceId id = vm.make("Quest1");
            ASSERT_NE(id, 0u);
            EXPECT_EQ(vm.varInt(id, "counter"), 1) << "OnInit is queued, not run, by attach";
            const TickResult tick = vm.tick();
            EXPECT_EQ(tick.stacksRun, 1u);
            EXPECT_EQ(tick.stacksFinished, 1u);
            EXPECT_TRUE(tick.errors.empty());
            EXPECT_EQ(vm.varInt(id, "counter"), 42);
            EXPECT_EQ(vm.tick().stacksRun, 0u) << "nothing is queued any more";
        }
    }

    TEST(PapyrusVmCore, variables_start_at_the_values_the_file_gives_and_defaults_by_type)
    {
        Vm vm;
        ScriptBuilder s("Defaults");
        s.var("i", "Int", Int(5))
            .var("f", "Float", Flt(1.5f))
            .var("b", "Bool", Bool(true))
            .var("s", "String", Str("hi"))
            .var("zi", "Int")
            .var("zs", "String")
            .var("o", "Actor");
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Defaults");
        PapyrusValue value;
        ASSERT_TRUE(vm.rt().getVariable(id, "i", value));
        EXPECT_EQ(value.kind, PapyrusKind::Int);
        EXPECT_EQ(value.integer, 5);
        ASSERT_TRUE(vm.rt().getVariable(id, "f", value));
        EXPECT_EQ(value.kind, PapyrusKind::Float);
        EXPECT_FLOAT_EQ(value.number, 1.5f);
        ASSERT_TRUE(vm.rt().getVariable(id, "b", value));
        EXPECT_EQ(value.kind, PapyrusKind::Bool);
        EXPECT_EQ(value.integer, 1);
        ASSERT_TRUE(vm.rt().getVariable(id, "S", value)) << "names are compared without regard to case";
        EXPECT_EQ(value.kind, PapyrusKind::String);
        EXPECT_EQ(value.text, "hi");
        ASSERT_TRUE(vm.rt().getVariable(id, "zi", value));
        EXPECT_EQ(value.integer, 0);
        ASSERT_TRUE(vm.rt().getVariable(id, "zs", value));
        EXPECT_EQ(value.kind, PapyrusKind::String);
        EXPECT_EQ(value.text, "");
        ASSERT_TRUE(vm.rt().getVariable(id, "o", value));
        EXPECT_EQ(value.kind, PapyrusKind::None);
        EXPECT_FALSE(vm.rt().getVariable(id, "nothing", value));
        EXPECT_FALSE(vm.rt().getVariable(id + 100, "i", value));
    }

    TEST(PapyrusVmCore, attach_refuses_an_unknown_script_and_a_key_that_is_attached_already)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(ScriptBuilder("One")));
        AttachKey key;
        key.formId = 0x77;
        EXPECT_EQ(vm.rt().attach(key, "Missing"), 0u);
        const InstanceId first = vm.rt().attach(key, "One");
        EXPECT_NE(first, 0u);
        EXPECT_EQ(vm.rt().attach(key, "One"), 0u);
        AttachKey alias = key;
        alias.kind = AttachKind::Alias;
        alias.aliasIndex = 3;
        const InstanceId second = vm.rt().attach(alias, "ONE");
        EXPECT_NE(second, 0u);
        EXPECT_NE(second, first);
    }

    TEST(PapyrusVmCore, call_of_an_unknown_instance_or_function_is_a_report_not_a_crash)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(ScriptBuilder("One")));
        const InstanceId id = vm.make("One");
        CallResult result = vm.call(id + 50, "Run");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[no such instance]")) << result.error.format();
        result = vm.call(id, "NoSuchFunction");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[no such function]")) << result.error.format();
    }

    // ---- arithmetic, comparisons, jumps ----------------------------------------------------------------------------

    TEST(PapyrusVmCore, integer_arithmetic)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder s("Calc");
            s.function("Run", "Int")
                .local("a", "Int")
                .local("b", "Int")
                .local("r", "Int")
                .assign(Id("a"), Int(7))
                .assign(Id("b"), Int(3))
                .op(Op::IMul, { Id("r"), Id("a"), Id("b") }) // 21
                .op(Op::ISub, { Id("r"), Id("r"), Int(1) }) // 20
                .op(Op::IDiv, { Id("r"), Id("r"), Id("b") }) // 6
                .op(Op::IMod, { Id("r"), Id("r"), Int(4) }) // 2
                .op(Op::IAdd, { Id("r"), Id("r"), Int(100) }) // 102
                .op(Op::INeg, { Id("r"), Id("r") }) // -102
                .ret(Id("r"));
            ASSERT_TRUE(vm.add(s));
            EXPECT_EQ(vm.runInt(vm.make("Calc"), "Run"), -102);
        }
    }

    TEST(PapyrusVmCore, integer_overflow_wraps_and_the_extremes_do_not_trap)
    {
        Vm vm;
        ScriptBuilder s("Wrap");
        s.function("Add", "Int")
            .local("r", "Int")
            .op(Op::IAdd, { Id("r"), Int(std::numeric_limits<std::int32_t>::max()), Int(1) })
            .ret(Id("r"));
        s.function("Div", "Int")
            .local("r", "Int")
            .op(Op::IDiv, { Id("r"), Int(std::numeric_limits<std::int32_t>::min()), Int(-1) })
            .ret(Id("r"));
        s.function("Mod", "Int")
            .local("r", "Int")
            .op(Op::IMod, { Id("r"), Int(std::numeric_limits<std::int32_t>::min()), Int(-1) })
            .ret(Id("r"));
        s.function("Neg", "Int")
            .local("r", "Int")
            .op(Op::INeg, { Id("r"), Int(std::numeric_limits<std::int32_t>::min()) })
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Wrap");
        EXPECT_EQ(vm.runInt(id, "Add"), std::numeric_limits<std::int32_t>::min());
        EXPECT_EQ(vm.runInt(id, "Div"), std::numeric_limits<std::int32_t>::min());
        EXPECT_EQ(vm.runInt(id, "Mod"), 0);
        EXPECT_EQ(vm.runInt(id, "Neg"), std::numeric_limits<std::int32_t>::min());
    }

    TEST(PapyrusVmCore, float_arithmetic)
    {
        Vm vm;
        ScriptBuilder s("FCalc");
        s.function("Run", "Float")
            .local("r", "Float")
            .op(Op::FAdd, { Id("r"), Flt(1.5f), Flt(2.5f) }) // 4
            .op(Op::FMul, { Id("r"), Id("r"), Flt(3.0f) }) // 12
            .op(Op::FSub, { Id("r"), Id("r"), Flt(2.0f) }) // 10
            .op(Op::FDiv, { Id("r"), Id("r"), Flt(4.0f) }) // 2.5
            .op(Op::FNeg, { Id("r"), Id("r") }) // -2.5
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const PapyrusValue value = vm.run(vm.make("FCalc"), "Run");
        EXPECT_EQ(value.kind, PapyrusKind::Float);
        EXPECT_FLOAT_EQ(value.number, -2.5f);
    }

    TEST(PapyrusVmCore, division_by_zero_ends_the_stack_with_a_located_error)
    {
        Vm vm;
        ScriptBuilder s("Zero");
        s.function("IntDiv", "Int").local("r", "Int").op(Op::IDiv, { Id("r"), Int(5), Int(0) }).ret(Id("r"));
        s.function("IntMod", "Int").local("r", "Int").op(Op::IMod, { Id("r"), Int(5), Int(0) }).ret(Id("r"));
        s.function("FloatDiv", "Float")
            .local("r", "Float")
            .op(Op::FDiv, { Id("r"), Flt(5.0f), Flt(0.0f) })
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Zero");
        for (const char* name : { "IntDiv", "IntMod", "FloatDiv" })
        {
            const CallResult result = vm.call(id, name);
            EXPECT_FALSE(result.ok) << name;
            EXPECT_TRUE(hasText(result.error, "[divide by zero]")) << result.error.format();
            EXPECT_TRUE(hasText(result.error, std::string("Zero.") + name + "@0")) << result.error.format();
        }
        EXPECT_EQ(vm.rt().stats().errors[static_cast<std::size_t>(ScriptPapyrus::ErrorKind::DivideByZero)], 3u);
        // The instance and the runtime are fine afterwards.
        ScriptBuilder ok("Fine");
        ok.function("One", "Int").ret(Int(1));
        ASSERT_TRUE(vm.add(ok));
        EXPECT_EQ(vm.runInt(vm.make("Fine"), "One"), 1);
    }

    TEST(PapyrusVmCore, comparisons_give_bool_for_numbers_strings_and_objects)
    {
        Vm vm;
        ScriptBuilder s("Cmp");
        s.var("other", "Cmp");
        s.function("Ints", "Int")
            .local("c", "Bool")
            .local("r", "Int")
            .assign(Id("r"), Int(0))
            .op(Op::CompareEq, { Id("c"), Int(3), Int(3) })
            .jmpf(Id("c"), 2)
            .iadd(Id("r"), Id("r"), Int(1))
            .op(Op::CompareLt, { Id("c"), Int(2), Int(3) })
            .jmpf(Id("c"), 2)
            .iadd(Id("r"), Id("r"), Int(10))
            .op(Op::CompareLe, { Id("c"), Int(3), Int(3) })
            .jmpf(Id("c"), 2)
            .iadd(Id("r"), Id("r"), Int(100))
            .op(Op::CompareGt, { Id("c"), Int(4), Int(3) })
            .jmpf(Id("c"), 2)
            .iadd(Id("r"), Id("r"), Int(1000))
            .op(Op::CompareGe, { Id("c"), Int(2), Int(3) }) // false
            .jmpf(Id("c"), 2)
            .iadd(Id("r"), Id("r"), Int(10000))
            .ret(Id("r"));
        s.function("Floats", "Bool")
            .local("c", "Bool")
            .op(Op::CompareLt, { Id("c"), Flt(1.5f), Flt(2.5f) })
            .ret(Id("c"));
        s.function("Strings", "Bool")
            .local("c", "Bool")
            .op(Op::CompareEq, { Id("c"), Str("Hello"), Str("hELLO") })
            .ret(Id("c"));
        s.function("StringsDiffer", "Bool")
            .local("c", "Bool")
            .op(Op::CompareEq, { Id("c"), Str("Hello"), Str("World") })
            .ret(Id("c"));
        s.function("SelfIsSelf", "Bool")
            .local("c", "Bool")
            .op(Op::CompareEq, { Id("c"), Id("self"), Id("self") })
            .ret(Id("c"));
        s.function("NoneIsNone", "Bool")
            .local("c", "Bool")
            .op(Op::CompareEq, { Id("c"), Id("other"), Nil() })
            .ret(Id("c"));
        s.function("SelfIsNotNone", "Bool")
            .local("c", "Bool")
            .op(Op::CompareEq, { Id("c"), Id("self"), Nil() })
            .ret(Id("c"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Cmp");
        EXPECT_EQ(vm.runInt(id, "Ints"), 1111);
        EXPECT_FLOAT_EQ(static_cast<float>(vm.run(id, "Floats").integer), 1.0f);
        EXPECT_EQ(vm.run(id, "Strings").integer, 1) << "string equality ignores case";
        EXPECT_EQ(vm.run(id, "StringsDiffer").integer, 0);
        EXPECT_EQ(vm.run(id, "SelfIsSelf").integer, 1);
        EXPECT_EQ(vm.run(id, "NoneIsNone").integer, 1);
        EXPECT_EQ(vm.run(id, "SelfIsNotNone").integer, 0);
        const PapyrusValue value = vm.run(id, "Floats");
        EXPECT_EQ(value.kind, PapyrusKind::Bool);
    }

    TEST(PapyrusVmCore, a_loop_with_a_backward_jump_adds_one_to_ten)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder s("Loop");
            s.function("Sum", "Int")
                .local("i", "Int")
                .local("sum", "Int")
                .local("c", "Bool")
                .assign(Id("i"), Int(0)) // 0
                .assign(Id("sum"), Int(0)) // 1
                .op(Op::CompareLt, { Id("c"), Id("i"), Int(10) }) // 2
                .jmpf(Id("c"), 4) // 3 -> 7
                .iadd(Id("i"), Id("i"), Int(1)) // 4
                .iadd(Id("sum"), Id("sum"), Id("i")) // 5
                .jmp(-4) // 6 -> 2
                .ret(Id("sum")); // 7
            ASSERT_TRUE(vm.add(s));
            const CallResult result = vm.call(vm.make("Loop"), "Sum");
            ASSERT_TRUE(result.ok) << result.error.format();
            EXPECT_EQ(result.returned.integer, 55);
            EXPECT_GT(result.instructionsExecuted, 50u);
        }
    }

    TEST(PapyrusVmCore, jmpt_jmpf_and_not_use_truthiness_of_every_kind)
    {
        Vm vm;
        ScriptBuilder s("Truth");
        s.var("o", "Truth");
        // Returns 1 when the value is true: jmpt over the "return 0".
        auto truthy = [&s](const char* name, Operand value) {
            s.function(name, "Int").jmpt(std::move(value), 2).ret(Int(0)).ret(Int(1));
        };
        truthy("IntZero", Int(0));
        truthy("IntOne", Int(-1));
        truthy("FloatZero", Flt(0.0f));
        truthy("FloatHalf", Flt(0.5f));
        truthy("StringEmpty", Str(""));
        truthy("StringText", Str("x"));
        truthy("ObjectNone", Id("o"));
        truthy("ObjectSelf", Id("self"));
        s.function("NotZero", "Bool").local("c", "Bool").op(Op::Not, { Id("c"), Int(0) }).ret(Id("c"));
        s.function("NotSelf", "Bool").local("c", "Bool").op(Op::Not, { Id("c"), Id("self") }).ret(Id("c"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Truth");
        EXPECT_EQ(vm.runInt(id, "IntZero"), 0);
        EXPECT_EQ(vm.runInt(id, "IntOne"), 1);
        EXPECT_EQ(vm.runInt(id, "FloatZero"), 0);
        EXPECT_EQ(vm.runInt(id, "FloatHalf"), 1);
        EXPECT_EQ(vm.runInt(id, "StringEmpty"), 0);
        EXPECT_EQ(vm.runInt(id, "StringText"), 1);
        EXPECT_EQ(vm.runInt(id, "ObjectNone"), 0);
        EXPECT_EQ(vm.runInt(id, "ObjectSelf"), 1);
        EXPECT_EQ(vm.run(id, "NotZero").integer, 1);
        EXPECT_EQ(vm.run(id, "NotSelf").integer, 0);
    }

    TEST(PapyrusVmCore, running_off_the_end_of_a_function_returns_none)
    {
        Vm vm;
        ScriptBuilder s("Falls");
        s.function("Run").nop();
        ASSERT_TRUE(vm.add(s));
        const CallResult result = vm.call(vm.make("Falls"), "Run");
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.returned.kind, PapyrusKind::None);
    }

    // ---- casts and strings -----------------------------------------------------------------------------------------

    TEST(PapyrusVmCore, casts_between_the_number_kinds_and_strings)
    {
        Vm vm;
        ScriptBuilder s("Casts");
        s.function("IntToFloat", "Float").local("f", "Float").op(Op::Cast, { Id("f"), Int(3) }).ret(Id("f"));
        s.function("FloatToInt", "Int").local("i", "Int").op(Op::Cast, { Id("i"), Flt(-2.75f) }).ret(Id("i"));
        s.function("IntToBool", "Bool").local("b", "Bool").op(Op::Cast, { Id("b"), Int(5) }).ret(Id("b"));
        s.function("ZeroToBool", "Bool").local("b", "Bool").op(Op::Cast, { Id("b"), Int(0) }).ret(Id("b"));
        s.function("BoolToInt", "Int").local("i", "Int").op(Op::Cast, { Id("i"), Bool(true) }).ret(Id("i"));
        s.function("IntToString", "String").local("t", "String").op(Op::Cast, { Id("t"), Int(-407) }).ret(Id("t"));
        s.function("StringToInt", "Int").local("i", "Int").op(Op::Cast, { Id("i"), Str("123") }).ret(Id("i"));
        s.function("JunkToInt", "Int").local("i", "Int").op(Op::Cast, { Id("i"), Str("abc") }).ret(Id("i"));
        s.function("StringToFloat", "Float").local("f", "Float").op(Op::Cast, { Id("f"), Str("2.5") }).ret(Id("f"));
        s.function("SelfToBool", "Bool").local("b", "Bool").op(Op::Cast, { Id("b"), Id("self") }).ret(Id("b"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Casts");
        EXPECT_FLOAT_EQ(vm.run(id, "IntToFloat").number, 3.0f);
        EXPECT_EQ(vm.runInt(id, "FloatToInt"), -2);
        EXPECT_EQ(vm.run(id, "IntToBool").integer, 1);
        EXPECT_EQ(vm.run(id, "ZeroToBool").integer, 0);
        EXPECT_EQ(vm.runInt(id, "BoolToInt"), 1);
        EXPECT_EQ(vm.run(id, "IntToString").text, "-407");
        EXPECT_EQ(vm.runInt(id, "StringToInt"), 123);
        EXPECT_EQ(vm.runInt(id, "JunkToInt"), 0);
        EXPECT_FLOAT_EQ(vm.run(id, "StringToFloat").number, 2.5f);
        EXPECT_EQ(vm.run(id, "SelfToBool").integer, 1);
    }

    TEST(PapyrusVmCore, strcat_joins_strings_and_makes_text_of_numbers)
    {
        Vm vm;
        ScriptBuilder s("Cat");
        s.function("Run", "String")
            .local("a", "String")
            .local("b", "String")
            .op(Op::StrCat, { Id("a"), Str("ab"), Str("cd") })
            .op(Op::StrCat, { Id("b"), Id("a"), Int(7) })
            .ret(Id("b"));
        ASSERT_TRUE(vm.add(s));
        const PapyrusValue value = vm.run(vm.make("Cat"), "Run");
        EXPECT_EQ(value.kind, PapyrusKind::String);
        EXPECT_EQ(value.text, "abcd7");
    }

    TEST(PapyrusVmCore, a_string_over_the_cap_ends_the_stack)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxStringBytes = 16;
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ScriptBuilder s("Long");
        s.function("Run", "String")
            .local("a", "String")
            .op(Op::StrCat, { Id("a"), Str("0123456789"), Str("0123456789") })
            .ret(Id("a"));
        ASSERT_TRUE(vm.add(s));
        const CallResult result = vm.call(vm.make("Long"), "Run");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[limit exceeded]")) << result.error.format();
    }

    // ---- properties ------------------------------------------------------------------------------------------------

    ScriptBuilder holderScript()
    {
        ScriptBuilder s("Holder");
        s.var("base", "Int", Int(5)).var("other", "Holder");
        s.autoProperty("Total", "Int", Int(7));
        s.fullProperty("Computed", "Int");
        s.getter().local("t", "Int").iadd(Id("t"), Id("base"), Int(100)).ret(Id("t"));
        s.setter().assign(Id("base"), Id("aValue")).ret(Nil());
        s.function("ReadTotal", "Int")
            .local("r", "Int")
            .op(Op::PropGet, { Str("Total"), Id("self"), Id("r") })
            .ret(Id("r"));
        s.function("WriteTotal").param("v", "Int").op(Op::PropSet, { Str("Total"), Id("self"), Id("v") }).ret(Nil());
        s.function("ReadComputed", "Int")
            .local("r", "Int")
            .op(Op::PropGet, { Str("Computed"), Id("self"), Id("r") })
            .ret(Id("r"));
        s.function("WriteComputed")
            .param("v", "Int")
            .op(Op::PropSet, { Str("Computed"), Id("self"), Id("v") })
            .ret(Nil());
        s.function("ReadOtherTotal", "Int")
            .local("r", "Int")
            .op(Op::PropGet, { Str("Total"), Id("other"), Id("r") })
            .ret(Id("r"));
        s.function("ReadOtherOfNone", "Int")
            .local("o", "Holder")
            .local("r", "Int")
            .op(Op::PropGet, { Str("Total"), Id("o"), Id("r") })
            .ret(Id("r"));
        return s;
    }

    TEST(PapyrusVmCore, auto_properties_read_and_write_their_backing_variable)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ASSERT_TRUE(vm.add(holderScript()));
            const InstanceId id = vm.make("Holder");
            EXPECT_EQ(vm.runInt(id, "ReadTotal"), 7);
            vm.run(id, "WriteTotal", { PapyrusValue::makeInt(19) });
            EXPECT_EQ(vm.runInt(id, "ReadTotal"), 19);
            EXPECT_EQ(vm.varInt(id, "Total"), 19) << "a property is readable by name from outside";
        }
    }

    TEST(PapyrusVmCore, full_properties_run_their_accessor_functions)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(holderScript()));
        const InstanceId id = vm.make("Holder");
        EXPECT_EQ(vm.runInt(id, "ReadComputed"), 105);
        vm.run(id, "WriteComputed", { PapyrusValue::makeInt(9) });
        EXPECT_EQ(vm.varInt(id, "base"), 9);
        EXPECT_EQ(vm.runInt(id, "ReadComputed"), 109);
    }

    TEST(PapyrusVmCore, a_property_of_another_instance_is_read_through_an_object_variable)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(holderScript()));
        const InstanceId a = vm.make("Holder");
        const InstanceId b = vm.make("Holder");
        vm.run(b, "WriteTotal", { PapyrusValue::makeInt(31) });
        ASSERT_TRUE(vm.rt().setVariable(a, "other", PapyrusValue::makeInstance(b)));
        EXPECT_EQ(vm.runInt(a, "ReadOtherTotal"), 31);
        PapyrusValue value;
        ASSERT_TRUE(vm.rt().getVariable(a, "other", value));
        EXPECT_EQ(value.kind, PapyrusKind::Object);
        EXPECT_TRUE(value.attached);
        EXPECT_EQ(value.handle, b);
    }

    TEST(PapyrusVmCore, a_property_of_none_is_zero_when_tolerant_and_an_error_when_strict)
    {
        {
            Vm vm;
            ASSERT_TRUE(vm.add(holderScript()));
            const InstanceId id = vm.make("Holder");
            EXPECT_EQ(vm.runInt(id, "ReadOtherOfNone"), 0);
            EXPECT_EQ(vm.rt().stats().noneReceiverCalls, 1u);
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(holderScript()));
            const InstanceId id = vm.make("Holder");
            const CallResult result = vm.call(id, "ReadOtherOfNone");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[none receiver]")) << result.error.format();
        }
    }

    TEST(PapyrusVmCore, setvariable_checks_the_kind_it_stores)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(holderScript()));
        const InstanceId id = vm.make("Holder");
        EXPECT_TRUE(vm.rt().setVariable(id, "base", PapyrusValue::makeInt(3)));
        EXPECT_TRUE(vm.rt().setVariable(id, "Total", PapyrusValue::makeInt(4)));
        EXPECT_FALSE(vm.rt().setVariable(id, "base", PapyrusValue::makeString("x"))) << "a string is not an Int";
        EXPECT_FALSE(vm.rt().setVariable(id, "nothing", PapyrusValue::makeInt(1)));
        EXPECT_EQ(vm.varInt(id, "base"), 3);
    }

    // ---- states ----------------------------------------------------------------------------------------------------

    ScriptBuilder doorScript(const std::string& autoState = {})
    {
        ScriptBuilder s("Door");
        if (!autoState.empty())
        {
            s.autoState(autoState);
        }
        s.function("GotoState").param("newState", "String").assign(Id("::State"), Id("newState")).ret(Nil());
        s.function("GetState", "String").ret(Id("::State"));
        s.function("Describe", "Int").ret(Int(1));
        s.function("OnlyDefault", "Int").ret(Int(7));
        s.state("Open").function("Describe", "Int").ret(Int(2));
        s.state("Locked").function("Describe", "Int").ret(Int(3));
        return s;
    }

    TEST(PapyrusVmCore, a_state_function_wins_over_the_default_one_and_the_default_is_the_fallback)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ASSERT_TRUE(vm.add(doorScript()));
            const InstanceId id = vm.make("Door");
            EXPECT_EQ(vm.rt().stateOf(id), "");
            EXPECT_EQ(vm.runInt(id, "Describe"), 1);
            vm.run(id, "GotoState", { PapyrusValue::makeString("Open") });
            EXPECT_EQ(vm.rt().stateOf(id), "Open");
            EXPECT_EQ(vm.runInt(id, "Describe"), 2);
            EXPECT_EQ(vm.runInt(id, "OnlyDefault"), 7) << "no Open version: the empty state answers";
            EXPECT_EQ(vm.run(id, "GetState").text, "Open");
            vm.run(id, "GotoState", { PapyrusValue::makeString("LOCKED") });
            EXPECT_EQ(vm.runInt(id, "Describe"), 3) << "state names are compared without regard to case";
            vm.run(id, "GotoState", { PapyrusValue::makeString("") });
            EXPECT_EQ(vm.runInt(id, "Describe"), 1);
        }
    }

    TEST(PapyrusVmCore, the_auto_state_is_the_state_an_instance_starts_in)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(doorScript("Locked")));
        const InstanceId id = vm.make("Door");
        EXPECT_EQ(vm.rt().stateOf(id), "Locked");
        EXPECT_EQ(vm.runInt(id, "Describe"), 3);
    }

    TEST(PapyrusVmCore, oninit_runs_in_the_auto_state)
    {
        Vm vm;
        ScriptBuilder s("Starter");
        s.autoState("Ready").var("how", "Int");
        s.function("OnInit").assign(Id("how"), Int(1)).ret(Nil());
        s.state("Ready").function("OnInit").assign(Id("how"), Int(2)).ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.makeAndInit("Starter");
        EXPECT_EQ(vm.varInt(id, "how"), 2);
    }

    TEST(PapyrusVmCore, a_child_finds_the_default_function_of_its_parent_while_in_a_state)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(doorScript()));
        ScriptBuilder child("BigDoor");
        child.parent("Door");
        child.state("Open").function("Extra", "Int").ret(Int(9));
        ASSERT_TRUE(vm.add(child));
        const InstanceId id = vm.make("BigDoor");
        vm.run(id, "GotoState", { PapyrusValue::makeString("Open") });
        EXPECT_EQ(vm.runInt(id, "Extra"), 9);
        EXPECT_EQ(vm.runInt(id, "Describe"), 2) << "the parent's Open state";
        EXPECT_EQ(vm.runInt(id, "OnlyDefault"), 7) << "the parent's default state";
    }

    // ---- inheritance and calls -------------------------------------------------------------------------------------

    TEST(PapyrusVmCore, callparent_runs_the_parents_version_and_variables_are_shared_down_the_chain)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder base("Base");
            base.var("baseVar", "Int", Int(5));
            base.function("Value", "Int").ret(Int(10));
            base.function("Describe", "Int").ret(Id("baseVar"));
            ScriptBuilder child("Child");
            child.parent("Base");
            child.var("childVar", "Int", Int(6));
            child.function("Value", "Int")
                .local("r", "Int")
                .callParent("Value", Id("r"))
                .iadd(Id("r"), Id("r"), Int(1))
                .ret(Id("r"));
            child.function("Sum", "Int").local("r", "Int").iadd(Id("r"), Id("baseVar"), Id("childVar")).ret(Id("r"));
            ASSERT_TRUE(vm.add(base));
            ASSERT_TRUE(vm.add(child));
            EXPECT_EQ(vm.runInt(vm.make("Base"), "Value"), 10);
            const InstanceId c = vm.make("Child");
            EXPECT_EQ(vm.runInt(c, "Value"), 11);
            EXPECT_EQ(vm.runInt(c, "Describe"), 5) << "an inherited function reads the parent's variable";
            EXPECT_EQ(vm.runInt(c, "Sum"), 11) << "the child sees the parent's variable by name";
            EXPECT_EQ(vm.varInt(c, "baseVar"), 5);
            EXPECT_EQ(vm.varInt(c, "childVar"), 6);
        }
    }

    TEST(PapyrusVmCore, callstatic_runs_a_global_function_with_arguments)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder util("Util");
            util.function("Add", "Int")
                .global()
                .param("a", "Int")
                .param("b", "Int")
                .local("r", "Int")
                .iadd(Id("r"), Id("a"), Id("b"))
                .ret(Id("r"));
            ScriptBuilder caller("Caller");
            caller.function("Run", "Int")
                .local("r", "Int")
                .callStatic("util", "ADD", Id("r"), { Int(2), Int(3) })
                .ret(Id("r"));
            ASSERT_TRUE(vm.add(util));
            ASSERT_TRUE(vm.add(caller));
            EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 5) << "names are compared without regard to case";
        }
    }

    TEST(PapyrusVmCore, callmethod_runs_a_function_of_another_instance_and_recursion_works)
    {
        Vm vm;
        ScriptBuilder s("Fact");
        s.var("peer", "Fact");
        s.function("Of", "Int")
            .param("n", "Int")
            .local("c", "Bool")
            .local("m", "Int")
            .local("r", "Int")
            .op(Op::CompareLe, { Id("c"), Id("n"), Int(1) }) // 0
            .jmpf(Id("c"), 2) // 1 -> 3
            .ret(Int(1)) // 2
            .op(Op::ISub, { Id("m"), Id("n"), Int(1) }) // 3
            .callMethod("Of", Id("self"), Id("r"), { Id("m") }) // 4
            .op(Op::IMul, { Id("r"), Id("r"), Id("n") }) // 5
            .ret(Id("r")); // 6
        s.function("ViaPeer", "Int").local("r", "Int").callMethod("Of", Id("peer"), Id("r"), { Int(5) }).ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId a = vm.make("Fact");
        const InstanceId b = vm.make("Fact");
        EXPECT_EQ(vm.runInt(a, "Of", { PapyrusValue::makeInt(10) }), 3628800);
        ASSERT_TRUE(vm.rt().setVariable(a, "peer", PapyrusValue::makeInstance(b)));
        EXPECT_EQ(vm.runInt(a, "ViaPeer"), 120);
        EXPECT_GT(vm.rt().stats().scriptCalls, 10u);
    }

    TEST(PapyrusVmCore, a_wrong_argument_count_is_an_error)
    {
        Vm vm;
        ScriptBuilder s("Args");
        s.function("Two", "Int").param("a", "Int").param("b", "Int").ret(Id("a"));
        s.function("Bad", "Int").local("r", "Int").callMethod("Two", Id("self"), Id("r"), { Int(1) }).ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Args");
        EXPECT_FALSE(vm.call(id, "Bad").ok);
        const CallResult result = vm.call(id, "Two", { PapyrusValue::makeInt(1) });
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[argument count]")) << result.error.format();
    }

    TEST(PapyrusVmCore, arguments_are_converted_to_the_parameter_type)
    {
        Vm vm;
        ScriptBuilder s("Conv");
        s.function("Half", "Float")
            .param("x", "Float")
            .local("r", "Float")
            .op(Op::FMul, { Id("r"), Id("x"), Flt(0.5f) })
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        EXPECT_FLOAT_EQ(vm.run(vm.make("Conv"), "Half", { PapyrusValue::makeInt(5) }).number, 2.5f);
    }

    // ---- events ----------------------------------------------------------------------------------------------------

    TEST(PapyrusVmCore, an_event_is_queued_with_its_arguments_and_run_by_tick)
    {
        Vm vm;
        ScriptBuilder s("Listener");
        s.var("last", "Int", Int(-1)).var("hits", "Int");
        s.function("OnHit")
            .param("amount", "Int")
            .assign(Id("last"), Id("amount"))
            .iadd(Id("hits"), Id("hits"), Int(1))
            .ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Listener");
        vm.tick();
        const CallResult sent = vm.rt().send(id, "OnHit", std::vector<PapyrusValue>{ PapyrusValue::makeInt(9) });
        EXPECT_TRUE(sent.ok) << sent.error.format();
        EXPECT_EQ(vm.varInt(id, "last"), -1) << "send queues";
        vm.rt().send(id, "OnHit", std::vector<PapyrusValue>{ PapyrusValue::makeInt(4) });
        const TickResult tick = vm.tick();
        EXPECT_EQ(tick.stacksRun, 2u);
        EXPECT_EQ(vm.varInt(id, "hits"), 2);
        EXPECT_EQ(vm.varInt(id, "last"), 4) << "events run in the order they were queued";
    }

    TEST(PapyrusVmCore, an_event_the_instance_does_not_define_is_ignored)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(ScriptBuilder("Quiet")));
        const InstanceId id = vm.make("Quiet");
        vm.tick();
        const CallResult sent = vm.rt().send(id, "OnNothing", {});
        EXPECT_TRUE(sent.ok);
        const TickResult tick = vm.tick();
        EXPECT_TRUE(tick.errors.empty());
        EXPECT_EQ(vm.rt().stats().ignoredEvents, 1u + 1u) << "OnInit (not defined) and OnNothing";
    }

    TEST(PapyrusVmCore, send_to_an_unknown_instance_is_a_report)
    {
        Vm vm;
        const CallResult sent = vm.rt().send(99, "OnHit", {});
        EXPECT_FALSE(sent.ok);
        EXPECT_TRUE(hasText(sent.error, "[no such instance]")) << sent.error.format();
    }

    TEST(PapyrusVmCore, a_failing_event_leaves_the_other_stacks_and_the_instance_alone)
    {
        Vm vm;
        ScriptBuilder s("Mixed");
        s.var("ok", "Int");
        s.function("OnBad").local("r", "Int").op(Op::IDiv, { Id("r"), Int(1), Int(0) }).ret(Nil());
        s.function("OnGood").assign(Id("ok"), Int(1)).ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Mixed");
        vm.tick();
        vm.rt().send(id, "OnBad", {});
        vm.rt().send(id, "OnGood", {});
        const TickResult tick = vm.tick();
        EXPECT_EQ(tick.stacksRun, 2u);
        EXPECT_EQ(tick.stacksFinished, 2u);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[divide by zero]"));
        EXPECT_EQ(vm.varInt(id, "ok"), 1);
    }

    TEST(PapyrusVmCore, a_missing_on_function_called_by_script_is_ignored_and_another_missing_one_is_not)
    {
        Vm vm;
        ScriptBuilder s("Calls");
        s.function("CallsEvent", "Int").local("n", "None").callMethod("OnNobody", Id("self"), Id("n")).ret(Int(1));
        s.function("CallsMissing", "Int").local("n", "None").callMethod("Nobody", Id("self"), Id("n")).ret(Int(1));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Calls");
        EXPECT_EQ(vm.runInt(id, "CallsEvent"), 1);
        // A missing ordinary function: tolerant mode logs once and carries on, as for a native that is not there.
        EXPECT_EQ(vm.runInt(id, "CallsMissing"), 1);
        EXPECT_EQ(vm.runInt(id, "CallsMissing"), 1);
        EXPECT_EQ(vm.mWorld.loggedCount("Nobody"), 1u);
    }

    // ---- determinism -----------------------------------------------------------------------------------------------

    TEST(PapyrusVmCore, the_same_inputs_give_the_same_outputs_and_the_same_error_text)
    {
        auto runOnce = []() {
            Vm vm;
            ScriptBuilder s("Det");
            s.var("n", "Int");
            s.function("OnInit").assign(Id("n"), Int(3)).ret(Nil());
            s.function("Run", "Int")
                .local("i", "Int")
                .local("c", "Bool")
                .assign(Id("i"), Int(0))
                .op(Op::CompareLt, { Id("c"), Id("i"), Id("n") })
                .jmpf(Id("c"), 3)
                .iadd(Id("i"), Id("i"), Int(1))
                .jmp(-3)
                .op(Op::IDiv, { Id("i"), Id("i"), Int(0) })
                .ret(Id("i"));
            EXPECT_TRUE(vm.add(s));
            const InstanceId id = vm.makeAndInit("Det");
            const CallResult result = vm.call(id, "Run");
            return std::make_pair(result.error.format(), result.instructionsExecuted);
        };
        const auto first = runOnce();
        const auto second = runOnce();
        EXPECT_FALSE(first.first.empty());
        EXPECT_EQ(first, second);
    }
}
