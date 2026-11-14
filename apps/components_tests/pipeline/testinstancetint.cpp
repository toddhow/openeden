// Task 0153 follow-up 2: the INSTANCE_TINT vertex mode of the object shader (per-instance colour and sway speed, tilt
// and yaw in the rotation input), used only by the neutral grass. Pipeline names plus a text check of both shader
// languages. Written before the code (run red against the stubs).
#include <components/pipeline/mode.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace Pipeline
{
    std::string getDefine(Mode mode); // components/pipeline/graphics.cpp
}

namespace
{
    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    TEST(InstanceTint, theModeIsADefineOfTheVertexStageOnly)
    {
        EXPECT_EQ(Pipeline::getDefine(Pipeline::Mode::INSTANCE_TINT), "INSTANCE_TINT");
        EXPECT_NE(std::find(Pipeline::vertexModes.begin(), Pipeline::vertexModes.end(), Pipeline::Mode::INSTANCE_TINT),
            Pipeline::vertexModes.end());
        EXPECT_EQ(
            std::find(Pipeline::fragmentModes.begin(), Pipeline::fragmentModes.end(), Pipeline::Mode::INSTANCE_TINT),
            Pipeline::fragmentModes.end());
    }

    TEST(InstanceTint, theGlslDeclaresTheInputAfterTheRotationAndBeforeTheTextureCoordinates)
    {
        const std::string attributes = readSource("lib/attributes.glsl");
        EXPECT_TRUE(contains(attributes, "inInstanceTint"));
        EXPECT_TRUE(contains(attributes, "INSTANCE_TINT_LOCATION+1"));
        EXPECT_TRUE(contains(readSource("default/main.vert"), "INSTANCE, INSTANCE_TINT,"));
    }

    TEST(InstanceTint, theGlslVertexStageMultipliesTheColourReadsTheTiltAndScalesTheClock)
    {
        const std::string vert = readSource("default/main.vert");
        EXPECT_TRUE(contains(vert, "groundcoverTiltRotation(inInstanceRotation.xy, inInstanceRotation.z)"));
        EXPECT_TRUE(contains(vert, "vert_out.color.rgb *= inInstanceTint.rgb"));
        EXPECT_TRUE(contains(vert, "scene.time * inInstanceTint.w"));
        EXPECT_TRUE(contains(readSource("lib/groundcover.glsl"), "groundcoverTiltRotation"));
    }

    TEST(InstanceTint, theSlangTwinDoesTheSame)
    {
        const std::string attributes = readSource("lib/attributes.slang");
        EXPECT_TRUE(contains(attributes, "INSTANCE_TINT_LOCATION"));
        EXPECT_TRUE(contains(attributes, "inInstanceTint"));
        const std::string vert = readSource("default/default.vert.slang");
        EXPECT_TRUE(contains(vert, "groundcoverTiltRotation(input.inInstanceRotation.xy, input.inInstanceRotation.z)"));
        EXPECT_TRUE(contains(vert, "output.io.color.rgb *= input.inInstanceTint.rgb"));
        EXPECT_TRUE(contains(vert, "scene.time * input.inInstanceTint.w"));
        EXPECT_TRUE(contains(readSource("lib/groundcover.slang"), "groundcoverTiltRotation"));
    }
}
