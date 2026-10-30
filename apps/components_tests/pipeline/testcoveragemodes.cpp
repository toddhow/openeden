// Tests for Pipeline::coverageModes (task 0012, step V4j: alpha to coverage, "Which draws").
//
// Written from the spec alone, without reading the implementation. Assumptions:
// - Declared in <components/pipeline/graphics.hpp> as
//   Pipeline::CoverageModes Pipeline::coverageModes(const Pipeline::Options& key, bool alphaToCoverage,
//   bool adjustCoverage), returning a struct with members `bool alphaToCoverage` and `bool adjustCoverage`.
// - A draw qualifies when it alpha-tests (key.alphaTestMode != 0), does not blend (key.blend == false), uses the
//   standard object shader (key.shader.path empty) and has the MATERIAL shader mode. A qualifying draw gets each
//   output equal to its corresponding switch; any non-qualifying draw gets both false.
// - A default-constructed Pipeline::Options has alphaTestMode 0, blend false, an empty shader path and no modes.
// - Alpha test modes 1..7 are the non-"always" comparison functions; all count as alpha testing.

#include <components/pipeline/graphics.hpp>
#include <components/pipeline/mode.hpp>
#include <components/pipeline/options.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{
    // An alpha-tested, opaque, standard-shader draw with MATERIAL: the case alpha to coverage applies to.
    Pipeline::Options qualifyingKey(std::uint32_t alphaTestMode = 1)
    {
        Pipeline::Options key;
        key.alphaTestMode = alphaTestMode;
        key.blend = false;
        key.shader.path.clear();
        key.shader.addMode(Pipeline::Mode::MATERIAL);
        return key;
    }

    TEST(PipelineCoverageModesTest, bothSwitchesOffGivesBothFalse)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(), false, false);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, bothSwitchesOnGivesBothTrueForQualifyingDraw)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(), true, true);
        EXPECT_TRUE(result.alphaToCoverage);
        EXPECT_TRUE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, onlyAlphaToCoverageSwitch)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(), true, false);
        EXPECT_TRUE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, onlyAdjustCoverageSwitch)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(), false, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_TRUE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, alphaTestModesOneToSevenQualify)
    {
        for (std::uint32_t mode = 1; mode <= 7; ++mode)
        {
            SCOPED_TRACE("alphaTestMode " + std::to_string(mode));
            const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(mode), true, true);
            EXPECT_TRUE(result.alphaToCoverage);
            EXPECT_TRUE(result.adjustCoverage);
        }
    }

    TEST(PipelineCoverageModesTest, noAlphaTestGivesBothFalse)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(qualifyingKey(0), true, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, blendingGivesBothFalse)
    {
        Pipeline::Options key = qualifyingKey();
        key.blend = true;
        const Pipeline::CoverageModes result = Pipeline::coverageModes(key, true, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, nonStandardShaderPathGivesBothFalse)
    {
        for (const std::string path : { "sky/clouds", "water" })
        {
            SCOPED_TRACE("shader.path " + path);
            Pipeline::Options key = qualifyingKey();
            key.shader.path = path;
            const Pipeline::CoverageModes result = Pipeline::coverageModes(key, true, true);
            EXPECT_FALSE(result.alphaToCoverage);
            EXPECT_FALSE(result.adjustCoverage);
        }
    }

    TEST(PipelineCoverageModesTest, withoutMaterialModeGivesBothFalse)
    {
        Pipeline::Options key;
        key.alphaTestMode = 1;
        key.blend = false;
        const Pipeline::CoverageModes result = Pipeline::coverageModes(key, true, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, otherModesWithoutMaterialGiveBothFalse)
    {
        Pipeline::Options key;
        key.alphaTestMode = 1;
        key.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
        key.shader.addMode(Pipeline::Mode::TEXCOORD);
        const Pipeline::CoverageModes result = Pipeline::coverageModes(key, true, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, addingMaterialModeQualifies)
    {
        Pipeline::Options key;
        key.alphaTestMode = 1;
        key.shader.addMode(Pipeline::Mode::MATERIAL);
        const Pipeline::CoverageModes result = Pipeline::coverageModes(key, true, true);
        EXPECT_TRUE(result.alphaToCoverage);
        EXPECT_TRUE(result.adjustCoverage);
    }

    TEST(PipelineCoverageModesTest, defaultOptionsGiveBothFalse)
    {
        const Pipeline::CoverageModes result = Pipeline::coverageModes(Pipeline::Options{}, true, true);
        EXPECT_FALSE(result.alphaToCoverage);
        EXPECT_FALSE(result.adjustCoverage);
    }
}
