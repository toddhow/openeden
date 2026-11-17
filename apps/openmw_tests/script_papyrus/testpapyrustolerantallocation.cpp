// Task 0273b (review finding 4): the tolerant native path allocates nothing once warm.
//
// In plain words: the real Skyrim scripts call natives the game pack has not written yet, thousands of times a
// scenario. A tolerant runtime answers them with the zero of their type; it must not build a name string and insert
// into a map on every call. Only the FIRST call of each such native may allocate (the name is kept for the report and
// the one log line). The same holds for a latent native the runtime skips.

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

    struct Fixture
    {
        QuietWorld mWorld;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime
            = ScriptPapyrus::createRuntime(mWorld, UnknownFunctionPolicy::Tolerant);
        InstanceId mInstance = 0;

        Fixture()
        {
            Options options;
            options.mTarget = Target::SkyrimSE;
            ScriptBuilder game("Game");
            game.function("Unknown", "Int").global().native().param("a", "Int");
            game.function("Sleepy", "Int").global().native().param("a", "Int");
            EXPECT_TRUE(mRuntime->loadScript(game.build(options).mBytes, game.name()).ok);
            ScriptBuilder s("Tol");
            s.var("counter", "Int");
            s.function("Work", "Int")
                .local("i", "Int")
                .local("c", "Bool")
                .local("r", "Int")
                .assign(Id("i"), Int(0)) // 0
                .op(Op::CompareLt, { Id("c"), Id("i"), Int(200) }) // 1
                .jmpf(Id("c"), 6) // 2 -> 8
                .iadd(Id("i"), Id("i"), Int(1)) // 3
                .callStatic("Game", "Unknown", Id("r"), { Id("i") }) // 4
                .callStatic("Game", "Sleepy", Id("r"), { Id("i") }) // 5
                .assign(Id("counter"), Id("i")) // 6
                .jmp(-6) // 7 -> 1
                .ret(Id("counter")); // 8
            EXPECT_TRUE(mRuntime->loadScript(s.build(options).mBytes, s.name()).ok);
            NativeDescriptor sleepy;
            sleepy.className = "Game";
            sleepy.functionName = "Sleepy";
            sleepy.global = true;
            sleepy.latent = true; // skipped while the runtime does not run latent natives
            ErrorReport error;
            EXPECT_TRUE(mRuntime->registerNative(
                sleepy, [](const NativeCall&) { return NativeResult{ PapyrusValue::makeInt(0) }; }, error));
            AttachKey key;
            key.formId = 0x42;
            mInstance = mRuntime->attach(key, "Tol");
            EXPECT_NE(mInstance, 0u);
            mRuntime->tick(0.0);
        }
    };

    TEST(PapyrusTolerantAllocation, an_unregistered_and_a_skipped_latent_native_allocate_nothing_once_warm)
    {
        Fixture setup;
        const CallResult warm = setup.mRuntime->call(setup.mInstance, "Work", {});
        ASSERT_TRUE(warm.ok) << warm.error.format();
        const std::uint64_t unregisteredBefore = setup.mRuntime->stats().unregisteredNativeCalls;
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            const CallResult second = setup.mRuntime->call(setup.mInstance, "Work", {});
            allocations = counter.count();
            EXPECT_TRUE(second.ok);
        }
        EXPECT_EQ(allocations, 0u) << "400 tolerant native calls must not allocate once the first of each has run";
        const auto& stats = setup.mRuntime->stats();
        EXPECT_EQ(stats.unregisteredNativeCalls - unregisteredBefore, 200u);
        ASSERT_EQ(stats.unregisteredByName.count("Game.Unknown"), 1u) << "the ranking still names the native";
        EXPECT_EQ(stats.unregisteredByName.at("Game.Unknown"), 400u);
        EXPECT_EQ(stats.latentSkipped, 400u);
    }
}
