// Task 0092 items 1 and 2: forms as values and declared functions allocate nothing while a script runs.
//
// In plain words: after a first run (which may grow the runtime's number stack and gives each declared function its one
// notice), running a block that reads form values by name, compares them and calls declared functions must not take
// memory from the heap. The world here answers a form's name without allocating (a contract rule: the runtime asks it
// every time such a line runs). The count comes from the allocation counter that openmw-tests already has.

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include <openeden/script/obscript.hpp>

#include <components/script_obscript/obscriptruntime.hpp>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace openeden::script;

    bool sameName(std::string_view given, std::string_view lowerName)
    {
        if (given.size() != lowerName.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < given.size(); ++i)
        {
            const char c = given[i] >= 'A' && given[i] <= 'Z' ? static_cast<char>(given[i] - 'A' + 'a') : given[i];
            if (c != lowerName[i])
            {
                return false;
            }
        }
        return true;
    }

    class NoAllocationWorld : public ObScriptWorld
    {
    public:
        bool resolveForm(std::string_view editorId, FormId& form) const override
        {
            if (sameName(editorId, "crate"))
            {
                form = 0x300;
                return true;
            }
            if (sameName(editorId, "player"))
            {
                form = 7;
                return true;
            }
            return false;
        }
        bool findVariable(FormId, std::string_view, ValueKind&) const override { return false; }
        Value getVariable(FormId, std::string_view) const override { return {}; }
        void setVariable(FormId, std::string_view, Value) override {}
        ValueKind globalKind(std::string_view) const override { return ValueKind::String; }
        Value getGlobal(std::string_view) const override { return {}; }
        void setGlobal(std::string_view, Value) override {}
        void report(std::string_view) override {}
    };

    const char* const sScript = R"(ScriptName AllocForms
short i
short hits
ref target
Begin GameMode
    Set i to 0
    Set hits to 0
    While ( i < 300 )
        Set i to i + 1
        Set target to Crate
        If ( target == Crate )
            Set hits to hits + 1
        EndIf
        If ( target != Player )
            Set hits to hits + 1
        EndIf
        Frobnicate Crate i
        Set hits to hits + GetThingCount Crate
    EndWhile
End
)";

    TEST(ObScriptFormAllocation, a_warm_block_with_form_values_and_declared_functions_allocates_nothing)
    {
        NoAllocationWorld world;
        auto runtime = ScriptObscript::createRuntime(world, UnknownFunctionPolicy::Strict);

        ErrorReport error;
        FunctionDescriptor frobnicate;
        frobnicate.name = "Frobnicate";
        frobnicate.signature = "rl/";
        ASSERT_TRUE(runtime->declareFunction(frobnicate, error)) << error.format();
        FunctionDescriptor count;
        count.name = "GetThingCount";
        count.signature = "r/";
        ASSERT_TRUE(runtime->declareFunction(count, error)) << error.format();

        ScriptSource source;
        source.name = "AllocForms";
        source.text = sScript;
        const CompileResult compiled = runtime->compile(source);
        ASSERT_TRUE(compiled.ok) << (compiled.reports.empty() ? "" : compiled.reports.front().format());
        const InstanceId instance = runtime->createInstance(compiled.script, 7);

        Budget budget;
        budget.maxInstructions = 1000000;
        ASSERT_TRUE(runtime->run(instance, "GameMode", budget).ok); // warm-up

        RunResult result;
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            result = runtime->run(instance, "GameMode", budget);
            allocations = counter.count();
        }
        EXPECT_TRUE(result.ok);
        EXPECT_GT(result.instructionsExecuted, 3000u);
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(runtime->getLocal(instance, "hits").integer, 600);
    }
}
