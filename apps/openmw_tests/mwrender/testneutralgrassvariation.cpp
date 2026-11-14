// Task 0153 follow-up 2: the neutral grass hands each tuft's tint, tilt and sway speed to the groundcover chunk, and
// its context asks for the INSTANCE_TINT pipeline mode; Morrowind's groundcover context does not. Written before the
// code.
#include "apps/openmw/mwrender/groundcoverbuild.hpp"
#include "apps/openmw/mwrender/neutralgrass.hpp"

#include <components/mwanimation/context.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/override.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include <gtest/gtest.h>

#include <vsg/io/Options.h>

namespace
{
    namespace sdk = openeden::sdk::game;

    MWAnim::Context baseContext()
    {
        MWAnim::Context context;
        context.nodeOptions = vsg::Options::create();
        return context;
    }

    Pipeline::Options optionsFor(const MWAnim::Context& context)
    {
        Pipeline::Options options;
        const Pipeline::Override* override_ = Pipeline::Override::get(*context.nodeOptions);
        if (override_ && override_->pipelineOptions)
            override_->pipelineOptions(options);
        return options;
    }

    sdk::GrassCell oneTuft()
    {
        sdk::GrassCell cell;
        cell.models.push_back({ "meshes\\landscape\\grass\\fescue.nif", false });
        sdk::GrassInstance i;
        i.x = 10.f;
        i.y = 20.f;
        i.z = 30.f;
        i.yaw = 1.25f;
        i.scale = 0.9f;
        i.tiltX = -0.3f;
        i.tiltY = 0.2f;
        i.tintR = 0.5f;
        i.tintG = 0.6f;
        i.tintB = 0.7f;
        i.waveSpeed = 1.5f;
        cell.instances.push_back(i);
        return cell;
    }

    TEST(NeutralGrassVariation, aTuftKeepsItsTintTiltAndSwaySpeedAsGroundcoverInstanceData)
    {
        const MWRender::GroundcoverInstances out = MWRender::groundcoverFromGrass(oneTuft());
        ASSERT_EQ(out.size(), 1u);
        const auto& list = out.begin()->second;
        ASSERT_EQ(list.size(), 1u);
        const MWRender::GroundcoverInstance& g = list[0];
        EXPECT_TRUE(g.tinted);
        EXPECT_FLOAT_EQ(g.tint.x, 0.5f);
        EXPECT_FLOAT_EQ(g.tint.y, 0.6f);
        EXPECT_FLOAT_EQ(g.tint.z, 0.7f);
        EXPECT_FLOAT_EQ(g.tint.w, 1.5f);
        // With INSTANCE_TINT the rotation input is (tilt x, tilt y, yaw).
        EXPECT_FLOAT_EQ(g.position.rot[0], -0.3f);
        EXPECT_FLOAT_EQ(g.position.rot[1], 0.2f);
        EXPECT_FLOAT_EQ(g.position.rot[2], 1.25f);
        EXPECT_FLOAT_EQ(g.scale, 0.9f);
    }

    TEST(NeutralGrassVariation, aNonFiniteTiltOrTintDropsTheTuft)
    {
        sdk::GrassCell cell = oneTuft();
        cell.instances[0].tiltX = std::nanf("");
        EXPECT_TRUE(MWRender::groundcoverFromGrass(cell).empty());
        cell = oneTuft();
        cell.instances[0].tintG = INFINITY;
        EXPECT_TRUE(MWRender::groundcoverFromGrass(cell).empty());
        cell = oneTuft();
        cell.instances[0].waveSpeed = std::nanf("");
        EXPECT_TRUE(MWRender::groundcoverFromGrass(cell).empty());
    }

    TEST(NeutralGrassVariation, aTiltOfLengthOneOrMoreIsAcceptedAndKeptFinite)
    {
        sdk::GrassCell cell = oneTuft();
        cell.instances[0].tiltX = 2.f;
        cell.instances[0].tiltY = 2.f;
        const MWRender::GroundcoverInstances out = MWRender::groundcoverFromGrass(cell);
        ASSERT_EQ(out.size(), 1u);
        const auto& g = out.begin()->second[0];
        EXPECT_LT(g.position.rot[0] * g.position.rot[0] + g.position.rot[1] * g.position.rot[1], 1.0f);
    }

    TEST(NeutralGrassVariation, theGrassContextAsksForTheTintModeAndGroundcoversDoesNot)
    {
        const Pipeline::Options grass = optionsFor(MWRender::makeNeutralGrassContext(baseContext()));
        EXPECT_TRUE(grass.shader.hasMode(Pipeline::Mode::INSTANCE));
        EXPECT_TRUE(grass.shader.hasMode(Pipeline::Mode::INSTANCE_TINT));
        const Pipeline::Options morrowind = optionsFor(MWRender::makeGroundcoverContext(baseContext()));
        EXPECT_TRUE(morrowind.shader.hasMode(Pipeline::Mode::INSTANCE));
        EXPECT_FALSE(morrowind.shader.hasMode(Pipeline::Mode::INSTANCE_TINT));
    }

    TEST(NeutralGrassVariation, morrowindsInstancesAreNotTinted)
    {
        const MWRender::GroundcoverInstance plain;
        EXPECT_FALSE(plain.tinted);
    }
}
