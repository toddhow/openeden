// Task 0122: the gated real-data run of the conditions engine. Evaluates every INFO condition list of the five Skyrim
// SE masters against a stub world that answers 0 or false, and prints AGGREGATE numbers only (lines starting
// SKYRIMSE-CONDITIONS): how many lists hit an unknown function and which function indices they are. No record name,
// form id or path is printed. Skipped when OPENEDEN_SKYRIMSE_DATA does not name a Data folder.
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace game_skyrimse::conditions;

    class ZeroWorld final : public ConditionWorld
    {
    public:
        Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 0; }
        bool hasBaseId(Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
    };

    class InfoVisitor final : public Content::Visitor
    {
    public:
        InfoVisitor(Evaluator& evaluator, const std::string& file)
            : mEvaluator(evaluator)
            , mFile(file)
        {
        }
        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.mType != Content::fourCC("INFO"))
                return true;
            std::span<const std::byte> data = record.mData;
            if (record.isCompressed())
            {
                Content::Error error;
                if (!inflateRecord(record, mFile, mBuffer, error))
                {
                    ++mInflateFailures;
                    return true;
                }
                data = mBuffer;
            }
            std::vector<survey::Ctda> list;
            if (!decodeConditions(data, list))
                ++mMalformed;
            if (list.empty())
                return true;
            ++mListsWithConditions;
            mEvaluator.evaluate(list, mWorld);
            return true;
        }
        std::uint64_t mListsWithConditions = 0;
        std::uint64_t mMalformed = 0;
        std::uint64_t mInflateFailures = 0;

    private:
        Evaluator& mEvaluator;
        const std::string& mFile;
        ZeroWorld mWorld;
        std::vector<std::byte> mBuffer;
    };

    TEST(SkyrimSeConditionsReal, every_info_condition_list_of_the_masters)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder";
        const fs::path data = env;

        std::vector<std::pair<std::string, fs::path>> files;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::is_regular_file(data / name))
                files.emplace_back(name, data / name);
        ASSERT_FALSE(files.empty());

        Evaluator evaluator;
        std::uint64_t logged = 0;
        evaluator.setLog([&](const std::string&) { ++logged; });
        std::uint64_t lists = 0, malformed = 0, inflateFailures = 0;
        for (const auto& [name, path] : files)
        {
            Content::Error error;
            auto plugin = Content::Plugin::openFile(path, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            InfoVisitor visitor(evaluator, name);
            const auto walkError = plugin->walk(visitor);
            EXPECT_FALSE(walkError.has_value()) << (walkError ? walkError->describe() : "");
            lists += visitor.mListsWithConditions;
            malformed += visitor.mMalformed;
            inflateFailures += visitor.mInflateFailures;
        }

        auto line = [](const std::string& text) { std::cout << "SKYRIMSE-CONDITIONS " << text << "\n"; };
        const std::uint64_t evaluated = evaluator.mLists - evaluator.mListsFailedClosed;
        auto pct = [](std::uint64_t a, std::uint64_t b) {
            return b == 0 ? std::string("0") : std::to_string(static_cast<double>(a) * 100.0 / static_cast<double>(b));
        };
        line("INFO lists with conditions " + std::to_string(lists) + ", malformed " + std::to_string(malformed)
            + ", inflate failures " + std::to_string(inflateFailures));
        line("lists fully evaluated (every function known) " + std::to_string(evaluated) + " ("
            + pct(evaluated, evaluator.mLists) + " percent), failed closed on an unknown function "
            + std::to_string(evaluator.mListsFailedClosed));
        line("conditions " + std::to_string(evaluator.mConditions) + ", unknown "
            + std::to_string(evaluator.mConditionsUnknown) + " ("
            + pct(evaluator.mConditionsUnknown, evaluator.mConditions) + " percent), bad compare "
            + std::to_string(evaluator.mConditionsBadCompare) + ", distinct unknown functions "
            + std::to_string(evaluator.mUnknownFunctions.size()) + ", log lines " + std::to_string(logged));
        std::vector<std::pair<std::uint16_t, std::uint64_t>> unknown(
            evaluator.mUnknownFunctions.begin(), evaluator.mUnknownFunctions.end());
        std::sort(unknown.begin(), unknown.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        for (std::size_t i = 0; i < unknown.size() && i < 15; ++i)
            line("unknown " + survey::describeFunction(unknown[i].first) + " conditions "
                + std::to_string(unknown[i].second));

        EXPECT_GT(lists, 0u);
        EXPECT_EQ(evaluator.mLists, lists);
        EXPECT_EQ(malformed, 0u);
    }
}
