// Task 0092, the last compile limits on the New Vegas masters: "%" and a minus after a command with skipped arguments.
//
// In plain words: these tests compile and run generated scripts (invented names, no game text) to check what the two
// parser options compute: the remainder operator "%" (binds like "*" and "/", works on whole numbers, a division by
// zero stops the script like "/" does) and "Command - x" (a subtraction when the command's arguments are not known).
// The last test is the gated real-data comparison: with OPENEDEN_FNV_DATA set it compiles the New Vegas masters and
// can write one digest per compiled unit to a file outside the repository (OPENEDEN_OBSCRIPT_DIGEST_OUT) and compare
// against an earlier file (OPENEDEN_OBSCRIPT_DIGEST_BASE). It prints only counts.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "realdatasupport.hpp"
#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::InstanceId;
    using openeden::script::ScriptSource;
    using openeden::script::UnknownFunctionPolicy;

    struct ResidueFixture
    {
        explicit ResidueFixture(UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant)
            : mFixture(policy)
        {
        }

        bool compile(const std::string& body)
        {
            ScriptSource source;
            source.name = "Residue";
            source.text = "ScriptName Residue\nlong number\nlong other\nlong third\nfloat real\nBegin GameMode\n" + body
                + "\nEnd\n";
            const auto result = mFixture.mRuntime->compile(source);
            mOk = result.ok;
            mScript = result.script;
            mReports.clear();
            for (const auto& report : result.reports)
            {
                mReports += report.format() + "; ";
            }
            return mOk;
        }

        bool run()
        {
            mInstance = mFixture.mRuntime->createInstance(mScript, sPlayer);
            const auto result = mFixture.mRuntime->run(mInstance, "GameMode");
            mRunError = result.error.format();
            return result.ok;
        }

        std::int32_t integer(const char* name) const { return mFixture.mRuntime->getLocal(mInstance, name).integer; }

        RuntimeFixture mFixture;
        bool mOk = false;
        openeden::script::ScriptId mScript = 0;
        InstanceId mInstance = 0;
        std::string mReports;
        std::string mRunError;
    };

    std::int32_t evaluate(const std::string& expression)
    {
        ResidueFixture f;
        EXPECT_TRUE(f.compile("Set number to " + expression)) << expression << ": " << f.mReports;
        EXPECT_TRUE(f.run()) << expression << ": " << f.mRunError;
        return f.integer("number");
    }

    TEST(ObScriptResidue, modulo_gives_the_remainder)
    {
        EXPECT_EQ(evaluate("7 % 3"), 1);
        EXPECT_EQ(evaluate("9 % 3"), 0);
        EXPECT_EQ(evaluate("2 % 5"), 2);
    }

    TEST(ObScriptResidue, modulo_binds_like_multiplication_and_division)
    {
        EXPECT_EQ(evaluate("1 + 7 % 4"), 4);
        EXPECT_EQ(evaluate("20 % 7 % 4"), 2);
        EXPECT_EQ(evaluate("2 * 7 % 4"), 2); // (2 * 7) % 4
        EXPECT_EQ(evaluate("14 / 4 % 2"), 1); // (14 / 4) % 2
        EXPECT_EQ(evaluate("( 1 + 7 ) % 3"), 2);
    }

    TEST(ObScriptResidue, modulo_takes_the_sign_of_the_left_operand)
    {
        EXPECT_EQ(evaluate("0 - 7 % 3"), -1);
        EXPECT_EQ(evaluate("( 0 - 7 ) % 3"), -1);
        EXPECT_EQ(evaluate("7 % ( 0 - 3 )"), 1);
    }

    TEST(ObScriptResidue, modulo_truncates_a_floating_point_operand_to_a_whole_number)
    {
        EXPECT_EQ(evaluate("7.9 % 4"), 3);
        EXPECT_EQ(evaluate("9 % 4.9"), 1);
    }

    TEST(ObScriptResidue, modulo_by_zero_stops_the_script_like_division_does)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 0\nSet number to 5 % other"));
        EXPECT_FALSE(f.run());
        ResidueFixture division;
        ASSERT_TRUE(division.compile("Set other to 0\nSet number to 5 / other"));
        EXPECT_FALSE(division.run());
    }

    TEST(ObScriptResidue, the_most_negative_number_modulo_minus_one_is_zero_and_does_not_trap)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 0 - 2147483647 - 1\nSet third to 0 - 1\nSet number to other % third"))
            << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), 0);
    }

    TEST(ObScriptResidue, modulo_works_in_a_condition)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 9\nIf ( other % 2 == 1 && other % 3 == 0 )\n Set number to 1\nEndIf"))
            << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), 1);
    }

    TEST(ObScriptResidue, modulo_with_variables_on_both_sides)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 17\nSet third to 5\nSet number to other % third")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), 2);
    }

    TEST(ObScriptResidue, a_minus_and_a_name_after_an_unknown_command_is_a_subtraction)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 5\nSet number to Wibble - other")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), -5);
    }

    TEST(ObScriptResidue, a_minus_and_a_bracket_after_an_unknown_command_is_a_subtraction)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 5\nSet number to Wibble - ( other + 1 )")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), -6);
    }

    TEST(ObScriptResidue, a_minus_after_an_unknown_command_works_in_a_condition)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 5\nIf ( Wibble - other < 0 )\n Set number to 1\nEndIf")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), 1);
    }

    TEST(ObScriptResidue, a_minus_after_the_member_of_a_call_result_is_a_subtraction)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 5\nSet number to Crate.Wibble - other")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), -5);
    }

    TEST(ObScriptResidue, a_minus_and_a_number_after_an_unknown_command_is_still_an_argument)
    {
        for (const char* text : { "Wibble - 1", "Wibble -1", "Wibble - 2.5" })
        {
            ResidueFixture f;
            ASSERT_TRUE(f.compile(std::string("Set number to ") + text)) << text << ": " << f.mReports;
            ASSERT_TRUE(f.run()) << text << ": " << f.mRunError;
            EXPECT_EQ(f.integer("number"), 0) << text;
        }
    }

    TEST(ObScriptResidue, a_known_command_keeps_its_arguments)
    {
        ResidueFixture f;
        ASSERT_TRUE(f.compile("Set other to 5\nSet number to GetItemCount Crate - other")) << f.mReports;
        ASSERT_TRUE(f.run()) << f.mRunError;
        EXPECT_EQ(f.integer("number"), 0 - 5 + 0);
    }

    // ---- the gated real-data comparison ---------------------------------------------------------------------------

    std::uint64_t digestOf(openeden::script::ObScriptContract& runtime, const RealData::Unit& unit, bool& ok)
    {
        ScriptSource source;
        source.name = "script";
        source.text = unit.mSource;
        source.kind = unit.mIsScript ? openeden::script::ScriptKind::Script : openeden::script::ScriptKind::Fragment;
        const auto result = runtime.compile(source);
        ok = result.ok;
        std::uint64_t digest = 0;
        if (result.ok)
        {
            ScriptObscript::programDigest(runtime, result.script, digest);
        }
        return digest;
    }

    TEST(ObScriptRealData, program_digests_of_the_new_vegas_masters)
    {
        RealData::Masters masters;
        if (!RealData::openMasters(masters))
        {
            GTEST_SKIP() << "OPENEDEN_FNV_DATA does not name a Fallout New Vegas Data folder";
        }
        const std::vector<RealData::Unit> units = RealData::readUnits(masters);
        RealData::DataWorld world;
        std::set<std::string> variableNames;
        RealData::buildWorld(masters, world, variableNames);

        // The digests of an earlier run: one line "index digest" per unit that compiled.
        std::map<std::size_t, std::uint64_t> base;
        if (const char* path = std::getenv("OPENEDEN_OBSCRIPT_DIGEST_BASE"))
        {
            std::ifstream in(path);
            std::size_t index = 0;
            std::uint64_t digest = 0;
            while (in >> index >> std::hex >> digest >> std::dec)
            {
                base[index] = digest;
            }
        }
        std::ofstream out;
        if (const char* path = std::getenv("OPENEDEN_OBSCRIPT_DIGEST_OUT"))
        {
            out.open(path);
        }

        std::size_t compiled = 0;
        std::size_t comparedBoth = 0;
        std::size_t changed = 0;
        std::size_t newlyCompiled = 0;
        std::size_t lostNow = 0;
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            // a runtime of its own per unit: the codes of unknown names then do not depend on the units before
            auto runtime = ScriptObscript::createRuntime(world, UnknownFunctionPolicy::Tolerant);
            bool ok = false;
            const std::uint64_t digest = digestOf(*runtime, units[i], ok);
            const auto before = base.find(i);
            if (ok)
            {
                ++compiled;
                if (out.is_open())
                {
                    out << i << " " << std::hex << digest << std::dec << "\n";
                }
                if (before != base.end())
                {
                    ++comparedBoth;
                    changed += before->second != digest ? 1 : 0;
                }
                else if (!base.empty())
                {
                    ++newlyCompiled;
                }
            }
            else if (before != base.end())
            {
                ++lostNow;
            }
        }
        std::cout << "OBSCRIPT-DIGEST units " << units.size() << ", compiled " << compiled << "\n";
        if (!base.empty())
        {
            std::cout << "OBSCRIPT-DIGEST against the earlier run: compiled in both " << comparedBoth
                      << ", program changed " << changed << ", newly compiled " << newlyCompiled
                      << ", no longer compiled " << lostNow << "\n";
            EXPECT_EQ(changed, 0u);
            EXPECT_EQ(lostNow, 0u);
        }
    }
}
