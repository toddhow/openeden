#ifndef OPENMW_COMPONENTS_BGSM_FILE_HPP
#define OPENMW_COMPONENTS_BGSM_FILE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/files/istreamptr.hpp>

namespace Bgsm
{
    class BGSMStream;

    enum class ShaderType
    {
        Lighting,
        Effect,
    };

    struct MaterialFile
    {
        ShaderType mShaderType;
        std::uint32_t mVersion;
        std::uint32_t mClamp;
        vsg::vec2 mUVOffset, mUVScale;
        float mTransparency;
        bool mAlphaBlend;
        std::uint32_t mSourceBlendMode;
        std::uint32_t mDestinationBlendMode;
        std::uint8_t mAlphaTestThreshold;
        bool mAlphaTest;
        bool mDepthWrite, mDepthTest;
        bool mSSR;
        bool mWetnessControlSSR;
        bool mDecal;
        bool mTwoSided;
        bool mDecalNoFade;
        bool mNonOccluder;
        bool mRefraction;
        bool mRefractionFalloff;
        float mRefractionPower;
        bool mEnvMapEnabled;
        float mEnvMapMaskScale;
        bool mDepthBias;
        bool mGrayscaleToPaletteColor;
        std::uint8_t mMaskWrites;

        MaterialFile() = default;
        virtual void read(BGSMStream& stream);
        virtual ~MaterialFile() = default;

        bool wrapT() const { return mClamp & 1; }
        bool wrapS() const { return mClamp & 2; }
    };

    struct BGSMFile : MaterialFile
    {
        std::string mDiffuseMap;
        std::string mNormalMap;
        std::string mSmoothSpecMap;
        std::string mGrayscaleMap;
        std::string mGlowMap;
        std::string mWrinkleMap;
        std::string mSpecularMap;
        std::string mLightingMap;
        std::string mFlowMap;
        std::string mDistanceFieldAlphaMap;
        std::string mEnvMap;
        std::string mInnerLayerMap;
        std::string mDisplacementMap;
        bool mEnableEditorAlphaThreshold;
        bool mTranslucency;
        bool mTranslucencyThickObject;
        bool mTranslucencyMixAlbedoWithSubsurfaceColor;
        vsg::vec3 mTranslucencySubsurfaceColor;
        float mTranslucencyTransmissiveScale;
        float mTranslucencyTurbulence;
        bool mRimLighting;
        float mRimPower;
        float mBackLightPower;
        bool mSubsurfaceLighting;
        float mSubsurfaceLightingRolloff;
        bool mSpecularEnabled;
        vsg::vec3 mSpecularColor;
        float mSpecularMult;
        float mSmoothness;
        float mFresnelPower;
        float mWetnessControlSpecScale;
        float mWetnessControlSpecPowerScale;
        float mWetnessControlSpecMinvar;
        float mWetnessControlEnvMapScale;
        float mWetnessControlFresnelPower;
        float mWetnessControlMetalness;
        bool mPBR;
        bool mCustomPorosity;
        float mPorosityValue;
        std::string mRootMaterialPath;
        bool mAnisoLighting;
        bool mEmitEnabled;
        vsg::vec3 mEmittanceColor;
        float mEmittanceMult;
        bool mModelSpaceNormals;
        bool mExternalEmittance;
        float mLumEmittance;
        bool mUseAdaptiveEmissive;
        vsg::vec3 mAdaptiveEmissiveExposureParams;
        bool mBackLighting;
        bool mReceiveShadows;
        bool mHideSecret;
        bool mCastShadows;
        bool mDissolveFade;
        bool mAssumeShadowmask;
        bool mGlowMapEnabled;
        bool mEnvMapWindow;
        bool mEnvMapEye;
        bool mHair;
        vsg::vec3 mHairTintColor;
        bool mTree;
        bool mFacegen;
        bool mSkinTint;
        bool mTessellate;
        vsg::vec2 mDisplacementMapParams;
        vsg::vec3 mTessellationParams;
        float mGrayscaleToPaletteScale;
        bool mSkewSpecularAlpha;
        bool mTerrain;
        vsg::vec3 mTerrainParams;

        void read(BGSMStream& stream) override;
    };

    struct BGEMFile : MaterialFile
    {
        std::string mBaseMap;
        std::string mGrayscaleMap;
        std::string mEnvMap;
        std::string mNormalMap;
        std::string mEnvMapMask;
        std::string mSpecularMap;
        std::string mLightingMap;
        std::string mGlowMap;
        bool mBlood;
        bool mEffectLighting;
        bool mFalloff;
        bool mFalloffColor;
        bool mGrayscaleToPaletteAlpha;
        bool mSoft;
        vsg::vec3 mBaseColor;
        float mBaseColorScale;
        vsg::vec4 mFalloffParams;
        float mLightingInfluence;
        std::uint8_t mEnvmapMinLOD;
        float mSoftDepth;
        vsg::vec3 mEmittanceColor;
        vsg::vec3 mAdaptiveEmissiveExposureParams;
        bool mGlowMapEnabled;
        bool mEffectPbrSpecular;

        void read(BGSMStream& stream) override;
    };

    using MaterialFilePtr = std::shared_ptr<const Bgsm::MaterialFile>;
    MaterialFilePtr parse(Files::IStreamPtr&& stream);
}
#endif
