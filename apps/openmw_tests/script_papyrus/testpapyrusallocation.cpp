// Task 0093 stage c: the Papyrus instruction loop allocates nothing.
//
// In plain words: once the runtime has run a function once, running it again must not take memory from the heap for
// anything but the opcodes that make data (strcat, a cast to a string, array and struct creation, array growth, a
// native's string result). That covers arithmetic, comparisons, jumps, reads and writes of locals, variables and auto
// properties, script calls (method, static and parent), a registered native, state changes and the dispatch of an
// event. The count comes from the allocation counter that openmw-tests already has
// (apps/openmw_tests/support/allocationcounter.hpp: the program's one replacement of operator new reads it). The first
// run is a warm-up: caches fill, stacks and scratch buffers grow to the size the script needs and keep it. The
// allocating opcodes are measured too, so that the counter is shown to be live.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/script_papyrus/papyrusruntime.hpp>

#include "apps/components_tests/papyrus/pexassembler.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace PexAsm;
    using namespace openeden::script;

    class QuietWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId, std::string&) const override { return false; }
        void report(std::string_view) override {}
        void log(Severity, std::string_view) override { ++mLines; }
        std::size_t mLines = 0;
    };

    bool load(PapyrusContract& runtime, const ScriptBuilder& script, Target target)
    {
        Options options;
        options.mTarget = target;
        return runtime.loadScript(script.build(options).mBytes, script.name()).ok;
    }

    ScriptBuilder workScript()
    {
        ScriptBuilder s("Alloc");
        s.parent("Base");
        s.var("counter", "Int").var("ratio", "Float").var("text", "String", Str("hello")).var("list", "Int[]");
        s.autoProperty("Total", "Int", Int(1));
        s.function("Helper", "Int").param("x", "Int").local("r", "Int").iadd(Id("r"), Id("x"), Int(1)).ret(Id("r"));
        s.function("Mode", "Int").ret(Int(0));
        s.function("GotoState").param("newState", "String").assign(Id("::State"), Id("newState")).ret(Nil());
        s.function("Work", "Int")
            .local("i", "Int")
            .local("c", "Bool")
            .local("f", "Float")
            .local("r", "Int")
            .local("t", "Int")
            .local("n", "None")
            .assign(Id("i"), Int(0)) // 0
            .op(Op::CompareLt, { Id("c"), Id("i"), Int(200) }) // 1
            .jmpf(Id("c"), 22) // 2 -> 24
            .iadd(Id("i"), Id("i"), Int(1)) // 3
            .op(Op::IMul, { Id("t"), Id("i"), Int(3) }) // 4
            .op(Op::IMod, { Id("t"), Id("t"), Int(7) }) // 5
            .op(Op::Cast, { Id("f"), Id("t") }) // 6
            .op(Op::FMul, { Id("f"), Id("f"), Flt(1.5f) }) // 7
            .assign(Id("ratio"), Id("f")) // 8
            .op(Op::PropGet, { Str("Total"), Id("self"), Id("r") }) // 9
            .iadd(Id("r"), Id("r"), Int(1)) // 10
            .op(Op::PropSet, { Str("Total"), Id("self"), Id("r") }) // 11
            .callMethod("Helper", Id("self"), Id("r"), { Id("i") }) // 12
            .callMethod("Mode", Id("self"), Id("r")) // 13
            .callStatic("Util", "Twice", Id("r"), { Id("i") }) // 14
            .callStatic("Game", "Add", Id("r"), { Id("i"), Int(2) }) // 15
            .callParent("Base", Id("r")) // 16
            .callMethod("GotoState", Id("self"), Id("n"), { Str("Open") }) // 17
            .callMethod("Mode", Id("self"), Id("r")) // 18
            .callMethod("GotoState", Id("self"), Id("n"), { Str("") }) // 19
            .op(Op::CompareEq, { Id("c"), Id("text"), Str("HELLO") }) // 20
            .assign(Id("counter"), Id("i")) // 21
            .jmp(-21) // 22 -> 1
            .nop() // 23
            .ret(Id("counter")); // 24
        s.function("OnPing").param("amount", "Int").iadd(Id("counter"), Id("counter"), Id("amount")).ret(Nil());
        s.function("ArrayWork", "Int")
            .local("v", "Int")
            .local("n", "Int")
            .local("k", "Int")
            .op(Op::ArrayLength, { Id("n"), Id("list") })
            .op(Op::ArraySetElement, { Id("list"), Int(2), Int(7) })
            .op(Op::ArrayGetElement, { Id("v"), Id("list"), Int(2) })
            .op(Op::ArrayFindElement, { Id("list"), Id("k"), Int(7), Int(0) })
            .iadd(Id("v"), Id("v"), Id("k"))
            .ret(Id("v"));
        s.function("Make").op(Op::ArrayCreate, { Id("list"), Int(4) }).ret(Nil());
        s.function("Concat", "String")
            .local("a", "String")
            .op(Op::StrCat, { Id("a"), Id("text"), Str("!") })
            .ret(Id("a"));
        // A state's functions last: a chosen state stays chosen for the functions that follow.
        s.state("Open").function("Mode", "Int").ret(Int(1));
        return s;
    }

    struct Fixture
    {
        QuietWorld mWorld;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime
            = ScriptPapyrus::createRuntime(mWorld, UnknownFunctionPolicy::Strict);
        InstanceId mInstance = 0;
        std::int32_t mNativeCalls = 0;

        explicit Fixture(Target target)
        {
            ScriptBuilder base("Base");
            base.function("Base", "Int").ret(Int(5));
            ScriptBuilder util("Util");
            util.function("Twice", "Int")
                .global()
                .param("x", "Int")
                .local("r", "Int")
                .op(Op::IMul, { Id("r"), Id("x"), Int(2) })
                .ret(Id("r"));
            ScriptBuilder game("Game");
            game.function("Add", "Int").global().native().param("a", "Int").param("b", "Int");
            EXPECT_TRUE(load(*mRuntime, base, target));
            EXPECT_TRUE(load(*mRuntime, util, target));
            EXPECT_TRUE(load(*mRuntime, game, target));
            EXPECT_TRUE(load(*mRuntime, workScript(), target));
            NativeDescriptor descriptor;
            descriptor.className = "Game";
            descriptor.functionName = "Add";
            descriptor.global = true;
            ErrorReport error;
            EXPECT_TRUE(mRuntime->registerNative(
                descriptor,
                [this](const NativeCall& call) {
                    ++mNativeCalls;
                    return NativeResult{ PapyrusValue::makeInt(call.arguments[0].integer + call.arguments[1].integer) };
                },
                error));
            AttachKey key;
            key.formId = 0x42;
            mInstance = mRuntime->attach(key, "Alloc");
            EXPECT_NE(mInstance, 0u);
            mRuntime->tick(0.0);
        }
    };

    TEST(PapyrusAllocation, a_warm_function_runs_without_any_allocation)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            Fixture setup(target);
            const CallResult warm = setup.mRuntime->call(setup.mInstance, "Work", {});
            ASSERT_TRUE(warm.ok) << warm.error.format();
            std::uint64_t instructions = 0;
            std::size_t allocations = 0;
            {
                TestSupport::AllocationCounter counter;
                const CallResult second = setup.mRuntime->call(setup.mInstance, "Work", {});
                allocations = counter.count();
                instructions = second.instructionsExecuted;
                EXPECT_TRUE(second.ok);
            }
            EXPECT_GT(instructions, 4000u) << "a long loop was run";
            EXPECT_EQ(allocations, 0u) << "the second run of a warm function must not allocate";
            EXPECT_GT(setup.mNativeCalls, 300) << "the registered native ran in both";
        }
    }

    TEST(PapyrusAllocation, dispatching_an_event_allocates_nothing_once_warm)
    {
        Fixture setup(Target::SkyrimSE);
        const std::vector<PapyrusValue> arguments{ PapyrusValue::makeInt(1) };
        for (int i = 0; i < 20; ++i)
        {
            setup.mRuntime->send(setup.mInstance, "OnPing", arguments);
            setup.mRuntime->tick(0.0);
        }
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 100; ++i)
            {
                setup.mRuntime->send(setup.mInstance, "OnPing", arguments);
                setup.mRuntime->tick(0.0);
            }
            allocations = counter.count();
        }
        EXPECT_EQ(allocations, 0u);
        PapyrusValue value;
        ASSERT_TRUE(setup.mRuntime->getVariable(setup.mInstance, "counter", value));
        EXPECT_EQ(value.integer, 120);
    }

    TEST(PapyrusAllocation, array_reads_and_writes_do_not_allocate_but_creation_and_strcat_do)
    {
        Fixture setup(Target::SkyrimSE);
        ASSERT_TRUE(setup.mRuntime->call(setup.mInstance, "Make", {}).ok);
        ASSERT_TRUE(setup.mRuntime->call(setup.mInstance, "ArrayWork", {}).ok);
        {
            TestSupport::AllocationCounter counter;
            const CallResult result = setup.mRuntime->call(setup.mInstance, "ArrayWork", {});
            EXPECT_EQ(counter.count(), 0u);
            EXPECT_EQ(result.returned.integer, 9);
        }
        {
            TestSupport::AllocationCounter counter;
            setup.mRuntime->call(setup.mInstance, "Concat", {});
            EXPECT_GT(counter.count(), 0u) << "strcat takes memory from the VM heap: the counter is live";
        }
        {
            TestSupport::AllocationCounter counter;
            setup.mRuntime->call(setup.mInstance, "Make", {});
            EXPECT_GT(counter.count(), 0u) << "array creation takes memory from the VM heap";
        }
    }
}
