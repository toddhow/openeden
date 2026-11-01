#include "apps/opencs/model/mesh/texturereport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        using Size = std::pair<std::uint32_t, std::uint32_t>;
        using Lookup = std::function<std::optional<Size>(const std::string&)>;

        TEST(CSMMeshTextureReportTest, foundTexturesCarryTheirSize)
        {
            const Lookup lookup = [](const std::string& name) -> std::optional<Size> {
                if (name == "textures/a.dds")
                    return Size{ 256, 128 };
                return std::nullopt;
            };
            const TextureReport report = buildTextureReport({ "textures/a.dds" }, lookup);
            ASSERT_EQ(report.entries.size(), 1u);
            EXPECT_EQ(report.entries[0].name, "textures/a.dds");
            EXPECT_TRUE(report.entries[0].found);
            EXPECT_EQ(report.entries[0].width, 256u);
            EXPECT_EQ(report.entries[0].height, 128u);
            EXPECT_TRUE(report.warnings.empty());
        }

        TEST(CSMMeshTextureReportTest, missingTexturesAreFlaggedWithAWarning)
        {
            const Lookup lookup = [](const std::string&) -> std::optional<Size> { return std::nullopt; };
            const TextureReport report = buildTextureReport({ "textures/gone.dds" }, lookup);
            ASSERT_EQ(report.entries.size(), 1u);
            EXPECT_EQ(report.entries[0].name, "textures/gone.dds");
            EXPECT_FALSE(report.entries[0].found);
            ASSERT_EQ(report.warnings.size(), 1u);
            EXPECT_EQ(report.warnings[0], "missing texture: textures/gone.dds");
        }

        TEST(CSMMeshTextureReportTest, namesAreDistinctIgnoringCaseKeepingTheFirstSpellingAndOrder)
        {
            const Lookup lookup = [](const std::string&) -> std::optional<Size> { return Size{ 4, 4 }; };
            const TextureReport report
                = buildTextureReport({ "Tex/B.dds", "tex/a.dds", "TEX/b.DDS", "tex/A.dds", "tex/c.dds" }, lookup);
            ASSERT_EQ(report.entries.size(), 3u);
            EXPECT_EQ(report.entries[0].name, "Tex/B.dds");
            EXPECT_EQ(report.entries[1].name, "tex/a.dds");
            EXPECT_EQ(report.entries[2].name, "tex/c.dds");
        }

        TEST(CSMMeshTextureReportTest, eachDistinctNameIsLookedUpOnce)
        {
            std::map<std::string, int> calls;
            const Lookup lookup = [&calls](const std::string& name) -> std::optional<Size> {
                ++calls[name];
                return std::nullopt;
            };
            const TextureReport report = buildTextureReport({ "a", "A", "a", "b" }, lookup);

            int total = 0;
            for (const auto& [name, count] : calls)
            {
                EXPECT_EQ(count, 1) << name;
                total += count;
            }
            EXPECT_EQ(total, 2);
            // a duplicate gives one warning, not several
            EXPECT_EQ(report.warnings.size(), 2u);
        }

        TEST(CSMMeshTextureReportTest, emptyNamesAreSkippedAndNeverLookedUp)
        {
            int calls = 0;
            const Lookup lookup = [&calls](const std::string&) -> std::optional<Size> {
                ++calls;
                return Size{ 1, 1 };
            };
            const TextureReport report = buildTextureReport({ "", "x", "" }, lookup);
            ASSERT_EQ(report.entries.size(), 1u);
            EXPECT_EQ(report.entries[0].name, "x");
            EXPECT_EQ(calls, 1);
            EXPECT_TRUE(report.warnings.empty());
        }

        TEST(CSMMeshTextureReportTest, mixedFoundAndMissingKeepOrderAndWarnOnlyForMissing)
        {
            const Lookup lookup = [](const std::string& name) -> std::optional<Size> {
                if (name == "ok1" || name == "ok2")
                    return Size{ 64, 64 };
                return std::nullopt;
            };
            const TextureReport report = buildTextureReport({ "ok1", "bad1", "ok2", "bad2" }, lookup);
            ASSERT_EQ(report.entries.size(), 4u);
            EXPECT_TRUE(report.entries[0].found);
            EXPECT_FALSE(report.entries[1].found);
            EXPECT_TRUE(report.entries[2].found);
            EXPECT_FALSE(report.entries[3].found);
            const std::vector<std::string> expected{ "missing texture: bad1", "missing texture: bad2" };
            EXPECT_EQ(report.warnings, expected);
        }

        TEST(CSMMeshTextureReportTest, noNamesGiveAnEmptyReport)
        {
            const Lookup lookup = [](const std::string&) -> std::optional<Size> { return Size{ 1, 1 }; };
            const TextureReport report = buildTextureReport({}, lookup);
            EXPECT_TRUE(report.entries.empty());
            EXPECT_TRUE(report.warnings.empty());
        }

        TEST(CSMMeshTextureReportTest, defaultEntryIsNotFoundAndZeroSized)
        {
            const TextureEntry entry;
            EXPECT_TRUE(entry.name.empty());
            EXPECT_EQ(entry.width, 0u);
            EXPECT_EQ(entry.height, 0u);
            EXPECT_FALSE(entry.found);
        }
    }
}
