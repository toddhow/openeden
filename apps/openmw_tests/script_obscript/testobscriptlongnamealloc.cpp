// Task 0092, review fix 6: explicit references with long editor ids allocate nothing while a script runs.
//
// In plain words: "ref.Function" and a reference argument look a name up in the world on every call. A name of 15
// characters or fewer fits in a std::string without the heap, so the original allocation test (which used "player")
// could not see a copy of a longer name. This test uses an editor id of 41 characters. The count comes from the
// allocation counter that openmw-tests already has.

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include <openeden/script/obscript.hpp>

#include <components/script_obscript/obscriptruntime.hpp>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace openeden::script;

    constexpr std::string_view kLongId = "TheVeryLongEditorIdOfAnObjectInTheWorld01";
    constexpr FormId kLongForm = 0x4242;

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

    class LongNameWorld : public ObScriptWorld
    {
    public:
        bool resolveForm(std::string_view editorId, FormId& form) const override
        {
            if (sameName(editorId,
                    "thevery"
                    "longeditoridofanobjectintheworld01"))
            {
                form = kLongForm;
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

    const char* const sScript = R"(ScriptName LongAlloc
short i
Begin GameMode
    Set i to 0
    While ( i < 200 )
        Set i to i + 1
        TheVeryLongEditorIdOfAnObjectInTheWorld01.Poke i
        PokeRef TheVeryLongEditorIdOfAnObjectInTheWorld01
    EndWhile
End
)";

    TEST(ObScriptAllocation, explicit_references_and_reference_arguments_with_long_names_allocate_nothing)
    {
        LongNameWorld world;
        auto runtime = ScriptObscript::createRuntime(world, UnknownFunctionPolicy::Strict);

        std::int32_t pokes = 0;
        FormId lastReference = 0;
        FunctionDescriptor poke;
        poke.name = "Poke";
        poke.signature = "l";
        ErrorReport error;
        ASSERT_TRUE(runtime->registerFunction(
            poke,
            [&](const CallArgs& args) {
                pokes += 1;
                lastReference = args.reference;
                return Value::makeInteger(0);
            },
            error))
            << error.format();
        FunctionDescriptor pokeRef;
        pokeRef.name = "PokeRef";
        pokeRef.signature = "r";
        ASSERT_TRUE(runtime->registerFunction(
            pokeRef,
            [&](const CallArgs& args) {
                pokes += 1;
                lastReference = args.arguments.empty() ? 0 : static_cast<FormId>(args.arguments[0].integer);
                return Value::makeInteger(0);
            },
            error))
            << error.format();

        ScriptSource source;
        source.name = "LongAlloc";
        source.text = sScript;
        const CompileResult compiled = runtime->compile(source);
        ASSERT_TRUE(compiled.ok) << (compiled.reports.empty() ? "" : compiled.reports.front().format());
        const InstanceId instance = runtime->createInstance(compiled.script, 7);

        Budget budget;
        budget.maxInstructions = 1000000;
        ASSERT_TRUE(runtime->run(instance, "GameMode", budget).ok);
        EXPECT_EQ(lastReference, kLongForm);

        RunResult result;
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            result = runtime->run(instance, "GameMode", budget);
            allocations = counter.count();
        }
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(allocations, 0u);
        EXPECT_GT(pokes, 400);
    }
}
