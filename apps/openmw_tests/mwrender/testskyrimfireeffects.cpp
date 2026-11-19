// Task 0359 (Skyrim fire and effect models), stage a: which models draw their BSEffectShaderProperty shapes. The spell
// models under meshes/magic do with [Game] neutral spell effects (task 0191); with [Game] neutral fire effects every
// other Skyrim model does too (the flame cards of hearths, braziers, torches and candles). Refraction shapes stay out
// whatever the setting says (that is the flattener's rule, pinned in testflatteneffects.cpp).

#include <apps/openmw/mwrender/skyrimmodel.hpp>

#include <gtest/gtest.h>

namespace
{
    using MWRender::SkyrimModel::effectShapesWanted;

    TEST(SkyrimFireEffects, a_world_model_draws_its_effect_shapes_only_with_the_fire_setting)
    {
        EXPECT_TRUE(effectShapesWanted("meshes/lighting/skyrimcandlefire.nif", false, true));
        EXPECT_FALSE(effectShapesWanted("meshes/lighting/skyrimcandlefire.nif", false, false));
        EXPECT_FALSE(effectShapesWanted("meshes/lighting/skyrimcandlefire.nif", true, false));
    }

    TEST(SkyrimFireEffects, archive_paths_with_backslashes_are_read_like_slashes)
    {
        const std::string backslashed = std::string("meshes") + char(92) + "magic" + char(92) + "firebolt.nif";
        EXPECT_TRUE(effectShapesWanted(backslashed, true, false));
        EXPECT_TRUE(effectShapesWanted(std::string("MESHES") + char(92) + "LIGHTING" + char(92) + "a.nif", false, true));
    }

    TEST(SkyrimFireEffects, the_spell_setting_alone_still_means_the_magic_folder_only)
    {
        EXPECT_TRUE(effectShapesWanted("meshes/magic/firebolt.nif", true, false));
        EXPECT_TRUE(effectShapesWanted("Meshes/Magic/WardBody.nif", true, false));
        EXPECT_FALSE(effectShapesWanted("meshes/magic/firebolt.nif", false, false));
    }

    TEST(SkyrimFireEffects, a_magic_model_follows_the_spell_setting_not_the_fire_setting)
    {
        // The fire setting is about the world; spells are still behind their own setting (they need neutral combat).
        EXPECT_FALSE(effectShapesWanted("meshes/magic/firebolt.nif", false, true));
        EXPECT_TRUE(effectShapesWanted("meshes/magic/firebolt.nif", true, true));
    }
}
