// Task 0072, stage g: the tonemap's constants repeat Render::Hdr and Render::Sss. tonemap.cpp used to write them into
// its GLSL string; they are now in post/tonemapconstants.glsl and its Slang twin post/tonemapconstants.slang, and are
// read back here. The ambient occlusion pass's settings come from ao.settings (Resource::ShaderSettings), as
// ambientocclusion.cpp wrote them in from [Rendering] ao radius, ao slices and ao steps.
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
