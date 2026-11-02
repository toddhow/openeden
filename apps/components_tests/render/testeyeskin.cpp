// Tests for keeping eyes out of the skin mark (task 0033, decision 0010 answer 5).
//
// Written from the task text alone: a model marked as skin keeps the mark on its skin textures, but a texture whose
// name has the word "eye" or "eyes" in it (split at '_', '.', '-', ' ', '/' and '\') is not skin.

#include <components/render/materialoverrides.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace Render::Pbr;

    TEST(RenderEyeSkin, EyeTextureNamesAreRecognised)
    {
        EXPECT_TRUE(isEyeTexture("textures/tx_b_n_breton_m_eyes_01.dds"));
        EXPECT_TRUE(isEyeTexture("Textures\TX_Eye.TGA"));
        EXPECT_TRUE(isEyeTexture("tx_a_eye"));
        EXPECT_TRUE(isEyeTexture("textures/eyes/brown.dds"));
    }

    TEST(RenderEyeSkin, OtherTexturesAreNot)
    {
        EXPECT_FALSE(isEyeTexture(""));
        EXPECT_FALSE(isEyeTexture("textures/tx_b_n_breton_f_h01.dds"));
        EXPECT_FALSE(isEyeTexture("textures/tx_a_chitin_h_eyepiece.dds"));
        EXPECT_FALSE(isEyeTexture("textures/tx_eyebrow_01.dds"));
        EXPECT_FALSE(isEyeTexture("textures/tx_skeye.dds"));
    }

    TEST(RenderEyeSkin, SkinModelKeepsMarkOnSkinTexture)
    {
        OverrideTable table;
        table.markSubsurfaceModel("meshes/b/b_n_breton_f_head_01.nif");
        const OverrideValues v
            = table.resolveValues("b/b_n_breton_f_head_01.nif", "textures/tx_b_n_breton_f_h01.dds");
        ASSERT_TRUE(v.subsurface.has_value());
        EXPECT_TRUE(*v.subsurface);
    }

    TEST(RenderEyeSkin, SkinModelDoesNotMarkItsEyeTexture)
    {
        OverrideTable table;
        table.markSubsurfaceModel("meshes/b/b_n_breton_f_head_01.nif");
        const OverrideValues v
            = table.resolveValues("b/b_n_breton_f_head_01.nif", "textures/tx_b_n_breton_eyes_01.dds");
        EXPECT_FALSE(v.subsurface.has_value() && *v.subsurface);
    }

    TEST(RenderEyeSkin, AnExplicitOverrideStillWins)
    {
        OverrideTable table;
        table.markSubsurfaceModel("meshes/head.nif");
        OverrideSet set;
        OverrideEntry entry;
        entry.kind = OverrideEntry::Kind::Texture;
        entry.path = "textures/tx_eyes.dds";
        entry.values.subsurface = true;
        set.entries.push_back(entry);
        table.add(set);
        const OverrideValues v = table.resolveValues("head.nif", "textures/tx_eyes.dds");
        ASSERT_TRUE(v.subsurface.has_value());
        EXPECT_TRUE(*v.subsurface);
    }

    TEST(RenderEyeSkin, NonSkinModelIsUnchanged)
    {
        OverrideTable table;
        EXPECT_FALSE(table.resolveValues("x.nif", "textures/tx_eyes.dds").subsurface.has_value());
    }
}
