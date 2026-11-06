#include <components/nif/exception.hpp>
#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "shaderbuilder.hpp"

// Task 0082, item 4: BSLightingShaderProperty, BSEffectShaderProperty, BSShaderTextureSet and NiAlphaProperty as value
// structs in the scene. Every number below is written by the test that expects it.
namespace
{
    using namespace NifScene;
    using namespace NifScene::Testing;

    constexpr VFS::Path::NormalizedView path("material-test.nif");

    SceneOptions optionsAny()
    {
        SceneOptions options;
        options.mRead.mAllowedVersions = Nif::VersionSet::Any;
        return options;
    }

    Scene read(const GamebryoFile& file)
    {
        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return readScene(std::move(input), path, optionsAny());
    }

    std::string errorOf(const GamebryoFile& file)
    {
        try
        {
            read(file);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    bool hasWarning(const Scene& scene, const std::string& needle)
    {
        return std::any_of(scene.mWarnings.begin(), scene.mWarnings.end(),
            [&](const std::string& w) { return w.find(needle) != std::string::npos; });
    }

    // Records: 0 root, 1 shape, 2 shader, 3 texture set, 4 alpha property.
    struct Case
    {
        GamebryoFile mFile;
        ShapeSpec mShape;

        Case(std::uint32_t stream = 100)
        {
            mFile.mStreamVersion = stream;
            mShape.mName = "Shape";
            mShape.mFlags = VF_Vertex;
            mShape.mStoreData = stream < 130; // Fallout 4 vertices are packed differently: these shapes carry none
            Vertex a;
            a.mPosition = { 0.f, 0.f, 0.f };
            Vertex b;
            b.mPosition = { 1.f, 0.f, 0.f };
            Vertex c;
            c.mPosition = { 0.f, 1.f, 0.f };
            mShape.mVertices = { a, b, c };
            mShape.mTriangles = { { 0, 1, 2 } };
            mShape.mShader = 2;
        }

        GamebryoFile build(Block shader, bool withAlpha = false, std::uint16_t alphaFlags = 0,
            std::uint8_t threshold = 0, const std::vector<std::string>& textures = { "diffuse.dds", "normal.dds" })
        {
            mFile.mBlocks.clear();
            mFile.mBlocks.push_back(node(mFile, "Root", { 1 }));
            mShape.mAlphaProperty = withAlpha ? 4 : -1;
            mFile.mBlocks.push_back(shape(mFile, mShape));
            mFile.mBlocks.push_back(std::move(shader));
            mFile.mBlocks.push_back(textureSet(textures));
            if (withAlpha)
                mFile.mBlocks.push_back(alphaProperty(mFile, alphaFlags, threshold));
            return mFile;
        }
    };

    LightingSpec baseSpec()
    {
        LightingSpec spec;
        spec.mTextureSet = 3;
        return spec;
    }

    // ---------------------------------------------------------------- Skyrim lighting shader

    TEST(NifSceneMaterial, a_skyrim_lighting_shader_gives_its_values_and_texture_paths)
    {
        Case c;
        LightingSpec spec = baseSpec();
        const Scene scene = read(c.build(lightingShader(c.mFile, spec), false, 0, 0,
            { "textures\\a_d.dds", "textures\\a_n.dds", "", "", "textures\\a_e.dds" }));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        ASSERT_EQ(scene.mGeometries[0].mMaterial, 0u);
        ASSERT_EQ(scene.mMaterials.size(), 1u);
        const ShaderMaterial& m = scene.mMaterials[0];
        EXPECT_EQ(m.mKind, ShaderKind::Lighting);
        EXPECT_EQ(m.mRecordType, "BSLightingShaderProperty");
        EXPECT_EQ(m.mShaderType, 0u);
        EXPECT_EQ(m.mFlags1, 0x80000001u);
        EXPECT_EQ(m.mFlags2, 0x00000010u);
        EXPECT_EQ(m.mUVOffset, (Vec2{ 0.25f, 0.5f }));
        EXPECT_EQ(m.mUVScale, (Vec2{ 2.f, 4.f }));
        EXPECT_EQ(m.mEmissiveColor, (Vec3{ 0.5f, 0.25f, 0.125f }));
        EXPECT_EQ(m.mEmissiveMultiple, 2.f);
        EXPECT_EQ(m.mClampMode, 1u);
        EXPECT_EQ(m.mAlpha, 0.75f);
        EXPECT_EQ(m.mRefractionStrength, 0.0625f);
        EXPECT_EQ(m.mGlossiness, 32.f);
        EXPECT_EQ(m.mSpecularColor, (Vec3{ 1.f, 0.5f, 0.25f }));
        EXPECT_EQ(m.mSpecularStrength, 1.5f);
        EXPECT_EQ(m.mLightingEffects, (Vec2{ 0.125f, 0.375f }));
        ASSERT_NE(m.mTextureSet, sNone);
        ASSERT_LT(m.mTextureSet, scene.mTextureSets.size());
        EXPECT_EQ(scene.mTextureSets[m.mTextureSet].mTextures,
            (std::vector<std::string>{ "textures\\a_d.dds", "textures\\a_n.dds", "", "", "textures\\a_e.dds" }));
    }

    TEST(NifSceneMaterial, a_shape_without_a_shader_property_has_no_material)
    {
        Case c;
        c.mShape.mShader = -1;
        const Scene scene = read(c.build(lightingShader(c.mFile, baseSpec())));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        EXPECT_EQ(scene.mGeometries[0].mMaterial, sNone);
        EXPECT_EQ(scene.mGeometries[0].mAlpha, sNone);
        EXPECT_TRUE(scene.mMaterials.empty());
    }

    TEST(NifSceneMaterial, a_lighting_shader_without_a_texture_set_has_none)
    {
        Case c;
        LightingSpec spec = baseSpec();
        spec.mTextureSet = -1;
        const Scene scene = read(c.build(lightingShader(c.mFile, spec)));
        ASSERT_EQ(scene.mMaterials.size(), 1u);
        EXPECT_EQ(scene.mMaterials[0].mTextureSet, sNone);
    }

    TEST(NifSceneMaterial, the_type_specific_tail_of_each_skyrim_shader_type_is_read)
    {
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 1;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mShaderType, 1u);
            EXPECT_EQ(m.mEnvMapScale, 1.25f);
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 5;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mSkinTintColor, (Vec3{ 0.9f, 0.8f, 0.7f }));
            // Not stored by Skyrim: stays at its default.
            EXPECT_EQ(m.mSkinTintAlpha, 1.f);
            EXPECT_EQ(m.mEnvMapScale, 0.f);
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 6;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mHairTintColor, (Vec3{ 0.1f, 0.2f, 0.3f }));
            EXPECT_EQ(m.mSkinTintColor, (Vec3{ 0.f, 0.f, 0.f }));
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 7;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mParallaxMaxPasses, 4.f);
            EXPECT_EQ(m.mParallaxScale, 0.5f);
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 11;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mInnerLayerThickness, 0.5f);
            EXPECT_EQ(m.mInnerRefractionScale, 1.5f);
            EXPECT_EQ(m.mInnerLayerTextureScale, (Vec2{ 2.f, 3.f }));
            EXPECT_EQ(m.mInnerEnvMapScale, 0.75f);
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 14;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mSparkle, (Vec4{ 1.f, 2.f, 3.f, 4.f }));
        }
        {
            Case c;
            LightingSpec spec = baseSpec();
            spec.mType = 16;
            const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
            EXPECT_EQ(m.mShaderType, 16u);
            EXPECT_EQ(m.mEyeCubeMapScale, 0.875f);
            EXPECT_EQ(m.mLeftEyeReflectionCenter, (Vec3{ 1.f, 2.f, 3.f }));
            EXPECT_EQ(m.mRightEyeReflectionCenter, (Vec3{ -1.f, -2.f, -3.f }));
        }
    }

    TEST(NifSceneMaterial, fields_of_other_shader_types_and_other_games_keep_their_defaults)
    {
        Case c;
        LightingSpec spec = baseSpec();
        spec.mType = 0;
        const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
        const ShaderMaterial defaults;
        EXPECT_EQ(m.mEnvMapScale, defaults.mEnvMapScale);
        EXPECT_EQ(m.mHairTintColor, defaults.mHairTintColor);
        EXPECT_EQ(m.mSparkle, defaults.mSparkle);
        EXPECT_EQ(m.mSmoothness, defaults.mSmoothness);
        EXPECT_EQ(m.mRimlightPower, defaults.mRimlightPower);
        EXPECT_EQ(m.mWetnessMetalness, defaults.mWetnessMetalness);
        EXPECT_EQ(m.mRootMaterial, "");
        EXPECT_EQ(m.mSourceTexture, "");
    }

    TEST(NifSceneMaterial, a_name_on_a_skyrim_shader_property_is_kept)
    {
        Case c;
        LightingSpec spec = baseSpec();
        spec.mName = "my property";
        EXPECT_EQ(read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0).mName, "my property");
    }

    TEST(NifSceneMaterial, two_shapes_sharing_a_shader_property_and_texture_set_share_one_material)
    {
        Case c;
        ShapeSpec first = c.mShape;
        first.mName = "First";
        first.mShader = 3;
        ShapeSpec second = first;
        second.mName = "Second";
        LightingSpec spec = baseSpec();
        spec.mTextureSet = 4;
        // Records: 0 root, 1 and 2 the shapes, 3 the shader, 4 the texture set.
        c.mFile.mBlocks.push_back(node(c.mFile, "Root", { 1, 2 }));
        c.mFile.mBlocks.push_back(shape(c.mFile, first));
        c.mFile.mBlocks.push_back(shape(c.mFile, second));
        c.mFile.mBlocks.push_back(lightingShader(c.mFile, spec));
        c.mFile.mBlocks.push_back(textureSet({ "d.dds" }));
        const Scene scene = read(c.mFile);
        ASSERT_EQ(scene.mGeometries.size(), 2u);
        EXPECT_EQ(scene.mGeometries[0].mMaterial, 0u);
        EXPECT_EQ(scene.mGeometries[1].mMaterial, 0u);
        EXPECT_EQ(scene.mMaterials.size(), 1u);
        EXPECT_EQ(scene.mTextureSets.size(), 1u);
    }

    // ---------------------------------------------------------------- effect shader

    TEST(NifSceneMaterial, a_skyrim_effect_shader_gives_its_values)
    {
        Case c;
        EffectSpec spec;
        spec.mName = "flame";
        const Scene scene = read(c.build(effectShader(c.mFile, spec)));
        ASSERT_EQ(scene.mMaterials.size(), 1u);
        const ShaderMaterial& m = scene.mMaterials[0];
        EXPECT_EQ(m.mKind, ShaderKind::Effect);
        EXPECT_EQ(m.mRecordType, "BSEffectShaderProperty");
        EXPECT_EQ(m.mName, "flame");
        EXPECT_EQ(m.mFlags1, 0x40000040u);
        EXPECT_EQ(m.mFlags2, 0x00000010u);
        EXPECT_EQ(m.mUVOffset, (Vec2{ 0.f, 0.5f }));
        EXPECT_EQ(m.mUVScale, (Vec2{ 1.f, 3.f }));
        EXPECT_EQ(m.mSourceTexture, "textures\\effects\\flame.dds");
        EXPECT_EQ(m.mClampMode, 2u);
        EXPECT_EQ(m.mLightingInfluence, 7u);
        EXPECT_EQ(m.mEnvMapMinLod, 3u);
        EXPECT_EQ(m.mFalloff, (Vec4{ 1.f, 0.5f, 0.25f, 0.125f }));
        EXPECT_EQ(m.mBaseColor, (Vec4{ 0.5f, 0.25f, 0.75f, 1.f }));
        EXPECT_EQ(m.mBaseColorScale, 1.5f);
        EXPECT_EQ(m.mSoftFalloffDepth, 20.f);
        EXPECT_EQ(m.mGreyscaleTexture, "textures\\effects\\gradient.dds");
        // Fallout 4 only.
        EXPECT_EQ(m.mEnvMapTexture, "");
        EXPECT_EQ(m.mNormalTexture, "");
        EXPECT_EQ(m.mEffectEnvMapScale, 0.f);
        EXPECT_EQ(m.mTextureSet, sNone);
    }

    // ---------------------------------------------------------------- alpha property

    TEST(NifSceneMaterial, an_alpha_property_splits_its_flags)
    {
        Case c;
        // Blending on, source blend 6 (source alpha), destination blend 7 (inverse source alpha), test on, test mode 4
        // (greater), no sorter.
        const std::uint16_t flags
            = static_cast<std::uint16_t>(1u | (6u << 1) | (7u << 5) | (1u << 9) | (4u << 10) | (1u << 13));
        const Scene scene = read(c.build(lightingShader(c.mFile, baseSpec()), true, flags, 128));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        ASSERT_EQ(scene.mGeometries[0].mAlpha, 0u);
        ASSERT_EQ(scene.mAlphaProperties.size(), 1u);
        const AlphaProperty& a = scene.mAlphaProperties[0];
        EXPECT_EQ(a.mFlags, flags);
        EXPECT_EQ(a.mThreshold, 128u);
        EXPECT_TRUE(a.mBlend);
        EXPECT_TRUE(a.mTest);
        EXPECT_TRUE(a.mNoSorter);
        EXPECT_EQ(a.mSourceBlend, 6u);
        EXPECT_EQ(a.mDestinationBlend, 7u);
        EXPECT_EQ(a.mTestMode, 4u);
    }

    TEST(NifSceneMaterial, an_alpha_property_with_everything_off_reads_as_off)
    {
        Case c;
        const Scene scene = read(c.build(lightingShader(c.mFile, baseSpec()), true, 0, 0));
        const AlphaProperty& a = scene.mAlphaProperties.at(0);
        EXPECT_FALSE(a.mBlend);
        EXPECT_FALSE(a.mTest);
        EXPECT_FALSE(a.mNoSorter);
        EXPECT_EQ(a.mSourceBlend, 0u);
        EXPECT_EQ(a.mDestinationBlend, 0u);
        EXPECT_EQ(a.mTestMode, 0u);
    }

    // ---------------------------------------------------------------- Fallout 4

    TEST(NifSceneMaterial, a_fallout_4_lighting_shader_names_its_material_file_and_reads_its_values)
    {
        Case c(130);
        LightingSpec spec = baseSpec();
        spec.mName = "materials\\actors\\body.bgsm";
        spec.mRootMaterial = "materials\\root.bgsm";
        spec.mType = 5;
        spec.mGlossiness = 0.5f;
        const Scene scene = read(c.build(lightingShader(c.mFile, spec)));
        ASSERT_EQ(scene.mMaterials.size(), 1u);
        const ShaderMaterial& m = scene.mMaterials[0];
        EXPECT_EQ(m.mName, "materials\\actors\\body.bgsm");
        EXPECT_EQ(m.mRootMaterial, "materials\\root.bgsm");
        EXPECT_EQ(m.mShaderType, 5u);
        EXPECT_EQ(m.mSmoothness, 0.5f);
        EXPECT_EQ(m.mGlossiness, 0.f);
        EXPECT_EQ(m.mSubsurfaceRolloff, 0.25f);
        EXPECT_EQ(m.mRimlightPower, 2.f);
        EXPECT_EQ(m.mBacklightPower, 0.f);
        EXPECT_EQ(m.mGrayscaleToPaletteScale, 0.5f);
        EXPECT_EQ(m.mFresnelPower, 6.f);
        EXPECT_EQ(m.mWetnessSpecScale, 1.f);
        EXPECT_EQ(m.mWetnessSpecPower, 2.f);
        EXPECT_EQ(m.mWetnessMinVar, 3.f);
        EXPECT_EQ(m.mWetnessEnvMapScale, 4.f);
        EXPECT_EQ(m.mWetnessFresnelPower, 5.f);
        EXPECT_EQ(m.mWetnessMetalness, 6.f);
        EXPECT_EQ(m.mSkinTintColor, (Vec3{ 0.9f, 0.8f, 0.7f }));
        EXPECT_EQ(m.mSkinTintAlpha, 0.5f);
        // Skyrim-only fields stay at their defaults.
        EXPECT_EQ(m.mLightingEffects, (Vec2{ 0.f, 0.f }));
    }

    TEST(NifSceneMaterial, a_fallout_4_backlight_power_is_read_only_when_the_rim_light_power_is_the_float_maximum)
    {
        Case c(130);
        LightingSpec spec = baseSpec();
        spec.mRimlightPower = std::numeric_limits<float>::max();
        spec.mBacklightPower = 3.f;
        const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
        EXPECT_EQ(m.mRimlightPower, std::numeric_limits<float>::max());
        EXPECT_EQ(m.mBacklightPower, 3.f);
        // The fields after it are still where the layout puts them.
        EXPECT_EQ(m.mGrayscaleToPaletteScale, 0.5f);
        EXPECT_EQ(m.mWetnessMetalness, 6.f);
    }

    TEST(NifSceneMaterial, a_fallout_4_env_map_shader_has_its_screen_space_reflection_flags)
    {
        Case c(130);
        LightingSpec spec = baseSpec();
        spec.mType = 1;
        spec.mUseSSR = 1;
        spec.mWetnessUseSSR = 1;
        const ShaderMaterial m = read(c.build(lightingShader(c.mFile, spec))).mMaterials.at(0);
        EXPECT_TRUE(m.mUseSSR);
        EXPECT_TRUE(m.mWetnessUseSSR);
        // Fallout 4 up to stream 139 stores the env map scale as well.
        EXPECT_EQ(m.mEnvMapScale, 1.25f);
    }

    TEST(NifSceneMaterial, a_fallout_4_effect_shader_has_its_extra_texture_paths)
    {
        Case c(130);
        EffectSpec spec;
        spec.mName = "materials\\fx\\glow.bgem";
        const ShaderMaterial m = read(c.build(effectShader(c.mFile, spec))).mMaterials.at(0);
        EXPECT_EQ(m.mName, "materials\\fx\\glow.bgem");
        EXPECT_EQ(m.mSourceTexture, "textures\\effects\\flame.dds");
        EXPECT_EQ(m.mEnvMapTexture, "env.dds");
        EXPECT_EQ(m.mNormalTexture, "normal.dds");
        EXPECT_EQ(m.mEnvMaskTexture, "mask.dds");
        EXPECT_EQ(m.mEffectEnvMapScale, 0.625f);
    }

    // ---------------------------------------------------------------- what the scene does not read, hostile input

    TEST(NifSceneMaterial, a_shader_property_of_another_kind_is_a_warning_and_no_material)
    {
        Case c;
        const Scene scene = read(c.build(waterShader(c.mFile)));
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        EXPECT_EQ(scene.mGeometries[0].mMaterial, sNone);
        EXPECT_TRUE(scene.mMaterials.empty());
        EXPECT_TRUE(hasWarning(scene, "BSWaterShaderProperty")) << dump(scene);
        EXPECT_TRUE(hasWarning(scene, "Shape")) << dump(scene);
    }

    TEST(NifSceneMaterial, a_texture_set_with_a_giant_count_is_a_located_error_and_not_a_crash)
    {
        Case c;
        GamebryoFile file = c.build(lightingShader(c.mFile, baseSpec()));
        Block huge;
        huge.mType = "BSShaderTextureSet";
        huge.mData.put(static_cast<std::uint32_t>(0x7FFFFFFFu));
        file.mBlocks[3] = std::move(huge);
        const std::string error = errorOf(file);
        // The record readers do not name the file (the loader that called the reader does); the size check names the
        // numbers.
        EXPECT_NE(error.find("stream size"), std::string::npos) << error;
    }

    TEST(NifSceneMaterial, truncating_a_file_with_shaders_at_every_length_never_crashes)
    {
        Case c;
        const std::string bytes = c.build(lightingShader(c.mFile, baseSpec()), true, 0x0201, 5).build();
        std::size_t refused = 0;
        for (std::size_t length = 0; length + 1 < bytes.size(); ++length)
        {
            auto input = std::make_unique<std::istringstream>(bytes.substr(0, length));
            input->exceptions(std::ios::failbit | std::ios::badbit);
            try
            {
                readScene(std::move(input), path, optionsAny());
            }
            catch (const std::exception&)
            {
                ++refused;
            }
        }
        EXPECT_GT(refused, bytes.size() / 2);
    }

    // ---------------------------------------------------------------- the dump

    TEST(NifSceneMaterial, the_dump_shows_materials_texture_sets_and_alpha_properties)
    {
        Case c;
        const Scene scene
            = read(c.build(lightingShader(c.mFile, baseSpec()), true, 0x0201, 5, { "textures\\unique_diffuse.dds" }));
        const std::string text = dump(scene);
        EXPECT_NE(text.find("unique_diffuse.dds"), std::string::npos) << text;
        EXPECT_NE(text.find("material 0"), std::string::npos) << text;
        EXPECT_NE(text.find("alpha 0"), std::string::npos) << text;
        // Same file, same text.
        Case again;
        EXPECT_EQ(text,
            dump(read(again.build(
                lightingShader(again.mFile, baseSpec()), true, 0x0201, 5, { "textures\\unique_diffuse.dds" }))));
    }

    TEST(NifSceneMaterial, the_dump_of_a_scene_without_materials_has_no_material_lines)
    {
        Case c;
        c.mShape.mShader = -1;
        const std::string text = dump(read(c.build(lightingShader(c.mFile, baseSpec()))));
        EXPECT_EQ(text.find("material"), std::string::npos) << text;
        EXPECT_EQ(text.find("alpha"), std::string::npos) << text;
    }

    TEST(NifSceneMaterial, a_changed_value_changes_the_dump)
    {
        Case a;
        Case b;
        LightingSpec other = baseSpec();
        other.mSpecularStrength = 9.f;
        EXPECT_NE(dump(read(a.build(lightingShader(a.mFile, baseSpec())))),
            dump(read(b.build(lightingShader(b.mFile, other)))));
    }
}
