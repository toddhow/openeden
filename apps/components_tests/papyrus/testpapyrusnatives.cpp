// Task 0093, native support: the VM-pure natives (Math, Utility.RandomInt/RandomFloat/Wait/WaitMenuMode, the Debug
// trace and message functions). Written from the header's contract before the implementation and run red against a
// stub first. Every script is generated; no game asset anywhere. The expected values of the Math functions are the
// mathematical ones in DEGREES (RECALLED-VERIFY for the engine; the header says so).

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <set>

#include <components/script_papyrus/papyrusnatives.hpp>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using ScriptPapyrus::PureMath::Float;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kMax = std::numeric_limits<float>::max();

    // ---- the pure functions ---------------------------------------------------------------------------------------

    TEST(PapyrusPureMath, conversions_clamp_and_flag_the_undefined)
    {
        EXPECT_EQ(ScriptPapyrus::PureMath::toInt(2.7), 2);
        EXPECT_EQ(ScriptPapyrus::PureMath::toInt(-2.7), -2);
        EXPECT_EQ(ScriptPapyrus::PureMath::toInt(1.0e12), std::numeric_limits<std::int32_t>::max());
        EXPECT_EQ(ScriptPapyrus::PureMath::toInt(-1.0e12), std::numeric_limits<std::int32_t>::min());
        EXPECT_EQ(ScriptPapyrus::PureMath::toInt(std::numeric_limits<double>::quiet_NaN()), 0);
        Float nan = ScriptPapyrus::PureMath::toFloat(std::numeric_limits<double>::quiet_NaN());
        EXPECT_EQ(nan.value, 0.0f);
        EXPECT_TRUE(nan.domainError);
        Float big = ScriptPapyrus::PureMath::toFloat(std::numeric_limits<double>::infinity());
        EXPECT_EQ(big.value, kMax);
        EXPECT_TRUE(big.domainError);
        Float small = ScriptPapyrus::PureMath::toFloat(-std::numeric_limits<double>::infinity());
        EXPECT_EQ(small.value, -kMax);
        Float fine = ScriptPapyrus::PureMath::toFloat(1.5);
        EXPECT_EQ(fine.value, 1.5f);
        EXPECT_FALSE(fine.domainError);
    }

    TEST(PapyrusPureMath, the_trigonometry_works_in_degrees)
    {
        namespace M = ScriptPapyrus::PureMath;
        EXPECT_NEAR(M::sin(90.0f).value, 1.0f, 1e-6f);
        EXPECT_NEAR(M::sin(30.0f).value, 0.5f, 1e-6f);
        EXPECT_NEAR(M::cos(60.0f).value, 0.5f, 1e-6f);
        EXPECT_NEAR(M::cos(180.0f).value, -1.0f, 1e-6f);
        EXPECT_NEAR(M::tan(45.0f).value, 1.0f, 1e-6f);
        EXPECT_NEAR(M::asin(1.0f).value, 90.0f, 1e-4f);
        EXPECT_NEAR(M::acos(0.0f).value, 90.0f, 1e-4f);
        EXPECT_NEAR(M::atan(1.0f).value, 45.0f, 1e-4f);
        EXPECT_NEAR(M::degreesToRadians(180.0f).value, 3.14159265f, 1e-6f);
        EXPECT_NEAR(M::radiansToDegrees(3.14159265f).value, 180.0f, 1e-4f);
    }

    TEST(PapyrusPureMath, undefined_inputs_give_zero_and_a_flag_never_a_nan)
    {
        namespace M = ScriptPapyrus::PureMath;
        EXPECT_TRUE(M::abs(kNaN).domainError);
        EXPECT_EQ(M::abs(kNaN).value, 0.0f);
        EXPECT_EQ(M::abs(-kInf).value, kMax);
        EXPECT_EQ(M::abs(-2.5f).value, 2.5f);
        EXPECT_TRUE(M::sqrt(-1.0f).domainError);
        EXPECT_EQ(M::sqrt(-1.0f).value, 0.0f);
        EXPECT_FALSE(M::sqrt(0.0f).domainError);
        EXPECT_NEAR(M::sqrt(16.0f).value, 4.0f, 1e-6f);
        EXPECT_TRUE(M::asin(1.5f).domainError);
        EXPECT_EQ(M::asin(1.5f).value, 0.0f);
        EXPECT_TRUE(M::acos(-1.5f).domainError);
        EXPECT_TRUE(M::pow(-8.0f, 0.5f).domainError);
        EXPECT_EQ(M::pow(-8.0f, 0.5f).value, 0.0f);
        EXPECT_NEAR(M::pow(2.0f, 10.0f).value, 1024.0f, 1e-3f);
        EXPECT_NEAR(M::pow(-2.0f, 3.0f).value, -8.0f, 1e-5f);
        Float over = M::pow(10.0f, 100.0f);
        EXPECT_TRUE(over.domainError);
        EXPECT_EQ(over.value, kMax);
        EXPECT_TRUE(M::sin(kNaN).domainError);
        EXPECT_TRUE(M::sin(kInf).domainError);
    }

    TEST(PapyrusPureMath, every_function_returns_a_finite_float_for_any_input)
    {
        namespace M = ScriptPapyrus::PureMath;
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> small(-400.0f, 400.0f);
        const float specials[] = { 0.0f, -0.0f, 1.0f, -1.0f, kInf, -kInf, kNaN, kMax, -kMax, 1e-30f, 90.0f, 270.0f };
        std::vector<float> inputs(std::begin(specials), std::end(specials));
        for (int i = 0; i < 4000; ++i)
        {
            inputs.push_back(small(rng));
        }
        for (const float a : inputs)
        {
            for (const float b : { 0.0f, 0.5f, -3.0f, 100.0f, kNaN, kInf })
            {
                const Float results[] = { M::abs(a), M::sqrt(a), M::pow(a, b), M::sin(a), M::cos(a), M::tan(a),
                    M::asin(a), M::acos(a), M::atan(a), M::degreesToRadians(a), M::radiansToDegrees(a) };
                for (const Float& r : results)
                {
                    ASSERT_TRUE(std::isfinite(r.value)) << a << " " << b;
                }
            }
            (void)ScriptPapyrus::PureMath::toInt(a);
        }
    }

    TEST(PapyrusPureRandom, the_same_seed_gives_the_same_numbers_and_the_state_restores)
    {
        ScriptPapyrus::PureRandom a(42);
        ScriptPapyrus::PureRandom b(42);
        ScriptPapyrus::PureRandom c(43);
        bool differs = false;
        for (int i = 0; i < 100; ++i)
        {
            const std::uint64_t x = a.next();
            EXPECT_EQ(x, b.next());
            differs = differs || x != c.next();
        }
        EXPECT_TRUE(differs);
        const std::uint64_t saved = a.state();
        const std::uint64_t expected = a.next();
        a.setState(saved);
        EXPECT_EQ(a.next(), expected);
    }

    TEST(PapyrusPureRandom, integers_cover_both_ends_and_stay_inside)
    {
        ScriptPapyrus::PureRandom rng(1);
        std::set<int> seen;
        for (int i = 0; i < 2000; ++i)
        {
            const int v = rng.intBetween(-3, 4);
            ASSERT_GE(v, -3);
            ASSERT_LE(v, 4);
            seen.insert(v);
        }
        EXPECT_EQ(seen.size(), 8u);
        EXPECT_EQ(rng.intBetween(5, 5), 5);
        for (int i = 0; i < 200; ++i)
        {
            const int v = rng.intBetween(10, 7); // the bounds come in either order
            ASSERT_GE(v, 7);
            ASSERT_LE(v, 10);
        }
        // The whole int range does not overflow.
        for (int i = 0; i < 200; ++i)
        {
            (void)rng.intBetween(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        }
    }

    TEST(PapyrusPureRandom, floats_stay_in_the_half_open_range)
    {
        ScriptPapyrus::PureRandom rng(9);
        double sum = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const float v = rng.floatBetween(2.0f, 3.0f);
            ASSERT_GE(v, 2.0f);
            ASSERT_LT(v, 3.0f);
            sum += v;
        }
        EXPECT_NEAR(sum / 20000.0, 2.5, 0.02);
        EXPECT_EQ(rng.floatBetween(1.25f, 1.25f), 1.25f);
        const float swapped = rng.floatBetween(5.0f, 4.0f);
        EXPECT_GE(swapped, 4.0f);
        EXPECT_LE(swapped, 5.0f);
        // A tiny interval next to a large value must not round up to the bound.
        for (int i = 0; i < 5000; ++i)
        {
            const float v = rng.floatBetween(1.0e7f, std::nextafter(1.0e7f, 1.0e8f));
            ASSERT_GE(v, 1.0e7f);
            ASSERT_LT(v, std::nextafter(1.0e7f, 1.0e8f));
        }
    }

    // ---- registration against generated classes -------------------------------------------------------------------

    ScriptBuilder mathClass()
    {
        ScriptBuilder m("Math");
        m.function("Abs", "Float").global().native().param("afValue", "Float");
        m.function("Floor", "Int").global().native().param("afValue", "Float");
        m.function("Ceiling", "Int").global().native().param("afValue", "Float");
        m.function("Sqrt", "Float").global().native().param("afValue", "Float");
        m.function("Pow", "Float").global().native().param("x", "Float").param("y", "Float");
        for (const char* name : { "Sin", "Cos", "Tan", "Asin", "Acos", "Atan" })
        {
            m.function(name, "Float").global().native().param("afValue", "Float");
        }
        m.function("DegreesToRadians", "Float").global().native().param("afDegrees", "Float");
        m.function("RadiansToDegrees", "Float").global().native().param("afRadians", "Float");
        return m;
    }

    ScriptBuilder utilityClass()
    {
        ScriptBuilder u("Utility");
        u.function("RandomInt", "Int").global().native().param("aiMin", "Int").param("aiMax", "Int");
        u.function("RandomFloat", "Float").global().native().param("afMin", "Float").param("afMax", "Float");
        u.function("Wait").global().native().param("afSeconds", "Float");
        u.function("WaitMenuMode").global().native().param("afSeconds", "Float");
        u.function("GetCurrentRealTime", "Float").global().native();
        return u;
    }

    ScriptBuilder debugClass()
    {
        ScriptBuilder d("Debug");
        d.function("Trace").global().native().param("asTextToPrint", "String").param("aiSeverity", "Int");
        d.function("TraceStack").global().native().param("asTextToPrint", "String").param("aiSeverity", "Int");
        d.function("TraceFunction").global().native().param("asTextToPrint", "String").param("aiSeverity", "Int");
        d.function("Notification").global().native().param("asNotificationText", "String");
        d.function("MessageBox").global().native().param("asMessageBoxText", "String");
        d.function("QuitGame").global().native();
        return d;
    }

    /// A script with one wrapper per native: Sin(x) calls Math.Sin(x), and so on.
    ScriptBuilder probe()
    {
        ScriptBuilder p("Probe");
        auto f1 = [&p](const char* wrapper, const char* cls, const char* fn) {
            p.function(wrapper, "Float")
                .param("x", "Float")
                .local("r", "Float")
                .callStatic(cls, fn, Id("r"), { Id("x") })
                .ret(Id("r"));
        };
        for (const char* fn :
            { "Abs", "Sqrt", "Sin", "Cos", "Tan", "Asin", "Acos", "Atan", "DegreesToRadians", "RadiansToDegrees" })
        {
            f1(fn, "Math", fn);
        }
        p.function("Floor", "Int")
            .param("x", "Float")
            .local("r", "Int")
            .callStatic("Math", "Floor", Id("r"), { Id("x") })
            .ret(Id("r"));
        p.function("Ceiling", "Int")
            .param("x", "Float")
            .local("r", "Int")
            .callStatic("Math", "Ceiling", Id("r"), { Id("x") })
            .ret(Id("r"));
        p.function("Pow", "Float")
            .param("x", "Float")
            .param("y", "Float")
            .local("r", "Float")
            .callStatic("Math", "Pow", Id("r"), { Id("x"), Id("y") })
            .ret(Id("r"));
        p.function("RandomInt", "Int")
            .param("a", "Int")
            .param("b", "Int")
            .local("r", "Int")
            .callStatic("Utility", "RandomInt", Id("r"), { Id("a"), Id("b") })
            .ret(Id("r"));
        p.function("RandomFloat", "Float")
            .param("a", "Float")
            .param("b", "Float")
            .local("r", "Float")
            .callStatic("Utility", "RandomFloat", Id("r"), { Id("a"), Id("b") })
            .ret(Id("r"));
        p.function("Realtime", "Float")
            .local("r", "Float")
            .callStatic("Utility", "GetCurrentRealTime", Id("r"))
            .ret(Id("r"));
        p.function("Quit").local("n", "None").callStatic("Debug", "QuitGame", Id("n")).ret(Nil());
        for (const char* fn : { "Trace", "TraceStack", "TraceFunction" })
        {
            p.function(fn)
                .param("t", "String")
                .param("s", "Int")
                .local("n", "None")
                .callStatic("Debug", fn, Id("n"), { Id("t"), Id("s") })
                .ret(Nil());
        }
        for (const char* fn : { "Notification", "MessageBox" })
        {
            p.function(fn)
                .param("t", "String")
                .local("n", "None")
                .callStatic("Debug", fn, Id("n"), { Id("t") })
                .ret(Nil());
        }
        p.function("WaitThenMark", "Int")
            .param("w", "Float")
            .local("n", "None")
            .assign(Id("mark"), Int(1))
            .callStatic("Utility", "Wait", Id("n"), { Id("w") })
            .assign(Id("mark"), Int(2))
            .ret(Int(5));
        p.var("mark", "Int");
        return p;
    }

    struct Fixture
    {
        explicit Fixture(Target target = Target::SkyrimSE, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Strict,
            bool latent = false, std::uint64_t seed = 1)
            : vm(target, policy, options(latent))
            , natives(vm.mWorld, ScriptPapyrus::PureNativesOptions{ seed })
        {
            EXPECT_TRUE(vm.add(mathClass()));
            EXPECT_TRUE(vm.add(utilityClass()));
            EXPECT_TRUE(vm.add(debugClass()));
            EXPECT_TRUE(vm.add(probe()));
            report = natives.registerAll(vm.rt());
            id = vm.make("Probe");
        }
        static ScriptPapyrus::RuntimeOptions options(bool latent)
        {
            ScriptPapyrus::RuntimeOptions o;
            o.runLatentNatives = latent;
            return o;
        }
        float f(const char* fn, float a) { return vm.run(id, fn, { PapyrusValue::makeFloat(a) }).number; }
        Vm vm;
        ScriptPapyrus::PureNatives natives;
        ScriptPapyrus::PureNativesReport report;
        InstanceId id = 0;
    };

    TEST(PapyrusPureNatives, the_set_has_its_twenty_two_names_sorted_in_lower_case)
    {
        const auto names = ScriptPapyrus::PureNatives::names();
        ASSERT_EQ(names.size(), 22u);
        for (std::size_t i = 1; i < names.size(); ++i)
        {
            EXPECT_LT(names[i - 1], names[i]);
        }
        const std::set<std::string_view> set(names.begin(), names.end());
        for (const char* expected : { "math.abs", "math.floor", "math.pow", "math.radianstodegrees",
                 "utility.randomint", "utility.randomfloat", "utility.wait", "utility.waitmenumode", "debug.trace",
                 "debug.notification", "debug.messagebox", "debug.tracestack", "debug.tracefunction" })
        {
            EXPECT_TRUE(set.contains(expected)) << expected;
        }
        // The world natives named in the header are not in the set.
        for (const char* world : { "utility.getcurrentrealtime", "utility.getcurrentgametime", "utility.waitgametime",
                 "utility.isinmenumode", "debug.traceuser", "debug.quitgame", "game.getplayer" })
        {
            EXPECT_FALSE(set.contains(world)) << world;
        }
        for (const auto name : names)
        {
            for (const char c : name)
            {
                EXPECT_FALSE(c >= 'A' && c <= 'Z') << name;
            }
        }
    }

    TEST(PapyrusPureNatives, registers_what_the_classes_declare_and_counts_what_they_do_not)
    {
        for (const Target target : kBothTargets)
        {
            Fixture fx(target);
            // The test classes declare 13 Math, 4 Utility (+1 world) and 5 Debug (+1 world) natives; TraceFunction,
            // Trace... all are declared, so 22 are bound and 0 are missing.
            EXPECT_EQ(fx.report.registered, 22u);
            EXPECT_EQ(fx.report.notDeclared, 0u);
            EXPECT_TRUE(fx.report.ok()) << (fx.report.failures.empty() ? "" : fx.report.failures[0]);
        }
    }

    TEST(PapyrusPureNatives, a_loaded_class_without_a_function_skips_it_and_a_missing_class_registers_it)
    {
        Vm vm(Target::SkyrimSE);
        ScriptBuilder m("Math");
        m.function("Abs", "Float").global().native().param("afValue", "Float");
        ASSERT_TRUE(vm.add(m));
        ScriptPapyrus::PureNatives natives(vm.mWorld);
        const auto report = natives.registerAll(vm.rt());
        // Math.Abs is bound; the other 12 Math functions are not declared by the loaded class; Utility and Debug are
        // not loaded, so their 4 + 5 natives are bound without a check.
        EXPECT_EQ(report.registered, 10u);
        EXPECT_EQ(report.notDeclared, 12u);
        EXPECT_TRUE(report.ok());
    }

    TEST(PapyrusPureNatives, a_second_registration_reports_instead_of_replacing)
    {
        Fixture fx;
        const auto again = fx.natives.registerAll(fx.vm.rt());
        EXPECT_EQ(again.registered, 0u);
        EXPECT_EQ(again.failures.size(), 22u);
        EXPECT_NE(again.failures[0].find("already registered"), std::string::npos);
        EXPECT_NEAR(fx.f("Sqrt", 9.0f), 3.0f, 1e-6f); // the first handler still works
    }

    TEST(PapyrusPureNatives, a_native_the_pack_registered_first_keeps_its_handler)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(mathClass()));
        NativeDescriptor d;
        d.className = "Math";
        d.functionName = "Sqrt";
        d.global = true;
        ErrorReport error;
        ASSERT_TRUE(vm.rt().registerNative(
            d, [](const NativeCall&) { return NativeResult{ PapyrusValue::makeFloat(-7.0f) }; }, error));
        ScriptPapyrus::PureNatives natives(vm.mWorld);
        const auto report = natives.registerAll(vm.rt());
        ASSERT_EQ(report.failures.size(), 1u);
        EXPECT_NE(report.failures[0].find("math.sqrt"), std::string::npos);
        ASSERT_TRUE(vm.add(probe()));
    }

    TEST(PapyrusPureNatives, a_global_flag_that_disagrees_is_a_failure_not_a_skip)
    {
        Vm vm;
        ScriptBuilder m("Math");
        m.function("Abs", "Float").native().param("afValue", "Float"); // not global
        ASSERT_TRUE(vm.add(m));
        ScriptPapyrus::PureNatives natives(vm.mWorld);
        const auto report = natives.registerAll(vm.rt());
        ASSERT_EQ(report.failures.size(), 1u);
        EXPECT_NE(report.failures[0].find("math.abs"), std::string::npos);
    }

    // ---- behaviour in the VM --------------------------------------------------------------------------------------

    TEST(PapyrusPureNatives, math_runs_in_strict_mode_on_both_games)
    {
        for (const Target target : kBothTargets)
        {
            Fixture fx(target);
            EXPECT_FLOAT_EQ(fx.f("Abs", -2.5f), 2.5f);
            EXPECT_FLOAT_EQ(fx.f("Abs", 2.5f), 2.5f);
            EXPECT_NEAR(fx.f("Sqrt", 2.0f), 1.41421356f, 1e-6f);
            EXPECT_NEAR(fx.f("Sin", 90.0f), 1.0f, 1e-6f);
            EXPECT_NEAR(fx.f("Cos", 0.0f), 1.0f, 1e-6f);
            EXPECT_NEAR(fx.f("Tan", 45.0f), 1.0f, 1e-6f);
            EXPECT_NEAR(fx.f("Asin", 0.5f), 30.0f, 1e-4f);
            EXPECT_NEAR(fx.f("Acos", 0.5f), 60.0f, 1e-4f);
            EXPECT_NEAR(fx.f("Atan", 1.0f), 45.0f, 1e-4f);
            EXPECT_NEAR(fx.f("DegreesToRadians", 90.0f), 1.5707963f, 1e-6f);
            EXPECT_NEAR(fx.f("RadiansToDegrees", 1.5707963f), 90.0f, 1e-4f);
            EXPECT_EQ(fx.vm.runInt(fx.id, "Floor", { PapyrusValue::makeFloat(-1.5f) }), -2);
            EXPECT_EQ(fx.vm.runInt(fx.id, "Floor", { PapyrusValue::makeFloat(1.5f) }), 1);
            EXPECT_EQ(fx.vm.runInt(fx.id, "Ceiling", { PapyrusValue::makeFloat(-1.5f) }), -1);
            EXPECT_EQ(fx.vm.runInt(fx.id, "Ceiling", { PapyrusValue::makeFloat(1.2f) }), 2);
            EXPECT_EQ(fx.vm.runInt(fx.id, "Floor", { PapyrusValue::makeFloat(3.0e9f) }),
                std::numeric_limits<std::int32_t>::max());
            const PapyrusValue pw
                = fx.vm.run(fx.id, "Pow", { PapyrusValue::makeFloat(2.0f), PapyrusValue::makeFloat(8.0f) });
            EXPECT_NEAR(pw.number, 256.0f, 1e-3f);
            EXPECT_EQ(fx.vm.rt().stats().nativeCalls, 17u);
        }
    }

    TEST(PapyrusPureNatives, an_undefined_input_returns_zero_warns_once_and_counts)
    {
        Fixture fx;
        EXPECT_EQ(fx.f("Sqrt", -4.0f), 0.0f);
        EXPECT_EQ(fx.f("Sqrt", -9.0f), 0.0f);
        EXPECT_EQ(fx.f("Asin", 2.0f), 0.0f);
        EXPECT_EQ(fx.natives.domainErrors(), 3u);
        EXPECT_EQ(fx.vm.mWorld.loggedCount("Math.Sqrt"), 1u) << "one warning per function";
        EXPECT_EQ(fx.vm.mWorld.loggedCount("Math.Asin"), 1u);
        EXPECT_EQ(fx.vm.rt().stats().errors[static_cast<std::size_t>(ScriptPapyrus::ErrorKind::NativeFailed)], 0u);
    }

    TEST(PapyrusPureNatives, random_numbers_follow_the_seed_and_the_state_can_be_saved)
    {
        Fixture a(Target::SkyrimSE, UnknownFunctionPolicy::Strict, false, 77);
        Fixture b(Target::SkyrimSE, UnknownFunctionPolicy::Strict, false, 77);
        Fixture c(Target::SkyrimSE, UnknownFunctionPolicy::Strict, false, 78);
        bool differs = false;
        std::vector<std::int32_t> first;
        for (int i = 0; i < 50; ++i)
        {
            const PapyrusValue lo = PapyrusValue::makeInt(0);
            const PapyrusValue hi = PapyrusValue::makeInt(1000000);
            const std::int32_t x = a.vm.runInt(a.id, "RandomInt", { lo, hi });
            first.push_back(x);
            EXPECT_EQ(x, b.vm.runInt(b.id, "RandomInt", { lo, hi }));
            differs = differs || x != c.vm.runInt(c.id, "RandomInt", { lo, hi });
        }
        EXPECT_TRUE(differs);
        const std::uint64_t saved = a.natives.randomState();
        const float next
            = a.vm.run(a.id, "RandomFloat", { PapyrusValue::makeFloat(0), PapyrusValue::makeFloat(1) }).number;
        a.natives.setRandomState(saved);
        EXPECT_EQ(
            a.vm.run(a.id, "RandomFloat", { PapyrusValue::makeFloat(0), PapyrusValue::makeFloat(1) }).number, next);
        // Inclusive on both ends.
        std::set<int> seen;
        for (int i = 0; i < 400; ++i)
        {
            seen.insert(a.vm.runInt(a.id, "RandomInt", { PapyrusValue::makeInt(3), PapyrusValue::makeInt(5) }));
        }
        EXPECT_EQ(seen, (std::set<int>{ 3, 4, 5 }));
    }

    TEST(PapyrusPureNatives, random_with_a_nan_bound_gives_zero_and_a_domain_error)
    {
        Fixture fx;
        EXPECT_EQ(fx.vm.run(fx.id, "RandomFloat", { PapyrusValue::makeFloat(kNaN), PapyrusValue::makeFloat(1) }).number,
            0.0f);
        EXPECT_EQ(fx.natives.domainErrors(), 1u);
    }

    TEST(PapyrusPureNatives, trace_and_the_message_functions_are_logged_no_ops)
    {
        Fixture fx;
        const PapyrusValue text = PapyrusValue::makeString("hello world");
        const PapyrusValue severity = PapyrusValue::makeInt(2);
        fx.vm.run(fx.id, "Trace", { text, severity });
        fx.vm.run(fx.id, "TraceStack", { text, severity });
        fx.vm.run(fx.id, "TraceFunction", { text, severity });
        ASSERT_EQ(fx.vm.mWorld.mReports.size(), 3u);
        for (const std::string& line : fx.vm.mWorld.mReports)
        {
            EXPECT_EQ(line, "hello world") << "Trace passes the text as it is";
        }
        fx.vm.run(fx.id, "Notification", { text });
        fx.vm.run(fx.id, "MessageBox", { text });
        ASSERT_EQ(fx.vm.mWorld.mReports.size(), 5u);
        EXPECT_NE(fx.vm.mWorld.mReports[3].find("Notification"), std::string::npos);
        EXPECT_NE(fx.vm.mWorld.mReports[3].find("hello world"), std::string::npos);
        EXPECT_NE(fx.vm.mWorld.mReports[4].find("MessageBox"), std::string::npos);
        EXPECT_NE(fx.vm.mWorld.mReports[4].find("hello world"), std::string::npos);
    }

    TEST(PapyrusPureNatives, the_split_holds_world_natives_stay_unregistered)
    {
        Fixture fx; // strict
        const CallResult realtime = fx.vm.call(fx.id, "Realtime");
        EXPECT_FALSE(realtime.ok);
        EXPECT_TRUE(hasText(realtime.error, "unregistered native")) << realtime.error.format();
        const CallResult quit = fx.vm.call(fx.id, "Quit");
        EXPECT_FALSE(quit.ok);
        EXPECT_TRUE(hasText(quit.error, "unregistered native"));
        // Tolerant: the zero of its type and one log line.
        Fixture tolerant(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
        EXPECT_EQ(tolerant.vm.run(tolerant.id, "Realtime").number, 0.0f);
        EXPECT_EQ(tolerant.vm.rt().stats().unregisteredNativeCalls, 1u);
    }

    TEST(PapyrusPureNatives, wait_parks_the_stack_for_the_given_script_seconds)
    {
        Fixture fx(Target::SkyrimSE, UnknownFunctionPolicy::Strict, true);
        const CallResult result = fx.vm.call(fx.id, "WaitThenMark", { PapyrusValue::makeFloat(2.5f) });
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_TRUE(result.suspended);
        EXPECT_EQ(fx.vm.varInt(fx.id, "mark"), 1);
        EXPECT_EQ(fx.vm.rt().suspendedStacks(), 1u);
        fx.vm.tick(2.0);
        EXPECT_EQ(fx.vm.varInt(fx.id, "mark"), 1) << "not yet";
        fx.vm.tick(2.5);
        EXPECT_EQ(fx.vm.varInt(fx.id, "mark"), 2);
        EXPECT_EQ(fx.vm.rt().suspendedStacks(), 0u);
    }

    TEST(PapyrusPureNatives, wait_never_turns_a_bad_number_into_a_wait_for_the_host)
    {
        // A negative waitSeconds means "until the host completes it" to the scheduler: Utility.Wait must not pass one.
        for (const float bad : { -1.0f, -1.0e9f, kNaN, -kInf })
        {
            Fixture fx(Target::SkyrimSE, UnknownFunctionPolicy::Strict, true);
            const CallResult result = fx.vm.call(fx.id, "WaitThenMark", { PapyrusValue::makeFloat(bad) });
            ASSERT_TRUE(result.ok) << result.error.format();
            fx.vm.tick(0.0);
            EXPECT_EQ(fx.vm.varInt(fx.id, "mark"), 2) << bad << ": a bad wait is a wait of zero";
        }
        Fixture fx(Target::SkyrimSE, UnknownFunctionPolicy::Strict, true);
        ASSERT_TRUE(fx.vm.call(fx.id, "WaitThenMark", { PapyrusValue::makeFloat(kInf) }).ok);
        fx.vm.tick(1.0e6);
        EXPECT_EQ(fx.vm.varInt(fx.id, "mark"), 1) << "an infinite wait is cut by the runtime's own cap, not zero";
    }

    TEST(PapyrusPureNatives, with_latent_natives_off_wait_is_skipped_as_before)
    {
        Fixture tolerant(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, false);
        const CallResult result = tolerant.vm.call(tolerant.id, "WaitThenMark", { PapyrusValue::makeFloat(5.0f) });
        ASSERT_TRUE(result.ok);
        EXPECT_FALSE(result.suspended);
        EXPECT_EQ(tolerant.vm.rt().stats().latentSkipped, 1u);
        Fixture strict(Target::SkyrimSE, UnknownFunctionPolicy::Strict, false);
        const CallResult refused = strict.vm.call(strict.id, "WaitThenMark", { PapyrusValue::makeFloat(5.0f) });
        EXPECT_FALSE(refused.ok);
        EXPECT_TRUE(hasText(refused.error, "latent"));
    }

    TEST(PapyrusPureNatives, a_run_is_deterministic_with_the_same_seed)
    {
        auto run = [](std::uint64_t seed) {
            Fixture fx(Target::SkyrimSE, UnknownFunctionPolicy::Strict, false, seed);
            std::vector<double> out;
            for (int i = 0; i < 100; ++i)
            {
                out.push_back(
                    fx.vm.runInt(fx.id, "RandomInt", { PapyrusValue::makeInt(0), PapyrusValue::makeInt(99) }));
                out.push_back(fx.f("Sin", static_cast<float>(i) * 7.0f));
            }
            return out;
        };
        EXPECT_EQ(run(5), run(5));
        EXPECT_NE(run(5), run(6));
    }

    TEST(PapyrusPureNatives, the_runtime_without_the_set_is_unchanged)
    {
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(vm.add(mathClass()));
        ASSERT_TRUE(vm.add(probe()));
        const InstanceId id = vm.make("Probe");
        const CallResult result = vm.call(id, "Sqrt", { PapyrusValue::makeFloat(4.0f) });
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "unregistered native"));
    }

    TEST(PapyrusPureNatives, fuzzed_scripts_calling_the_set_never_crash)
    {
        Fixture fx(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant);
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> any(-1.0e6f, 1.0e6f);
        for (int i = 0; i < 3000; ++i)
        {
            const char* names[] = { "Abs", "Sqrt", "Sin", "Cos", "Tan", "Asin", "Acos", "Atan", "DegreesToRadians",
                "RadiansToDegrees" };
            const float a = i % 11 == 0 ? kNaN : (i % 13 == 0 ? kInf : any(rng));
            const CallResult r = fx.vm.call(fx.id, names[i % 10], { PapyrusValue::makeFloat(a) });
            ASSERT_TRUE(r.ok);
            ASSERT_TRUE(std::isfinite(r.returned.number)) << names[i % 10] << " " << a;
        }
    }

    // ---- contract 1.2: setProperty -------------------------------------------------------------------------------

    ScriptBuilder propertyHolder()
    {
        ScriptBuilder s("Holder");
        s.var("count", "Int");
        s.var("ratio", "Float");
        s.autoProperty("Label", "String", Str("none"));
        s.fullProperty("Computed", "Int");
        s.getter().ret(Int(1));
        s.setter().ret(Nil());
        return s;
    }

    TEST(PapyrusContractSetProperty, is_version_one_two_and_goes_through_the_contract_pointer)
    {
        EXPECT_EQ(PapyrusContract::kVersion.major, 1);
        EXPECT_EQ(PapyrusContract::kVersion.minor, 2);
        Vm vm;
        ASSERT_TRUE(vm.add(propertyHolder()));
        const InstanceId id = vm.make("Holder");
        PapyrusContract& contract = vm.rt();
        EXPECT_TRUE(contract.setProperty(id, "count", PapyrusValue::makeInt(9)));
        EXPECT_EQ(vm.varInt(id, "count"), 9);
        EXPECT_TRUE(contract.setProperty(id, "ratio", PapyrusValue::makeInt(2))) << "an Int fills a Float";
        PapyrusValue ratio;
        ASSERT_TRUE(vm.rt().getVariable(id, "ratio", ratio));
        EXPECT_EQ(ratio.kind, PapyrusKind::Float);
        EXPECT_EQ(ratio.number, 2.0f);
        EXPECT_TRUE(contract.setProperty(id, "Label", PapyrusValue::makeString("hello")));
        PapyrusValue label;
        ASSERT_TRUE(vm.rt().getVariable(id, "label", label));
        EXPECT_EQ(label.text, "hello");
    }

    TEST(PapyrusContractSetProperty, refuses_what_cannot_be_stored)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(propertyHolder()));
        const InstanceId id = vm.make("Holder");
        PapyrusContract& contract = vm.rt();
        EXPECT_FALSE(contract.setProperty(0, "count", PapyrusValue::makeInt(1)));
        EXPECT_FALSE(contract.setProperty(id + 40, "count", PapyrusValue::makeInt(1)));
        EXPECT_FALSE(contract.setProperty(id, "nothere", PapyrusValue::makeInt(1)));
        EXPECT_FALSE(contract.setProperty(id, "count", PapyrusValue::makeString("x")));
        EXPECT_FALSE(contract.setProperty(id, "Computed", PapyrusValue::makeInt(1))) << "a full property has code";
        EXPECT_EQ(vm.varInt(id, "count"), 0);
    }

    TEST(PapyrusContractSetProperty, a_provider_that_predates_it_refuses_by_default)
    {
        // The default body is the 1.1 behaviour: such an implementation never stores anything.
        class Old final : public PapyrusContract
        {
        public:
            LoadResult loadScript(std::span<const std::uint8_t>, std::string_view) noexcept override { return {}; }
            LinkReport link() const noexcept override { return {}; }
            bool registerNative(const NativeDescriptor&, NativeHandler, ErrorReport&) noexcept override
            {
                return false;
            }
            void setPolicy(UnknownFunctionPolicy) noexcept override {}
            InstanceId attach(const AttachKey&, std::string_view) noexcept override { return 0; }
            CallResult send(InstanceId, std::string_view, std::span<const PapyrusValue>) noexcept override
            {
                return {};
            }
            CallResult call(InstanceId, std::string_view, std::span<const PapyrusValue>) noexcept override
            {
                return {};
            }
            TickResult tick(double, const Budget&) noexcept override { return {}; }
            SaveBlob serialise() const noexcept override { return {}; }
            RestoreResult restore(const SaveBlob&) noexcept override { return {}; }
        } old;
        EXPECT_FALSE(old.setProperty(1, "x", PapyrusValue::makeInt(1)));
    }
}
