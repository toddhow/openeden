#ifndef VSGOPENMW_MWRENDER_WEATHERDATA_H
#define VSGOPENMW_MWRENDER_WEATHERDATA_H

#include <string>

#include <components/esm/refid.hpp>
#include <components/misc/vsgmath.hpp>

namespace MWRender
{
    struct WeatherResult
    {
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
