// Task 0093 stage c: native registration through the contract, the tolerant and strict policies, latent natives (not
// yet run: stage d), errors that end a stack, the budget and the depth cap. Written from the task's spec before the VM.

#include <gtest/gtest.h>

#include <stdexcept>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using ScriptPapyrus::ErrorKind;

    std::uint64_t errorCount(Vm& vm, ErrorKind kind)
    {
        return vm.rt().stats().errors[static_cast<std::size_t>(kind)];
    }

    ScriptBuilder gameClass()
    {
        ScriptBuilder g("Game");
        g.function("GetPlayer", "Actor").global().native();
        g.function("Add", "Int").global().native().param("a", "Int").param("b", "Int");
        g.function("Name", "String").global().native().param("n", "Int");
        g.function("Half", "Float").global().native().param("x", "Float");
        g.function("IsReady", "Bool").global().native();
        g.function("Wait").global().native().param("seconds", "Float");
        g.function("Ordinary", "Int").global().ret(Int(1));
        return g;
    }

    ScriptBuilder actorClass()
    {
        ScriptBuilder a("Actor");
        a.function("GetLevel", "Int").native();
        a.function("SetLevel").native().param("level", "Int");
        return a;
    }

    NativeDescriptor descriptor(std::string className, std::string functionName, bool global = false)
    {
        NativeDescriptor d;
        d.className = std::move(className);
        d.functionName = std::move(functionName);
        d.global = global;
        return d;
    }

    bool bind(Vm& vm, const NativeDescriptor& d, NativeHandler handler)
    {
        ErrorReport error;
        const bool ok = vm.rt().registerNative(d, std::move(handler), error);
        EXPECT_TRUE(ok) << error.format();
        return ok;
    }

    // ---- registration ----------------------------------------------------------------------------------------------

    TEST(PapyrusVmNatives, a_registered_global_native_gets_its_arguments_and_returns_its_value)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ASSERT_TRUE(vm.add(gameClass()));
            std::vector<PapyrusValue> seen;
            PapyrusKind selfKind = PapyrusKind::Int;
            ASSERT_TRUE(bind(vm, descriptor("Game", "Add", true), [&](const NativeCall& call) {
                selfKind = call.self.kind;
                seen.assign(call.arguments.begin(), call.arguments.end());
                return NativeResult{ PapyrusValue::makeInt(call.arguments[0].integer + call.arguments[1].integer) };
            }));
            ScriptBuilder caller("Caller");
            caller.function("Run", "Int")
                .local("r", "Int")
                .callStatic("Game", "Add", Id("r"), { Int(40), Int(2) })
                .ret(Id("r"));
            ASSERT_TRUE(vm.add(caller));
            EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 42);
            EXPECT_EQ(selfKind, PapyrusKind::None) << "a global function has no self";
            ASSERT_EQ(seen.size(), 2u);
            EXPECT_EQ(seen[0].integer, 40);
            EXPECT_EQ(vm.rt().stats().nativeCalls, 1u);
        }
    }

    TEST(PapyrusVmNatives, a_native_of_an_attached_script_gets_the_instance_as_self)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(actorClass()));
        ScriptBuilder npc("Npc");
        npc.parent("Actor");
        npc.function("Run", "Int").local("r", "Int").callMethod("GetLevel", Id("self"), Id("r")).ret(Id("r"));
        ASSERT_TRUE(vm.add(npc));
        const InstanceId id = vm.make("Npc");
        PapyrusValue self;
        ASSERT_TRUE(bind(vm, descriptor("Actor", "GetLevel"), [&](const NativeCall& call) {
            self = call.self;
            return NativeResult{ PapyrusValue::makeInt(12) };
        }));
        EXPECT_EQ(vm.runInt(id, "Run"), 12);
        EXPECT_EQ(self.kind, PapyrusKind::Object);
        EXPECT_TRUE(self.attached);
        EXPECT_EQ(self.handle, id);
    }

    TEST(PapyrusVmNatives, a_native_form_value_dispatches_by_its_class_name)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(vm.add(actorClass()));
        ASSERT_TRUE(bind(vm, descriptor("Game", "GetPlayer", true),
            [](const NativeCall&) { return NativeResult{ PapyrusValue::makeForm("Actor", 0x14) }; }));
        PapyrusValue self;
        ASSERT_TRUE(bind(vm, descriptor("Actor", "GetLevel"), [&](const NativeCall& call) {
            self = call.self;
            return NativeResult{ PapyrusValue::makeInt(30) };
        }));
        std::int32_t level = 0;
        ASSERT_TRUE(bind(vm, descriptor("Actor", "SetLevel"), [&](const NativeCall& call) {
            level = call.arguments[0].integer;
            return NativeResult{};
        }));
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int")
            .local("p", "Actor")
            .local("r", "Int")
            .local("n", "None")
            .callStatic("Game", "GetPlayer", Id("p"))
            .callMethod("SetLevel", Id("p"), Id("n"), { Int(31) })
            .callMethod("GetLevel", Id("p"), Id("r"))
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(caller));
        EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 30);
        EXPECT_EQ(level, 31);
        EXPECT_EQ(self.kind, PapyrusKind::Object);
        EXPECT_FALSE(self.attached);
        EXPECT_EQ(self.handle, 0x14u);
        EXPECT_EQ(self.text, "Actor");
    }

    TEST(PapyrusVmNatives, registration_is_checked_against_the_declaration)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(vm.add(actorClass()));
        ErrorReport error;
        auto nothing = [](const NativeCall&) { return NativeResult{}; };

        EXPECT_FALSE(vm.rt().registerNative(descriptor("Game", "NoSuchFunction", true), nothing, error));
        EXPECT_NE(error.message.find("NoSuchFunction"), std::string::npos) << error.format();

        error = {};
        EXPECT_FALSE(vm.rt().registerNative(descriptor("Game", "Add", false), nothing, error))
            << "the global flag must equal the declaration's";
        EXPECT_FALSE(error.message.empty());

        error = {};
        EXPECT_FALSE(vm.rt().registerNative(descriptor("Game", "Ordinary", true), nothing, error))
            << "a script function is not a native";
        EXPECT_FALSE(error.message.empty());

        error = {};
        EXPECT_TRUE(vm.rt().registerNative(descriptor("game", "add", true), nothing, error)) << error.format();
        error = {};
        EXPECT_FALSE(vm.rt().registerNative(descriptor("Game", "Add", true), nothing, error))
            << "the pair is bound already";
        EXPECT_NE(error.message.find("already"), std::string::npos) << error.format();

        error = {};
        EXPECT_TRUE(vm.rt().registerNative(descriptor("NotLoadedYet", "Whatever", true), nothing, error))
            << "a class that is not loaded: the binding waits for it";
    }

    TEST(PapyrusVmNatives, a_native_registered_before_its_class_is_loaded_works_once_it_is)
    {
        Vm vm;
        ASSERT_TRUE(bind(vm, descriptor("Game", "Add", true), [](const NativeCall& call) {
            return NativeResult{ PapyrusValue::makeInt(call.arguments[0].integer * call.arguments[1].integer) };
        }));
        ASSERT_TRUE(vm.add(gameClass()));
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int")
            .local("r", "Int")
            .callStatic("Game", "Add", Id("r"), { Int(6), Int(7) })
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(caller));
        EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 42);
    }

    TEST(PapyrusVmNatives, results_are_converted_to_the_declared_return_type)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(bind(vm, descriptor("Game", "Add", true),
            [](const NativeCall&) { return NativeResult{ PapyrusValue::makeFloat(2.9f) }; }));
        ASSERT_TRUE(bind(vm, descriptor("Game", "Half", true), [](const NativeCall& call) {
            return NativeResult{ PapyrusValue::makeFloat(call.arguments[0].number / 2) };
        }));
        ASSERT_TRUE(bind(vm, descriptor("Game", "Name", true),
            [](const NativeCall&) { return NativeResult{ PapyrusValue::makeString("Alduin") }; }));
        ASSERT_TRUE(bind(vm, descriptor("Game", "IsReady", true), [](const NativeCall&) { return NativeResult{}; }));
        ScriptBuilder caller("Caller");
        caller.function("AddIsInt", "Int")
            .local("r", "Int")
            .callStatic("Game", "Add", Id("r"), { Int(1), Int(1) })
            .ret(Id("r"));
        caller.function("HalfOfInt", "Float")
            .local("r", "Float")
            .callStatic("Game", "Half", Id("r"), { Int(5) })
            .ret(Id("r"));
        caller.function("NameOf", "String")
            .local("r", "String")
            .callStatic("Game", "Name", Id("r"), { Int(1) })
            .ret(Id("r"));
        caller.function("NoneIsFalse", "Bool").local("r", "Bool").callStatic("Game", "IsReady", Id("r")).ret(Id("r"));
        ASSERT_TRUE(vm.add(caller));
        const InstanceId id = vm.make("Caller");
        EXPECT_EQ(vm.runInt(id, "AddIsInt"), 2);
        EXPECT_FLOAT_EQ(vm.run(id, "HalfOfInt").number, 2.5f) << "an Int argument reaches a Float parameter as a Float";
        EXPECT_EQ(vm.run(id, "NameOf").text, "Alduin");
        const PapyrusValue none = vm.run(id, "NoneIsFalse");
        EXPECT_EQ(none.kind, PapyrusKind::Bool);
        EXPECT_EQ(none.integer, 0);
    }

    TEST(PapyrusVmNatives, a_native_that_throws_ends_the_stack_and_nothing_else)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(bind(vm, descriptor("Game", "Add", true),
            [](const NativeCall&) -> NativeResult { throw std::runtime_error("boom"); }));
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int")
            .local("r", "Int")
            .callStatic("Game", "Add", Id("r"), { Int(1), Int(1) })
            .ret(Id("r"));
        caller.function("Fine", "Int").ret(Int(5));
        ASSERT_TRUE(vm.add(caller));
        const InstanceId id = vm.make("Caller");
        const CallResult result = vm.call(id, "Run");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[native failed]")) << result.error.format();
        EXPECT_TRUE(hasText(result.error, "boom")) << result.error.format();
        EXPECT_EQ(vm.runInt(id, "Fine"), 5);
        EXPECT_EQ(errorCount(vm, ErrorKind::NativeFailed), 1u);
    }

    // ---- the extender policy ---------------------------------------------------------------------------------------

    ScriptBuilder callerOfUnregistered()
    {
        ScriptBuilder caller("Caller");
        caller.function("AddIt", "Int")
            .local("r", "Int")
            .callStatic("Game", "Add", Id("r"), { Int(1), Int(2) })
            .ret(Id("r"));
        caller.function("NameIt", "String")
            .local("r", "String")
            .callStatic("Game", "Name", Id("r"), { Int(1) })
            .ret(Id("r"));
        caller.function("HalfIt", "Float")
            .local("r", "Float")
            .callStatic("Game", "Half", Id("r"), { Flt(1.0f) })
            .ret(Id("r"));
        caller.function("ReadyIt", "Bool").local("r", "Bool").callStatic("Game", "IsReady", Id("r")).ret(Id("r"));
        caller.function("PlayerIt", "Actor").local("r", "Actor").callStatic("Game", "GetPlayer", Id("r")).ret(Id("r"));
        return caller;
    }

    TEST(PapyrusVmNatives, tolerant_mode_returns_the_zero_of_the_declared_type_and_logs_once)
    {
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(vm.add(callerOfUnregistered()));
        const InstanceId id = vm.make("Caller");
        EXPECT_EQ(vm.runInt(id, "AddIt"), 0);
        EXPECT_EQ(vm.runInt(id, "AddIt"), 0);
        EXPECT_EQ(vm.runInt(id, "AddIt"), 0);
        const PapyrusValue name = vm.run(id, "NameIt");
        EXPECT_EQ(name.kind, PapyrusKind::String);
        EXPECT_EQ(name.text, "");
        EXPECT_FLOAT_EQ(vm.run(id, "HalfIt").number, 0.0f);
        EXPECT_EQ(vm.run(id, "ReadyIt").kind, PapyrusKind::Bool);
        EXPECT_EQ(vm.run(id, "PlayerIt").kind, PapyrusKind::None);
        EXPECT_EQ(vm.rt().stats().unregisteredNativeCalls, 7u);
        EXPECT_EQ(vm.mWorld.loggedCount("Game.Add"), 1u) << "once for each (class, function)";
        EXPECT_EQ(vm.mWorld.loggedCount("Game.Name"), 1u);
        EXPECT_EQ(vm.rt().stats().nativeCalls, 0u);
    }

    TEST(PapyrusVmNatives, strict_mode_fails_the_call_with_a_located_error)
    {
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(vm.add(gameClass()));
        ASSERT_TRUE(vm.add(callerOfUnregistered()));
        const CallResult result = vm.call(vm.make("Caller"), "AddIt");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[unregistered native]")) << result.error.format();
        EXPECT_TRUE(hasText(result.error, "Game.Add")) << result.error.format();
        EXPECT_TRUE(hasText(result.error, "Caller.AddIt@0")) << result.error.format();
        EXPECT_EQ(errorCount(vm, ErrorKind::UnregisteredNative), 1u);
    }

    TEST(PapyrusVmNatives, a_call_into_a_class_that_is_not_loaded)
    {
        ScriptBuilder caller("Caller");
        caller.function("Number", "Int").local("r", "Int").callStatic("Nowhere", "Do", Id("r")).ret(Id("r"));
        caller.function("Text", "String").local("r", "String").callStatic("Nowhere", "Do", Id("r")).ret(Id("r"));
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
            ASSERT_TRUE(vm.add(caller));
            const InstanceId id = vm.make("Caller");
            EXPECT_EQ(vm.runInt(id, "Number"), 0);
            const PapyrusValue text = vm.run(id, "Text");
            EXPECT_EQ(text.kind, PapyrusKind::String) << "the zero of the destination's type";
            EXPECT_EQ(text.text, "");
            EXPECT_EQ(vm.rt().stats().missingClassCalls, 2u);
            EXPECT_EQ(vm.mWorld.loggedCount("Nowhere"), 1u);
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(caller));
            const CallResult result = vm.call(vm.make("Caller"), "Number");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[missing class]")) << result.error.format();
        }
    }

    TEST(PapyrusVmNatives, a_function_the_loaded_class_does_not_have)
    {
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int").local("r", "Int").callStatic("Game", "NoSuch", Id("r")).ret(Id("r"));
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
            ASSERT_TRUE(vm.add(gameClass()));
            ASSERT_TRUE(vm.add(caller));
            EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 0);
            EXPECT_TRUE(vm.mWorld.logged("NoSuch"));
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(gameClass()));
            ASSERT_TRUE(vm.add(caller));
            const CallResult result = vm.call(vm.make("Caller"), "Run");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[no such function]")) << result.error.format();
        }
    }

    TEST(PapyrusVmNatives, a_call_on_none)
    {
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int")
            .local("p", "Actor")
            .local("r", "Int")
            .callMethod("GetLevel", Id("p"), Id("r"))
            .ret(Id("r"));
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
            ASSERT_TRUE(vm.add(actorClass()));
            ASSERT_TRUE(vm.add(caller));
            EXPECT_EQ(vm.runInt(vm.make("Caller"), "Run"), 0);
            EXPECT_EQ(vm.rt().stats().noneReceiverCalls, 1u);
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(actorClass()));
            ASSERT_TRUE(vm.add(caller));
            const CallResult result = vm.call(vm.make("Caller"), "Run");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[none receiver]")) << result.error.format();
        }
    }

    TEST(PapyrusVmNatives, a_missing_parent_is_tolerated_only_when_tolerant)
    {
        ScriptBuilder child("Child");
        child.parent("Quest");
        child.function("Run", "Int").ret(Int(3));
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
            ASSERT_TRUE(vm.add(child));
            EXPECT_EQ(vm.runInt(vm.make("Child"), "Run"), 3);
            EXPECT_TRUE(vm.mWorld.logged("Quest"));
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(child));
            AttachKey key;
            key.formId = 1;
            EXPECT_EQ(vm.rt().attach(key, "Child"), 0u) << "strict mode refuses a script whose parent is not loaded";
            EXPECT_TRUE(vm.mWorld.logged("Quest"));
        }
    }

    // ---- latent natives are stage d --------------------------------------------------------------------------------

    TEST(PapyrusVmNatives, a_latent_native_is_not_run_in_this_stage)
    {
        ScriptBuilder caller("Caller");
        caller.function("Run", "Int").local("n", "None").callStatic("Game", "Wait", Id("n"), { Flt(1.0f) }).ret(Int(8));
        NativeDescriptor wait = descriptor("Game", "Wait", true);
        wait.latent = true;
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
            ASSERT_TRUE(vm.add(gameClass()));
            bool called = false;
            ASSERT_TRUE(bind(vm, wait, [&](const NativeCall&) {
                called = true;
                return NativeResult{ PapyrusValue::makeNone(), 1.0 };
            }));
            ASSERT_TRUE(vm.add(caller));
            const CallResult result = vm.call(vm.make("Caller"), "Run");
            EXPECT_TRUE(result.ok) << result.error.format();
            EXPECT_FALSE(result.suspended) << "nothing is suspended in stage c";
            EXPECT_EQ(result.returned.integer, 8);
            EXPECT_FALSE(called);
            EXPECT_EQ(vm.rt().stats().latentSkipped, 1u);
        }
        {
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
            ASSERT_TRUE(vm.add(gameClass()));
            ASSERT_TRUE(bind(vm, wait, [](const NativeCall&) { return NativeResult{}; }));
            ASSERT_TRUE(vm.add(caller));
            const CallResult result = vm.call(vm.make("Caller"), "Run");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[latent unsupported]")) << result.error.format();
            EXPECT_TRUE(hasText(result.error, "not supported yet")) << result.error.format();
        }
    }

    // ---- budget and depth ------------------------------------------------------------------------------------------

    ScriptBuilder spinner()
    {
        ScriptBuilder s("Spin");
        s.var("counter", "Int");
        s.function("Forever").nop().jmp(-1);
        s.function("OnSpin").nop().jmp(-1);
        s.function("OnFine").assign(Id("counter"), Int(1)).ret(Nil());
        s.function("Three", "Int").assign(Id("counter"), Int(1)).assign(Id("counter"), Int(2)).ret(Int(3));
        s.function("Recurse", "Int")
            .param("n", "Int")
            .local("r", "Int")
            .local("m", "Int")
            .op(Op::IAdd, { Id("m"), Id("n"), Int(1) })
            .callMethod("Recurse", Id("self"), Id("r"), { Id("m") })
            .ret(Id("r"));
        return s;
    }

    TEST(PapyrusVmNatives, an_endless_loop_ends_at_the_budget_with_a_located_error)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.defaultBudget = { 5000, 64 };
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(spinner()));
        const InstanceId id = vm.make("Spin");
        const CallResult result = vm.call(id, "Forever");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[budget exceeded]")) << result.error.format();
        EXPECT_TRUE(hasText(result.error, "Spin.Forever@")) << result.error.format();
        EXPECT_EQ(result.instructionsExecuted, 5000u) << "exactly the budget runs";
        // The runtime is fine afterwards.
        EXPECT_EQ(vm.runInt(id, "Three"), 3);
    }

    TEST(PapyrusVmNatives, the_budget_of_a_tick_applies_to_each_stack_and_does_not_touch_a_short_script)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(spinner()));
        const InstanceId id = vm.make("Spin");
        vm.tick();
        vm.rt().send(id, "OnSpin", {});
        vm.rt().send(id, "OnFine", {});
        Budget budget;
        budget.maxInstructions = 1000;
        const TickResult tick = vm.rt().tick(0.0, budget);
        EXPECT_EQ(tick.stacksRun, 2u);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[budget exceeded]"));
        EXPECT_EQ(vm.varInt(id, "counter"), 1) << "the second stack ran in full";
    }

    TEST(PapyrusVmNatives, runaway_recursion_ends_at_the_depth_cap)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(spinner()));
        const InstanceId id = vm.make("Spin");
        const CallResult result = vm.call(id, "Recurse", { PapyrusValue::makeInt(0) });
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[call depth]")) << result.error.format();
        EXPECT_EQ(errorCount(vm, ErrorKind::DepthExceeded), 1u);
        EXPECT_EQ(vm.runInt(id, "Three"), 3);
    }

    TEST(PapyrusVmNatives, the_depth_cap_is_a_setting_and_a_tick_can_lower_it)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.defaultBudget = { 100000, 10 };
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ScriptBuilder s("Fact");
        s.function("Of", "Int")
            .param("n", "Int")
            .local("c", "Bool")
            .local("m", "Int")
            .local("r", "Int")
            .op(Op::CompareLe, { Id("c"), Id("n"), Int(1) })
            .jmpf(Id("c"), 2)
            .ret(Int(1))
            .op(Op::ISub, { Id("m"), Id("n"), Int(1) })
            .callMethod("Of", Id("self"), Id("r"), { Id("m") })
            .op(Op::IMul, { Id("r"), Id("r"), Id("n") })
            .ret(Id("r"));
        s.function("OnDeep").local("r", "Int").callMethod("Of", Id("self"), Id("r"), { Int(6) }).ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Fact");
        vm.tick();
        EXPECT_EQ(vm.runInt(id, "Of", { PapyrusValue::makeInt(5) }), 120);
        EXPECT_FALSE(vm.call(id, "Of", { PapyrusValue::makeInt(40) }).ok);
        vm.rt().send(id, "OnDeep", {});
        Budget shallow;
        shallow.maxCallDepth = 3;
        const TickResult tick = vm.rt().tick(0.0, shallow);
        ASSERT_EQ(tick.errors.size(), 1u);
        EXPECT_TRUE(hasText(tick.errors[0], "[call depth]"));
    }

    TEST(PapyrusVmNatives, instructions_executed_counts_each_instruction_including_the_return)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(spinner()));
        const CallResult result = vm.call(vm.make("Spin"), "Three");
        ASSERT_TRUE(result.ok);
        EXPECT_EQ(result.instructionsExecuted, 3u);
        EXPECT_EQ(vm.rt().stats().instructions, 3u);
    }
}
