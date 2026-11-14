// Task 0166: the base style sheet and the shipped themes carry the looks of the second component slice (Checkbox,
// Toggle, Slider, ProgressBar, ScrollView, Tabs) through tokens: every new token is required, declared by every shipped
// theme and read by the base sheet, and every class the components use is styled there.
#include <components/ui_rml/themesheet.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    using namespace UiRml;

    std::string readSource(const std::string& relative)
    {
        std::ifstream file(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / relative);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    bool has(const std::vector<std::string>& list, const std::string& value)
    {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    const char* const kNewTokens[]
        = { "--color-track", "--color-accent", "--color-tab", "--color-tab-active", "--size-check" };

    TEST(UiWidgetSheet, theNewTokensAreRequired)
    {
        for (const char* token : kNewTokens)
            EXPECT_TRUE(has(requiredTokens(), token)) << token;
    }

    TEST(UiWidgetSheet, everyShippedThemeDeclaresTheNewTokens)
    {
        for (const char* theme : { "neutral", "high-contrast" })
        {
            const auto defined = tokensDefined(readSource(std::string("ui/themes/") + theme + "/theme.rcss"));
            for (const char* token : kNewTokens)
                EXPECT_TRUE(has(defined, token)) << theme << " lacks " << token;
        }
    }

    TEST(UiWidgetSheet, theBaseSheetReadsTheNewTokensAndStylesEveryComponentClass)
    {
        const std::string base = readSource("ui/base.rcss");
        ASSERT_FALSE(base.empty());
        const auto used = tokensUsed(base);
        for (const char* token : kNewTokens)
            EXPECT_TRUE(has(used, token)) << token << " is declared but nothing reads it";
        for (const char* selector :
            { ".checkbox", ".check-box", ".check-label", ".toggle", ".toggle-track", ".toggle-knob", ".toggle-label",
                "input.slider", ".progress", ".progress-fill", ".scrollview", ".tab-bar", ".tab-panel", ".tab " })
            EXPECT_NE(base.find(selector), std::string::npos) << selector;
        EXPECT_TRUE(validateStyleSheet(base, "ui/base.rcss").empty());
    }
}
