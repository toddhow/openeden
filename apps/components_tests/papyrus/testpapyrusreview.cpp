// Task 0093, fixes of the fresh review of 2026-11-08 (items 2 and 3): the reader and the compile step must not repeat
// work on a long name for every operand, and one tick must not run an unbounded number of instructions.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "papyrustestutil.hpp"
#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using Clock = std::chrono::steady_clock;

    const std::string kShortName = "hostilelocal";

    // Ten functions at the per-function cap, every operand naming one local. Built with a short name; widenName then
    // rewrites the string table entry to 65,535 bytes (the assembler would copy a long name into every operand).
    ScriptBuilder hostileScript()
    {
        ScriptBuilder script("Hostile");
        for (int f = 0; f < 10; ++f)
        {
            script.function("F" + std::to_string(f)).local(kShortName, "Int");
            for (int i = 0; i < 32766; ++i)
            {
                script.assign(Id(kShortName), Id(kShortName));
            }
            script.ret(Nil());
        }
        return script;
    }

    // The string table entry is a 16 bit length (the file's byte order) and the bytes; nothing else in the file holds
    // an offset after the table that depends on its size (the object size field counts bytes after itself).
    std::vector<std::uint8_t> widenName(const std::vector<std::uint8_t>& bytes, bool bigEndian)
    {
        const std::uint8_t length = static_cast<std::uint8_t>(kShortName.size());
        std::vector<std::uint8_t> needle
            = bigEndian ? std::vector<std::uint8_t>{ 0, length } : std::vector<std::uint8_t>{ length, 0 };
        needle.insert(needle.end(), kShortName.begin(), kShortName.end());
        const auto at = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
        if (at == bytes.end())
        {
            ADD_FAILURE() << "name not found in the string table";
            return bytes;
        }
        std::vector<std::uint8_t> out(bytes.begin(), at);
        out.push_back(0xFF);
        out.push_back(0xFF);
        out.insert(out.end(), kShortName.begin(), kShortName.end());
        out.insert(out.end(), 65535 - kShortName.size(), static_cast<std::uint8_t>('a'));
        out.insert(out.end(), at + needle.size(), bytes.end());
        return out;
    }

    double secondsSince(Clock::time_point start)
    {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    TEST(PapyrusReview, the_reader_does_work_bounded_by_the_file_for_a_huge_name_used_by_many_operands)
    {
        const ScriptBuilder script = hostileScript();
        const Built built = script.build(optionsFor(Target::SkyrimSE));
        const std::vector<std::uint8_t> bytes = widenName(built.mBytes, true);
        const auto start = Clock::now();
        const ScriptPapyrus::Pex::ParseResult result = parseBytes(bytes);
        const double seconds = secondsSince(start);
        EXPECT_TRUE(result.mOk) << messages(result);
        EXPECT_LT(seconds, 2.0) << "the reader lowered the whole name for every operand";
    }

    TEST(PapyrusReview, load_and_attach_do_work_bounded_by_the_file_for_a_huge_name_used_by_many_operands)
    {
        Vm vm;
        const ScriptBuilder script = hostileScript();
        const Built built = script.build(optionsFor(Target::SkyrimSE));
        const std::vector<std::uint8_t> bytes = widenName(built.mBytes, true);
        const auto start = Clock::now();
        ASSERT_TRUE(vm.rt().loadScript(bytes, "Hostile").ok);
        const InstanceId id = vm.make("Hostile");
        EXPECT_NE(id, 0u);
        const double seconds = secondsSince(start);
        EXPECT_LT(seconds, 3.0) << "the compile step hashed the whole name for every operand";
    }

    // ---- the budget of a whole tick

    ScriptBuilder spinnerScript()
    {
        ScriptBuilder s("Spinner");
        s.var("done", "Int");
        s.function("OnSpin").op(Op::Jmp, { Int(0) }).ret(Nil());
        s.function("OnFine").iadd(Id("done"), Id("done"), Int(1)).ret(Nil());
        return s;
    }

    TEST(PapyrusReview, one_tick_stops_starting_stacks_when_its_instruction_budget_is_spent)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxInstructionsPerTick = 3000;
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(spinnerScript()));
        const InstanceId id = vm.make("Spinner");
        vm.tick();
        for (int i = 0; i < 10; ++i)
        {
            vm.rt().send(id, "OnSpin", {});
        }
        vm.rt().send(id, "OnFine", {});
        Budget budget;
        budget.maxInstructions = 1000;
        const TickResult first = vm.rt().tick(0.0, budget);
        EXPECT_EQ(first.stacksRun, 3u);
        EXPECT_LE(first.instructionsExecuted, 3000u);
        EXPECT_EQ(first.stacksWaiting, 8u) << "the unfinished stacks stay queued";
        EXPECT_EQ(vm.varInt(id, "done"), 0);
        for (int i = 0; i < 10; ++i)
        {
            if (vm.rt().tick(0.0, budget).stacksWaiting == 0)
            {
                break;
            }
        }
        EXPECT_EQ(vm.varInt(id, "done"), 1) << "the queued event ran in a later tick";
    }

    TEST(PapyrusReview, the_default_tick_budget_does_not_change_an_ordinary_tick)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(spinnerScript()));
        const InstanceId id = vm.make("Spinner");
        vm.tick();
        for (int i = 0; i < 50; ++i)
        {
            vm.rt().send(id, "OnFine", {});
        }
        const TickResult tick = vm.tick();
        EXPECT_EQ(tick.stacksRun, 50u);
        EXPECT_EQ(tick.stacksWaiting, 0u);
        EXPECT_EQ(vm.varInt(id, "done"), 50);
    }
}
