#ifndef VSGOPENMW_MWRENDER_SKY_H
#define VSGOPENMW_MWRENDER_SKY_H

// vsgopenmw-move-me("sky.hpp")

#include <components/misc/vsgmath.hpp>
#include <components/render/cloudgpu.hpp>
#include <components/vsgutil/composite.hpp>
#include <components/vsgutil/toggle.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/quat.h>

#include "rain.hpp"

namespace Resource
{
    class ResourceSystem;
}
namespace vsgUtil
{
    class CompileContext;
}
namespace MWRender
{
    struct Atmosphere;
    class AtmosphereNight;
    class Clouds;
    class Cirrus;
    class Sun;
    class Moon;
    struct WeatherResult;
    struct MoonState;

    /// Handles rendering of sky domes, celestial bodies as well as other objects that need to be rendered
    /// relative to the camera (e.g. weather particle effects)
    class Sky : public vsgUtil::Composite<vsg::Group>
    {
    public:
        Sky(Resource::ResourceSystem& resourceSystem, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            bool occlusionQueryPrecise);
        ~Sky();

        vsg::ref_ptr<vsg::Node> resetNode();

        void update(float duration);

        void setEnabled(bool enabled);

        void setHour(double hour);
        ///< will be called even when sky is disabled.

        void setDate(int day, int month);
        ///< will be called even when sky is disabled.

        int getMasserPhase() const;
        ///< 0 new moon, 1 waxing or waning cresecent, 2 waxing or waning half,
        /// 3 waxing or waning gibbous, 4 full moon

        int getSecundaPhase() const;
        ///< 0 new moon, 1 waxing or waning cresecent, 2 waxing or waning half,
        /// 3 waxing or waning gibbous, 4 full moon

        void setMoonColour(bool red);
        ///< change Secunda colour to red

        void setWeather(const WeatherResult& weather);

        void setAtmosphereNightRoll(float roll);

        void sunEnable();

        void sunDisable();

        bool isEnabled();

        bool hasRain() const;

        float getRainIntensity() const;
        float getPrecipitationAlpha() const { return mPrecipitationAlpha; }

        void setRainSpeed(float speed);

        void setStormParticleDirection(const vsg::vec3& direction);

        /// The weather's particle effect model ("" for none) and its orientation (identity unless storming).
        const std::string& getParticleEffect() const { return mCurrentParticleEffect; }
        const vsg::quat& getParticleOrientation() const { return mParticleOrientation; }
        /// The current weather's rain, for Rain (task 0012).
        void getRainParameters(Rain::Parameters& out) const;

        /// OpenEden (task 0038): what the renderer needs to draw the clouds' shadows on the ground. The grids belong to
        /// the Sky and stay valid until the next call of setWeather or update; `valid` is false when cloud shadows are
        /// off or the weather has no cloud picture.
        struct CloudShadowInput
        {
            bool valid = false;
            const float* grid[2] = { nullptr, nullptr };
            float scroll = 0.f;
            float blend = 0.f;
            float share = 0.f;
            Render::Cloud::Rotation rotation[2] = { Render::Cloud::identityRotation, Render::Cloud::identityRotation };
        };
        CloudShadowInput getCloudShadow() const;

        /// OpenEden (task 0042): the ray-marched clouds. RenderManager gives the sky the resolved picture once the
        /// cloud passes exist; until then (and with the setting off) the sky is the vanilla one.
        void setVolumeClouds(vsg::ref_ptr<vsg::Descriptor> image);
        /// What the cloud passes need each frame: the weather's coverage, the wind offsets in metres (wrapped by the
        /// noise tile), and the share of the sky the volume draws (0: the vanilla layers only).
        struct VolumeCloudInput
        {
            bool valid = false;
            float coverage = 0.f;
            float windX = 0.f;
            float windY = 0.f;
            float share = 0.f;
        };
        VolumeCloudInput getVolumeClouds() const;

        void setSunDirection(const vsg::vec3& direction);

        void setMasserState(const MoonState& state);
        void setSecundaState(const MoonState& state);

        void setGlareTimeOfDayFade(float val);

        float getBaseWindSpeed() const { return mBaseWindSpeed; }
        vsg::vec4 getSkyColor() const { return mSkyColor; }
        float getLightSize() const { return mLightSize; }

    private:
        vsg::ref_ptr<vsg::Descriptor> getOrCreateCloudTexture(const std::string& file);
        void createRain();
        void destroyRain();
        void switchUnderwaterRain();
        void updateRainParameters();
        void updateParticleOrientation();
        void applyVolumeClouds(const WeatherResult& weather);
        void applyExtraClouds(const WeatherResult& weather);
        void applySunAndMoons(const WeatherResult& weather);
        void applyCirrus(const WeatherResult& weather);
        void updateCirrus(float duration);

        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        std::map<std::string, vsg::ref_ptr<vsg::Descriptor>> mCloudTextures;
        vsg::ref_ptr<const vsg::Options> mTextureOptions;
        vsg::ref_ptr<vsg::Switch> mSwitch;

        std::unique_ptr<Atmosphere> mAtmosphere;
        vsgUtil::Toggle<AtmosphereNight> mAtmosphereNight;
        vsgUtil::Toggle<Clouds> mClouds;
        vsgUtil::Toggle<Clouds> mNextClouds;
        // Task 0127 stage W-S3: more cloud pictures over the first (a game pack's weather; empty for Morrowind, so
        // nothing extra is made), each with its own drift.
        std::vector<vsgUtil::Toggle<Clouds>> mExtraClouds;
        std::vector<float> mExtraCloudTimers;
        std::vector<float> mExtraCloudSpeeds;
        std::string mSunTextureName;
        std::string mSunGlareTextureName;
        bool mMasserAvailable = true;
        bool mSecundaAvailable = true;
        vsgUtil::Toggle<Sun> mSun;
        vsgUtil::Toggle<Moon> mMasser;
        vsgUtil::Toggle<Moon> mSecunda;
        vsgUtil::Toggle<Cirrus> mCirrus; // task 0079: made only with [Rendering] cirrus (and hdr, atmosphere, slangc)

        bool mIsStorm = false;

        int mDay = 0;
        int mMonth = 0;

        float mCloudAnimationTimer = 0.f;

        float mRainTimer = 0.f;

        // particle system rotation is independent of cloud rotation internally
        vsg::vec3 mStormParticleDirection;
        vsg::vec3 mStormDirection;
        vsg::vec3 mNextStormDirection;

        float mCloudBlendFactor = 0.f;
        // OpenEden (task 0038): the lit look of the clouds, and the weather's share of it (now, and heading for).
        bool mCloudsLit = false;
        // Cloud shadows (task 0038): whether they are on, the coarse grid of each cloud picture (built once per
        // picture, from its pixels), the two layers' current pictures and rotations (dome space to world).
        bool mCloudShadowsOn = false;
        std::map<std::string, std::vector<float>> mCloudGrids;
        std::map<std::string, float> mCloudMeanThickness; // task 0042: per picture, for the volumetric coverage
        const std::vector<float>* mCloudGrid[2] = { nullptr, nullptr };
        Render::Cloud::Rotation mCloudRotation[2]
            = { Render::Cloud::identityRotation, Render::Cloud::identityRotation };
        // Volumetric clouds (task 0042): the setting (the pictures' grids are built for the coverage), the resolved
        // picture once it exists, the share of the sky the volume draws now and heading for, the coverage the weather
        // gives (two layers, blended by the weather manager's factor) and the wind (double: it grows all session).
        bool mVolumeCloudsOn = false;
        // OpenEden (task 0042 follow-up): the volume's dome is lowered below the horizon and its fade is not applied.
        bool mVolumeHorizon = false;
        vsg::ref_ptr<vsg::Descriptor> mVolumeImage;
        float mVolumeShare = 0.f;
        float mVolumeShareTarget = 0.f;
        bool mVolumeShareSet = false;
        float mVolumeCoverage = 0.f;
        float mVolumeCoverageLogged = -1.f; // only to log a change
        double mVolumeWind[2] = { 0.0, 0.0 };
        // Cirrus (task 0079): the setting took effect (the cloud pictures' grids are then built for the weather share),
        // the share of the layer the weather allows now and heading for, the sum of the wind's drift along the layer's
        // wind (double: it grows all session; wrapped by the pattern's period) and of the simulation seconds (the
        // morph), the sun's true height, and what the settings gave.
        bool mCirrusOn = false;
        bool mCirrusDebugOn = false; // a debug view is on: the vanilla cloud layers are not drawn
        float mCirrusShare = 0.f;
        float mCirrusShareTarget = 0.f;
        bool mCirrusShareSet = false;
        float mCirrusCoverageLogged = -1.f; // only to log a change
        bool mCirrusShareLogged = false; // the share was above 0 (to log when it reaches 0)
        double mCirrusDrift = 0.0;
        double mCirrusSeconds = 0.0;
        float mSunElevation = 0.f;
        vsg::vec3 mCirrusTint{ 1.f, 1.f, 1.f };
        float mCloudShare = 0.f;
        float mCloudShareTarget = 0.f;
        bool mCloudShareSet = false;
        float mCloudSpeed = 0.f;
        vsg::vec4 mSkyColor;

        std::string mCurrentParticleEffect;
        bool mRainRipplesEnabled = false;
        bool mSnowRipplesEnabled = false;
        vsg::quat mParticleOrientation;

        bool mRainEnabled = false;
        std::string mRainEffect;
        float mRainSpeed = 0.f;
        float mRainDiameter = 0.f;
        float mRainMinHeight = 0.f;
        float mRainMaxHeight = 0.f;
        float mRainEntranceSpeed = 1.f;
        int mRainMaxRaindrops = 0;
        float mWindSpeed = 0.f;
        float mBaseWindSpeed = 0.f;

        float mGlareTimeOfDayFade = 0.f;

        float mPrecipitationAlpha = 0.f;
        bool mDirtyParticlesEffect = false;

        vsg::vec4 mMoonScriptColor;
        float mLightSize{};
    };
}

#endif
