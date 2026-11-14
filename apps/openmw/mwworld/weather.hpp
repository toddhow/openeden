#ifndef GAME_MWWORLD_WEATHER_H
#define GAME_MWWORLD_WEATHER_H

#include <cstdint>
#include <map>
#include <string>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec4.h>

#include <components/esm/refid.hpp>
#include <components/fallback/fallback.hpp>
#include <components/render/covermath.hpp>
#include <components/render/lightningmath.hpp>

#include "../mwbase/soundmanager.hpp"

#include "../mwrender/weatherdata.hpp"

#include "weatherparams.hpp"

namespace ESM
{
    struct Region;
    struct RegionWeatherState;
    class ESMWriter;
    class ESMReader;
}

namespace MWRender
{
    class RenderManager;
}

namespace Loading
{
    class Listener;
}

namespace Fallback
{
    class Map;
}

namespace MWWorld
{
    class TimeStamp;
    class CellStore;

    enum NightDayMode
    {
        Default = 0,
        ExteriorNight = 1,
        InteriorDay = 2
    };

    struct WeatherSetting
    {
        float mPreSunriseTime;
        float mPostSunriseTime;
        float mPreSunsetTime;
        float mPostSunsetTime;
    };

    struct TimeOfDaySettings
    {
        float mNightStart;
        float mNightEnd;
        float mDayStart;
        float mDayEnd;

        std::map<std::string, WeatherSetting> mSunriseTransitions;

        float mStarsPostSunsetStart;
        float mStarsPreSunriseFinish;
        float mStarsFadingDuration;

        WeatherSetting getSetting(const std::string& type) const
        {
            std::map<std::string, WeatherSetting>::const_iterator it = mSunriseTransitions.find(type);
            if (it != mSunriseTransitions.end())
            {
                return it->second;
            }
            else
            {
                return { 1.f, 1.f, 1.f, 1.f };
            }
        }

        void addSetting(const std::string& type)
        {
            WeatherSetting setting = { Fallback::Map::getFloat("Weather_" + type + "_Pre-Sunrise_Time"),
                Fallback::Map::getFloat("Weather_" + type + "_Post-Sunrise_Time"),
                Fallback::Map::getFloat("Weather_" + type + "_Pre-Sunset_Time"),
                Fallback::Map::getFloat("Weather_" + type + "_Post-Sunset_Time") };

            mSunriseTransitions[type] = setting;
        }
    };

    /// Interpolates between 4 data points (sunrise, day, sunset, night) based on the time of day.
    /// The template value could be a floating point number, or a color.
    template <typename T>
    class TimeOfDayInterpolator
    {
    public:
        TimeOfDayInterpolator(const T& sunrise, const T& day, const T& sunset, const T& night)
            : mSunriseValue(sunrise)
            , mDayValue(day)
            , mSunsetValue(sunset)
            , mNightValue(night)
        {
        }

        T getValue(const float gameHour, const TimeOfDaySettings& timeSettings, const std::string& prefix) const;

        const T& getSunriseValue() const { return mSunriseValue; }
        const T& getDayValue() const { return mDayValue; }
        const T& getSunsetValue() const { return mSunsetValue; }
        const T& getNightValue() const { return mNightValue; }

        void setSunriseValue(const T& sunriseValue) { mSunriseValue = sunriseValue; }
        void setDayValue(const T& dayValue) { mDayValue = dayValue; }
        void setSunsetValue(const T& sunsetValue) { mSunsetValue = sunsetValue; }
        void setNightValue(const T& nightValue) { mNightValue = nightValue; }

    private:
        T mSunriseValue, mDayValue, mSunsetValue, mNightValue;
    };

    /// Defines a single weather setting (according to INI)
    class Weather
    {
    public:
        static vsg::vec3 defaultDirection();

        Weather(const ESM::RefId id, const int scriptId, const std::string& name, float stormWindSpeed, float rainSpeed,
            float dlFactor, float dlOffset, const std::string& particleEffect);

        // OpenEden (task 0127): the same weather from a plain description. The constructor above is this one with
        // `weatherParamsFromFallback(name)`.
        Weather(const ESM::RefId id, const int scriptId, const std::string& name, float stormWindSpeed, float rainSpeed,
            float dlFactor, float dlOffset, const std::string& particleEffect, const WeatherParams& params);

        ESM::RefId mId;
        int mScriptId;
        std::string mName;
        std::string mCloudTexture;
        size_t mCloudTextureIndex = 0;
        // Task 0127 stage W-S3 (a game pack's weather only): more cloud pictures and the image space.
        std::vector<ExtraCloudParams> mExtraClouds;
        std::optional<ImageSpaceParams> mImageSpace;

        // Sky (atmosphere) color
        TimeOfDayInterpolator<vsg::vec4> mSkyColor;
        // Fog color
        TimeOfDayInterpolator<vsg::vec4> mFogColor;
        // Ambient lighting color
        TimeOfDayInterpolator<vsg::vec4> mAmbientColor;
        // Sun (directional) lighting color
        TimeOfDayInterpolator<vsg::vec4> mSunColor;

        // Fog depth/density
        TimeOfDayInterpolator<float> mLandFogDepth;

        // Color modulation for the sun itself during sunset
        vsg::vec4 mSunDiscSunsetColor;

        // Used by scripts to animate signs, etc based on the wind (GetWindSpeed)
        float mWindSpeed;

        // Cloud animation speed multiplier
        float mCloudSpeed;

        // Value between 0 and 1, defines the strength of the sun glare effect.
        // Also appears to modify how visible the sun, moons, and stars are for various weather effects.
        float mGlareView;

        // Fog factor and offset used with distant land rendering.
        struct
        {
            float FogFactor;
            float FogOffset;
        } mDL;

        // Sound effects
        // This is used for Blight, Ashstorm and Blizzard (Bloodmoon)
        ESM::RefId mAmbientLoopSoundID;
        // This is used for Rain and Thunderstorm
        ESM::RefId mRainLoopSoundID;

        std::array<ESM::RefId, 4> mThunderSoundID;

        // Task 0127 stage W-S4 (a game pack's weather only): its sounds as files, and the thunder that has been struck
        // and is not heard yet (it comes after the flash, NeutralWeatherSound::thunderDelaySeconds).
        NeutralWeatherSound::Set mSounds;
        NeutralWeatherSound::DelayedThunder mDelayedThunder;
        std::uint32_t mStrikeCounter = 0;
        /// Drops thunder waiting to be heard (the weather's sounds were stopped).
        void clearPendingThunder() { mDelayedThunder.clear(); }

        // Is this an ash storm / blight storm? If so, the following will happen:
        // - The particles and clouds will be oriented so they appear to come from the Red Mountain.
        // - Characters will animate their hand to protect eyes from the storm when looking in its direction (idlestorm
        // animation)
        // - Slower movement when walking against the storm (fStromWalkMult)
        bool mIsStorm;

        // How fast does rain travel down?
        // In Morrowind.ini this is set globally, but we may want to change it per weather later.
        float mRainSpeed;

        // How often does a new rain mesh spawn?
        float mRainEntranceSpeed;

        // Maximum count of rain particles
        int mRainMaxRaindrops;

        // Radius of rain effect
        float mRainDiameter;

        // Transition threshold to spawn rain
        float mRainThreshold;

        // Height of rain particles spawn
        float mRainMinHeight;
        float mRainMaxHeight;

        std::string mParticleEffect;

        std::string mRainEffect;

        vsg::vec3 mStormDirection;

        float mCloudsMaximumPercent;

        // Note: For Weather Blight, there is a "Disease Chance" (=0.1) setting. But according to MWSFD this feature
        // is broken in the vanilla game and was disabled.

        float transitionDelta() const;
        float cloudBlendFactor(const float transitionRatio) const;

        float calculateThunder(const float transitionRatio, const float elapsedSeconds, const bool isPaused);

        // OpenEden (task 0040): whether a strike has happened since the last call, and how strong the strongest was
        // (1 for the closest thunder sound, 0.25 for the farthest). Clears the strike. The flash the vanilla game draws
        // is unchanged; this only lets the renderer's lightning follow the same thunder schedule.
        bool takeStrike(float& strength);

    private:
        float mTransitionDelta;

        // Note: In MW, only thunderstorms support these attributes, but in the interest of making weather more
        // flexible, these settings are imported for all weather types. Only thunderstorms will normally have any
        // non-zero values.
        float mThunderFrequency;
        float mThunderThreshold;
        float mFlashDecrement;

        float mFlashBrightness;
        float mPendingStrike = 0.f; // task 0040: the strongest strike not yet taken, 0 for none

        void flashDecrement(const float elapsedSeconds);
        float thunderChance(const float transitionRatio, const float elapsedSeconds) const;
        void lightningAndThunder(void);
    };

    /// A class for storing a region's weather.
    class RegionWeather
    {
    public:
        explicit RegionWeather(const ESM::Region& region);
        explicit RegionWeather(const ESM::RegionWeatherState& state);
        // OpenEden (task 0127): a region made of a neutral world's weather list (chances by table position).
        explicit RegionWeather(std::vector<uint8_t> chances);

        operator ESM::RegionWeatherState() const;

        void setChances(const std::vector<uint8_t>& chances);

        void setWeather(int weatherID);

        int getWeather();

    private:
        int mWeather;
        std::vector<uint8_t> mChances;

        void chooseNewWeather();
    };

    /// A class that acts as a model for the moons.
    class MoonModel
    {
    public:
        MoonModel(const std::string& name);
        MoonModel(float fadeInStart, float fadeInFinish, float fadeOutStart, float fadeOutFinish, float axisOffset,
            float speed, float dailyIncrement, float fadeStartAngle, float fadeEndAngle,
            float moonShadowEarlyFadeAngle);

        MWRender::MoonState calculateState(const TimeStamp& gameTime) const;

    private:
        float mFadeInStart;
        float mFadeInFinish;
        float mFadeOutStart;
        float mFadeOutFinish;
        float mAxisOffset;
        float mSpeed;
        float mDailyIncrement;
        float mFadeStartAngle;
        float mFadeEndAngle;
        float mMoonShadowEarlyFadeAngle;

        float angle(int gameDay, float gameHour) const;
        float moonPhaseHour(int gameDay) const;
        float moonRiseHour(int gameDay) const;
        float rotation(float hours) const;
        MWRender::MoonState::Phase phase(const TimeStamp& gameTime) const;
        bool isVisible(int gameDay, float gameHour) const;
        float shadowBlend(float angle) const;
        float hourlyAlpha(float gameHour) const;
        float earlyMoonShadowAlpha(float angle) const;
    };

    /// Interface for weather settings
    class WeatherManager
    {
    public:
        // Have to pass fallback and Store, can't use singleton since World isn't fully constructed yet at the time
        // OpenEden (task 0127): with `neutral` the weather table, the clock and the region lists come from a game
        // pack (neutralweather.hpp) instead of Morrowind's INI keys and the store's regions.
        WeatherManager(
            MWRender::RenderManager& rendering, MWWorld::ESMStore& store, const NeutralWeatherSetup* neutral = nullptr);
        ~WeatherManager();

        /// The weather region of a cell: its `ESM::Region`, or for a neutral world the provider's key (the region is
        /// then created from the provider's list the first time it is asked for).
        ESM::RefId regionOf(const CellStore& cell);

        /**
         * Change the weather in the specified region by id of the weather
         * @param region that should be changed
         * @param ID of the weather setting to shift to
         */
        void changeWeather(const ESM::RefId& regionID, const unsigned int weatherID);
        void changeWeather(const ESM::RefId& regionID, const ESM::RefId& weatherID);
        void modRegion(const ESM::RefId& regionID, const std::vector<uint8_t>& chances);
        void playerTeleported(const ESM::RefId& playerRegion, bool isExterior);

        /**
         * Per-frame update
         * @param duration
         * @param paused
         */
        void update(float duration, bool paused, const TimeStamp& time, bool isExterior);

        void stopSounds();

        float getWindSpeed() const;
        NightDayMode getNightDayMode() const;

        /// Are we in an ash or blight storm?
        bool isInStorm() const;

        vsg::vec3 getStormDirection() const;

        void advanceTime(double hours, bool incremental);

        const std::vector<Weather>& getAllWeather() { return mWeatherSettings; }

        const Weather& getWeather() { return mWeatherSettings[mCurrentWeather]; }

        const Weather* getWeather(size_t index) const;

        const Weather* getWeather(const ESM::RefId& id) const;

        int getWeatherID() const { return mCurrentWeather; }

        const Weather* getNextWeather()
        {
            if (mNextWeather > -1)
                return &mWeatherSettings[mNextWeather];
            return nullptr;
        }

        int getNextWeatherID() const { return mNextWeather; }

        float getTransitionFactor() const { return mTransitionFactor; }

        bool useTorches(float hour) const;

        float getSunPercentage(float hour) const;

        float getSunVisibility() const;

        void write(ESM::ESMWriter& writer, Loading::Listener& progress);

        bool readRecord(ESM::ESMReader& reader, uint32_t type);

        void clear();

    private:
        WeatherManager(MWRender::RenderManager& rendering, MWWorld::ESMStore& store, const NeutralWeatherSetup* neutral,
            WeatherManagerParams params);

        MWWorld::ESMStore& mStore;
        MWRender::RenderManager& mRendering;
        std::shared_ptr<RegionWeatherProvider> mRegionProvider; // null for Morrowind
        /// Developer aid (OPENEDEN_NEUTRAL_FORCE_WEATHER, a pack's weather only): the table index every region starts
        /// in.
        int mForcedWeather = -1;
        float mAtmosphereNightRoll = 0;
        float mSunriseTime;
        float mSunsetTime;
        float mSunriseDuration;
        float mSunsetDuration;
        float mSunPreSunsetTime;

        TimeOfDaySettings mTimeSettings;

        // fading of night skydome
        TimeOfDayInterpolator<float> mNightFade;

        float mHoursBetweenWeatherChanges;
        // Task 0127 stage W-S3 (a game pack's climate): the sun pictures and the moons the sky has.
        std::string mSunTexture;
        std::string mSunGlareTexture;
        bool mMasserAvailable = true;
        bool mSecundaAvailable = true;
        float mRainSpeed;

        // underwater fog not really related to weather, but we handle it here because it's convenient
        TimeOfDayInterpolator<float> mUnderwaterFog;

        std::vector<Weather> mWeatherSettings;
        MoonModel mMasser;
        MoonModel mSecunda;

        float mWindSpeed;
        float mCurrentWindSpeed;
        float mNextWindSpeed;
        bool mIsStorm;
        bool mPrecipitation;
        vsg::vec3 mStormDirection;

        ESM::RefId mCurrentRegion;
        float mTimePassed;
        bool mFastForward;
        float mWeatherUpdateTime;
        float mTransitionFactor;
        NightDayMode mNightDayMode;
        int mCurrentWeather;
        int mNextWeather;
        int mQueuedWeather;
        std::map<ESM::RefId, RegionWeather> mRegions;
        MWRender::WeatherResult mResult;
        // task 0034: mResult after the atmosphere model, kept between frames to reuse its strings
        MWRender::WeatherResult mAtmosphereResult;
        // task 0037: how wet the ground is, 0 to 1 (not saved: after a load it builds up again in seconds)
        float mWetness = 0.f;
        // task 0039: how much snow and ash lies on the ground, 0 to 1 each (not saved)
        Render::Cover::State mCover;
        // task 0040: lightning as light (not saved): the flashes shown, the game seconds counted for them, and the
        // strikes counted for the flash directions
        Render::Lightning::Flasher mFlasher;
        float mLightningTime = 0.f;

        MWBase::Sound* mAmbientSound{ nullptr };
        ESM::RefId mPlayingAmbientSoundID;
        MWBase::Sound* mRainSound{ nullptr };
        ESM::RefId mPlayingRainSoundID;

        // Task 0127 stage W-S4: a game pack's weather sounds are files. One loop each for the default, the
        // precipitation and the wind (a wind that is not a loop is a gust now and then); `file` is what has been asked
        // of the sound manager (also when it has no device: the calls are logged), `sound` the handle it gave.
        struct NeutralLoop
        {
            std::string file;
            MWBase::Sound* sound = nullptr;
        };
        NeutralLoop mNeutralLoops[3];
        int mMoonTraceHour = -2; // the game hour of the last "moon" log line of a game pack's weather
        NeutralWeatherSound::GustTimer mNeutralGusts;
        void updateNeutralSounds(float duration, bool paused);
        void stopNeutralSounds();
        /// The weather whose sounds play now: the next one past the rain threshold of a transition, else the current.
        [[nodiscard]] int soundWeather() const;

        void addWeather(
            const std::string& name, float dlFactor, float dlOffset, const std::string& particleEffect = "");
        void addWeather(const WeatherEntry& entry);

        void importRegions();

        void regionalWeatherChanged(const ESM::RefId& regionID, RegionWeather& region);
        bool updateWeatherTime();
        bool updateWeatherRegion(const ESM::RefId& playerRegion);
        void updateWeatherTransitions(const float elapsedRealSeconds);
        void forceWeather(const int weatherID);

        bool inTransition() const;
        void addWeatherTransition(const int weatherID);

        void calculateWeatherResult(const float gameHour, const float elapsedSeconds, const bool isPaused);
        void applyAtmosphere(MWRender::WeatherResult& result, float sunHeight) const;
        void updateWetness(float duration, bool paused, bool isExterior);
        void updateCover(float duration, bool paused, bool isExterior);
        // task 0040: the flash colour to add to the sun, ambient and fog this frame (zero when `lightning` is off or
        // the vanilla flash is used); tells the renderer the flash's brightness and direction
        vsg::vec3 updateLightning(const float elapsedSeconds, const bool isPaused);
        void calculateResult(const int weatherID, const float gameHour);
        void calculateTransitionResult(const float factor, const float gameHour);
        float calculateWindSpeed(int weatherId, float currentSpeed);
    };
}

#endif // GAME_MWWORLD_WEATHER_H
