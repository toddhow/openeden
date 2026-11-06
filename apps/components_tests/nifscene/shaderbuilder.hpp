#ifndef OPENEDEN_TEST_SUITE_NIFSCENE_SHADERBUILDER_H
#define OPENEDEN_TEST_SUITE_NIFSCENE_SHADERBUILDER_H

// Generated shader property, texture set and alpha property blocks for the scene tests (task 0082, item 4).
// Layouts follow Nif::BSLightingShaderProperty::read and its siblings in components/nif, written here by hand; every
// byte comes from the test that asks for it (no game asset).

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "ssebuilder.hpp"

namespace NifScene::Testing
{
    constexpr std::uint32_t noString = 0xFFFFFFFFu;

    // BSShaderTextureSet: a count and that many sized strings.
    inline Block textureSet(const std::vector<std::string>& paths)
    {
        Block block;
        block.mType = "BSShaderTextureSet";
        block.mData.put(static_cast<std::uint32_t>(paths.size()));
        for (const std::string& path : paths)
            block.mData.sized(path);
        return block;
    }

    // NiAlphaProperty: the NiObjectNET part (name, no extra data, no controller), 16-bit flags, an 8-bit threshold.
    inline Block alphaProperty(GamebryoFile& file, std::uint16_t flags, std::uint8_t threshold)
    {
        Block block;
        block.mType = "NiAlphaProperty";
        block.mData.put(file.string("alpha"));
        block.mData.put(static_cast<std::uint32_t>(0));
        block.mData.put(static_cast<std::int32_t>(-1));
        block.mData.put(flags);
        block.mData.put(threshold);
        return block;
    }

    struct LightingSpec
    {
        std::string mName; // the property's name (Fallout 4: a material path)
        std::uint32_t mType = 0;
        std::uint32_t mFlags1 = 0x80000001u;
        std::uint32_t mFlags2 = 0x00000010u;
        std::array<float, 2> mUVOffset{ 0.25f, 0.5f };
        std::array<float, 2> mUVScale{ 2.f, 4.f };
        std::int32_t mTextureSet = -1;
        std::array<float, 3> mEmissive{ 0.5f, 0.25f, 0.125f };
        float mEmissiveMultiple = 2.f;
        std::string mRootMaterial; // Fallout 4
        std::uint32_t mClamp = 1;
        float mAlpha = 0.75f;
        float mRefraction = 0.0625f;
        float mGlossiness = 32.f; // Skyrim; the Fallout 4 smoothness
        std::array<float, 3> mSpecular{ 1.f, 0.5f, 0.25f };
        float mSpecularStrength = 1.5f;
        std::array<float, 2> mLightingEffects{ 0.125f, 0.375f }; // Skyrim
        // Fallout 4
        float mSubsurfaceRolloff = 0.25f;
        float mRimlightPower = 2.f;
        float mBacklightPower = 3.f; // written only when the rim light power is the float maximum
        float mGrayscaleToPalette = 0.5f;
        float mFresnel = 6.f;
        // spec scale, power, min var, env map scale (stream 130 only), fresnel, metalness
        std::array<float, 6> mWetness{ 1.f, 2.f, 3.f, 4.f, 5.f, 6.f };
        // Tails
        float mEnvMapScale = 1.25f;
        std::uint8_t mUseSSR = 1;
        std::uint8_t mWetnessUseSSR = 0;
        std::array<float, 3> mSkinTint{ 0.9f, 0.8f, 0.7f };
        float mSkinTintAlpha = 0.5f;
        std::array<float, 3> mHairTint{ 0.1f, 0.2f, 0.3f };
        float mParallaxPasses = 4.f;
        float mParallaxScale = 0.5f;
        // thickness, refraction scale, texture scale x and y, env map scale
        std::array<float, 5> mMultiLayer{ 0.5f, 1.5f, 2.f, 3.f, 0.75f };
        std::array<float, 4> mSparkle{ 1.f, 2.f, 3.f, 4.f };
        float mEyeScale = 0.875f;
        std::array<float, 3> mLeftEye{ 1.f, 2.f, 3.f };
        std::array<float, 3> mRightEye{ -1.f, -2.f, -3.f };
    };

    // BSLightingShaderProperty of a Skyrim (stream < 130) or Fallout 4 (130 to 139) file, by file.mStreamVersion.
    inline Block lightingShader(GamebryoFile& file, const LightingSpec& s)
    {
        const bool fo4 = file.mStreamVersion >= 130;
        Block block;
        block.mType = "BSLightingShaderProperty";
        ByteWriter& out = block.mData;
        out.put(s.mType);
        out.put(s.mName.empty() ? noString : file.string(s.mName)); // NiObjectNET name
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::int32_t>(-1));
        out.put(s.mFlags1);
        out.put(s.mFlags2);
        out.floats({ s.mUVOffset[0], s.mUVOffset[1], s.mUVScale[0], s.mUVScale[1] });
        out.put(s.mTextureSet);
        out.floats({ s.mEmissive[0], s.mEmissive[1], s.mEmissive[2], s.mEmissiveMultiple });
        if (fo4)
            out.put(s.mRootMaterial.empty() ? noString : file.string(s.mRootMaterial));
        out.put(s.mClamp);
        out.floats({ s.mAlpha, s.mRefraction, s.mGlossiness });
        out.floats({ s.mSpecular[0], s.mSpecular[1], s.mSpecular[2], s.mSpecularStrength });
        if (!fo4)
        {
            out.floats({ s.mLightingEffects[0], s.mLightingEffects[1] });
        }
        else
        {
            out.put(s.mSubsurfaceRolloff);
            out.put(s.mRimlightPower);
            if (s.mRimlightPower == std::numeric_limits<float>::max())
                out.put(s.mBacklightPower);
            out.put(s.mGrayscaleToPalette);
            out.put(s.mFresnel);
            out.put(s.mWetness[0]);
            out.put(s.mWetness[1]);
            out.put(s.mWetness[2]);
            if (file.mStreamVersion == 130)
                out.put(s.mWetness[3]);
            out.put(s.mWetness[4]);
            out.put(s.mWetness[5]);
        }
        switch (s.mType)
        {
            case 1:
                out.put(s.mEnvMapScale); // streams up to 139
                if (fo4)
                {
                    out.put(s.mUseSSR);
                    out.put(s.mWetnessUseSSR);
                }
                break;
            case 5:
                out.floats({ s.mSkinTint[0], s.mSkinTint[1], s.mSkinTint[2] });
                if (fo4)
                    out.put(s.mSkinTintAlpha);
                break;
            case 6:
                out.floats({ s.mHairTint[0], s.mHairTint[1], s.mHairTint[2] });
                break;
            case 7:
                out.floats({ s.mParallaxPasses, s.mParallaxScale });
                break;
            case 11:
                out.floats(
                    { s.mMultiLayer[0], s.mMultiLayer[1], s.mMultiLayer[2], s.mMultiLayer[3], s.mMultiLayer[4] });
                break;
            case 14:
                out.floats({ s.mSparkle[0], s.mSparkle[1], s.mSparkle[2], s.mSparkle[3] });
                break;
            case 16:
                out.floats({ s.mEyeScale, s.mLeftEye[0], s.mLeftEye[1], s.mLeftEye[2], s.mRightEye[0], s.mRightEye[1],
                    s.mRightEye[2] });
                break;
            default:
                break;
        }
        return block;
    }

    struct EffectSpec
    {
        std::string mName;
        std::uint32_t mFlags1 = 0x40000040u;
        std::uint32_t mFlags2 = 0x00000010u;
        std::array<float, 2> mUVOffset{ 0.f, 0.5f };
        std::array<float, 2> mUVScale{ 1.f, 3.f };
        std::string mSource = "textures\\effects\\flame.dds";
        std::uint8_t mClamp = 2;
        std::uint8_t mLightingInfluence = 7;
        std::uint8_t mEnvMapMinLod = 3;
        std::array<float, 4> mFalloff{ 1.f, 0.5f, 0.25f, 0.125f };
        std::array<float, 4> mBaseColor{ 0.5f, 0.25f, 0.75f, 1.f };
        float mBaseColorScale = 1.5f;
        float mFalloffDepth = 20.f;
        std::string mGreyscale = "textures\\effects\\gradient.dds";
        // Fallout 4
        std::string mEnvMap = "env.dds";
        std::string mNormal = "normal.dds";
        std::string mEnvMask = "mask.dds";
        float mEnvMapScale = 0.625f;
    };

    inline Block effectShader(GamebryoFile& file, const EffectSpec& s)
    {
        const bool fo4 = file.mStreamVersion >= 130;
        Block block;
        block.mType = "BSEffectShaderProperty";
        ByteWriter& out = block.mData;
        out.put(s.mName.empty() ? noString : file.string(s.mName));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::int32_t>(-1));
        out.put(s.mFlags1);
        out.put(s.mFlags2);
        out.floats({ s.mUVOffset[0], s.mUVOffset[1], s.mUVScale[0], s.mUVScale[1] });
        out.sized(s.mSource);
        out.put(static_cast<std::uint32_t>(s.mClamp | (static_cast<std::uint32_t>(s.mLightingInfluence) << 8)
            | (static_cast<std::uint32_t>(s.mEnvMapMinLod) << 16)));
        out.floats({ s.mFalloff[0], s.mFalloff[1], s.mFalloff[2], s.mFalloff[3] });
        out.floats({ s.mBaseColor[0], s.mBaseColor[1], s.mBaseColor[2], s.mBaseColor[3] });
        out.floats({ s.mBaseColorScale, s.mFalloffDepth });
        out.sized(s.mGreyscale);
        if (fo4)
        {
            out.sized(s.mEnvMap);
            out.sized(s.mNormal);
            out.sized(s.mEnvMask);
            out.put(s.mEnvMapScale);
        }
        return block;
    }

    // BSWaterShaderProperty: a shader property the scene does not read.
    inline Block waterShader(GamebryoFile& file)
    {
        Block block;
        block.mType = "BSWaterShaderProperty";
        ByteWriter& out = block.mData;
        out.put(file.string("water"));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::int32_t>(-1));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::uint32_t>(0));
        out.floats({ 0.f, 0.f, 1.f, 1.f });
        out.put(static_cast<std::uint32_t>(0));
        return block;
    }
}

#endif
