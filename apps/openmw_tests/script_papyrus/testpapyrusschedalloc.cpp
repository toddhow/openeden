// Task 0093 stage d: the scheduler allocates nothing once warm.
//
// In plain words: a tick that wakes parked stacks, runs them to their next latent call and parks them again must not
// take memory from the heap once the runtime has done it a few times (the heap of timers, the list of due stacks and
// the stacks themselves keep the capacity they grew to). The count comes from the allocation counter that
// openmw-tests already has (apps/openmw_tests/support/allocationcounter.hpp). A probe that does allocate shows the
// counter is live.

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
        void log(Severity, std::string_view) override {}
    };

    bool load(PapyrusContract& runtime, const ScriptBuilder& script, Target target)
    {
        Options options;
        options.mTarget = target;
        return runtime.loadScript(script.build(options).mBytes, script.name()).ok;
    }

    // OnInit loops for ever: count one, wait a quarter second, again.
    ScriptBuilder looper()
    {
        ScriptBuilder s("Looper");
        s.var("counter", "Int");
        s.function("OnInit")
            .local("n", "None")
            .iadd(Id("counter"), Id("counter"), Int(1))
            .callStatic("Game", "Wait", Id("n"), { Flt(0.25f) })
            .jmp(-2)
            .ret(Nil());
        return s;
    }

    TEST(PapyrusSchedAllocation, waking_and_parking_stacks_allocates_nothing_once_warm)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            QuietWorld world;
            ScriptPapyrus::RuntimeOptions options;
            options.runLatentNatives = true;
            std::unique_ptr<ScriptPapyrus::PapyrusRuntime> runtime
                = ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Strict, options);
            ScriptBuilder game("Game");
            game.function("Wait").global().native().param("seconds", "Float");
            ASSERT_TRUE(load(*runtime, game, target));
            ASSERT_TRUE(load(*runtime, looper(), target));
            NativeDescriptor wait;
            wait.className = "Game";
            wait.functionName = "Wait";
            wait.global = true;
            wait.latent = true;
            ErrorReport error;
            ASSERT_TRUE(runtime->registerNative(
                wait,
                [](const NativeCall& call) {
                    NativeResult result;
                    result.waitSeconds = call.arguments[0].number;
                    return result;
                },
                error))
                << error.format();
            std::vector<InstanceId> ids;
            for (std::uint32_t i = 0; i < 50; ++i)
            {
                AttachKey key;
                key.formId = 0x100 + i;
                ids.push_back(runtime->attach(key, "Looper"));
                ASSERT_NE(ids.back(), 0u);
            }
            double time = 0.0;
            for (int i = 0; i < 30; ++i)
            {
                time += 0.25;
                runtime->tick(time);
            }
            ASSERT_EQ(runtime->suspendedStacks(), 50u);
            std::size_t allocations = 0;
            std::uint64_t woken = 0;
            {
                TestSupport::AllocationCounter counter;
                for (int i = 0; i < 200; ++i)
                {
                    time += 0.25;
                    const TickResult tick = runtime->tick(time);
                    woken += tick.stacksRun;
                }
                allocations = counter.count();
            }
            EXPECT_EQ(allocations, 0u) << "waking, running and parking 50 stacks 200 times";
            EXPECT_EQ(woken, 50u * 200u);
            PapyrusValue value;
            ASSERT_TRUE(runtime->getVariable(ids[7], "counter", value));
            EXPECT_EQ(value.integer, 230);

            // The counter is live: a vector that grows is counted.
            std::size_t probe = 0;
            {
                TestSupport::AllocationCounter counter;
                std::vector<int> grows;
                for (int i = 0; i < 100; ++i)
                {
                    grows.push_back(i);
                }
                probe = counter.count();
            }
            EXPECT_GT(probe, 0u);
        }
    }
}
