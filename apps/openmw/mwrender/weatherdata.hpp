#ifndef VSGOPENMW_MWRENDER_WEATHERDATA_H
#define VSGOPENMW_MWRENDER_WEATHERDATA_H

#include <cstddef>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/misc/vsgmath.hpp>

namespace MWRender
{
    /// The renderer's pool of extra cloud layers (the SDK allows this many per weather).
    inline constexpr std::size_t kMaxExtraCloudLayers = 8;

    /// One more cloud picture drawn over the weather's first (OpenEden task 0127, stage W-S3; a game pack's weather
    /// only). The alpha already holds the weather's share of a transition.
    struct ExtraCloudLayer
    {
        std::string mTexture;
        float mAlpha = 0.f;
        vsg::vec3 mTint{ 1.f, 1.f, 1.f };
        float mSpeed = 0.f;
    };

    struct WeatherResult
    {
        // OpenEden (task 0127, stage W-S3). All of these keep their defaults for Morrowind: no extra layers, no image
        // space, the engine's own sun pictures, both moons.
        std::vector<ExtraCloudLayer> mExtraClouds;
        bool mHasGrade = false;
        vsg::vec4 mGrade{ 1.f, 1.f, 1.f, 0.f }; // brightness, saturation, contrast, tint amount
        vsg::vec3 mGradeTint{ 1.f, 1.f, 1.f };
        std::string mSunTexture;
        std::string mSunGlareTexture;
        bool mMasserAvailable = true;
        bool mSecundaAvailable = true;
        /// Task 0127 stage W-S4: the moons are lit (see Sky::setMasserState). Set only by a game pack's weather; the
        /// moons of Morrowind's weather are drawn as they always were in this renderer.
        bool mLitMoons = false;

        std::string mCloudTexture;
        std::string mNextCloudTexture;
        float mCloudBlendFactor;
        // OpenEden (task 0034): multiplies the cloud texture; white (as before) unless the atmosphere sets it.
        vsg::vec3 mCloudTint{ 1.f, 1.f, 1.f };
        // OpenEden (task 0079): the sun's true height in radians (negative below the horizon) as the atmosphere model
        // took it (the colour tables' hour with `sun follows colour tables`), set only by the atmosphere
        // (WeatherManager::applyAtmosphere); the cirrus layer follows it so the sky and the layer agree.
        float mSunElevation = 0.f;

        vsg::vec4 mFogColor;

        vsg::vec4 mAmbientColor;

        vsg::vec4 mSkyColor;

        // sun light color
        vsg::vec4 mSunColor;

        // alpha is the sun transparency
        vsg::vec4 mSunDiscColor;

        float mFogDepth;

        float mDLFogFactor;
        float mDLFogOffset;

        float mWindSpeed;
        float mBaseWindSpeed;
        float mCurrentWindSpeed;
        float mNextWindSpeed;

        float mCloudSpeed;

        float mGlareView;

        bool mNight; // use night skybox
        float mNightFade; // fading factor for night skybox

        bool mIsStorm;

        ESM::RefId mAmbientLoopSoundID;
        ESM::RefId mRainLoopSoundID;
        float mAmbientSoundVolume;

        std::string mParticleEffect;
        std::string mRainEffect;
        float mPrecipitationAlpha;

        float mRainDiameter;
        float mRainMinHeight;
        float mRainMaxHeight;
        float mRainSpeed;
        float mRainEntranceSpeed;
        int mRainMaxRaindrops;

        vsg::vec3 mStormDirection;
        vsg::vec3 mNextStormDirection;
    };

    struct MoonState
    {
        enum class Phase
        {
            Full = 0,
            WaningGibbous,
            ThirdQuarter,
            WaningCrescent,
            New,
            WaxingCrescent,
            FirstQuarter,
            WaxingGibbous,
            Count
        };

        float mRotationFromHorizon;
        float mRotationFromNorth;
        Phase mPhase;
        float mShadowBlend;
        float mMoonAlpha;
    };
}

#endif
