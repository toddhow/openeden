#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <components/lua/luastate.hpp>
#include <components/lua/utilpackage.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>

// Pins what a Lua script can observe from util.vector2/3/4, util.transform, util.box and util.color, so that the move
// of their implementation from OSG's math types to VSG's (task 0013, Design item 4) changes none of it. Mods depend on
// these.
//
// Every expected value is a literal string, and nothing here includes a math type's header, so the tests stay valid
// after the move. Each value goes through show() below: numbers print with %.17g and vectors with __tostring's 38
// digits, both of which identify a float exactly, so a result computed in double, or in a different order, fails the
// comparison. A few TransformM and Box tostrings use default stream precision (6 digits), as the binding does.
//
// Two kinds of check are looser, because the platforms really differ. The Linux build has -mfma (from Jolt's flags), so
// GCC fuses a * b + c * d and rounds differently from MSVC. EXPECT_LUA_NEAR allows task 0013's 1e-6 relative on the
// sums of products where the two disagree by a bit or two; everything else is exact on both. And when a TransformM's
// scale is even, decompose's second rotation is arbitrary (0.103 radians on Linux for a pure rotation, nothing on
// Windows), so EXPECT_LUA_STARTS stops before it.
//
// A NaN is checked with isnan(), since its text differs between C libraries. Error messages are matched on the part
// that does not name a C++ type. The values were read from the code at the start of V5's Lua step, when Box, the Euler
// angle helpers and Color were already on VSG (through ports of OSG's functions) and the rest was still on OSG.
namespace
{
    using namespace testing;

    constexpr const char* setup = R"lua(
        vec2, vec3, vec4, T, box, color = util.vector2, util.vector3, util.vector4, util.transform, util.box, util.color
        pi = math.pi
        local function pack(...) return select('#', ...), { ... } end
        function show(code)
            local f, err = loadstring('return ' .. code)
            if not f then return 'COMPILE ' .. err end
            local n, r = pack(pcall(f))
            if not r[1] then return 'ERROR ' .. tostring(r[2]) end
            local out = {}
            for i = 2, n do
                out[#out + 1] = type(r[i]) == 'number' and string.format('%.17g', r[i]) or tostring(r[i])
            end
            return table.concat(out, ' | ')
        end
        function verts(b)
            local out = {}
            local vs = b.vertices
            for i = 1, #vs do out[i] = tostring(vs[i]) end
            return #vs .. ': ' .. table.concat(out, ' ')
        end
        function isnan(x) return x ~= x end
    )lua";

    // True if the two strings differ only in their numbers, by at most 1e-6 relative.
    bool nearlySame(const std::string& actual, const std::string& expected)
    {
        static const std::regex number(R"(-?\d+(\.\d+)?(e[-+]?\d+)?)");
        std::sregex_iterator a(actual.begin(), actual.end(), number);
        std::sregex_iterator e(expected.begin(), expected.end(), number);
        const std::sregex_iterator end;
        std::string_view restA = actual;
        std::string_view restE = expected;
        for (; a != end && e != end; ++a, ++e)
        {
            if (a->prefix().str() != e->prefix().str())
                return false;
            const double x = std::strtod(a->str().c_str(), nullptr);
            const double y = std::strtod(e->str().c_str(), nullptr);
            if (std::abs(x - y) > 1e-6 * std::max(std::abs(x), std::abs(y)))
                return false;
            restA = std::string_view(actual).substr(a->position() + a->length());
            restE = std::string_view(expected).substr(e->position() + e->length());
        }
        return a == end && e == end && restA == restE;
    }

    struct LuaUtilMathTest : Test
    {
        LuaUtil::LuaState mLuaState{ nullptr, nullptr };

        LuaUtilMathTest()
        {
            mLuaState.addInternalLibSearchPath(
                std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "components" / "lua");
            sol::state_view lua = mLuaState.unsafeState();
            lua["util"] = LuaUtil::initUtilPackage(lua);
            lua.safe_script(setup);
        }

        std::string show(const std::string& code)
        {
            sol::state_view lua = mLuaState.unsafeState();
            sol::function fn = lua["show"];
            return fn(code).get<std::string>();
        }
    };

#define EXPECT_LUA(code, expected) EXPECT_EQ(show(code), expected) << (code)
#define EXPECT_LUA_ERROR(code, part) EXPECT_THAT(show(code), AllOf(StartsWith("ERROR "), HasSubstr(part))) << (code)
#define EXPECT_LUA_NEAR(code, expected)                                                                                \
    EXPECT_TRUE(nearlySame(show(code), expected)) << (code) << " gave " << show(code)
#define EXPECT_LUA_STARTS(code, start) EXPECT_THAT(show(code), StartsWith(start)) << (code)

    constexpr const char* noOverload = "no matching function call";

    TEST_F(LuaUtilMathTest, ConstructorsTakeNumbersAndExtraArgumentsAreIgnored)
    {
        EXPECT_LUA("vec2(1, 2)", "(1, 2)");
        EXPECT_LUA("vec3(1, 2, 3)", "(1, 2, 3)");
        EXPECT_LUA("vec4(1, 2, 3, 4)", "(1, 2, 3, 4)");
        EXPECT_LUA("vec3(1, 2, 3, 4)", "(1, 2, 3)");
        EXPECT_LUA_ERROR("vec2()", "expected number");
        EXPECT_LUA_ERROR("vec2(1)", "expected number");
        EXPECT_LUA_ERROR("vec3(1, 2)", "expected number");
        EXPECT_LUA_ERROR("vec4(1, 2, 3)", "expected number");
        EXPECT_LUA_ERROR("vec2('1', '2')", "expected number");
        EXPECT_LUA_ERROR("vec2(nil, 2)", "expected number");
        EXPECT_LUA_ERROR("vec2({}, 1)", "expected number");
    }

    TEST_F(LuaUtilMathTest, ComponentsAreSinglePrecision)
    {
        EXPECT_LUA("vec3(0.1, 0.2, 0.3).x", "0.10000000149011612");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3).y", "0.20000000298023224");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3).z", "0.30000001192092896");
        EXPECT_LUA("vec2(0.1, 0.7).x", "0.10000000149011612");
        EXPECT_LUA("vec2(0.1, 0.7).y", "0.69999998807907104");
        EXPECT_LUA("vec4(0.1, 0.2, 0.3, 0.7).w", "0.69999998807907104");
        EXPECT_LUA("vec3(0.1 + 0.2, 0, 0).x", "0.30000001192092896");
        EXPECT_LUA("(vec3(0.1, 0, 0) + vec3(0.2, 0, 0)).x", "0.30000001192092896");
        EXPECT_LUA("vec3(16777217, 0, 0).x", "16777216");
        EXPECT_LUA("vec3(0 * -1, 0, 0).x", "-0");
        EXPECT_LUA("isnan(vec2(0/0, 0).x)", "true");
    }

    TEST_F(LuaUtilMathTest, ToStringPrintsEveryDigitOfAFloat)
    {
        EXPECT_LUA("vec3(0.1, 0.2, 0.3)",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("vec3(1/3, 2/3, 1e10)", "(0.3333333432674407958984375, 0.666666686534881591796875, 10000000000)");
        EXPECT_LUA("vec3(1e39, -1e39, 1e-46)", "(inf, -inf, 0)");
        EXPECT_LUA("vec3(1e30, 1e-30, 1e-45)",
            "(1000000015047466219876688855040, 1.0000000031710768509710513471352647538e-30, "
            "1.4012984643248170709237295832899161313e-45)");
        EXPECT_LUA("vec4(123456789, 0.000001, 1e20, -1e-20)",
            "(123456792, 9.999999974752427078783512115478515625e-07, 100000002004087734272, "
            "-9.9999996826552253889678874634872052241e-21)");
        EXPECT_LUA("vec2(math.huge, -math.huge)", "(inf, -inf)");
        EXPECT_LUA("vec3(0 * -1, 0, 0)", "(-0, 0, 0)");
    }

    TEST_F(LuaUtilMathTest, FieldsAreReadOnlyAndUnknownOnesAreNil)
    {
        EXPECT_LUA("vec2(1, 2).z", "nil");
        EXPECT_LUA("vec2(1, 2).w", "nil");
        EXPECT_LUA("vec3(1, 2, 3).w", "nil");
        EXPECT_LUA("vec2(1, 2)[1]", "nil");
        EXPECT_LUA("vec3(1, 2, 3).foo", "nil");
        EXPECT_LUA("vec3(1, 2, 3).X", "nil");
        EXPECT_LUA_ERROR("(function() local v = vec3(1, 2, 3); v.x = 5; return v end)()", "readonly property");
        EXPECT_LUA_ERROR("(function() local v = vec3(1, 2, 3); v.foo = 5; return v end)()", "cannot set");
    }

    TEST_F(LuaUtilMathTest, Swizzles)
    {
        EXPECT_LUA("vec3(1, 2, 3)['0']", "0");
        EXPECT_LUA("vec3(1, 2, 3)['1']", "1");
        EXPECT_LUA("vec4(1, 2, 3, 4)['0']", "0");
        EXPECT_LUA("vec2(1, 2)['1']", "1");
        EXPECT_LUA("vec2(1, 2).yx", "(2, 1)");
        EXPECT_LUA("vec2(1, 2).xy", "(1, 2)");
        EXPECT_LUA("vec2(1, 2).xyx", "(1, 2, 1)");
        EXPECT_LUA("vec2(1, 2).yxyx", "(2, 1, 2, 1)");
        EXPECT_LUA("vec2(1, 2).x01", "(1, 0, 1)");
        EXPECT_LUA("vec2(1, 2)['10yx']", "(1, 0, 2, 1)");
        EXPECT_LUA("vec3(1, 2, 3).zy", "(3, 2)");
        EXPECT_LUA("vec3(1, 2, 3).zyx", "(3, 2, 1)");
        EXPECT_LUA("vec3(1, 2, 3).zyxx", "(3, 2, 1, 1)");
        EXPECT_LUA("vec3(1, 2, 3)['1z0y']", "(1, 3, 0, 2)");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3).zx", "(0.300000011920928955078125, 0.100000001490116119384765625)");
        EXPECT_LUA("vec4(1, 2, 3, 4).wz", "(4, 3)");
        EXPECT_LUA("vec4(1, 2, 3, 4).wzy", "(4, 3, 2)");
        EXPECT_LUA("vec4(1, 2, 3, 4).wzyx", "(4, 3, 2, 1)");
        EXPECT_LUA("vec4(1, 2, 3, 4)['0w1x']", "(0, 4, 1, 1)");
        // A swizzle names only components the vector has, and at most four.
        EXPECT_LUA("vec2(1, 2).xz", "nil");
        EXPECT_LUA("vec2(1, 2).xxxxx", "nil");
        EXPECT_LUA("vec3(1, 2, 3).xw", "nil");
        EXPECT_LUA("vec4(1, 2, 3, 4).xyzwx", "nil");
        // Each size of swizzle gives the vector type of that size.
        EXPECT_LUA("vec3(1, 2, 3).xy == vec2(1, 2)", "true");
        EXPECT_LUA("vec2(1, 2).xy0 == vec3(1, 2, 0)", "true");
        EXPECT_LUA("vec2(1, 2).xy01 == vec4(1, 2, 0, 1)", "true");
        EXPECT_LUA("vec3(1, 2, 3).xyz == vec3(1, 2, 3)", "true");
    }

    TEST_F(LuaUtilMathTest, AddSubtractAndNegate)
    {
        EXPECT_LUA("vec2(1, 2) + vec2(0.1, 0.2)", "(1.10000002384185791015625, 2.2000000476837158203125)");
        EXPECT_LUA("vec2(1, 2) - vec2(0.1, 0.2)", "(0.89999997615814208984375, 1.7999999523162841796875)");
        EXPECT_LUA("vec3(1, 2, 3) + vec3(0.1, 0.2, 0.3)",
            "(1.10000002384185791015625, 2.2000000476837158203125, 3.2999999523162841796875)");
        EXPECT_LUA("vec3(1, 2, 3) - vec3(0.1, 0.2, 0.3)",
            "(0.89999997615814208984375, 1.7999999523162841796875, 2.7000000476837158203125)");
        EXPECT_LUA("vec4(1, 2, 3, 4) + vec4(0.1, 0.2, 0.3, 0.4)",
            "(1.10000002384185791015625, 2.2000000476837158203125, 3.2999999523162841796875, 4.400000095367431640625)");
        EXPECT_LUA("vec4(1, 2, 3, 4) - vec4(0.1, 0.2, 0.3, 0.4)",
            "(0.89999997615814208984375, 1.7999999523162841796875, 2.7000000476837158203125, 3.599999904632568359375)");
        EXPECT_LUA("vec3(1e30, -1e30, 1) + vec3(1e38, 1e38, 1e-10)",
            "(99999996802856924650656260769173209088, 99999996802856924650656260769173209088, 1)");
        EXPECT_LUA("-vec2(1, -2)", "(-1, 2)");
        EXPECT_LUA("-vec2(0, 0)", "(-0, -0)");
        EXPECT_LUA("-vec3(1, -2, 0)", "(-1, 2, -0)");
        EXPECT_LUA("-vec4(1, -2, 0, 4)", "(-1, 2, -0, -4)");
    }

    TEST_F(LuaUtilMathTest, MultiplyByAScalar)
    {
        EXPECT_LUA("vec2(0.1, 0.2) * 3", "(0.300000011920928955078125, 0.60000002384185791015625)");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3) * 3",
            "(0.300000011920928955078125, 0.60000002384185791015625, 0.900000035762786865234375)");
        EXPECT_LUA("vec4(0.1, 0.2, 0.3, 0.4) * 3",
            "(0.300000011920928955078125, 0.60000002384185791015625, 0.900000035762786865234375, "
            "1.2000000476837158203125)");
        EXPECT_LUA("vec3(1, 2, 3) * 0.1",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("vec3(1, 2, 3) * 1e40", "(inf, inf, inf)");
    }

    TEST_F(LuaUtilMathTest, MultiplyTwoVectorsIsADotProduct)
    {
        EXPECT_LUA_NEAR("vec2(0.1, 0.2) * vec2(3, 7)", "1.7000000476837158");
        EXPECT_LUA_NEAR("vec2(0.1, 0.2):dot(vec2(3, 7))", "1.7000000476837158");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3) * vec3(3, 7, 11)", "5");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3):dot(vec3(3, 7, 11))", "5");
        EXPECT_LUA_NEAR("vec4(0.1, 0.2, 0.3, 0.4) * vec4(3, 7, 11, 13)", "10.200000762939453");
        EXPECT_LUA_NEAR("vec4(0.1, 0.2, 0.3, 0.4):dot(vec4(3, 7, 11, 13))", "10.200000762939453");
        // Summed in single precision: 1e8 + 1 is 1e8 in a float.
        EXPECT_LUA("vec3(1e8, 1, -1e8) * vec3(1, 1, 1)", "0");
        EXPECT_LUA("math.abs(vec3(1e20, 1e20, 0) * vec3(1e-20, -1e-20, 0)) < 1e-7", "true");
        EXPECT_LUA("vec3(1e30, 1e30, 0) * vec3(1e30, 1e30, 0)", "inf");
        EXPECT_LUA("vec2(1e30, 1e30):dot(vec2(1e30, 1e30))", "inf");
    }

    TEST_F(LuaUtilMathTest, DivideByAScalarDividesEachComponent)
    {
        // Each component divides by c: v / c can differ from v * (1 / c) in a float, as 9 / 10 does here.
        EXPECT_LUA("vec2(0.1, 0.7) / 3", "(0.0333333350718021392822265625, 0.23333333432674407958984375)");
        EXPECT_LUA("vec2(3, 7) / 10", "(0.300000011920928955078125, 0.699999988079071044921875)");
        EXPECT_LUA("vec2(9, 7) / 10", "(0.89999997615814208984375, 0.699999988079071044921875)");
        EXPECT_LUA("vec2(1, 3) / 49", "(0.02040816284716129302978515625, 0.0612244904041290283203125)");
        EXPECT_LUA("vec3(0.1, 0.7, 5) / 3",
            "(0.0333333350718021392822265625, 0.23333333432674407958984375, 1.66666662693023681640625)");
        EXPECT_LUA("vec3(3, 7, 9) / 10",
            "(0.300000011920928955078125, 0.699999988079071044921875, 0.89999997615814208984375)");
        EXPECT_LUA("vec3(1, 3, 7) / 49",
            "(0.02040816284716129302978515625, 0.0612244904041290283203125, 0.14285714924335479736328125)");
        EXPECT_LUA("vec4(0.1, 0.7, 5, 11) / 3",
            "(0.0333333350718021392822265625, 0.23333333432674407958984375, 1.66666662693023681640625, "
            "3.6666667461395263671875)");
        EXPECT_LUA("vec4(3, 7, 9, 11) / 10",
            "(0.300000011920928955078125, 0.699999988079071044921875, 0.89999997615814208984375, "
            "1.10000002384185791015625)");
        EXPECT_LUA("vec4(1, 3, 7, 9) / 49",
            "(0.02040816284716129302978515625, 0.0612244904041290283203125, 0.14285714924335479736328125, "
            "0.1836734712123870849609375)");
        EXPECT_LUA("vec2(1, -1) / 0", "(inf, -inf)");
        EXPECT_LUA("vec3(1, -1, 1) / 0", "(inf, -inf, inf)");
        EXPECT_LUA("vec4(1, -1, 1, 1) / 0", "(inf, -inf, inf, inf)");
        EXPECT_LUA("isnan((vec3(0, 1, 1) / 0).x)", "true");
        EXPECT_LUA("vec3(1, 2, 3) / 1e-40", "(inf, inf, inf)");
    }

    TEST_F(LuaUtilMathTest, OperatorsThatAreNotBound)
    {
        EXPECT_LUA_ERROR("2 * vec2(1, 2)", noOverload);
        EXPECT_LUA_ERROR("2 * vec3(1, 2, 3)", noOverload);
        EXPECT_LUA_ERROR("vec2(1, 2) * vec3(1, 2, 3)", noOverload);
        EXPECT_LUA_ERROR("vec2(1, 2) * 'a'", noOverload);
        EXPECT_LUA_ERROR("vec2(1, 2) * '2'", noOverload);
        EXPECT_LUA_ERROR("2 / vec2(1, 2)", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2) / vec2(1, 2)", "expected number");
        EXPECT_LUA_ERROR("vec3(1, 2, 3) / vec3(1, 2, 3)", "expected number");
        EXPECT_LUA_ERROR("vec2(1, 2) + 1", "expected userdata");
        EXPECT_LUA_ERROR("vec3(1, 2, 3) + 1", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2) + vec3(1, 2, 3)", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2) ^ vec2(1, 2)", "attempt to perform arithmetic");
        EXPECT_LUA_ERROR("vec4(1, 2, 3, 4) ^ vec4(1, 2, 3, 4)", "attempt to perform arithmetic");
        EXPECT_LUA_ERROR("vec3(1, 2, 3) ^ 2", "expected userdata");
        EXPECT_LUA_ERROR("2 ^ vec3(1, 2, 3)", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2) % 2", "attempt to perform arithmetic");
        EXPECT_LUA_ERROR("vec2(1, 2) < vec2(2, 3)", "attempt to compare");
        EXPECT_LUA_ERROR("vec2(1, 2) <= vec2(2, 3)", "attempt to compare");
        EXPECT_LUA_ERROR("vec3(1, 2, 3) < vec3(2, 3, 4)", "attempt to compare");
        EXPECT_LUA_ERROR("vec2(1, 2) .. 'a'", "attempt to concatenate");
        EXPECT_LUA_ERROR("#vec2(1, 2)", "attempt to get length");
        EXPECT_LUA_ERROR("vec2(1, 2):dot(2)", "expected userdata");
        EXPECT_LUA_ERROR("vec3(1, 2, 3):dot(vec2(1, 2))", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2):emul(2)", "expected userdata");
        EXPECT_LUA_ERROR("vec2(1, 2):ediv(vec3(1, 2, 3))", "expected userdata");
        EXPECT_LUA("vec2(1, 2).cross", "nil");
        EXPECT_LUA("vec4(1, 2, 3, 4).cross", "nil");
        EXPECT_LUA("vec3(1, 2, 3).rotate", "nil");
        EXPECT_LUA("vec4(1, 2, 3, 4).rotate", "nil");
    }

    TEST_F(LuaUtilMathTest, CrossProduct)
    {
        EXPECT_LUA("vec3(1, 2, 3) ^ vec3(4, 5, 6)", "(-3, 6, -3)");
        EXPECT_LUA("vec3(1, 2, 3):cross(vec3(4, 5, 6))", "(-3, 6, -3)");
        EXPECT_LUA("vec3(4, 5, 6) ^ vec3(1, 2, 3)", "(3, -6, 3)");
        EXPECT_LUA("vec3(1, 0, 0) ^ vec3(0, 1, 0)", "(0, 0, 1)");
        EXPECT_LUA("vec3(0, 1, 0) ^ vec3(1, 0, 0)", "(0, 0, -1)");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3) ^ vec3(0.4, 0.5, 0.6)",
            "(-0.0300000011920928955078125, 0.060000002384185791015625, -0.0300000049173831939697265625)");
        EXPECT_LUA("vec3(1, 2, 3) ^ vec3(1, 2, 3)", "(0, 0, 0)");
        EXPECT_LUA("vec3(1e30, 0, 0) ^ vec3(0, 1e30, 0)", "(0, 0, inf)");
    }

    TEST_F(LuaUtilMathTest, Length)
    {
        EXPECT_LUA("vec2(1, 1):length()", "1.4142135381698608");
        EXPECT_LUA("vec2(1, 1):length2()", "2");
        EXPECT_LUA("vec3(1, 1, 1):length()", "1.7320507764816284");
        EXPECT_LUA("vec3(1, 1, 1):length2()", "3");
        EXPECT_LUA("vec4(1, 1, 1, 1):length()", "2");
        EXPECT_LUA("vec4(1, 1, 1, 2):length()", "2.6457512378692627");
        EXPECT_LUA("vec4(1, 1, 1, 2):length2()", "7");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3):length()", "0.37416577339172363");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3):length2()", "0.14000001549720764");
        EXPECT_LUA("vec2(0.1, 0.7):length()", "0.70710676908493042");
        EXPECT_LUA("vec2(0.1, 0.7):length2()", "0.49999997019767761");
        EXPECT_LUA("vec4(0.1, 0.2, 0.3, 0.7):length()", "0.79372537136077881");
        EXPECT_LUA("vec4(0.1, 0.2, 0.3, 0.7):length2()", "0.62999999523162842");
        EXPECT_LUA("vec3(0, 0, 0):length()", "0");
        EXPECT_LUA("vec3(0, 0, 0):length2()", "0");
        // The squares are floats too, so they overflow and underflow long before the length would.
        EXPECT_LUA("vec3(1e30, 0, 0):length()", "inf");
        EXPECT_LUA("vec3(1e30, 0, 0):length2()", "inf");
        EXPECT_LUA("vec2(1e30, 0):length()", "inf");
        EXPECT_LUA("vec4(1e30, 0, 0, 0):length()", "inf");
        EXPECT_LUA("vec3(1e19, 0, 0):length()", "9.9999999805064479e+18");
        EXPECT_LUA("vec3(1e19, 0, 0):length2()", "9.9999996802856925e+37");
        EXPECT_LUA("vec3(1e-30, 0, 0):length()", "0");
        EXPECT_LUA("vec3(1e-30, 0, 0):length2()", "0");
        EXPECT_LUA("vec3(16777216, 1, 0):length()", "16777216");
        EXPECT_LUA("vec3(16777216, 1, 0):length2()", "281474976710656");
    }

    TEST_F(LuaUtilMathTest, NormalizeReturnsTheVectorAndItsLength)
    {
        EXPECT_LUA("vec2(3, 4):normalize()", "(0.60000002384185791015625, 0.800000011920928955078125) | 5");
        EXPECT_LUA(
            "vec2(1, 1):normalize()", "(0.707106769084930419921875, 0.707106769084930419921875) | 1.4142135381698608");
        EXPECT_LUA("vec3(1, 2, 3):normalize()",
            "(0.2672612369060516357421875, 0.534522473812103271484375, 0.80178368091583251953125) | "
            "3.7416574954986572");
        EXPECT_LUA("vec3(1, 1, 1):normalize()",
            "(0.57735025882720947265625, 0.57735025882720947265625, 0.57735025882720947265625) | 1.7320507764816284");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3):normalize()",
            "(0.2672612369060516357421875, 0.534522473812103271484375, 0.80178368091583251953125) | "
            "0.37416577339172363");
        EXPECT_LUA("vec4(1, 2, 3, 4):normalize()",
            "(0.1825741827487945556640625, 0.365148365497589111328125, 0.5477225780487060546875, "
            "0.73029673099517822265625) | 5.4772257804870605");
        EXPECT_LUA("vec2(0, 0):normalize()", "(0, 0) | 0");
        EXPECT_LUA("vec3(0, 0, 0):normalize()", "(0, 0, 0) | 0");
        EXPECT_LUA("vec4(0, 0, 0, 0):normalize()", "(0, 0, 0, 0) | 0");
        EXPECT_LUA("vec3(0 * -1, 0, 0):normalize()", "(0, 0, 0) | 0");
        EXPECT_LUA("select('#', vec3(0, 0, 0):normalize())", "2");
        // When the squared length overflows or underflows in a float, the result is a zero vector.
        EXPECT_LUA("vec3(1e30, 0, 0):normalize()", "(0, 0, 0) | inf");
        EXPECT_LUA("vec3(1e-30, 0, 0):normalize()", "(0, 0, 0) | 0");
        EXPECT_LUA("vec3(1e19, 1e19, 0):normalize()",
            "(0.7071068286895751953125, 0.7071068286895751953125, 0) | 1.4142135160245584e+19");
        EXPECT_LUA("vec3(1e18, 1e18, 1e18):normalize()",
            "(0.577350318431854248046875, 0.577350318431854248046875, 0.577350318431854248046875) | "
            "1.7320507716308828e+18");
        EXPECT_LUA("vec3(1e-18, 0, 0):normalize()", "(1, 0, 0) | 1.000000045813705e-18");
        EXPECT_LUA("vec3(1e-20, 1e-20, 0):normalize()",
            "(0.707108676433563232421875, 0.707108676433563232421875, 0) | 1.4142096906491708e-20");
    }

    TEST_F(LuaUtilMathTest, ElementWiseMultiplyAndDivide)
    {
        EXPECT_LUA("vec2(0.1, 0.2):emul(vec2(3, 7))", "(0.300000011920928955078125, 1.39999997615814208984375)");
        EXPECT_LUA("vec3(0.1, 0.2, 0.3):emul(vec3(3, 7, 11))",
            "(0.300000011920928955078125, 1.39999997615814208984375, 3.30000019073486328125)");
        EXPECT_LUA("vec4(0.1, 0.2, 0.3, 0.4):emul(vec4(3, 7, 11, 13))",
            "(0.300000011920928955078125, 1.39999997615814208984375, 3.30000019073486328125, "
            "5.200000286102294921875)");
        EXPECT_LUA(
            "vec3(1e30, 1, 1):emul(vec3(1e30, 1e-30, 0))", "(inf, 1.0000000031710768509710513471352647538e-30, 0)");
        EXPECT_LUA(
            "vec2(0.1, 0.7):ediv(vec2(3, 7))", "(0.0333333350718021392822265625, 0.100000001490116119384765625)");
        EXPECT_LUA("vec3(0.1, 0.7, 5):ediv(vec3(3, 7, 11))",
            "(0.0333333350718021392822265625, 0.100000001490116119384765625, 0.4545454680919647216796875)");
        EXPECT_LUA("vec4(0.1, 0.7, 5, 9):ediv(vec4(3, 7, 11, 49))",
            "(0.0333333350718021392822265625, 0.100000001490116119384765625, 0.4545454680919647216796875, "
            "0.1836734712123870849609375)");
        EXPECT_LUA("vec3(1, -1, 2):ediv(vec3(0, 0, 4))", "(inf, -inf, 0.5)");
        EXPECT_LUA("isnan(vec3(0, 1, 1):ediv(vec3(0, 1, 1)).x)", "true");
    }

    TEST_F(LuaUtilMathTest, EqualityIsExactOnFloats)
    {
        EXPECT_LUA("vec2(1, 2) == vec2(1, 2)", "true");
        EXPECT_LUA("vec2(1, 2) ~= vec2(1, 3)", "true");
        EXPECT_LUA("vec4(1, 2, 3, 4) == vec4(1, 2, 3, 5)", "false");
        EXPECT_LUA("vec3(0.1, 0, 0) == vec3(0.1 + 1e-10, 0, 0)", "true");
        EXPECT_LUA("vec3(0.1, 0, 0) == vec3(0.1 + 1e-7, 0, 0)", "false");
        EXPECT_LUA("vec3(0, 0, 0) == vec3(0 * -1, 0, 0)", "true");
        EXPECT_LUA("vec3(0/0, 0, 0) == vec3(0/0, 0, 0)", "false");
        EXPECT_LUA("vec2(0, 1) / 0 == vec2(0, 1) / 0", "false");
        EXPECT_LUA("vec3(1, 2, 3) == 1", "false");
        EXPECT_LUA("vec3(1, 2, 3) == nil", "false");
        EXPECT_LUA("vec2(1, 2) == vec3(1, 2, 0)", "false");
    }

    TEST_F(LuaUtilMathTest, Vector2Rotate)
    {
        EXPECT_LUA("vec2(1, 0):rotate(pi / 2)", "(-4.3711388286737928865477442741394042969e-08, 1)");
        EXPECT_LUA("vec2(0, 1):rotate(pi / 2)", "(-1, -4.3711388286737928865477442741394042969e-08)");
        EXPECT_LUA("vec2(1, 2):rotate(0)", "(1, 2)");
        EXPECT_LUA("vec2(1, 2):rotate(1)", "(-1.142639636993408203125, 1.9220755100250244140625)");
        EXPECT_LUA("vec2(0.1, 0.2):rotate(-0.5)", "(0.1836433708667755126953125, 0.12757396697998046875)");
        EXPECT_LUA("vec2(1e10, 0):rotate(pi)", "(-10000000000, -874.227783203125)");
        EXPECT_LUA("vec2(0, 0):rotate(1)", "(0, 0)");
        EXPECT_LUA("vec2(1, 2):rotate(1e6)", "(1.63673913478851318359375, 1.52351081371307373046875)");
        EXPECT_LUA_ERROR("vec2(1, 2):rotate('a')", "expected number");
    }

    TEST_F(LuaUtilMathTest, TransformIdentity)
    {
        EXPECT_LUA("T.identity", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.identity * vec3(1, 2, 3)", "(1, 2, 3)");
        EXPECT_LUA("T.identity:apply(vec3(0.1, 0.2, 0.3))",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("T.identity:inverse()", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.identity:inverse() * vec3(0.1, 0.2, 0.3)",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("T.identity * T.identity", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.identity * T.move(1, 2, 3)", "TransformM{ move(1, 2, 3) }");
        EXPECT_LUA("T.move(1, 2, 3) * T.identity", "TransformM{ move(1, 2, 3) }");
        EXPECT_LUA("T.identity:getYaw()", "0");
        EXPECT_LUA("T.identity:getPitch()", "-0");
        EXPECT_LUA("T.identity:getAnglesXZ()", "-0 | 0");
        EXPECT_LUA("T.identity:getAnglesZYX()", "0 | -0 | 0");
        EXPECT_LUA("T.identity.foo", "nil");
        EXPECT_LUA("T.nothing", "nil");
    }

    TEST_F(LuaUtilMathTest, TransformMoveAndScale)
    {
        EXPECT_LUA("T.move(vec3(1, 2, 3))", "TransformM{ move(1, 2, 3) }");
        EXPECT_LUA("T.move(0.1, 0.2, 0.3)", "TransformM{ move(0.1, 0.2, 0.3) }");
        EXPECT_LUA("T.move(0, 0, 0)", "TransformM{ }");
        EXPECT_LUA("T.move(1, 2, 3):apply(vec3(0, 0, 0))", "(1, 2, 3)");
        EXPECT_LUA("T.move(1e7, 0, 0) * vec3(0.5, 0, 0)", "(10000000, 0, 0)");
        EXPECT_LUA("T.move(0.1, 0.2, 0.3) * vec3(0.2, 0.2, 0.2)",
            "(0.300000011920928955078125, 0.4000000059604644775390625, 0.5)");
        EXPECT_LUA("T.scale(vec3(2, 3, 4))", "TransformM{ scale(2, 3, 4) }");
        EXPECT_LUA("T.scale(1, 1, 1)", "TransformM{ }");
        EXPECT_LUA("T.scale(2, 3, 4):apply(vec3(1, 1, 1))", "(2, 3, 4)");
        EXPECT_LUA("T.scale(0.1, 0.2, 0.3) * vec3(1, 1, 1)",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("T.scale(1e30, 1e30, 1e30) * vec3(1e30, 1, 1)",
            "(inf, 1000000015047466219876688855040, 1000000015047466219876688855040)");
        EXPECT_LUA_ERROR("T.move(1, 2) * vec3(0, 0, 0)", noOverload);
        EXPECT_LUA_ERROR("T.move('a')", noOverload);
        EXPECT_LUA_ERROR("T.move(vec2(1, 2))", noOverload);
        EXPECT_LUA_ERROR("T.move(1, 2, 3) * vec2(1, 2)", noOverload);
        EXPECT_LUA_ERROR("T.move(1, 2, 3) * 2", noOverload);
        EXPECT_LUA_ERROR("T.identity * 2", noOverload);
        EXPECT_LUA_ERROR("T.identity * vec4(1, 2, 3, 4)", noOverload);
        EXPECT_LUA_ERROR("2 * T.identity", noOverload);
    }

    TEST_F(LuaUtilMathTest, TransformRotationDirections)
    {
        EXPECT_LUA("T.rotateX(pi / 2) * vec3(1, 2, 3)", "(1, 3, -1.99999964237213134765625)");
        EXPECT_LUA("T.rotateY(pi / 2) * vec3(1, 2, 3)", "(-2.9999997615814208984375, 2, 1.0000002384185791015625)");
        EXPECT_LUA("T.rotateZ(pi / 2) * vec3(1, 2, 3)", "(2, -0.99999988079071044921875, 3)");
        EXPECT_LUA("T.rotateX(1) * vec3(1, 2, 3)", "(1, 3.60501766204833984375, -0.06203520298004150390625)");
        EXPECT_LUA_NEAR("T.rotateY(1) * vec3(1, 2, 3)", "(-1.9841105937957763671875, 2, 2.4623777866363525390625)");
        EXPECT_LUA("T.rotateZ(1) * vec3(1, 2, 3)", "(2.223244190216064453125, 0.2391335964202880859375, 3)");
        EXPECT_LUA("T.rotateZ(0.5):apply(vec3(0.1, 0.2, 0.3))",
            "(0.18364335596561431884765625, 0.12757396697998046875, 0.300000011920928955078125)");
        EXPECT_LUA("T.rotate(1, vec3(1, 2, 3)) * vec3(4, 5, 6)",
            "(2.5372688770294189453125, 6.152342319488525390625, 5.719348907470703125)");
        EXPECT_LUA(
            "T.rotateZ(1) * vec3(1e30, 0, 0)", "(540302269803680906392817893376, -841471003354033215110471745536, 0)");
        EXPECT_LUA("T.rotateZ(1) * vec3(0, 0, 0)", "(0, 0, 0)");
        EXPECT_LUA("T.rotateX(pi / 2)", "TransformQ{ rotation(angle=1.5708, axis=(-1, 0, 0)) }");
        EXPECT_LUA("T.rotateY(pi / 2)", "TransformQ{ rotation(angle=1.5708, axis=(0, -1, 0)) }");
        EXPECT_LUA("T.rotateZ(pi / 2)", "TransformQ{ rotation(angle=1.5708, axis=(0, 0, -1)) }");
        EXPECT_LUA("T.rotate(pi / 2, vec3(0, 0, 1))", "TransformQ{ rotation(angle=1.5708, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(pi, vec3(0, 0, 1))", "TransformQ{ rotation(angle=3.14159, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(-1, vec3(0, 0, 1))", "TransformQ{ rotation(angle=1, axis=(-0, -0, -1)) }");
        EXPECT_LUA_ERROR("T.rotate(1)", "expected userdata");
        EXPECT_LUA_ERROR("T.rotate(vec3(0, 0, 1), 1)", "expected number");
    }

    TEST_F(LuaUtilMathTest, TransformRotationAxisIsNormalizedAndZeroMeansIdentity)
    {
        // Lua has no way to build a non-unit quaternion: T.rotate normalizes its axis, in double precision.
        EXPECT_LUA("T.rotate(1, vec3(2, 4, 6)) * vec3(4, 5, 6)",
            "(2.5372688770294189453125, 6.152342319488525390625, 5.719348907470703125)");
        EXPECT_LUA("T.rotate(1, vec3(0, 0, 2))", "TransformQ{ rotation(angle=1, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(1, vec3(1e30, 0, 0))", "TransformQ{ rotation(angle=1, axis=(1, 0, 0)) }");
        EXPECT_LUA("T.rotate(1, vec3(1e30, 1e30, 0)) * vec3(0, 0, 1)",
            "(0.595009863376617431640625, -0.595009863376617431640625, 0.540302276611328125)");
        EXPECT_LUA("T.rotate(1, vec3(1e-6, 0, 0))", "TransformQ{ rotation(angle=1, axis=(1, 0, 0)) }");
        EXPECT_LUA("T.rotate(1, vec3(1e-10, 0, 0))", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(1, vec3(0, 0, 0))", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(1, vec3(0, 0, 0)) * vec3(1, 2, 3)", "(1, 2, 3)");
        EXPECT_LUA("T.rotate(1, vec3(0, 0, 0)):inverse()", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
        EXPECT_LUA("T.rotate(0, vec3(1, 2, 3))", "TransformQ{ rotation(angle=0, axis=(0, 0, 1)) }");
    }

    TEST_F(LuaUtilMathTest, TransformRotationAngleIsAFloat)
    {
        EXPECT_LUA("T.rotate(2 * pi, vec3(0, 0, 1)) * vec3(1, 2, 3)",
            "(0.99999964237213134765625, 2.0000002384185791015625, 3)");
        EXPECT_LUA("T.rotate(4 * pi, vec3(0, 0, 1)) * vec3(1, 2, 3)",
            "(0.9999992847442626953125, 2.0000002384185791015625, 3)");
        EXPECT_LUA_NEAR("T.rotate(100, vec3(1, 1, 1)) * vec3(1, 2, 3)",
            "(0.84533083438873291015625, 2.58470058441162109375, 2.569968700408935546875)");
        EXPECT_LUA("T.rotate(1e6, vec3(1, 1, 1)) * vec3(1, 2, 3)",
            "(0.861179053783416748046875, 2.40413761138916015625, 2.7346832752227783203125)");
        EXPECT_LUA("isnan((T.rotateZ(math.huge) * vec3(1, 0, 0)).x)", "true");
    }

    TEST_F(LuaUtilMathTest, TransformCompositionAppliesTheRightOperandFirst)
    {
        // M * M
        EXPECT_LUA("T.move(1, 0, 0) * T.scale(2, 2, 2) * vec3(1, 1, 1)", "(3, 2, 2)");
        EXPECT_LUA("T.scale(2, 2, 2) * T.move(1, 0, 0) * vec3(1, 1, 1)", "(4, 2, 2)");
        // M * Q
        EXPECT_LUA("T.move(1, 2, 3) * T.rotateZ(pi / 2) * vec3(1, 0, 0)", "(0.999999940395355224609375, 1, 3)");
        EXPECT_LUA("T.scale(2, 3, 4) * T.rotateZ(1) * vec3(1, 2, 3)",
            "(4.44648838043212890625, 0.7174007892608642578125, 12)");
        EXPECT_LUA("T.scale(2, 3, 4) * (T.rotateZ(1) * vec3(1, 2, 3))",
            "(4.44648838043212890625, 0.7174007892608642578125, 12)");
        // Q * M
        EXPECT_LUA("T.rotateZ(pi / 2) * T.move(1, 2, 3) * vec3(1, 0, 0)", "(2, -2, 3)");
        EXPECT_LUA("T.rotateZ(1) * T.scale(2, 3, 4) * vec3(1, 2, 3)",
            "(6.129430294036865234375, 1.558871746063232421875, 12)");
        EXPECT_LUA("T.rotateZ(1) * (T.scale(2, 3, 4) * vec3(1, 2, 3))",
            "(6.129430294036865234375, 1.558871746063232421875, 12)");
        // Q * Q
        EXPECT_LUA("T.rotateZ(1) * T.rotateX(1) * vec3(1, 2, 3)",
            "(3.5738201141357421875, 1.106328487396240234375, -0.06203496456146240234375)");
        EXPECT_LUA("T.rotateX(1) * T.rotateZ(1) * vec3(1, 2, 3)",
            "(2.223244190216064453125, 2.653617382049560546875, 1.419682979583740234375)");
        // Applying one at a time rounds to float in between, unlike composing the double precision quaternions.
        EXPECT_LUA("T.rotateX(1) * (T.rotateZ(1) * vec3(1, 2, 3))",
            "(2.223244190216064453125, 2.653617382049560546875, 1.4196827411651611328125)");
        EXPECT_LUA("T.rotateZ(1) * T.rotateX(1)",
            "TransformQ{ rotation(angle=1.38344, axis=(-0.659603, 0.360343, -0.659603)) }");
        EXPECT_LUA("T.rotateX(1) * T.rotateZ(1)",
            "TransformQ{ rotation(angle=1.38344, axis=(-0.659603, -0.360343, -0.659603)) }");
        EXPECT_LUA("T.rotate(1, vec3(1, 2, 3)) * T.rotate(2, vec3(3, 2, 1)) * T.rotate(3, vec3(1, 0, 1))",
            "TransformQ{ rotation(angle=4.3702, axis=(0.867001, -0.155082, -0.473559)) }");
        EXPECT_LUA_NEAR(
            "T.rotate(1, vec3(1, 2, 3)) * T.rotate(2, vec3(3, 2, 1)) * T.rotate(3, vec3(1, 0, 1)) * vec3(1, 2, 3)",
            "(-1.78974330425262451171875, 2.40412998199462890625, -2.239861965179443359375)");
        // Longer chains
        EXPECT_LUA("T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4) * vec3(1, 2, 3)",
            "(7.129430294036865234375, 3.558871746063232421875, 15)");
        EXPECT_LUA("T.scale(2, 3, 4) * T.rotateZ(1) * T.move(1, 2, 3) * vec3(1, 2, 3)",
            "(8.8929767608642578125, 1.434801578521728515625, 24)");
        EXPECT_LUA_NEAR("T.rotateZ(1) * T.rotateX(1) * T.move(1, 2, 3) * vec3(1, 2, 3)",
            "(7.147640228271484375, 2.212656497955322265625, -0.12407016754150390625)");
        // A zero rotation, on both sides of a matrix
        EXPECT_LUA("T.rotateZ(0) * T.move(1, 2, 3) * vec3(0.1, 0.2, 0.3)",
            "(1.10000002384185791015625, 2.2000000476837158203125, 3.2999999523162841796875)");
        EXPECT_LUA("T.move(1, 2, 3) * T.rotateZ(0) * vec3(0.1, 0.2, 0.3)",
            "(1.10000002384185791015625, 2.2000000476837158203125, 3.2999999523162841796875)");
        EXPECT_LUA("T.move(1, 2, 3) * T.rotate(1, vec3(0, 0, 0)) * vec3(0.1, 0.2, 0.3)",
            "(1.10000002384185791015625, 2.2000000476837158203125, 3.2999999523162841796875)");
    }

    TEST_F(LuaUtilMathTest, TransformMatricesAreSinglePrecision)
    {
        EXPECT_LUA("T.move(0.1, 0, 0) * T.move(0.2, 0, 0) * vec3(0, 0, 0)", "(0.300000011920928955078125, 0, 0)");
        EXPECT_LUA("T.scale(0.1, 1, 1) * T.scale(3, 1, 1) * vec3(1, 1, 1)", "(0.300000011920928955078125, 1, 1)");
        EXPECT_LUA("T.scale(0.1, 1, 1) * (T.scale(3, 1, 1) * vec3(1, 1, 1))", "(0.300000011920928955078125, 1, 1)");
        // The moves cancel before the vector arrives; one at a time, 0.5 is lost next to -1e8.
        EXPECT_LUA("T.move(1e8, 0, 0) * T.move(-1e8, 0, 0) * vec3(0.5, 0, 0)", "(0.5, 0, 0)");
        EXPECT_LUA("T.move(1e8, 0, 0) * (T.move(-1e8, 0, 0) * vec3(0.5, 0, 0))", "(0, 0, 0)");
    }

    TEST_F(LuaUtilMathTest, TransformMToString)
    {
        EXPECT_LUA("T.move(1, 2, 3) * T.scale(2, 3, 4)", "TransformM{ move(1, 2, 3) scale(2, 3, 4) }");
        EXPECT_LUA("T.scale(0.1, 0.2, 0.3)", "TransformM{ scale(0.1, 0.2, 0.3) }");
        EXPECT_LUA("T.scale(1/3, 1/3, 1/3)", "TransformM{ scale(0.333333, 0.333333, 0.333333) }");
        EXPECT_LUA_STARTS("T.scale(0, 1, 1)", "TransformM{ scale(0, 1, 1) ");
        EXPECT_LUA("T.scale(0, 0, 0)", "TransformM{ scale(0, 0, 0) }");
        EXPECT_LUA("T.scale(-1, -1, -1)", "TransformM{ scale(-1, -1, -1) }");
        // An odd number of negative scales comes out as a half turn and all three negative.
        EXPECT_LUA("T.scale(-1, 1, 1)", "TransformM{ rotation(angle=3.14159, axis=(1, -0, -0)) scale(-1, -1, -1) }");
        EXPECT_LUA("T.scale(-2, 3, 4)", "TransformM{ rotation(angle=3.14159, axis=(1, -0, -0)) scale(-2, -3, -4) }");
        EXPECT_LUA("T.move(1e7, 1e-7, -1e7)", "TransformM{ move(1e+07, 1e-07, -1e+07) }");
        EXPECT_LUA_STARTS("T.move(1, 2, 3) * T.rotateZ(1)",
            "TransformM{ move(1, 2, 3) rotation(angle=1, axis=(0, 0, -1)) scale(1, 1, 1) ");
        EXPECT_LUA_STARTS(
            "T.move(0, 0, 0) * T.rotateX(1)", "TransformM{ rotation(angle=1, axis=(-1, 0, 0)) scale(1, 1, 1) ");
        EXPECT_LUA_STARTS(
            "T.rotateZ(1) * T.scale(2, 3, 4)", "TransformM{ rotation(angle=1, axis=(0, 0, -1)) scale(2, 3, 4) ");
        EXPECT_LUA_STARTS("T.scale(2, 3, 4) * T.rotateZ(1)",
            "TransformM{ rotation(angle=1, axis=(0, 0, -1)) scale(3, 2, 4) rotation(angle=0.570796, axis=(0, 0, -1)) "
            "}");
        EXPECT_LUA("T.rotateZ(0.5) * T.scale(2, 1, 1) * T.rotateZ(0.5)",
            "TransformM{ rotation(angle=1, axis=(0, 0, -1)) scale(2, 1, 1) rotation(angle=0.5, axis=(0, 0, 1)) }");
        // The axis of the last rotation here is mostly rounding noise, so only the start is pinned.
        EXPECT_THAT(show("T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4) * T.rotateX(0.5)"),
            StartsWith("TransformM{ move(1, 2, 3) rotation(angle=1.10848, axis=(-0.412538, 0.22537, -0.882622)) "
                       "scale(2, 3, 4) rotation(angle=0.5, axis=(1, "));
    }

    TEST_F(LuaUtilMathTest, TransformMInverse)
    {
        EXPECT_LUA("T.move(1, 2, 3):inverse()", "TransformM{ move(-1, -2, -3) }");
        EXPECT_LUA("T.move(1, 2, 3):inverse() * vec3(1, 2, 3)", "(0, 0, 0)");
        EXPECT_LUA("T.move(0.1, 0.2, 0.3):inverse() * vec3(0, 0, 0)",
            "(-0.100000001490116119384765625, -0.20000000298023223876953125, -0.300000011920928955078125)");
        EXPECT_LUA("T.scale(2, 4, 8):inverse() * vec3(1, 1, 1)", "(0.5, 0.25, 0.125)");
        // Through a determinant in float, so one third is one bit off what 1 / 3 would give.
        EXPECT_LUA("T.scale(3, 7, 9):inverse() * vec3(1, 1, 1)",
            "(0.333333313465118408203125, 0.142857134342193603515625, 0.111111104488372802734375)");
        EXPECT_LUA("T.scale(3, 7, 9):inverse() * T.scale(3, 7, 9) * vec3(1, 1, 1)",
            "(0.999999940395355224609375, 0.999999940395355224609375, 0.999999940395355224609375)");
        EXPECT_LUA("T.scale(0.1, 0.2, 0.3):inverse() * vec3(1, 1, 1)", "(10, 5, 3.3333332538604736328125)");
        EXPECT_LUA("T.scale(-3, 7, -9):inverse() * vec3(1, 1, 1)",
            "(-0.333333313465118408203125, 0.142857134342193603515625, -0.111111104488372802734375)");
        EXPECT_LUA("T.scale(1e-3, 1e-3, 1e-3):inverse() * vec3(1, 1, 1)",
            "(999.99993896484375, 999.99993896484375, 999.99993896484375)");
        EXPECT_LUA(
            "T.scale(1e-5, 1e-5, 1e-5):inverse() * vec3(1, 1, 1)", "(100000.0078125, 100000.0078125, 100000.0078125)");
        EXPECT_LUA("T.scale(1e-10, 1e-10, 1e-10):inverse() * vec3(1, 1, 1)", "(10000000000, 9999998976, 9999998976)");
        EXPECT_LUA("T.scale(1e-30, 1, 1):inverse() * vec3(1, 1, 1)", "(1000000015047466219876688855040, 1, 1)");
        EXPECT_LUA("(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)):inverse() * vec3(1, 2, 3)", "(0, 0, 0)");
        EXPECT_LUA(
            "(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)):inverse() * (T.move(1, 2, 3) * T.rotateZ(1) * "
            "T.scale(2, 3, 4)) * vec3(1, 2, 3)",
            "(1.00000011920928955078125, 2, 3.000000476837158203125)");
        EXPECT_LUA("(T.rotateZ(0.5) * T.scale(2, 3, 4) * T.rotateX(0.25)):inverse() * vec3(1, 2, 3)",
            "(-0.0406343042850494384765625, 0.53615462779998779296875, 0.91096651554107666015625)");
        EXPECT_LUA("(T.move(1e6, 0, 0) * T.rotateZ(1)):inverse() * vec3(1e6, 1, 0)", "(-0.8125, 0.5625, 0)");
        EXPECT_LUA_STARTS("(T.rotateZ(1) * T.move(1, 2, 3)):inverse()",
            "TransformM{ move(-1, -2, -3) rotation(angle=1, axis=(0, 0, 1)) scale(1, 1, 1) ");
    }

    TEST_F(LuaUtilMathTest, TransformMInverseOfASingularOrHugeMatrixDoesNotFail)
    {
        // The binding throws "This Transform is not invertible" if the inversion reports failure, but for these
        // matrices it does not: the determinant is computed and divided by, whatever it is.
        EXPECT_LUA("isnan((T.scale(0, 1, 1):inverse() * vec3(1, 1, 1)).x)", "true");
        EXPECT_LUA("isnan((T.scale(0, 1, 1):inverse() * vec3(1, 1, 1)).y)", "true");
        EXPECT_LUA("isnan((T.scale(0, 1, 1):inverse() * vec3(1, 1, 1)).z)", "true");
        EXPECT_LUA("isnan((T.scale(0, 0, 0):inverse() * vec3(1, 1, 1)).x)", "true");
        // The determinant overflows in a float at scale 1e15, so one over it is 0.
        EXPECT_LUA("T.scale(1e15, 1e15, 1e15):inverse() * vec3(1, 1, 1)", "(0, 0, 0)");
        EXPECT_LUA("isnan((T.scale(1e30, 1e30, 1e30):inverse() * vec3(1, 1, 1)).x)", "true");
        EXPECT_LUA("(T.scale(1e30, 1e30, 1e30):inverse() * vec3(1, 1, 1)).y", "0");
        EXPECT_LUA("(T.scale(1e30, 1e30, 1e30):inverse() * vec3(1, 1, 1)).z", "0");
    }

    TEST_F(LuaUtilMathTest, TransformQInverse)
    {
        EXPECT_LUA("T.rotateZ(1):inverse()", "TransformQ{ rotation(angle=1, axis=(-0, -0, 1)) }");
        EXPECT_LUA("T.rotateZ(1):inverse():inverse()", "TransformQ{ rotation(angle=1, axis=(0, 0, -1)) }");
        EXPECT_LUA_NEAR(
            "T.rotateZ(1):inverse() * vec3(1, 2, 3)", "(-1.14263975620269775390625, 1.9220755100250244140625, 3)");
        EXPECT_LUA("T.rotate(1, vec3(1, 2, 3)):inverse() * vec3(4, 5, 6)",
            "(3.8866245746612548828125, 3.4536311626434326171875, 7.0687046051025390625)");
        EXPECT_LUA("(T.rotateZ(1) * T.rotateZ(1):inverse()) * vec3(1, 2, 3)", "(1, 2, 3)");
        EXPECT_LUA("(T.rotate(1, vec3(1, 2, 3)) * T.rotate(2, vec3(3, 2, 1))):inverse() * vec3(1, 2, 3)",
            "(1.646373748779296875, 3.134932994842529296875, -1.2089865207672119140625)");
        EXPECT_LUA("T.rotateZ(1):inverse():getYaw()", "-1");
    }

    TEST_F(LuaUtilMathTest, TransformQAngles)
    {
        EXPECT_LUA("T.rotateZ(1):getYaw()", "1");
        EXPECT_LUA("T.rotateZ(1):getPitch()", "-0");
        EXPECT_LUA("T.rotateX(1):getYaw()", "0");
        EXPECT_LUA("T.rotateX(1):getPitch()", "1");
        EXPECT_LUA("T.rotateY(1):getYaw()", "0");
        EXPECT_LUA("T.rotateY(1):getPitch()", "-0");
        EXPECT_LUA("T.rotateZ(4):getYaw()", "-2.2831852436065674");
        EXPECT_LUA("T.rotateX(2):getPitch()", "1.1415927410125732");
        EXPECT_LUA("T.rotateX(pi / 2):getPitch()", "1.5707963705062866");
        EXPECT_LUA("T.rotateX(pi / 2):getYaw()", "0");
        EXPECT_LUA("(T.rotateZ(0.5) * T.rotateX(0.25)):getAnglesXZ()", "0.25 | 0.49999997019767761");
        EXPECT_LUA("(T.rotateX(0.25) * T.rotateZ(0.5)):getAnglesXZ()", "0.21886046230792999 | 0.51339966058731079");
        EXPECT_LUA("T.rotate(1, vec3(1, 2, 3)):getAnglesXZ()", "-0.43554642796516418 | -0.73652613162994385");
        EXPECT_LUA_NEAR(
            "(T.rotateX(0.25) * T.rotateY(0.75) * T.rotateZ(0.5)):getAnglesZYX()", "0.5 | 0.75 | 0.25000002980232239");
        EXPECT_LUA("(T.rotateZ(0.5) * T.rotateY(0.75) * T.rotateX(0.25)):getAnglesZYX()",
            "0.76180970668792725 | 0.47910609841346741 | 0.64534592628479004");
        EXPECT_LUA("T.rotateY(pi / 2):getAnglesZYX()", "0 | 1.5707963705062866 | 0");
        EXPECT_LUA("T.rotateZ(1):getAnglesZYX()", "1 | -0 | 0");
        EXPECT_LUA("T.rotateX(1):getAnglesZYX()", "0 | -0 | 1");
        EXPECT_LUA("T.rotateY(1):getAnglesZYX()", "0 | 1 | 0");
        EXPECT_LUA("T.rotate(1, vec3(1, 2, 3)):getAnglesZYX()",
            "-0.8157309889793396 | -0.58032035827636719 | -0.033343154937028885");
    }

    TEST_F(LuaUtilMathTest, TransformMAngles)
    {
        EXPECT_LUA("(T.move(1, 2, 3) * T.rotateZ(1)):getYaw()", "1");
        EXPECT_LUA("(T.move(1, 2, 3) * T.rotateX(1)):getPitch()", "1.0000001192092896");
        EXPECT_LUA("(T.move(1, 2, 3) * T.rotateZ(0.5) * T.rotateX(0.25)):getAnglesXZ()", "0.25 | 0.50000005960464478");
        EXPECT_LUA("(T.move(1, 2, 3) * T.rotateZ(0.5) * T.rotateY(0.75) * T.rotateX(0.25)):getAnglesZYX()",
            "0.76180970668792725 | 0.47910609841346741 | 0.64534592628479004");
        EXPECT_LUA("T.move(1, 2, 3):getYaw()", "0");
        EXPECT_LUA("T.move(1, 2, 3):getPitch()", "-0");
        EXPECT_LUA("T.move(1, 2, 3):getAnglesXZ()", "-0 | 0");
        EXPECT_LUA("T.move(1, 2, 3):getAnglesZYX()", "0 | -0 | 0");
        EXPECT_LUA("T.scale(2, 3, 4):getAnglesXZ()", "-0 | 0");
        EXPECT_LUA("T.scale(2, 3, 4):getAnglesZYX()", "0 | -0 | 0");
        // A scale applied after the rotation bends the forward vector the angles are read from.
        EXPECT_LUA("(T.scale(2, 3, 4) * T.rotateZ(0.5)):getYaw()", "0.34927022457122803");
        EXPECT_LUA("(T.rotateZ(0.5) * T.scale(2, 3, 4)):getYaw()", "0.5");
        EXPECT_LUA("(T.rotateZ(0.5) * T.scale(2, 3, 4) * T.rotateX(0.25)):getAnglesXZ()",
            "0.32814711332321167 | 0.50000005960464478");
        EXPECT_LUA("(T.rotateZ(0.5) * T.scale(2, 3, 4) * T.rotateX(0.25)):getAnglesZYX()",
            "0.49246001243591309 | -0.090297073125839233 | 0.16650669276714325");
        // Zero vectors stay zero when normalized, so these do not give NaN.
        EXPECT_LUA("T.scale(0, 0, 0):getAnglesXZ()", "-0 | 0");
        EXPECT_LUA("T.scale(0, 0, 0):getAnglesZYX()", "0 | -0 | 0");
    }

    TEST_F(LuaUtilMathTest, BoxFromCenterAndHalfSize)
    {
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6))", "Box{ center(1, 2, 3) halfSize(4, 5, 6) }");
        EXPECT_LUA(
            "box(vec3(0.1, 0.2, 0.3), vec3(0.4, 0.5, 0.7))", "Box{ center(0.1, 0.2, 0.3) halfSize(0.4, 0.5, 0.7) }");
        EXPECT_LUA("box(vec3(1e30, 1e-30, 0.1), vec3(1e10, 123456789, 1/3))",
            "Box{ center(1e+30, 1e-30, 0.1) halfSize(1e+10, 1.23457e+08, 0.333333) }");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)).center", "(1, 2, 3)");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)).halfSize", "(4, 5, 6)");
        EXPECT_LUA("box(vec3(0.1, 0.2, 0.3), vec3(0.4, 0.5, 0.7)).center",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("box(vec3(0.1, 0.2, 0.3), vec3(0.4, 0.5, 0.7)).halfSize",
            "(0.4000000059604644775390625, 0.5, 0.699999988079071044921875)");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)).transform", "TransformM{ move(1, 2, 3) scale(4, 5, 6) }");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)).transform * vec3(1, 1, 1)", "(5, 7, 9)");
        EXPECT_LUA("box(vec3(0.1, 0.2, 0.3), vec3(0.4, 0.5, 0.7)).transform * vec3(1, 1, 1)",
            "(0.5, 0.699999988079071044921875, 1)");
        EXPECT_LUA("verts(box(vec3(1, 2, 3), vec3(4, 5, 6)))",
            "8: (-3, -3, -3) (5, -3, -3) (5, 7, -3) (-3, 7, -3) (-3, -3, 9) (5, -3, 9) (5, 7, 9) (-3, 7, 9)");
        EXPECT_LUA("verts(box(vec3(0.1, 0.2, 0.3), vec3(0.4, 0.5, 0.7)))",
            "8: (-0.300000011920928955078125, -0.300000011920928955078125, -0.39999997615814208984375) "
            "(0.5, -0.300000011920928955078125, -0.39999997615814208984375) "
            "(0.5, 0.699999988079071044921875, -0.39999997615814208984375) "
            "(-0.300000011920928955078125, 0.699999988079071044921875, -0.39999997615814208984375) "
            "(-0.300000011920928955078125, -0.300000011920928955078125, 1) "
            "(0.5, -0.300000011920928955078125, 1) "
            "(0.5, 0.699999988079071044921875, 1) "
            "(-0.300000011920928955078125, 0.699999988079071044921875, 1)");
        EXPECT_LUA("box(vec3(0, 0, 0), vec3(-1, 2, 0))", "Box{ center(0, 0, 0) halfSize(-1, 2, 0) }");
        EXPECT_LUA("verts(box(vec3(0, 0, 0), vec3(-1, 2, 0)))",
            "8: (1, -2, 0) (-1, -2, 0) (-1, 2, 0) (1, 2, 0) (1, -2, 0) (-1, -2, 0) (-1, 2, 0) (1, 2, 0)");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(1, 1, 1)).foo", "nil");
        EXPECT_LUA_ERROR("box(vec3(1, 2, 3))", noOverload);
        EXPECT_LUA_ERROR("box(1)", noOverload);
        EXPECT_LUA_ERROR("box()", noOverload);
        EXPECT_LUA_ERROR("box(vec2(1, 2), vec2(1, 2))", noOverload);
    }

    TEST_F(LuaUtilMathTest, BoxFromTransformM)
    {
        EXPECT_LUA("box(T.move(1, 2, 3))", "Box{ center(1, 2, 3) halfSize(1, 1, 1) }");
        EXPECT_LUA("box(T.move(1, 2, 3)).center", "(1, 2, 3)");
        EXPECT_LUA("box(T.move(1, 2, 3)).halfSize", "(1, 1, 1)");
        EXPECT_LUA("box(T.scale(2, 3, 4))", "Box{ center(0, 0, 0) halfSize(2, 3, 4) }");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.scale(2, 3, 4))", "Box{ center(1, 2, 3) halfSize(2, 3, 4) }");
        EXPECT_LUA("verts(box(T.move(1, 2, 3) * T.scale(2, 3, 4)))",
            "8: (-1, -1, -1) (3, -1, -1) (3, 5, -1) (-1, 5, -1) (-1, -1, 7) (3, -1, 7) (3, 5, 7) (-1, 5, 7)");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.scale(2, 3, 4)).transform * vec3(1, 1, 1)", "(3, 5, 7)");
        EXPECT_LUA(
            "box(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4))", "Box{ center(1, 2, 3) halfSize(2, 3, 4) }");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)).center", "(1, 2, 3)");
        EXPECT_LUA(
            "box(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)).halfSize", "(1.99999988079071044921875, 3, 4)");
        EXPECT_LUA_NEAR("verts(box(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)))",
            "8: (-2.605017185211181640625, 2.062035083770751953125, -1) "
            "(-0.4438083171844482421875, -1.303848743438720703125, -1) "
            "(4.605017185211181640625, 1.937964916229248046875, -1) "
            "(2.4438083171844482421875, 5.303848743438720703125, -1) "
            "(-2.605017185211181640625, 2.062035083770751953125, 7) "
            "(-0.4438083171844482421875, -1.303848743438720703125, 7) "
            "(4.605017185211181640625, 1.937964916229248046875, 7) "
            "(2.4438083171844482421875, 5.303848743438720703125, 7)");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.rotateZ(1) * T.scale(2, 3, 4)).transform * vec3(1, 1, 1)",
            "(4.60501766204833984375, 1.93796503543853759765625, 7)");
        EXPECT_LUA("box(T.scale(0.1, 0.2, 0.3)).halfSize",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("box(T.scale(1e-20, 1e-20, 1e-20)).halfSize",
            "(9.9999996826552253889678874634872052241e-21, 9.9999996826552253889678874634872052241e-21, "
            "9.9999996826552253889678874634872052241e-21)");
        EXPECT_LUA("box(T.scale(1e20, 1e20, 1e20)).halfSize",
            "(100000002004087734272, 100000002004087734272, 100000002004087734272)");
        // Negative scales come out as a rotation and three negative half sizes, and still give the same corners.
        EXPECT_LUA("box(T.scale(-2, 3, 4))", "Box{ center(0, 0, 0) halfSize(-2, -3, -4) }");
        EXPECT_LUA("verts(box(T.scale(-2, 3, 4)))",
            "8: (2, -3, -4) (-2, -3, -4) (-2, 3, -4) (2, 3, -4) (2, -3, 4) (-2, -3, 4) (-2, 3, 4) (2, 3, 4)");
        EXPECT_LUA("box(T.scale(-2, -3, -4))", "Box{ center(0, 0, 0) halfSize(-2, -3, -4) }");
        EXPECT_LUA("verts(box(T.scale(-2, -3, -4)))",
            "8: (2, 3, 4) (-2, 3, 4) (-2, -3, 4) (2, -3, 4) (2, 3, -4) (-2, 3, -4) (-2, -3, -4) (2, -3, -4)");
        // Degenerate
        EXPECT_LUA("box(T.scale(0, 3, 4))", "Box{ center(0, 0, 0) halfSize(0, 3, 4) }");
        EXPECT_LUA("box(T.scale(0, 0, 0))", "Box{ center(0, 0, 0) halfSize(0, 0, 0) }");
        // Sheared: decompose's second rotation is dropped, so the box is not the transform it was made from.
        EXPECT_LUA(
            "box(T.rotateZ(0.5) * T.scale(2, 1, 1) * T.rotateZ(0.5))", "Box{ center(0, 0, 0) halfSize(2, 1, 1) }");
        EXPECT_LUA_NEAR("verts(box(T.rotateZ(0.5) * T.scale(2, 1, 1) * T.rotateZ(0.5)))",
            "8: (-1.9220755100250244140625, 1.14263975620269775390625, -1) "
            "(0.2391335964202880859375, -2.223244190216064453125, -1) "
            "(1.9220755100250244140625, -1.14263975620269775390625, -1) "
            "(-0.2391335964202880859375, 2.223244190216064453125, -1) "
            "(-1.9220755100250244140625, 1.14263975620269775390625, 1) "
            "(0.2391335964202880859375, -2.223244190216064453125, 1) "
            "(1.9220755100250244140625, -1.14263975620269775390625, 1) "
            "(-0.2391335964202880859375, 2.223244190216064453125, 1)");
    }

    TEST_F(LuaUtilMathTest, BoxFromTransformQ)
    {
        EXPECT_LUA("box(T.identity)", "Box{ center(0, 0, 0) halfSize(1, 1, 1) }");
        EXPECT_LUA("box(T.identity).center", "(0, 0, 0)");
        EXPECT_LUA("box(T.identity).halfSize", "(1, 1, 1)");
        EXPECT_LUA("box(T.identity).transform", "TransformM{ }");
        EXPECT_LUA("verts(box(T.identity))",
            "8: (-1, -1, -1) (1, -1, -1) (1, 1, -1) (-1, 1, -1) (-1, -1, 1) (1, -1, 1) (1, 1, 1) (-1, 1, 1)");
        EXPECT_LUA("box(T.rotateZ(1))", "Box{ center(0, 0, 0) halfSize(1, 1, 1) }");
        EXPECT_LUA("box(T.rotateZ(1)).center", "(0, 0, 0)");
        EXPECT_LUA("box(T.rotateZ(1)).halfSize", "(1, 1, 1)");
        EXPECT_LUA_NEAR("verts(box(T.rotateZ(1)))",
            "8: (-1.3817732334136962890625, 0.301168739795684814453125, -1) "
            "(-0.301168739795684814453125, -1.3817732334136962890625, -1) "
            "(1.3817732334136962890625, -0.301168739795684814453125, -1) "
            "(0.301168739795684814453125, 1.3817732334136962890625, -1) "
            "(-1.3817732334136962890625, 0.301168739795684814453125, 1) "
            "(-0.301168739795684814453125, -1.3817732334136962890625, 1) "
            "(1.3817732334136962890625, -0.301168739795684814453125, 1) "
            "(0.301168739795684814453125, 1.3817732334136962890625, 1)");
        EXPECT_LUA_NEAR("verts(box(T.rotate(1, vec3(1, 2, 3))))",
            "8: (-0.512422978878021240234375, -1.38411414623260498046875, -0.9064495563507080078125) "
            "(0.6338527202606201171875, 0.09658364951610565185546875, -1.6090066432952880859375) "
            "(-0.5841605663299560546875, 1.43987262248992919921875, -0.76519489288330078125) "
            "(-1.73043620586395263671875, -0.040825068950653076171875, -0.062637865543365478515625) "
            "(0.5841605663299560546875, -1.43987262248992919921875, 0.76519489288330078125) "
            "(1.73043620586395263671875, 0.040825068950653076171875, 0.062637865543365478515625) "
            "(0.512422978878021240234375, 1.38411414623260498046875, 0.9064495563507080078125) "
            "(-0.6338527202606201171875, -0.09658364951610565185546875, 1.6090066432952880859375)");
        EXPECT_LUA_STARTS(
            "box(T.rotateZ(1)).transform", "TransformM{ rotation(angle=1, axis=(0, 0, -1)) scale(1, 1, 1) ");
        EXPECT_LUA(
            "box(T.rotateZ(1)).transform * vec3(1, 1, 1)", "(1.3817732334136962890625, -0.3011686801910400390625, 1)");
        EXPECT_LUA("box(T.rotateX(1) * T.rotateZ(1)).transform * vec3(1, 2, 3)",
            "(2.223244190216064453125, 2.653617382049560546875, 1.41968286037445068359375)");
    }

    TEST_F(LuaUtilMathTest, BoxEquality)
    {
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)) == box(vec3(1, 2, 3), vec3(4, 5, 6))", "true");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)) == box(vec3(1, 2, 3), vec3(4, 5, 7))", "false");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)) == box(vec3(0, 2, 3), vec3(4, 5, 6))", "false");
        EXPECT_LUA("box(T.identity) == box(vec3(0, 0, 0), vec3(1, 1, 1))", "true");
        EXPECT_LUA("box(T.identity) == box(T.scale(1, 1, 1))", "true");
        EXPECT_LUA("box(T.rotateZ(1)) == box(T.rotateZ(1))", "true");
        EXPECT_LUA("box(T.rotateZ(1)) == box(T.identity)", "false");
        EXPECT_LUA("box(T.rotateZ(1)) == box(vec3(0, 0, 0), vec3(1, 1, 1))", "false");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.scale(2, 3, 4)) == box(vec3(1, 2, 3), vec3(2, 3, 4))", "true");
        EXPECT_LUA("box(T.move(1, 2, 3) * T.rotateZ(1)) == box(T.move(1, 2, 3) * T.rotateZ(1))", "true");
        EXPECT_LUA("box(vec3(1, 2, 3), vec3(4, 5, 6)) == 1", "false");
        // Transforms have no __eq, so two with the same value are different objects.
        EXPECT_LUA(
            "box(vec3(1, 2, 3), vec3(4, 5, 6)).transform == box(vec3(1, 2, 3), vec3(4, 5, 6)).transform", "false");
    }

    TEST_F(LuaUtilMathTest, Color)
    {
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7)", "(0.1, 0.2, 0.3, 0.7)");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7).r", "0.10000000149011612");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7).g", "0.20000000298023224");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7).b", "0.30000001192092896");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7).a", "0.69999998807907104");
        EXPECT_LUA("color.rgb(0.1, 0.2, 0.3).a", "1");
        EXPECT_LUA("color.rgba(2, -1, 0.5, 3)", "(1, 0, 0.5, 1)");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7):asRgba()",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125, "
            "0.699999988079071044921875)");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7):asRgb()",
            "(0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125)");
        EXPECT_LUA("color.rgba(0.1, 0.2, 0.3, 0.7):asHex()", "19334c");
        EXPECT_LUA("color.rgb(1, 1, 1):asHex()", "ffffff");
        EXPECT_LUA("color.rgb(0, 0, 0):asHex()", "000000");
        EXPECT_LUA("color.hex('102030')", "(0.0627451, 0.12549, 0.188235, 1)");
        EXPECT_LUA("color.hex('102030').r", "0.062745101749897003");
        EXPECT_LUA("color.hex('102030'):asHex()", "102030");
        EXPECT_LUA("color.hex('ABCDEF'):asRgb()",
            "(0.6705882549285888671875, 0.80392158031463623046875, 0.93725490570068359375)");
        EXPECT_LUA_ERROR("color.hex('12345')", "Invalid hex color");
        EXPECT_LUA_ERROR("color.hex('gggggg')", "Invalid hex color");
        EXPECT_LUA("color.rgb(1, 0, 0) == color.hex('ff0000')", "true");
        EXPECT_LUA("color.rgb(0.1, 0, 0) == color.rgb(0.2, 0, 0)", "false");
        EXPECT_LUA("color.rgba(1, 0, 0, 0.5) == color.rgb(1, 0, 0)", "false");
        EXPECT_LUA("color.commaString('16, 32, 48'):asRgb()",
            "(0.062745101749897003173828125, 0.12549020349979400634765625, 0.18823529779911041259765625)");
        EXPECT_LUA("color.commaString('16, 32, 48, 64').a", "0.25098040699958801");
    }

#undef EXPECT_LUA
#undef EXPECT_LUA_ERROR
#undef EXPECT_LUA_NEAR
#undef EXPECT_LUA_STARTS
}
