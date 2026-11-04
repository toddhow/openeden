// Task 0072, stage g: the tonemap's constants repeat Render::Hdr and Render::Sss. tonemap.cpp used to write them into
// its GLSL string; they are now in post/tonemapconstants.glsl and its Slang twin post/tonemapconstants.slang, and are
// read back here. The ambient occlusion pass's settings come from ao.settings (Resource::ShaderSettings), as
// ambientocclusion.cpp wrote them in from [Rendering] ao radius, ao slices and ao steps. Written before the twins.
#include <gtest/gtest.h>

#include <components/render/hdrmath.hpp>
#include <components/render/sssmath.hpp>
#include <components/resource/shadersettings.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/state/ShaderModule.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string readShaderText(const std::string& file)
    {
        std::ifstream in(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // name -> the numbers of its value, in order (a vector constructor gives several).
    std::map<std::string, std::vector<float>> readConstants(const std::string& text, const std::regex& line)
    {
        std::map<std::string, std::vector<float>> constants;
        const std::regex number(R"([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), line); it != std::sregex_iterator(); ++it)
        {
            const std::string value = (*it)[2];
            // Skip the type name of a vector constructor (vec3, float3), whose digit is not a number of the value.
            const std::size_t open = value.find('(');
            const std::string numbers = open == std::string::npos ? value : value.substr(open + 1);
            std::vector<float>& values = constants[(*it)[1]];
            for (auto n = std::sregex_iterator(numbers.begin(), numbers.end(), number); n != std::sregex_iterator();
                 ++n)
                values.push_back(std::strtof(n->str().c_str(), nullptr));
        }
        return constants;
    }

    void expectTheCppValues(const std::map<std::string, std::vector<float>>& constants)
    {
        const auto expect = [&constants](const std::string& name, const std::vector<float>& values) {
            const auto it = constants.find(name);
            ASSERT_NE(it, constants.end()) << name;
            // Exactly the float the C++ uses, as the old code wrote it with nine significant digits.
            EXPECT_EQ(it->second, values) << name;
        };
        const auto& sigma = Render::Sss::channelSigma;
        expect("SSS_CHANNEL_SIGMA", { sigma.x, sigma.y, sigma.z });
        expect("SSS_NARROW_SIGMA", { Render::Sss::narrowSigma });
        expect("SSS_REACH", { Render::Sss::reachInWidths });
        expect("SSS_TAPS", { static_cast<float>(Render::Sss::tapCount) });
        expect("SSS_EPSILON", { Render::Sss::epsilon });
        expect("SSS_GAIN_MIN", { Render::Sss::gainMin });
        expect("SSS_GAIN_MAX", { Render::Sss::gainMax });
        expect("DISPLAY_GAMMA", { Render::Hdr::displayGamma });
        expect("REINHARD_WHITE", { Render::Hdr::reinhardWhitePoint });
        expect("MAX_TARGET_VALUE", { Render::Hdr::maxTargetValue });
        EXPECT_EQ(constants.size(), 10u);
    }

    TEST(RenderTonemapConstants, the_glsl_holds_the_cpp_values)
    {
        const std::string text = readShaderText("post/tonemapconstants.glsl");
        ASSERT_FALSE(text.empty()) << "the shader sources are not beside the tests";
        expectTheCppValues(readConstants(text, std::regex(R"(#define\s+(\w+)\s+([^\r\n]+))")));
    }

    TEST(RenderTonemapConstants, the_slang_twin_holds_the_cpp_values)
    {
        const std::string text = readShaderText("post/tonemapconstants.slang");
        ASSERT_FALSE(text.empty()) << "post/tonemapconstants.slang is missing";
        expectTheCppValues(
            readConstants(text, std::regex(R"(static\s+const\s+(?:float3|float|int)\s+(\w+)\s*=\s*([^;]+);)")));
    }

    // Task 0072, review of stage g, finding 4: the two tests above compare floats, so "2.2" passes like "2.20000005".
    // The text matters too: glslang folds 1.0 / DISPLAY_GAMMA in double precision, and from 2.2 the quotient rounds to
    // another float than from 2.20000005 (the constant in the SPIR-V is 0x3EE8BA2F against 0x3EE8BA2E: seen when the
    // file was changed to 2.2 on purpose, which PostPassesSwitchOff caught in all 16 tonemap variants). So each value
    // must be written exactly as tonemap.cpp printed it into its string: an ostringstream at precision 9.
    std::string printedAsTonemapCpp(float value)
    {
        std::ostringstream out;
        out.precision(9);
        out << value;
        return out.str();
    }

    // name -> the value as tonemap.cpp wrote it after "#define <name> ", `vector` standing for "vec3" in GLSL and
    // "float3" in Slang.
    std::map<std::string, std::string> tonemapCppText(const std::string& vector)
    {
        const auto& sigma = Render::Sss::channelSigma;
        return {
            { "SSS_CHANNEL_SIGMA",
                vector + "(" + printedAsTonemapCpp(sigma.x) + ", " + printedAsTonemapCpp(sigma.y) + ", "
                    + printedAsTonemapCpp(sigma.z) + ")" },
            { "SSS_NARROW_SIGMA", printedAsTonemapCpp(Render::Sss::narrowSigma) },
            { "SSS_REACH", printedAsTonemapCpp(Render::Sss::reachInWidths) },
            { "SSS_TAPS", std::to_string(Render::Sss::tapCount) },
            { "SSS_EPSILON", printedAsTonemapCpp(Render::Sss::epsilon) },
            { "SSS_GAIN_MIN", printedAsTonemapCpp(Render::Sss::gainMin) },
            { "SSS_GAIN_MAX", printedAsTonemapCpp(Render::Sss::gainMax) },
            { "DISPLAY_GAMMA", printedAsTonemapCpp(Render::Hdr::displayGamma) },
            { "REINHARD_WHITE", printedAsTonemapCpp(Render::Hdr::reinhardWhitePoint) },
            { "MAX_TARGET_VALUE", printedAsTonemapCpp(Render::Hdr::maxTargetValue) },
        };
    }

    // name -> the value's text in the file (up to the end of the line, without trailing spaces or a carriage return).
    std::map<std::string, std::string> valueTexts(const std::string& text, const std::regex& line)
    {
        std::map<std::string, std::string> values;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), line); it != std::sregex_iterator(); ++it)
        {
            std::string value = (*it)[2];
            while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
                value.pop_back();
            values[(*it)[1]] = value;
        }
        return values;
    }

    TEST(RenderTonemapConstants, the_values_are_spelled_as_tonemap_cpp_printed_them)
    {
        // The check below can tell the spellings apart.
        EXPECT_EQ(printedAsTonemapCpp(2.2f), "2.20000005");
        EXPECT_NE(printedAsTonemapCpp(2.2f), "2.2");

        const std::string glsl = readShaderText("post/tonemapconstants.glsl");
        ASSERT_FALSE(glsl.empty()) << "the shader sources are not beside the tests";
        EXPECT_EQ(valueTexts(glsl, std::regex(R"(#define\s+(\w+)\s+([^\r\n]+))")), tonemapCppText("vec3"));

        const std::string slang = readShaderText("post/tonemapconstants.slang");
        ASSERT_FALSE(slang.empty()) << "post/tonemapconstants.slang is missing";
        EXPECT_EQ(valueTexts(slang, std::regex(R"(static\s+const\s+(?:float3|float|int)\s+(\w+)\s*=\s*([^;]+);)")),
            tonemapCppText("float3"));
    }

    // The pass took [Rendering] ao radius, ao slices and ao steps as RenderManager reads them (sanitised); the twin
    // takes them as specialization constants with the table's ids.
    TEST(RenderTonemapConstants, ao_settings_hold_the_ambient_occlusion_values)
    {
        auto& rendering = Settings::rendering();
        const float radius = rendering.mAoRadius.get();
        const int slices = rendering.mAoSlices.get();
        const int steps = rendering.mAoSteps.get();
        rendering.mAoRadius.set(37.5f);
        rendering.mAoSlices.set(3);
        rendering.mAoSteps.set(40); // sanitised to 16
        Resource::ShaderSettings settings;
        rendering.mAoRadius.set(radius);
        rendering.mAoSlices.set(slices);
        rendering.mAoSteps.set(steps);

        const auto* module = dynamic_cast<const vsg::ShaderModule*>(settings.objects.at("ao.settings").get());
        ASSERT_NE(module, nullptr);
        EXPECT_NE(module->source.find("const float AO_RADIUS = 37.5;"), std::string::npos) << module->source;
        EXPECT_NE(module->source.find("const int AO_SLICES = 3;"), std::string::npos) << module->source;
        EXPECT_NE(module->source.find("const int AO_STEPS = 16;"), std::string::npos) << module->source;

        std::map<std::string, uint32_t> ids;
        for (const auto& entry : Resource::settingsConstantTable())
            ids[std::string(entry.mName)] = entry.mId;
        ASSERT_EQ(ids.count("AO_RADIUS"), 1u);
        ASSERT_EQ(ids.count("AO_SLICES"), 1u);
        ASSERT_EQ(ids.count("AO_STEPS"), 1u);
        const auto& constants = settings.constants->constants;
        const auto* r = dynamic_cast<const vsg::floatValue*>(constants.at(ids.at("AO_RADIUS")).get());
        const auto* sl = dynamic_cast<const vsg::intValue*>(constants.at(ids.at("AO_SLICES")).get());
        const auto* st = dynamic_cast<const vsg::intValue*>(constants.at(ids.at("AO_STEPS")).get());
        ASSERT_TRUE(r && sl && st);
        EXPECT_EQ(r->value(), 37.5f);
        EXPECT_EQ(sl->value(), 3);
        EXPECT_EQ(st->value(), 16);
    }
}
