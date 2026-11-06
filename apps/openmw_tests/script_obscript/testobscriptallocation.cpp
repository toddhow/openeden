// Task 0092 stage d: the ObScript instruction loop allocates nothing.
//
// In plain words: running a script block, once the runtime has seen it once, must not take memory from the heap while
// it runs (arithmetic, comparisons, loops, calls to a registered function and reads and writes of locals and globals).
// The count comes from the allocation counter that openmw-tests already has
// (apps/openmw_tests/support/allocationcounter.hpp: the program's one replacement of operator new reads it). The first
// run of a block is a warm-up: the runtime's number stack grows to the size the script needs and keeps it.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <openeden/script/obscript.hpp>

#include <components/script_obscript/obscriptruntime.hpp>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace openeden::script;

    class QuietWorld : public ObScriptWorld
    {
    public:
        bool resolveForm(std::string_view editorId, FormId& form) const override
        {
            if (editorId == "player" || editorId == "PLAYER")
            {
                form = 7;
                return true;
            }
            return false;
        }
        bool findVariable(FormId, std::string_view, ValueKind&) const override { return false; }
        Value getVariable(FormId, std::string_view) const override { return {}; }
        void setVariable(FormId, std::string_view, Value) override {}
        ValueKind globalKind(std::string_view name) const override
        {
            return name == "counter" ? ValueKind::Integer : ValueKind::String;
        }
        Value getGlobal(std::string_view) const override { return Value::makeInteger(mGlobal); }
        void setGlobal(std::string_view, Value value) override { mGlobal = value.integer; }
        void report(std::string_view) override {}

        std::int32_t mGlobal = 0;
    };

    const char* const sLoop = R"(ScriptName Alloc
short i
long total
float f
Begin GameMode
    Set i to 0
    Set total to 0
    While ( i < 500 )
        Set i to i + 1
        If i > 250
            Set total to total + i * 2
        Else
            Set total to total - 1
        EndIf
        Set f to f + 0.5
        Set counter to counter + 1
        player.Poke i
    EndWhile
End
)";

    TEST(ObScriptAllocation, a_warm_block_runs_without_any_allocation)
    {
        QuietWorld world;
        auto runtime = ScriptObscript::createRuntime(world, UnknownFunctionPolicy::Strict);

        std::int32_t pokes = 0;
        FunctionDescriptor poke;
        poke.name = "Poke";
        poke.signature = "l";
        ErrorReport error;
        ASSERT_TRUE(runtime->registerFunction(
            poke,
            [&pokes](const CallArgs& args) {
                pokes += args.arguments.empty() ? 0 : args.arguments[0].integer;
                return Value::makeInteger(0);
            },
            error))
            << error.format();

        ScriptSource source;
        source.name = "Alloc";
        source.text = sLoop;
        const CompileResult compiled = runtime->compile(source);
        ASSERT_TRUE(compiled.ok) << (compiled.reports.empty() ? "" : compiled.reports.front().format());
        const InstanceId instance = runtime->createInstance(compiled.script, 7);

        // Warm-up: the first run may grow the number stack.
        Budget budget;
        budget.maxInstructions = 1000000;
        ASSERT_TRUE(runtime->run(instance, "GameMode", budget).ok);

        RunResult result;
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            result = runtime->run(instance, "GameMode", budget);
            allocations = counter.count();
        }
        EXPECT_TRUE(result.ok);
        EXPECT_GT(result.instructionsExecuted, 5000u);
        EXPECT_EQ(allocations, 0u);
        EXPECT_GT(pokes, 0);
    }
}
