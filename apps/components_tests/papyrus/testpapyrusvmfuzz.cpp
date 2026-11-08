// Task 0093 stage c: random programs on the VM. Every program is a structurally valid generated ".pex" (the reader
// accepts it) whose instructions use operands of any kind in any place: the VM must neither crash nor hang, must end
// every stack with a value or a located error, and must give the same outcome when the same program runs again.

#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;

    class Random
    {
    public:
        explicit Random(std::uint64_t seed)
            : mState(seed * 6364136223846793005ull + 1442695040888963407ull)
        {
        }
        std::uint32_t next()
        {
            mState = mState * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(mState >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return next() % n; }
        template <class T>
        const T& pick(const std::vector<T>& items)
        {
            return items[below(static_cast<std::uint32_t>(items.size()))];
        }

    private:
        std::uint64_t mState;
    };

    ScriptBuilder randomScript(Random& random, Target target)
    {
        const bool fo4 = target == Target::Fallout4;
        ScriptBuilder s("Fz");
        s.var("vi", "Int", Int(3)).var("vf", "Float", Flt(2.0f)).var("vb", "Bool").var("vs", "String", Str("s"));
        s.var("vo", "Fz").var("va", "Int[]");
        s.autoProperty("P", "Int", Int(1));
        if (fo4)
        {
            s.structDef("Pt").member("x", "Float", Flt(1.0f)).member("y", "Int");
            s.var("vt", "Fz#Pt");
        }
        s.function("F1", "Int").param("a", "Int").ret(Id("a"));
        s.function("GotoState").param("newState", "String").assign(Id("::State"), Id("newState")).ret(Nil());

        std::vector<std::string> locals = { "li", "lf", "lb", "ls", "lo", "la", "lr" };
        std::vector<std::string> places
            = { "li", "lf", "lb", "ls", "lo", "la", "lr", "vi", "vf", "vb", "vs", "vo", "va" };
        if (fo4)
        {
            locals.push_back("lt");
            locals.push_back("lta");
            places.push_back("lt");
            places.push_back("lta");
            places.push_back("vt");
        }
        std::vector<std::string> arrays = { "la", "lr", "va" };
        if (fo4)
        {
            arrays.push_back("lta");
        }

        auto& f = s.function("Main", "Int")
                      .local("li", "Int")
                      .local("lf", "Float")
                      .local("lb", "Bool")
                      .local("ls", "String")
                      .local("lo", "Fz")
                      .local("la", "Int[]")
                      .local("lr", "String[]");
        if (fo4)
        {
            f.local("lt", "Fz#Pt").local("lta", "Fz#Pt[]");
        }

        auto value = [&]() -> Operand {
            switch (random.below(10))
            {
                case 0:
                    return Int(static_cast<std::int32_t>(random.below(9)) - 2);
                case 1:
                    return Flt(static_cast<float>(random.below(7)) - 1.5f);
                case 2:
                    return Bool(random.below(2) == 0);
                case 3:
                    return Str(random.below(2) == 0 ? "" : "txt");
                case 4:
                    return Nil();
                case 5:
                    return Id("self");
                default:
                    return Id(random.pick(places));
            }
        };
        auto place = [&]() -> Operand {
            if (random.below(40) == 0)
            {
                return Id("::State");
            }
            return Id(random.pick(places));
        };
        auto array = [&]() -> Operand { return random.below(8) == 0 ? value() : Id(random.pick(arrays)); };
        auto index = [&]() -> Operand {
            return random.below(2) == 0 ? Int(static_cast<std::int32_t>(random.below(7)) - 1) : value();
        };
        const std::vector<std::string> functionNames = { "F1", "Main", "Nothing", "OnX", "GotoState", "P" };

        const std::uint32_t count = 4 + random.below(36);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const int remaining = static_cast<int>(count) - static_cast<int>(i); // instructions from here to the end
            const std::uint32_t kind = random.below(fo4 ? 48 : 36);
            switch (kind)
            {
                case 0:
                    f.nop();
                    break;
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                    f.op(static_cast<std::uint8_t>(kind), { place(), value(), value() });
                    break;
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                    f.op(static_cast<std::uint8_t>(kind), { place(), value() });
                    break;
                case 15:
                case 16:
                case 17:
                case 18:
                case 19:
                    f.op(static_cast<std::uint8_t>(kind), { place(), value(), value() });
                    break;
                case 20:
                    f.jmp(static_cast<std::int32_t>(random.below(static_cast<std::uint32_t>(remaining) + i + 1))
                        - static_cast<std::int32_t>(i));
                    break;
                case 21:
                    f.jmpt(value(),
                        static_cast<std::int32_t>(random.below(static_cast<std::uint32_t>(remaining) + i + 1))
                            - static_cast<std::int32_t>(i));
                    break;
                case 22:
                    f.jmpf(value(),
                        static_cast<std::int32_t>(random.below(static_cast<std::uint32_t>(remaining) + i + 1))
                            - static_cast<std::int32_t>(i));
                    break;
                case 23:
                {
                    std::vector<Operand> args;
                    for (std::uint32_t k = random.below(3); k > 0; --k)
                    {
                        args.push_back(value());
                    }
                    f.callMethod(random.pick(functionNames), value(), place(), std::move(args));
                    break;
                }
                case 24:
                    f.callParent(random.pick(functionNames), place(), { value() });
                    break;
                case 25:
                    f.callStatic(
                        random.below(3) == 0 ? "Nowhere" : "Fz", random.pick(functionNames), place(), { value() });
                    break;
                case 26:
                    f.ret(value());
                    break;
                case 27:
                    f.op(27, { place(), value(), value() });
                    break;
                case 28:
                    f.op(28, { Str(random.below(3) == 0 ? "Q" : "P"), value(), place() });
                    break;
                case 29:
                    f.op(29, { Str(random.below(3) == 0 ? "Q" : "P"), value(), value() });
                    break;
                case 30:
                    f.op(30, { Id(random.pick(arrays)), index() });
                    break;
                case 31:
                    f.op(31, { place(), array() });
                    break;
                case 32:
                    f.op(32, { place(), array(), index() });
                    break;
                case 33:
                    f.op(33, { array(), index(), value() });
                    break;
                case 34:
                case 35:
                    f.op(static_cast<std::uint8_t>(kind), { array(), place(), value(), index() });
                    break;
                case 36:
                    f.op(36, { place(), value(), Str(random.below(2) == 0 ? "Fz" : "Other") });
                    break;
                case 37:
                    f.op(37, { Id(random.below(3) == 0 ? random.pick(places) : "lt") });
                    break;
                case 38:
                    f.op(38, { place(), value(), Str(random.below(2) == 0 ? "x" : "z") });
                    break;
                case 39:
                    f.op(39, { value(), Str(random.below(2) == 0 ? "y" : "z"), value() });
                    break;
                case 40:
                case 41:
                    f.op(static_cast<std::uint8_t>(kind), { array(), place(), Str("y"), value(), index() });
                    break;
                case 42:
                case 43:
                    f.op(static_cast<std::uint8_t>(kind), { array(), value(), index() });
                    break;
                case 44:
                case 46:
                    f.op(static_cast<std::uint8_t>(kind), { array() });
                    break;
                case 45:
                    f.op(45, { array(), index(), index() });
                    break;
                default:
                    f.op(47, { array(), Id(random.pick(arrays)), Str("y"), value() });
                    break;
            }
        }
        f.ret(Int(0));
        return s;
    }

    struct Outcome
    {
        bool mLoaded = false;
        bool mOk = false;
        std::string mError;
        std::uint64_t mInstructions = 0;
        std::string mState;
        std::int32_t mVi = 0;
    };

    Outcome runProgram(std::uint64_t seed, Target target)
    {
        Random random(seed);
        const ScriptBuilder script = randomScript(random, target);
        ScriptPapyrus::RuntimeOptions options;
        options.defaultBudget = { 3000, 12 };
        options.maxHeapBytes = 1u << 18;
        Vm vm(target, seed % 2 == 0 ? UnknownFunctionPolicy::Tolerant : UnknownFunctionPolicy::Strict, options);
        Outcome outcome;
        const LoadResult loaded = vm.rt().loadScript(script.build(optionsFor(target)).mBytes, script.name());
        if (!loaded.ok)
        {
            return outcome;
        }
        outcome.mLoaded = true;
        AttachKey key;
        key.formId = 9;
        const InstanceId id = vm.rt().attach(key, "Fz");
        if (id == 0)
        {
            outcome.mError = "attach refused";
            return outcome;
        }
        vm.tick();
        const CallResult result = vm.call(id, "Main");
        outcome.mOk = result.ok;
        outcome.mError = result.error.format();
        outcome.mInstructions = result.instructionsExecuted;
        outcome.mState = vm.rt().stateOf(id);
        PapyrusValue value;
        if (vm.rt().getVariable(id, "vi", value))
        {
            outcome.mVi = value.integer;
        }
        // The runtime must be usable afterwards.
        const CallResult again = vm.call(id, "F1", { PapyrusValue::makeInt(5) });
        EXPECT_TRUE(again.ok) << again.error.format();
        return outcome;
    }

    TEST(PapyrusVmFuzz, random_programs_end_cleanly_and_repeat_exactly)
    {
        for (const Target target : kBothTargets)
        {
            std::size_t loaded = 0;
            std::size_t ok = 0;
            std::size_t failed = 0;
            std::map<std::string, std::size_t> tags;
            const std::uint64_t base = target == Target::Fallout4 ? 100000 : 0;
            for (std::uint64_t seed = 1; seed <= 3000; ++seed)
            {
                const Outcome first = runProgram(base + seed, target);
                if (!first.mLoaded)
                {
                    continue;
                }
                ++loaded;
                if (first.mOk)
                {
                    ++ok;
                }
                else
                {
                    ++failed;
                    const std::size_t open = first.mError.find('[');
                    const std::size_t close = first.mError.find(']', open);
                    ++tags[open == std::string::npos ? "?" : first.mError.substr(open + 1, close - open - 1)];
                    EXPECT_NE(first.mError.find('['), std::string::npos) << "seed " << seed << ": " << first.mError;
                }
                EXPECT_LE(first.mInstructions, 3000u) << "seed " << seed;
                if (seed % 5 == 0)
                {
                    const Outcome second = runProgram(base + seed, target);
                    EXPECT_EQ(first.mOk, second.mOk) << "seed " << seed;
                    EXPECT_EQ(first.mError, second.mError) << "seed " << seed;
                    EXPECT_EQ(first.mInstructions, second.mInstructions) << "seed " << seed;
                    EXPECT_EQ(first.mState, second.mState) << "seed " << seed;
                    EXPECT_EQ(first.mVi, second.mVi) << "seed " << seed;
                }
            }
            std::cout << (target == Target::Fallout4 ? "Fallout 4" : "Skyrim SE") << ": " << loaded
                      << " programs loaded, " << ok << " ran to the end, " << failed << " ended with an error:";
            for (const auto& [tag, n] : tags)
            {
                std::cout << " [" << tag << "] " << n;
            }
            std::cout << "\n";
            EXPECT_GT(loaded, 1500u) << "most generated programs must load";
            EXPECT_GT(ok, 100u) << "some programs must run to the end";
            EXPECT_GT(failed, 100u) << "and some must fail";
        }
    }
}
