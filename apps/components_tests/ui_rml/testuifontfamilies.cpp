// Task 0208: a second bitmap font family for text that has to be legible. Morrowind's first font (Magic Cards) draws
// "P" as a looped capital that reads as "D" at 16 px, in MyGUI and in RmlUi alike (docs/tasks/0208), so the pack's
// windows can ask for the second Morrowind font (Century Gothic) with the class `plain-font`. Written before the code
// and run red against a stub.
#include <components/ui_rml/fontfamilies.hpp>

#include <gtest/gtest.h>

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

    TEST(UiFontFamilies, theNamesAreStable)
    {
        EXPECT_EQ(kMainFontFamily, "Morrowind");
        EXPECT_EQ(kPlainFontFamily, "MorrowindPlain");
    }

    TEST(UiFontFamilies, thePlainFontComesFromItsOwnFileWhenTheGameNamesOne)
    {
        EXPECT_EQ(plainFontPath("century_gothic_font_regular", "Magic_Cards_Regular"),
            "fonts/century_gothic_font_regular.fnt");
    }

    TEST(UiFontFamilies, withNoPlainFontTheMainFontStandsInSoTheNameStillResolves)
    {
        EXPECT_EQ(plainFontPath("", "Magic_Cards_Regular"), "fonts/Magic_Cards_Regular.fnt");
    }

    TEST(UiFontFamilies, withNoFontAtAllThereIsNoPath)
    {
        EXPECT_EQ(plainFontPath("", ""), "");
    }

    TEST(UiFontFamilies, theBaseSheetHasThePlainFontClass)
    {
        const std::string base = readSource("ui/base.rcss");
        const std::size_t at = base.find(".plain-font");
        ASSERT_NE(at, std::string::npos);
        const std::size_t close = base.find('}', at);
        ASSERT_NE(close, std::string::npos);
        const std::string rule = base.substr(at, close - at);
        EXPECT_NE(rule.find("font-family"), std::string::npos);
        EXPECT_NE(rule.find(std::string(kPlainFontFamily)), std::string::npos);
    }

    TEST(UiFontFamilies, theCrimeWindowUsesIt)
    {
        const std::string window = readSource("openeden/skyrimse/crime_windows.lua");
        EXPECT_NE(window.find("plain-font"), std::string::npos);
    }
}
