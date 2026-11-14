// Task 0153 follow-up: the neutral grass's albedo scale. The grass context is groundcover's (a Pipeline::Override with
// the INSTANCE mode) plus a material hook that scales the colour; Morrowind's groundcover context must not get it.
#include "apps/openmw/mwrender/groundcoverbuild.hpp"
#include "apps/openmw/mwrender/neutralgrass.hpp"

#include <components/mwanimation/context.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include <gtest/gtest.h>

#include <vsg/io/Options.h>

namespace
{
    MWAnim::Context baseContext()
    {
        MWAnim::Context context;
        context.nodeOptions = vsg::Options::create();
        return context;
    }

    Pipeline::Data::Material apply(const MWAnim::Context& context)
    {
        Pipeline::Data::Material material = Pipeline::Material::createDefault();
        const Pipeline::Override* override_ = Pipeline::Override::get(*context.nodeOptions);
        if (override_ && override_->materialData)
            override_->materialData(material);
        return material;
    }

    TEST(NeutralGrassTint, theScaleIsASubstantialDimmingAndAtMostOne)
    {
        EXPECT_GT(MWRender::kGrassAlbedoScale, 0.3f);
        EXPECT_LT(MWRender::kGrassAlbedoScale, 1.0f);
        EXPECT_FLOAT_EQ(MWRender::grassAlbedoScale(), MWRender::kGrassAlbedoScale); // no developer override set
    }

    TEST(NeutralGrassTint, theGrassContextScalesTheMaterialsColour)
    {
        const MWAnim::Context context = MWRender::makeNeutralGrassContext(baseContext());
        const Pipeline::Data::Material plain = Pipeline::Material::createDefault();
        const Pipeline::Data::Material tinted = apply(context);
        for (int c = 0; c < 3; ++c)
        {
            EXPECT_FLOAT_EQ(tinted.diffuse[c], plain.diffuse[c] * MWRender::kGrassAlbedoScale);
            EXPECT_FLOAT_EQ(tinted.ambient[c], plain.ambient[c] * MWRender::kGrassAlbedoScale);
        }
    }

    TEST(NeutralGrassTint, theAlphaAndTheAmbientFlagWordAreLeftAlone)
    {
        const MWAnim::Context context = MWRender::makeNeutralGrassContext(baseContext());
        const Pipeline::Data::Material plain = Pipeline::Material::createDefault();
        const Pipeline::Data::Material tinted = apply(context);
        EXPECT_FLOAT_EQ(tinted.diffuse.w, plain.diffuse.w);
        EXPECT_FLOAT_EQ(tinted.ambient.w, plain.ambient.w);
    }

    TEST(NeutralGrassTint, theGrassContextKeepsGroundcoversAlphaTestCutoffHook)
    {
        const MWAnim::Context context = MWRender::makeNeutralGrassContext(baseContext());
        const Pipeline::Data::Material tinted = apply(context);
        EXPECT_NEAR(tinted.alphaTestCutoff, 128.f / 255.f, 1e-6f);
    }

    TEST(NeutralGrassTint, morrowindsGroundcoverContextIsNotTinted)
    {
        const MWAnim::Context context = MWRender::makeGroundcoverContext(baseContext());
        const Pipeline::Data::Material plain = Pipeline::Material::createDefault();
        const Pipeline::Data::Material applied = apply(context);
        for (int c = 0; c < 3; ++c)
        {
            EXPECT_FLOAT_EQ(applied.diffuse[c], plain.diffuse[c]);
            EXPECT_FLOAT_EQ(applied.ambient[c], plain.ambient[c]);
        }
    }
}
