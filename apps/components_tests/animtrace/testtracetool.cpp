// Task 0067, stage 0: the creativemotioncheck command line (components/animtrace/tracetool.hpp), called as a
// function with a log file written by the test.

#include <components/animtrace/tracetool.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace AnimTrace;

    /// A log of 12 player frames with the head turning 3 degrees per frame and a phase line after frame 5.
    std::string sampleLog()
    {
        std::string text = "[21:22:11.000 I] Loading settings\n";
        for (int i = 0; i < 12; ++i)
        {
            const double half = (3.0 * i) * 3.14159265358979323846 / 360.0;
            char head[96];
            std::snprintf(head, sizeof(head), " bip01 head=(0.0000 0.0000 %.6f %.6f)", std::sin(half), std::cos(half));
            text += "[21:22:11.100 I] CMTRACE ref=player dt=0.020000 g0=idle g1=idle g2=idle g3=idle pos=(0 0 0)"
                    " moved=(0 0 0) loco=(speed 0.0 phase 0) animspeed=1.000 driver=off"
                + std::string(head) + "\n";
            if (i == 5)
            {
                text += "[21:22:11.100 I] CMPHASE turn t=1.000\n";
            }
        }
        return text;
    }

    class TraceToolTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            mPath = std::filesystem::temp_directory_path() / "openeden_animtrace_tool_test.log";
            std::filesystem::remove(mPath);
        }

        void TearDown() override { std::filesystem::remove(mPath); }

        void writeLog(const std::string& text)
        {
            std::ofstream file(mPath, std::ios::binary);
            file << text;
        }

        int run(std::vector<std::string> args, std::string* out = nullptr, std::string* err = nullptr)
        {
            std::ostringstream outStream;
            std::ostringstream errStream;
            const int code = runTraceTool(args, outStream, errStream);
            if (out != nullptr)
            {
                *out = outStream.str();
            }
            if (err != nullptr)
            {
                *err = errStream.str();
            }
            return code;
        }

        std::filesystem::path mPath;
    };

    TEST_F(TraceToolTest, WritesATableForAGoodLog)
    {
        writeLog(sampleLog());
        std::string out;
        EXPECT_EQ(run({ "--label", "vanilla", mPath.string() }, &out), 0);
        EXPECT_NE(out.find("vanilla"), std::string::npos);
        EXPECT_NE(out.find("turn"), std::string::npos);
        EXPECT_NE(out.find("bip01 head"), std::string::npos);
    }

    TEST_F(TraceToolTest, WritesJsonWhenAsked)
    {
        writeLog(sampleLog());
        std::string out;
        EXPECT_EQ(run({ "--json", "--label", "x", mPath.string() }, &out), 0);
        EXPECT_EQ(out.front(), '{');
        EXPECT_NE(out.find("\"name\": \"turn\", \"frame\": 6"), std::string::npos);
    }

    TEST_F(TraceToolTest, TwoRunsGiveTheSameBytes)
    {
        writeLog(sampleLog());
        std::string first;
        std::string second;
        EXPECT_EQ(run({ "--json", mPath.string() }, &first), 0);
        EXPECT_EQ(run({ "--json", mPath.string() }, &second), 0);
        EXPECT_EQ(first, second);
    }

    TEST_F(TraceToolTest, TheWindowOptionIsRead)
    {
        writeLog(sampleLog());
        std::string out;
        EXPECT_EQ(run({ "--window", "2", mPath.string() }, &out), 0);
        std::string err;
        EXPECT_EQ(run({ "--window", "two", mPath.string() }, nullptr, &err), 2);
        EXPECT_NE(err.find("--window"), std::string::npos);
    }

    TEST_F(TraceToolTest, ALogWithoutTraceLinesOfTheActorIsOne)
    {
        writeLog("[21:22:11.000 I] Loading settings\n");
        std::string err;
        EXPECT_EQ(run({ mPath.string() }, nullptr, &err), 1);
        EXPECT_NE(err.find("creativemotion animation debug"), std::string::npos);
        writeLog(sampleLog());
        EXPECT_EQ(run({ "--ref", "somebodyelse", mPath.string() }, nullptr, &err), 1);
    }

    TEST_F(TraceToolTest, BadArgumentsAreTwo)
    {
        std::string err;
        EXPECT_EQ(run({}, nullptr, &err), 2);
        EXPECT_NE(err.find("usage"), std::string::npos);
        EXPECT_EQ(run({ "--nonsense", "x" }, nullptr, &err), 2);
        EXPECT_EQ(run({ "--label" }, nullptr, &err), 2);
        EXPECT_EQ(run({ "a.log", "b.log" }, nullptr, &err), 2);
        EXPECT_EQ(run({ (mPath.parent_path() / "no_such_openeden_log.log").string() }, nullptr, &err), 2);
        EXPECT_NE(err.find("cannot read"), std::string::npos);
    }
}
