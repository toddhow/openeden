// Task 0215: the shipped "skyrim" theme (the neutral theme with the plain face). Read from the source tree.
#include <components/ui_rml/themesheet.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace
{
    using namespace UiRml;

    std::string readShipped(const std::string& relative)
    {
        std::ifstream in(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / relative);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    TEST(UiThemeSkyrim, declaresEveryRequiredTokenAndPassesTheValidator)
    {
        const std::string text = readShipped("ui/themes/skyrim/theme.rcss");
        ASSERT_FALSE(text.empty());
        const std::vector<std::string> defined = tokensDefined(text);
        for (const std::string& token : requiredTokens())
            EXPECT_NE(std::find(defined.begin(), defined.end(), token), defined.end()) << token;
        EXPECT_TRUE(validateStyleSheet(text, "ui/themes/skyrim/theme.rcss").empty());
    }

    TEST(UiThemeSkyrim, usesThePlainFamilyAndNeutralKeepsTheBitmapOne)
    {
        EXPECT_NE(readShipped("ui/themes/skyrim/theme.rcss").find("--font-family: MorrowindPlain;"), std::string::npos);
        const std::string neutral = readShipped("ui/themes/neutral/theme.rcss");
        EXPECT_NE(neutral.find("--font-family: Morrowind;"), std::string::npos);
        EXPECT_EQ(neutral.find("MorrowindPlain"), std::string::npos);
    }

    TEST(UiThemeSkyrim, differsFromNeutralOnlyInTheFont)
    {
        // every declaration line other than the font family is the same in both themes
        const auto declarations = [](const std::string& text) {
            std::vector<std::string> lines;
            std::istringstream in(text);
            for (std::string line; std::getline(in, line);)
                if (line.find("--") != std::string::npos && line.find("--font-family") == std::string::npos
                    && line.find("/*") == std::string::npos && line.find("*/") == std::string::npos)
                    lines.push_back(line);
            return lines;
        };
        EXPECT_EQ(declarations(readShipped("ui/themes/skyrim/theme.rcss")),
            declarations(readShipped("ui/themes/neutral/theme.rcss")));
    }
}
