// Tests for the parity of the point-light shadow shaders with their C++ twin (task 0077, stage a). Written from the
// "Shader parity" paragraph of docs/tasks/0077-point-light-shadows.md alone: files/shaders/lib/light/pointshadow.slang
// and its GLSL twin pointshadow.glsl hold the Poisson table, two constants and the face table of the C++ maths, and
// lib/view/bindings.glsl and bindings.slang define the three binding numbers 17, 18 and 19. The shader files are read
// from the source tree; a missing file fails the test with a message (it does not skip).

#include <components/render/pointshadowmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Render::PointShadow;

    // The text of files/shaders/<relative>, or an empty string when the file is not there.
    std::string readShader(const std::string& relative)
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / std::filesystem::path(relative);
        std::ifstream in(source);
        if (!in)
        {
            return {};
        }
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // Reads a shader file and reports a missing or empty one as a failure with a clear message.
    bool loadShader(const std::string& relative, std::string& text)
    {
        text = readShader(relative);
        if (text.empty())
        {
            ADD_FAILURE() << "files/shaders/" << relative << " does not exist or is empty (task 0077 shader parity)";
            return false;
        }
        return true;
    }

    // The floating-point literals of a text, in order, with their sign: 0.5, -.25, 1.5e-3. Integers without a decimal
    // point are not taken (the Poisson entries all have one).
    std::vector<double> floatLiterals(const std::string& text)
    {
        static const std::regex re("[-+]?[0-9]*\\.[0-9]+(?:[eE][-+]?[0-9]+)?");
        std::vector<double> numbers;
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
        {
            numbers.push_back(std::stod(it->str()));
        }
        return numbers;
    }

    // The position of the first run of 32 consecutive literals that is the Poisson table pcfTap(0..15) (x then y).
    std::optional<std::size_t> findPoissonTable(const std::vector<double>& numbers)
    {
        if (numbers.size() < 32)
        {
            return std::nullopt;
        }
        for (std::size_t start = 0; start + 32 <= numbers.size(); ++start)
        {
            bool match = true;
            for (int i = 0; i < 16 && match; ++i)
            {
                const vsg::dvec2 tap = pcfTap(i);
                match = std::fabs(numbers[start + 2 * static_cast<std::size_t>(i)] - tap.x) < 1e-6
                    && std::fabs(numbers[start + 2 * static_cast<std::size_t>(i) + 1] - tap.y) < 1e-6;
            }
            if (match)
            {
                return start;
            }
        }
        return std::nullopt;
    }

    // The integer after `name`, with an optional "=" between them: `const int NAME = 2;` and `#define NAME 2`.
    std::optional<long> declaredInteger(const std::string& text, const std::string& name)
    {
        const std::regex re("\\b" + name + "\\s*(?:=\\s*)?([0-9]+)");
        std::smatch match;
        if (!std::regex_search(text, match, re))
        {
            return std::nullopt;
        }
        return std::stol(match[1].str());
    }

    // The numbers of the initialiser of the array `name`: from the "=" after the name to the next ";", with the
    // identifiers (float3, vec3, ...) and the array bounds ([6]) taken out first.
    std::vector<double> arrayNumbers(const std::string& text, const std::string& name)
    {
        const std::regex declaration("\\b" + name + "\\s*(?:\\[[^\\]]*\\])?\\s*=");
        std::smatch match;
        if (!std::regex_search(text, match, declaration))
        {
            return {};
        }
        const std::size_t begin = static_cast<std::size_t>(match.position(0) + match.length(0));
        const std::size_t end = text.find(';', begin);
        if (end == std::string::npos)
        {
            return {};
        }
        std::string body = text.substr(begin, end - begin);
        body = std::regex_replace(body, std::regex("\\[[^\\]]*\\]"), " ");
        body = std::regex_replace(body, std::regex("[A-Za-z_][A-Za-z_0-9]*"), " ");
        static const std::regex number("[-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?");
        std::vector<double> numbers;
        for (std::sregex_iterator it(body.begin(), body.end(), number), last; it != last; ++it)
        {
            numbers.push_back(std::stod(it->str()));
        }
        return numbers;
    }

    void expectFaceTable(const std::string& text, const std::string& label)
    {
        struct Table
        {
            const char* name;
            vsg::dvec3 FaceAxes::*member;
        };
        const Table tables[] = { { "pointShadowFaceForward", &FaceAxes::forward },
            { "pointShadowFaceUp", &FaceAxes::up }, { "pointShadowFaceRight", &FaceAxes::right } };
        for (const Table& table : tables)
        {
            const std::vector<double> numbers = arrayNumbers(text, table.name);
            ASSERT_EQ(numbers.size(), 18u)
                << label << ": " << table.name << " must be an array of 6 three-component entries";
            for (int face = 0; face < 6; ++face)
            {
                const vsg::dvec3 expected = faceAxes(face).*(table.member);
                const std::size_t base = 3 * static_cast<std::size_t>(face);
                EXPECT_DOUBLE_EQ(numbers[base + 0], expected.x) << label << " " << table.name << "[" << face << "].x";
                EXPECT_DOUBLE_EQ(numbers[base + 1], expected.y) << label << " " << table.name << "[" << face << "].y";
                EXPECT_DOUBLE_EQ(numbers[base + 2], expected.z) << label << " " << table.name << "[" << face << "].z";
            }
        }
    }

    // --- The shader files -------------------------------------------------------------------------------------------

    TEST(RenderPointShadowShader, bothShaderFilesExist)
    {
        std::string slang;
        std::string glsl;
        EXPECT_TRUE(loadShader("lib/light/pointshadow.slang", slang));
        EXPECT_TRUE(loadShader("lib/light/pointshadow.glsl", glsl));
    }

    TEST(RenderPointShadowShader, thePoissonTablesAreThePcfTapsInBothLanguages)
    {
        for (const char* file : { "lib/light/pointshadow.slang", "lib/light/pointshadow.glsl" })
        {
            std::string text;
            if (!loadShader(file, text))
            {
                continue;
            }
            const std::optional<std::size_t> at = findPoissonTable(floatLiterals(text));
            EXPECT_TRUE(at.has_value()) << file << " has no run of 16 (x, y) pairs equal to pcfTap(0..15)";
        }
    }

    TEST(RenderPointShadowShader, theHeaderAndSlotConstantsAreTheOnesOfTheCppSide)
    {
        for (const char* file : { "lib/light/pointshadow.slang", "lib/light/pointshadow.glsl" })
        {
            std::string text;
            if (!loadShader(file, text))
            {
                continue;
            }
            const std::optional<long> header = declaredInteger(text, "POINT_SHADOW_HEADER_VEC4S");
            const std::optional<long> slots = declaredInteger(text, "POINT_SHADOW_MAX_SLOTS");
            ASSERT_TRUE(header.has_value()) << file << " does not declare POINT_SHADOW_HEADER_VEC4S";
            ASSERT_TRUE(slots.has_value()) << file << " does not declare POINT_SHADOW_MAX_SLOTS";
            EXPECT_EQ(*header, 2) << file;
            EXPECT_EQ(*slots, 8) << file;
            // headerVec4s and maxSlots come from pointshadowgpu.hpp / pointshadowmath.hpp.
            EXPECT_EQ(static_cast<long>(maxSlots), *slots) << file;
        }
    }

    TEST(RenderPointShadowShader, theFaceTablesAreTheFaceAxesInBothLanguages)
    {
        for (const char* file : { "lib/light/pointshadow.slang", "lib/light/pointshadow.glsl" })
        {
            std::string text;
            if (!loadShader(file, text))
            {
                continue;
            }
            expectFaceTable(text, file);
        }
    }

    // --- The bindings ------------------------------------------------------------------------------------------------

    std::map<std::string, long> allBindings(const std::string& text)
    {
        static const std::regex re("\\b(VIEW_[A-Z0-9_]+_BINDING)\\s*(?:=\\s*)?([0-9]+)");
        std::map<std::string, long> bindings;
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
        {
            bindings.emplace((*it)[1].str(), std::stol((*it)[2].str()));
        }
        return bindings;
    }

    TEST(RenderPointShadowShader, theViewSetHasThePointShadowBindings17To19InBothLanguages)
    {
        const std::map<std::string, long> expected = { { "VIEW_POINT_SHADOW_MAP_BINDING", 17 },
            { "VIEW_POINT_SHADOW_SAMPLER_BINDING", 18 }, { "VIEW_POINT_SHADOW_DATA_BINDING", 19 } };
        for (const char* file : { "lib/view/bindings.glsl", "lib/view/bindings.slang" })
        {
            std::string text;
            if (!loadShader(file, text))
            {
                continue;
            }
            const std::map<std::string, long> bindings = allBindings(text);
            for (const auto& [name, number] : expected)
            {
                const auto found = bindings.find(name);
                ASSERT_NE(found, bindings.end()) << file << " does not define " << name;
                EXPECT_EQ(found->second, number) << file << " " << name;
            }
        }
    }

    TEST(RenderPointShadowShader, noTwoBindingsOfTheViewSetShareANumberAndTheTwoLanguagesAgree)
    {
        std::string glsl;
        std::string slang;
        if (!loadShader("lib/view/bindings.glsl", glsl) || !loadShader("lib/view/bindings.slang", slang))
        {
            return;
        }
        const std::map<std::string, long> glslBindings = allBindings(glsl);
        const std::map<std::string, long> slangBindings = allBindings(slang);
        EXPECT_EQ(glslBindings, slangBindings);
        std::set<long> numbers;
        for (const auto& [name, number] : glslBindings)
        {
            EXPECT_TRUE(numbers.insert(number).second) << name << " shares the number " << number;
        }
        // The old bindings keep their numbers (0 to 16) and the new ones follow (17 to 19, and 20 for the baked puddle
        // map, task 0095 stage W-d).
        EXPECT_EQ(glslBindings.size(), 21u);
        for (long number = 0; number < 21; ++number)
        {
            EXPECT_EQ(numbers.count(number), 1u) << "binding number " << number << " is not used";
        }
    }
}
