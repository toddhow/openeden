#include "weather.hpp"

#include <components/esm/stringrefid.hpp>
#include <components/render/atmospheremath.hpp>
#include <components/render/covermath.hpp>
#include <components/render/lightningmath.hpp>
#include <components/render/sunmath.hpp>
#include <components/render/wetnessmath.hpp>
#include <components/settings/values.hpp>

#include <components/misc/rng.hpp>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/esm3/weatherstate.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/datetimemanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwsound/sound.hpp"

#include "../mwrender/rendermanager.hpp"
#include "../mwrender/weather.hpp"

#include "cellstore.hpp"
#include "esmstore.hpp"
#include "neutralweather.hpp"
#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace MWWorld
{
    namespace
    {
        static const int invalidWeatherID = -1;

        // linear interpolate between x and y based on factor.
        float lerp(float x, float y, float factor)
        {
            return x * (1 - factor) + y * factor;
        }
        // linear interpolate between x and y based on factor.
        vsg::vec4 lerp(const vsg::vec4& x, const vsg::vec4& y, float factor)
        {
            return x * (1 - factor) + y * factor;
        }

        vsg::vec3 calculateStormDirection(const std::string& particleEffect)
        {
            vsg::vec3 stormDirection = MWWorld::Weather::defaultDirection();
            if (particleEffect == Settings::models().mWeatherashcloud.get()
                || particleEffect == Settings::models().mWeatherblightcloud.get())
            {
                vsg::vec3 playerPos = MWMechanics::getPlayer().getRefData().getPosition().asVec3();
                playerPos.z = 0;
                vsg::vec3 redMountainPos = vsg::vec3(25000.f, 70000.f, 0.f);
                stormDirection = (playerPos - redMountainPos);
                Misc::VsgMath::normalizeInPlace(stormDirection);
            }
            return stormDirection;
        }
    }

    template <typename T>
    T TimeOfDayInterpolator<T>::getValue(
        const float gameHour, const TimeOfDaySettings& timeSettings, const std::string& prefix) const
    {
        WeatherSetting setting = timeSettings.getSetting(prefix);
        float preSunriseTime = setting.mPreSunriseTime;
        float postSunriseTime = setting.mPostSunriseTime;
        float preSunsetTime = setting.mPreSunsetTime;
        float postSunsetTime = setting.mPostSunsetTime;

        // night
        if (gameHour < timeSettings.mNightEnd - preSunriseTime || gameHour > timeSettings.mNightStart + postSunsetTime)
            return mNightValue;
        // sunrise
        else if (gameHour >= timeSettings.mNightEnd - preSunriseTime
            && gameHour <= timeSettings.mDayStart + postSunriseTime)
        {
            float duration = timeSettings.mDayStart + postSunriseTime - timeSettings.mNightEnd + preSunriseTime;
            float middle = timeSettings.mNightEnd - preSunriseTime + duration / 2.f;

            if (gameHour <= middle)
            {
                // fade in
                float advance = middle - gameHour;
                float factor = 0.f;
                if (duration > 0)
                    factor = advance / duration * 2;
                return lerp(mSunriseValue, mNightValue, factor);
            }
            else
            {
                // fade out
                float advance = gameHour - middle;
                float factor = 1.f;
                if (duration > 0)
                    factor = advance / duration * 2;
                return lerp(mSunriseValue, mDayValue, factor);
            }
        }
        // day
        else if (gameHour > timeSettings.mDayStart + postSunriseTime && gameHour < timeSettings.mDayEnd - preSunsetTime)
            return mDayValue;
        // sunset
        else if (gameHour >= timeSettings.mDayEnd - preSunsetTime
            && gameHour <= timeSettings.mNightStart + postSunsetTime)
        {
            float duration = timeSettings.mNightStart + postSunsetTime - timeSettings.mDayEnd + preSunsetTime;
            float middle = timeSettings.mDayEnd - preSunsetTime + duration / 2.f;

            if (gameHour <= middle)
            {
                // fade in
                float advance = middle - gameHour;
                float factor = 0.f;
                if (duration > 0)
                    factor = advance / duration * 2;
                return lerp(mSunsetValue, mDayValue, factor);
            }
            else
            {
                // fade out
                float advance = gameHour - middle;
                float factor = 1.f;
                if (duration > 0)
                    factor = advance / duration * 2;
                return lerp(mSunsetValue, mNightValue, factor);
            }
        }
        // shut up compiler
        return T();
    }

    template class MWWorld::TimeOfDayInterpolator<float>;
    template class MWWorld::TimeOfDayInterpolator<vsg::vec4>;

    vsg::vec3 Weather::defaultDirection()
    {
        static const vsg::vec3 direction = vsg::vec3(0.f, 1.f, 0.f);
        return direction;
    }

    WeatherParams weatherParamsFromFallback(const std::string& name)
    {
        // Exactly the keys, in the way Weather's constructor read them before task 0127.
        const std::string p = "Weather_" + name;
        WeatherParams out;
        out.cloudTexture = std::string(Fallback::Map::getString(p + "_Cloud_Texture"));
        const auto four = [&](const char* group)
        {
            return std::array<vsg::vec4, 4>{ Fallback::Map::getColour(p + "_" + group + "_Sunrise_Color"),
                Fallback::Map::getColour(p + "_" + group + "_Day_Color"),
                Fallback::Map::getColour(p + "_" + group + "_Sunset_Color"),
                Fallback::Map::getColour(p + "_" + group + "_Night_Color") };
        };
        out.sky = four("Sky");
        out.fog = four("Fog");
        out.ambient = four("Ambient");
        out.sun = four("Sun");
        out.landFogDepth = { Fallback::Map::getFloat(p + "_Land_Fog_Day_Depth"),
            Fallback::Map::getFloat(p + "_Land_Fog_Day_Depth"), Fallback::Map::getFloat(p + "_Land_Fog_Day_Depth"),
            Fallback::Map::getFloat(p + "_Land_Fog_Night_Depth") };
        out.sunDiscSunsetColor = Fallback::Map::getColour(p + "_Sun_Disc_Sunset_Color");
        out.windSpeed = Fallback::Map::getFloat(p + "_Wind_Speed");
        out.cloudSpeed = Fallback::Map::getFloat(p + "_Cloud_Speed");
        out.glareView = Fallback::Map::getFloat(p + "_Glare_View");
        out.rainEntranceSpeed = Fallback::Map::getFloat(p + "_Rain_Entrance_Speed");
        out.rainMaxRaindrops = Fallback::Map::getInt(p + "_Max_Raindrops");
        out.rainDiameter = Fallback::Map::getFloat(p + "_Rain_Diameter");
        out.rainThreshold = Fallback::Map::getFloat(p + "_Rain_Threshold");
        out.rainMinHeight = Fallback::Map::getFloat(p + "_Rain_Height_Min");
        out.rainMaxHeight = Fallback::Map::getFloat(p + "_Rain_Height_Max");
        out.usingPrecip = Fallback::Map::getBool(p + "_Using_Precip");
        out.cloudsMaximumPercent = Fallback::Map::getFloat(p + "_Clouds_Maximum_Percent");
        out.transitionDelta = Fallback::Map::getFloat(p + "_Transition_Delta");
        out.thunderFrequency = Fallback::Map::getFloat(p + "_Thunder_Frequency");
        out.thunderThreshold = Fallback::Map::getFloat(p + "_Thunder_Threshold");
        out.flashDecrement = Fallback::Map::getFloat(p + "_Flash_Decrement");
        for (int i = 0; i < 4; ++i)
            out.thunderSoundId[i] = std::string(Fallback::Map::getString(p + "_Thunder_Sound_ID_" + std::to_string(i)));
        // Only the weathers with precipitation have this key (Fallback::Map throws for another one): as before.
        if (out.usingPrecip)
            out.rainLoopSoundId = std::string(Fallback::Map::getString(p + "_Rain_Loop_Sound_ID"));
        out.ambientLoopSoundId = std::string(Fallback::Map::getString(p + "_Ambient_Loop_Sound_ID"));
        return out;
    }

    WeatherManagerParams weatherManagerParamsFromFallback()
    {
        WeatherManagerParams out;
        out.sunriseTime = Fallback::Map::getFloat("Weather_Sunrise_Time");
        out.sunsetTime = Fallback::Map::getFloat("Weather_Sunset_Time");
        out.sunriseDuration = Fallback::Map::getFloat("Weather_Sunrise_Duration");
        out.sunsetDuration = Fallback::Map::getFloat("Weather_Sunset_Duration");
        out.sunPreSunsetTime = Fallback::Map::getFloat("Weather_Sun_Pre-Sunset_Time");
        out.hoursBetweenWeatherChanges = Fallback::Map::getFloat("Weather_Hours_Between_Weather_Changes");
        out.rainSpeed = Fallback::Map::getFloat("Weather_Precip_Gravity");
        out.underwaterFog = { Fallback::Map::getFloat("Water_UnderwaterSunriseFog"),
            Fallback::Map::getFloat("Water_UnderwaterDayFog"), Fallback::Map::getFloat("Water_UnderwaterSunsetFog"),
            Fallback::Map::getFloat("Water_UnderwaterNightFog") };
        for (const char* type : { "Sky", "Ambient", "Fog", "Sun" })
        {
            const std::string t = std::string("Weather_") + type;
            out.transitions[type] = { Fallback::Map::getFloat(t + "_Pre-Sunrise_Time"),
                Fallback::Map::getFloat(t + "_Post-Sunrise_Time"), Fallback::Map::getFloat(t + "_Pre-Sunset_Time"),
                Fallback::Map::getFloat(t + "_Post-Sunset_Time") };
        }
        out.starsPostSunsetStart = Fallback::Map::getFloat("Weather_Stars_Post-Sunset_Start");
        out.starsPreSunriseFinish = Fallback::Map::getFloat("Weather_Stars_Pre-Sunrise_Finish");
        out.starsFadingDuration = Fallback::Map::getFloat("Weather_Stars_Fading_Duration");
        return out;
    }

    Weather::Weather(ESM::RefId id, int scriptId, const std::string& name, float stormWindSpeed, float rainSpeed,
        float dlFactor, float dlOffset, const std::string& particleEffect)
        : Weather(id, scriptId, name, stormWindSpeed, rainSpeed, dlFactor, dlOffset, particleEffect,
              weatherParamsFromFallback(name))
    {
    }

    Weather::Weather(ESM::RefId id, int scriptId, const std::string& name, float stormWindSpeed, float rainSpeed,
        float dlFactor, float dlOffset, const std::string& particleEffect, const WeatherParams& params)
        : mId(id)
        , mScriptId(scriptId)
        , mName(name)
        , mCloudTexture(params.cloudTexture)
        , mSkyColor(params.sky[0], params.sky[1], params.sky[2], params.sky[3])
        , mFogColor(params.fog[0], params.fog[1], params.fog[2], params.fog[3])
        , mAmbientColor(params.ambient[0], params.ambient[1], params.ambient[2], params.ambient[3])
        , mSunColor(params.sun[0], params.sun[1], params.sun[2], params.sun[3])
        , mLandFogDepth(params.landFogDepth[0], params.landFogDepth[1], params.landFogDepth[2], params.landFogDepth[3])
        , mSunDiscSunsetColor(params.sunDiscSunsetColor)
        , mWindSpeed(params.windSpeed)
        , mCloudSpeed(params.cloudSpeed)
        , mGlareView(params.glareView)
        , mIsStorm(mWindSpeed > stormWindSpeed)
        , mRainSpeed(rainSpeed)
        , mRainEntranceSpeed(params.rainEntranceSpeed)
        , mRainMaxRaindrops(params.rainMaxRaindrops)
        , mRainDiameter(params.rainDiameter)
        , mRainThreshold(params.rainThreshold)
        , mRainMinHeight(params.rainMinHeight)
        , mRainMaxHeight(params.rainMaxHeight)
        , mParticleEffect(particleEffect)
        , mRainEffect(params.usingPrecip ? "raindrop.nif" : "")
        , mStormDirection(Weather::defaultDirection())
        , mCloudsMaximumPercent(params.cloudsMaximumPercent)
        , mTransitionDelta(params.transitionDelta)
        , mThunderFrequency(params.thunderFrequency)
        , mThunderThreshold(params.thunderThreshold)
        , mFlashDecrement(params.flashDecrement)
        , mFlashBrightness(0.0f)
    {
        mDL.FogFactor = dlFactor;
        mDL.FogOffset = dlOffset;
        for (size_t i = 0; i < mThunderSoundID.size(); ++i)
            mThunderSoundID[i] = ESM::RefId::stringRefId(params.thunderSoundId[i]);

        if (!mRainEffect.empty()) // NOTE: in vanilla, the weathers with rain seem to be hardcoded; changing
                                  // Using_Precip has no effect
        {
            mRainLoopSoundID = ESM::RefId::stringRefId(params.rainLoopSoundId);
            if (mRainLoopSoundID.empty()) // default to "rain" if not set
                mRainLoopSoundID = ESM::RefId::stringRefId("rain");
            else if (mRainLoopSoundID == "None")
                mRainLoopSoundID = ESM::RefId();
        }

        mAmbientLoopSoundID = ESM::RefId::stringRefId(params.ambientLoopSoundId);
        if (mAmbientLoopSoundID == "None")
            mAmbientLoopSoundID = ESM::RefId();
    }

    float Weather::transitionDelta() const
    {
        // Transition Delta describes how quickly transitioning to the weather in question will take, in Hz. Note that
        // the measurement is in real time, not in-game time.
        return mTransitionDelta;
    }

    float Weather::cloudBlendFactor(const float transitionRatio) const
    {
        // Clouds Maximum Percent affects how quickly the sky transitions from one sky texture to the next.
        return transitionRatio / mCloudsMaximumPercent;
    }

    float Weather::calculateThunder(const float transitionRatio, const float elapsedSeconds, const bool isPaused)
    {
        // When paused, the flash brightness remains the same and no new strikes can occur.
        if (!isPaused)
        {
            // Morrowind doesn't appear to do any calculations unless the transition ratio is higher than the Thunder
            // Threshold.
            if (transitionRatio >= mThunderThreshold && mThunderFrequency > 0.0f)
            {
                flashDecrement(elapsedSeconds);
                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                if (Misc::Rng::rollProbability(prng) <= thunderChance(transitionRatio, elapsedSeconds))
                {
                    lightningAndThunder();
                }
            }
            else
            {
                mFlashBrightness = 0.0f;
            }
        }

        return mFlashBrightness;
    }

    bool Weather::takeStrike(float& strength)
    {
        if (mPendingStrike <= 0.f)
            return false;
        strength = mPendingStrike;
        mPendingStrike = 0.f;
        return true;
    }

    inline void Weather::flashDecrement(const float elapsedSeconds)
    {
        // The Flash Decrement is measured in whole units per second. This means that if the flash brightness was
        // currently 1.0, then it should take approximately 0.25 seconds to decay to 0.0 (the minimum).
        float decrement = mFlashDecrement * elapsedSeconds;
        mFlashBrightness = decrement > mFlashBrightness ? 0.0f : mFlashBrightness - decrement;
    }

    inline float Weather::thunderChance(const float transitionRatio, const float elapsedSeconds) const
    {
        // This formula is reversed from the observation that with Thunder Frequency set to 1, there are roughly 10
        // strikes per minute. It doesn't appear to be tied to in game time as Timescale doesn't affect it. Various
        // values of Thunder Frequency seem to change the average number of strikes in a linear fashion.. During a
        // transition, it appears to scaled based on how far past it is past the Thunder Threshold.
        float scaleFactor = (transitionRatio - mThunderThreshold) / (1.0f - mThunderThreshold);
        return ((mThunderFrequency * 10.0f) / 60.0f) * elapsedSeconds * scaleFactor;
    }

    inline void Weather::lightningAndThunder(void)
    {
        // Morrowind seems to vary the intensity of the brightness based on which of the four sound IDs it selects.
        // They appear to go from 0 (brightest, closest) to 3 (faintest, farthest). The value of 0.25 per distance
        // was derived by setting the Flash Decrement to 0.1 and measuring how long each value took to decay to 0.
        // TODO: Determine the distribution of each distance to see if it's evenly weighted.
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        unsigned int distance = Misc::Rng::rollDice(4, prng);
        // Flash brightness appears additive, since if multiple strikes occur, it takes longer for it to decay to 0.
        mFlashBrightness += 1 - (distance * 0.25f);
        mPendingStrike = std::max(mPendingStrike, Render::Lightning::strikeStrength(distance));
        MWBase::Environment::get().getSoundManager()->playSound(mThunderSoundID[distance], 1.0, 1.0);
    }

    RegionWeather::RegionWeather(const ESM::Region& region)
        : mWeather(invalidWeatherID)
        , mChances(region.mData.mProbabilities.begin(), region.mData.mProbabilities.end())
    {
    }

    RegionWeather::RegionWeather(const ESM::RegionWeatherState& state)
        : mWeather(state.mWeather)
        , mChances(state.mChances)
    {
    }

    RegionWeather::RegionWeather(std::vector<uint8_t> chances)
        : mWeather(invalidWeatherID)
        , mChances(std::move(chances))
    {
    }

    RegionWeather::operator ESM::RegionWeatherState() const
    {
        return { mWeather, mChances };
    }

    void RegionWeather::setChances(const std::vector<uint8_t>& chances)
    {
        mChances = chances;

        // Regional weather no longer supports the current type, select a new weather pattern.
        if ((static_cast<size_t>(mWeather) >= mChances.size()) || (mChances[mWeather] == 0))
        {
            chooseNewWeather();
        }
    }

    void RegionWeather::setWeather(int weatherID)
    {
        mWeather = weatherID;
    }

    int RegionWeather::getWeather()
    {
        // If the region weather was already set (by ChangeWeather, or by a previous call) then just return that value.
        // Note that the region weather will be expired periodically when the weather update timer expires.
        if (mWeather == invalidWeatherID)
        {
            chooseNewWeather();
        }

        return mWeather;
    }

    void RegionWeather::chooseNewWeather()
    {
        // All probabilities must add to 100 (responsibility of the user).
        // If chances A and B has values 30 and 70 then by generating 100 numbers 1..100, 30% will be lesser or equal 30
        // and 70% will be greater than 30 (in theory).
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        unsigned int chance = static_cast<unsigned int>(Misc::Rng::rollDice(100, prng) + 1); // 1..100
        unsigned int sum = 0;
        for (size_t i = 0; i < mChances.size(); ++i)
        {
            sum += mChances[i];
            if (chance <= sum)
            {
                mWeather = static_cast<int>(i);
                return;
            }
        }

        // if we hit this path then the chances don't add to 100, choose a default weather instead
        mWeather = 0;
    }

    MoonModel::MoonModel(float fadeInStart, float fadeInFinish, float fadeOutStart, float fadeOutFinish,
        float axisOffset, float speed, float dailyIncrement, float fadeStartAngle, float fadeEndAngle,
        float moonShadowEarlyFadeAngle)
    {
        mFadeInStart = fadeInStart;
        mFadeInFinish = fadeInFinish;
        mFadeOutStart = fadeOutStart;
        mFadeOutFinish = fadeOutFinish;
        mAxisOffset = axisOffset;
        mDailyIncrement = dailyIncrement;
        mFadeStartAngle = fadeStartAngle;
        mFadeEndAngle = fadeEndAngle;
        mMoonShadowEarlyFadeAngle = moonShadowEarlyFadeAngle;

        // Morrowind appears to have a minimum speed to avoid situations where the moon can't
        // complete a full rotation in a single 24-hour period. The reverse-engineered formula is
        // 180 degrees (full hemisphere) / 23 hours / 15 degrees (1 hour travel at speed 1.0).
        mSpeed = std::max(speed, (180.0f / 23.0f / 15.0f));

        // Morrowind appears to reduce mDailyIncrement with modulo 24.0f to avoid situations where
        // the moon would increment more than an entire rotation in a single day.
        mDailyIncrement = std::fmod(mDailyIncrement, 24.0f);
    }

    MoonModel::MoonModel(const std::string& name)
        : mFadeInStart(Fallback::Map::getFloat("Moons_" + name + "_Fade_In_Start"))
        , mFadeInFinish(Fallback::Map::getFloat("Moons_" + name + "_Fade_In_Finish"))
        , mFadeOutStart(Fallback::Map::getFloat("Moons_" + name + "_Fade_Out_Start"))
        , mFadeOutFinish(Fallback::Map::getFloat("Moons_" + name + "_Fade_Out_Finish"))
        , mAxisOffset(Fallback::Map::getFloat("Moons_" + name + "_Axis_Offset"))
        , mSpeed(Fallback::Map::getFloat("Moons_" + name + "_Speed"))
        , mDailyIncrement(Fallback::Map::getFloat("Moons_" + name + "_Daily_Increment"))
        , mFadeStartAngle(Fallback::Map::getFloat("Moons_" + name + "_Fade_Start_Angle"))
        , mFadeEndAngle(Fallback::Map::getFloat("Moons_" + name + "_Fade_End_Angle"))
        , mMoonShadowEarlyFadeAngle(Fallback::Map::getFloat("Moons_" + name + "_Moon_Shadow_Early_Fade_Angle"))
    {
        // Morrowind appears to have a minimum speed to avoid situations where the moon can't
        // complete a full rotation in a single 24-hour period. The reverse-engineered formula is
        // 180 degrees (full hemisphere) / 23 hours / 15 degrees (1 hour travel at speed 1.0).
        mSpeed = std::max(mSpeed, (180.0f / 23.0f / 15.0f));

        // Morrowind appears to reduce mDailyIncrement with modulo 24.0f to avoid situations where
        // the moon would increment more than an entire rotation in a single day.
        mDailyIncrement = std::fmod(mDailyIncrement, 24.0f);
    }

    MWRender::MoonState MoonModel::calculateState(const TimeStamp& gameTime) const
    {
        float rotationFromHorizon = angle(gameTime.getDay(), gameTime.getHour());
        MWRender::MoonState state = { rotationFromHorizon,
            mAxisOffset, // Reverse engineered from Morrowind's scene graph rotation matrices.
            phase(gameTime), shadowBlend(rotationFromHorizon),
            earlyMoonShadowAlpha(rotationFromHorizon) * hourlyAlpha(gameTime.getHour()) };

        return state;
    }

    inline float MoonModel::angle(int gameDay, float gameHour) const
    {
        // Morrowind's moons start travel on one side of the horizon (let's call it H-rise) and travel 180 degrees to
        // the opposite horizon (let's call it H-set). Upon reaching H-set, they reset to H-rise until the next moon
        // rise.

        // When calculating the angle of the moon, several cases have to be taken into account:
        // 1. Moon rises and then sets in one day.
        // 2. Moon sets and doesn't rise in one day (occurs when the moon rise hour is >= 24).
        // 3. Moon sets and then rises in one day.
        float moonRiseHourToday = moonRiseHour(gameDay);
        float moonRiseAngleToday = 0.0f;

        if (gameHour < moonRiseHourToday)
        {
            // Rise hour increases by mDailyIncrement each day, so yesterday's is easy to calculate
            float moonRiseHourYesterday = moonRiseHourToday - mDailyIncrement;
            if (moonRiseHourYesterday < 24.0f)
            {
                // Morrowind offsets the increment by -1 when the previous day's visible point crosses into the next
                // day. The offset lasts from this point until the next 24-day loop starts. To find this point we add
                // mDailyIncrement to the previous visible point and check the result.
                float moonShadowEarlyFadeAngle1 = mFadeEndAngle - mMoonShadowEarlyFadeAngle;
                float timeToVisible = moonShadowEarlyFadeAngle1 / rotation(1.0f);
                float cycleOffset = moonRiseHourYesterday + timeToVisible > 24.0f ? mDailyIncrement : 0.0f;

                float moonRiseAngleYesterday = rotation(24.0f - (moonRiseHourYesterday + cycleOffset));
                if (moonRiseAngleYesterday < 180.0f)
                {
                    // The moon rose but did not set yesterday, so accumulate yesterday's angle with how much we've
                    // travelled today.
                    moonRiseAngleToday = rotation(gameHour) + moonRiseAngleYesterday;
                }
            }
        }
        else
        {
            moonRiseAngleToday = rotation(gameHour - moonRiseHourToday);
        }

        if (moonRiseAngleToday >= 180.0f)
        {
            // The moon set today, reset the angle to the horizon.
            moonRiseAngleToday = 0.0f;
        }

        return moonRiseAngleToday;
    }

    inline float MoonModel::moonPhaseHour(int gameDay) const
    {
        // Morrowind delays moon phase changes until one of these is true:
        //   * The moon is invisible at midnight.
        //   * The moon reached moonShadowEarlyFadeAngle2 one daily increment ago (therefore invisible).
        if (!isVisible(gameDay, 0.0f))
            return 0.0f;
        else
        {
            // Calculate the angle at which the moon becomes transparent and the starting angle.
            float moonShadowEarlyFadeAngle2 = (180.0f - mFadeEndAngle) + mMoonShadowEarlyFadeAngle;
            float midnightAngle = angle(gameDay, 0.0f);

            // We can assume that moonShadowEarlyFadeAngle2 > midnightAngle, because the opposite
            // case would make the moon invisible at midnight, which is checked above.
            return ((moonShadowEarlyFadeAngle2 - midnightAngle) / rotation(1.0f)) + std::max(mDailyIncrement, 0.0f);
        }
    }

    inline float MoonModel::moonRiseHour(int gameDay) const
    {
        if (mDailyIncrement == 0.0f)
            return 0.0f;

        // This arises from the start date of 16 Last Seed, 427
        // TODO: Find an alternate formula that doesn't rely on this day being fixed.
        constexpr int startDay = 16;

        // This formula finds the number of missed increments necessary to make the rise hour a 24-day loop.
        // The offset increases on the first day of the loop and is multiplied by the number of completed loops.
        float incrementOffset = (24.0f - std::abs(24.0f / mDailyIncrement)) * std::floor((gameDay + startDay) / 24.0f);

        // This odd formula arises from the fact that on 16 Last Seed, 17 increments have occurred, meaning
        // that upon starting a new game, it must only calculate the moon phase as far back as 1 Last Seed.
        // Note that we don't modulo after adding the latest daily increment because other calculations need to
        // know if doing so would cause the moon rise to be postponed until the next day (which happens when
        // the moon rise hour is >= 24 in Morrowind).
        return mDailyIncrement + std::fmod((gameDay - 1 + startDay - incrementOffset) * mDailyIncrement, 24.0f);
    }

    inline float MoonModel::rotation(float hours) const
    {
        // 15 degrees per hour was reverse engineered from the rotation matrices of the Morrowind scene graph.
        // Note that this correlates to 360 / 24, which is a full rotation every 24 hours, so speed is a measure
        // of whole rotations that could be completed in a day.
        return 15.0f * mSpeed * hours;
    }

    MWRender::MoonState::Phase MoonModel::phase(const TimeStamp& gameTime) const
    {
        // Morrowind starts with a full moon on 16 Last Seed and then begins to wane 17 Last Seed, working on 3 day
        // phase cycle.

        // If the moon didn't rise yet today, use yesterday's moon phase.
        if (gameTime.getHour() < moonPhaseHour(gameTime.getDay()))
            return static_cast<MWRender::MoonState::Phase>((gameTime.getDay() / 3) % 8);
        else
            return static_cast<MWRender::MoonState::Phase>(((gameTime.getDay() + 1) / 3) % 8);
    }

    inline bool MoonModel::isVisible(int gameDay, float gameHour) const
    {
        // Moons are "visible" when their alpha value is non-zero.
        return hourlyAlpha(gameHour) > 0.f && earlyMoonShadowAlpha(angle(gameDay, gameHour)) > 0.f;
    }

    inline float MoonModel::shadowBlend(float angle) const
    {
        // The Fade End Angle and Fade Start Angle describe a region where the moon transitions from a solid disk
        // that is roughly the color of the sky, to a textured surface.
        // Depending on the current angle, the following values describe the ratio between the textured moon
        // and the solid disk:
        // 1. From Fade End Angle 1 to Fade Start Angle 1 (during moon rise): 0..1
        // 2. From Fade Start Angle 1 to Fade Start Angle 2 (between moon rise and moon set): 1 (textured)
        // 3. From Fade Start Angle 2 to Fade End Angle 2 (during moon set): 1..0
        // 4. From Fade End Angle 2 to Fade End Angle 1 (between moon set and moon rise): 0 (solid disk)
        float fadeAngle = mFadeStartAngle - mFadeEndAngle;
        float fadeEndAngle2 = 180.0f - mFadeEndAngle;
        float fadeStartAngle2 = 180.0f - mFadeStartAngle;
        if ((angle >= mFadeEndAngle) && (angle < mFadeStartAngle))
            return (angle - mFadeEndAngle) / fadeAngle;
        else if ((angle >= mFadeStartAngle) && (angle < fadeStartAngle2))
            return 1.0f;
        else if ((angle >= fadeStartAngle2) && (angle < fadeEndAngle2))
            return (fadeEndAngle2 - angle) / fadeAngle;
        else
            return 0.0f;
    }

    inline float MoonModel::hourlyAlpha(float gameHour) const
    {
        // Morrowind culls the moon one minute before mFadeOutFinish
        constexpr float oneMinute = 0.0167f;
        float adjustedFadeOutFinish = mFadeOutFinish - oneMinute;

        // The Fade Out Start / Finish and Fade In Start / Finish describe the hours at which the moon
        // appears and disappears.
        // Depending on the current hour, the following values describe how transparent the moon is.
        // 1. From Fade Out Start to Fade Out Finish: 1..0
        // 2. From Fade Out Finish to Fade In Start: 0 (transparent)
        // 3. From Fade In Start to Fade In Finish: 0..1
        // 4. From Fade In Finish to Fade Out Start: 1 (solid)
        if ((gameHour >= mFadeOutStart) && (gameHour < adjustedFadeOutFinish))
            return (adjustedFadeOutFinish - gameHour) / (adjustedFadeOutFinish - mFadeOutStart);
        else if ((gameHour >= adjustedFadeOutFinish) && (gameHour < mFadeInStart))
            return 0.0f;
        else if ((gameHour >= mFadeInStart) && (gameHour < mFadeInFinish))
            return (gameHour - mFadeInStart) / (mFadeInFinish - mFadeInStart);
        else
            return 1.0f;
    }

    inline float MoonModel::earlyMoonShadowAlpha(float angle) const
    {
        // The Moon Shadow Early Fade Angle describes an arc relative to Fade End Angle.
        // Depending on the current angle, the following values describe how transparent the moon is.
        // 1. From Moon Shadow Early Fade Angle 1 to Fade End Angle 1 (during moon rise): 0..1
        // 2. From Fade End Angle 1 to Fade End Angle 2 (between moon rise and moon set): 1 (solid)
        // 3. From Fade End Angle 2 to Moon Shadow Early Fade Angle 2 (during moon set): 1..0
        // 4. From Moon Shadow Early Fade Angle 2 to Moon Shadow Early Fade Angle 1: 0 (transparent)
        float moonShadowEarlyFadeAngle1 = mFadeEndAngle - mMoonShadowEarlyFadeAngle;
        float fadeEndAngle2 = 180.0f - mFadeEndAngle;
        float moonShadowEarlyFadeAngle2 = fadeEndAngle2 + mMoonShadowEarlyFadeAngle;
        if ((angle >= moonShadowEarlyFadeAngle1) && (angle < mFadeEndAngle))
            return (angle - moonShadowEarlyFadeAngle1) / mMoonShadowEarlyFadeAngle;
        else if ((angle >= mFadeEndAngle) && (angle < fadeEndAngle2))
            return 1.0f;
        else if ((angle >= fadeEndAngle2) && (angle < moonShadowEarlyFadeAngle2))
            return (moonShadowEarlyFadeAngle2 - angle) / mMoonShadowEarlyFadeAngle;
        else
            return 0.0f;
    }

    WeatherManager::WeatherManager(
        MWRender::RenderManager& rendering, MWWorld::ESMStore& store, const NeutralWeatherSetup* neutral)
        : WeatherManager(
              rendering, store, neutral, neutral != nullptr ? neutral->manager : weatherManagerParamsFromFallback())
    {
    }

    WeatherManager::WeatherManager(MWRender::RenderManager& rendering, MWWorld::ESMStore& store,
        const NeutralWeatherSetup* neutral, WeatherManagerParams params)
        : mStore(store)
        , mRendering(rendering)
        , mRegionProvider(neutral != nullptr ? neutral->regions : nullptr)
        , mSunriseTime(params.sunriseTime)
        , mSunsetTime(params.sunsetTime)
        , mSunriseDuration(params.sunriseDuration)
        , mSunsetDuration(params.sunsetDuration)
        , mSunPreSunsetTime(params.sunPreSunsetTime)
        , mNightFade(0, 0, 0, 1)
        , mHoursBetweenWeatherChanges(params.hoursBetweenWeatherChanges)
        , mRainSpeed(params.rainSpeed)
        , mUnderwaterFog(params.underwaterFog[0], params.underwaterFog[1], params.underwaterFog[2],
              params.underwaterFog[3])
        , mWeatherSettings()
        , mMasser("Masser")
        , mSecunda("Secunda")
        , mWindSpeed(0.f)
        , mCurrentWindSpeed(0.f)
        , mNextWindSpeed(0.f)
        , mIsStorm(false)
        , mPrecipitation(false)
        , mStormDirection(Weather::defaultDirection())
        , mCurrentRegion()
        , mTimePassed(0)
        , mFastForward(false)
        , mWeatherUpdateTime(mHoursBetweenWeatherChanges)
        , mTransitionFactor(0)
        , mNightDayMode(Default)
        , mCurrentWeather(0)
        , mNextWeather(0)
        , mQueuedWeather(0)
        , mRegions()
        , mResult()
    {
        mTimeSettings.mNightStart = mSunsetTime + mSunsetDuration;
        mTimeSettings.mNightEnd = mSunriseTime;
        mTimeSettings.mDayStart = mSunriseTime + mSunriseDuration;
        mTimeSettings.mDayEnd = mSunsetTime;

        for (const auto& [type, window] : params.transitions)
            mTimeSettings.mSunriseTransitions[type] = { window[0], window[1], window[2], window[3] };

        // Morrowind handles stars settings differently for other ones
        mTimeSettings.mStarsPostSunsetStart = params.starsPostSunsetStart;
        mTimeSettings.mStarsPreSunriseFinish = params.starsPreSunriseFinish;
        mTimeSettings.mStarsFadingDuration = params.starsFadingDuration;

        WeatherSetting starSetting = { mTimeSettings.mStarsPreSunriseFinish,
            mTimeSettings.mStarsFadingDuration - mTimeSettings.mStarsPreSunriseFinish,
            mTimeSettings.mStarsPostSunsetStart,
            mTimeSettings.mStarsFadingDuration - mTimeSettings.mStarsPostSunsetStart };

        mTimeSettings.mSunriseTransitions["Stars"] = starSetting;

        if (neutral != nullptr)
        {
            // OpenEden (task 0127): a game pack's weathers; the regions are made when a cell asks for them.
            mWeatherSettings.reserve(neutral->weathers.size());
            for (const WeatherEntry& entry : neutral->weathers)
                addWeather(entry);
            if (const char* forced = std::getenv("OPENEDEN_NEUTRAL_FORCE_WEATHER"); forced != nullptr && *forced != 0)
            {
                mForcedWeather = findWeatherByName(neutral->weathers, forced);
                if (mForcedWeather < 0)
                    Log(Debug::Warning) << "OPENEDEN_NEUTRAL_FORCE_WEATHER: no weather named " << forced;
                else
                    Log(Debug::Info) << "Neutral weather: every region starts in " << forced << " (developer aid)";
            }
            forceWeather(mForcedWeather >= 0 ? mForcedWeather : 0);
            return;
        }

        mWeatherSettings.reserve(10);
        // These distant land fog factor and offset values are the defaults MGE XE provides. Should be
        // provided by settings somewhere?
        addWeather("Clear", 1.0f, 0.0f); // 0
        addWeather("Cloudy", 0.9f, 0.0f); // 1
        addWeather("Foggy", 0.2f, 30.0f); // 2
        addWeather("Overcast", 0.7f, 0.0f); // 3
        addWeather("Rain", 0.5f, 10.0f); // 4
        addWeather("Thunderstorm", 0.5f, 20.0f); // 5
        addWeather("Ashstorm", 0.2f, 50.0f, Settings::models().mWeatherashcloud.get()); // 6
        addWeather("Blight", 0.2f, 60.0f, Settings::models().mWeatherblightcloud.get()); // 7
        addWeather("Snow", 0.5f, 40.0f, Settings::models().mWeathersnow.get()); // 8
        addWeather("Blizzard", 0.16f, 70.0f, Settings::models().mWeatherblizzard.get()); // 9

        Store<ESM::Region>::iterator it = store.get<ESM::Region>().begin();
        for (; it != store.get<ESM::Region>().end(); ++it)
        {
            mRegions.insert(std::make_pair(it->mId, RegionWeather(*it)));
        }

        forceWeather(0);
    }

    WeatherManager::~WeatherManager()
    {
        stopSounds();
    }

    const Weather* WeatherManager::getWeather(size_t index) const
    {
        if (index < mWeatherSettings.size())
            return &mWeatherSettings[index];

        return nullptr;
    }

    const Weather* WeatherManager::getWeather(const ESM::RefId& id) const
    {
        auto it = std::find_if(
            mWeatherSettings.begin(), mWeatherSettings.end(), [id](const auto& weather) { return weather.mId == id; });

        if (it != mWeatherSettings.end())
            return &*it;

        return nullptr;
    }

    void WeatherManager::changeWeather(const ESM::RefId& regionID, const ESM::RefId& weatherID)
    {
        auto wIt = std::find_if(mWeatherSettings.begin(), mWeatherSettings.end(),
            [weatherID](const auto& weather) { return weather.mId == weatherID; });

        if (wIt != mWeatherSettings.end())
        {
            auto rIt = mRegions.find(regionID);
            if (rIt != mRegions.end())
            {
                rIt->second.setWeather(wIt->mScriptId);
                regionalWeatherChanged(rIt->first, rIt->second);
            }
        }
    }

    void WeatherManager::changeWeather(const ESM::RefId& regionID, const unsigned int weatherID)
    {
        // In Morrowind, this seems to have the following behavior, when applied to the current region:
        // - When there is no transition in progress, start transitioning to the new weather.
        // - If there is a transition in progress, queue up the transition and process it when the current one
        // completes.
        // - If there is a transition in progress, and a queued transition, overwrite the queued transition.
        // - If multiple calls to ChangeWeather are made while paused (console up), only the last call will be used,
        //   meaning that if there was no transition in progress, only the last ChangeWeather will be processed.
        // If the region isn't current, Morrowind will store the new weather for the region in question.

        if (weatherID < mWeatherSettings.size())
        {
            auto it = mRegions.find(regionID);
            if (it != mRegions.end())
            {
                it->second.setWeather(weatherID);
                regionalWeatherChanged(it->first, it->second);
            }
        }
    }

    void WeatherManager::modRegion(const ESM::RefId& regionID, const std::vector<uint8_t>& chances)
    {
        // Sets the region's probability for various weather patterns. Note that this appears to be saved permanently.
        // In Morrowind, this seems to have the following behavior when applied to the current region:
        // - If the region supports the current weather, no change in current weather occurs.
        // - If the region no longer supports the current weather, and there is no transition in progress, begin to
        //   transition to a new supported weather type.
        // - If the region no longer supports the current weather, and there is a transition in progress, queue a
        //   transition to a new supported weather type.

        auto it = mRegions.find(regionID);
        if (it != mRegions.end())
        {
            it->second.setChances(chances);
            regionalWeatherChanged(it->first, it->second);
        }
    }

    void WeatherManager::playerTeleported(const ESM::RefId& playerRegion, bool isExterior)
    {
        // If the player teleports to an outdoors cell in a new region (for instance, by travelling), the weather needs
        // to be changed immediately, and any transitions for the previous region discarded.
        {
            auto it = mRegions.find(playerRegion);
            if (it != mRegions.end() && playerRegion != mCurrentRegion)
            {
                mCurrentRegion = playerRegion;
                forceWeather(it->second.getWeather());
            }
        }
    }

    float WeatherManager::calculateWindSpeed(int weatherId, float currentSpeed)
    {
        float targetSpeed = std::min(8.0f * mWeatherSettings[weatherId].mWindSpeed, 70.f);
        if (currentSpeed == 0.f)
            currentSpeed = targetSpeed;

        float multiplier = mWeatherSettings[weatherId].mRainEffect.empty() ? 1.f : 0.5f;
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        float updatedSpeed = (Misc::Rng::rollClosedProbability(prng) - 0.5f) * multiplier * targetSpeed + currentSpeed;

        if (updatedSpeed > 0.5f * targetSpeed && updatedSpeed < 2.f * targetSpeed)
            currentSpeed = updatedSpeed;

        return currentSpeed;
    }

    // OpenEden (task 0034): replaces the sky, ambient and sun colours of a weather result with the atmosphere
    // model's, in the way the setting `atmosphere look` says. The weather's glare (Weather_<name>_Glare_View, from
    // the weather's record fallback) says how clear the air is. The alpha channels are kept, and the fog's
    // distance (the weather's rule) is not touched, only its colour.
    void WeatherManager::applyAtmosphere(MWRender::WeatherResult& result, float sunHeight) const
    {
        using namespace Render::Atmosphere;
        Colours vanilla;
        vanilla.sky = vsg::vec3(result.mSkyColor.x, result.mSkyColor.y, result.mSkyColor.z);
        vanilla.ambient = vsg::vec3(result.mAmbientColor.x, result.mAmbientColor.y, result.mAmbientColor.z);
        vanilla.sun = vsg::vec3(result.mSunColor.x, result.mSunColor.y, result.mSunColor.z);
        vanilla.cloudTint = result.mCloudTint;
        vanilla.horizon = vsg::vec3(result.mFogColor.x, result.mFogColor.y, result.mFogColor.z);
        const float clearness = std::clamp(result.mGlareView, 0.f, 1.f);
        const Colours out
            = applyLook(vanilla, sunHeight, clearness, parseLook(Settings::rendering().mAtmosphereLook.get()));

        const auto set = [](vsg::vec4& to, const vsg::vec3& from) { to = vsg::vec4(from.x, from.y, from.z, to.w); };
        // The sun disc follows the sun light's change, channel by channel.
        const auto ratio
            = [](float now, float before) { return before > 1e-4f ? std::clamp(now / before, 0.f, 4.f) : 1.f; };
        result.mSunDiscColor = vsg::vec4(result.mSunDiscColor.x * ratio(out.sun.x, vanilla.sun.x),
            result.mSunDiscColor.y * ratio(out.sun.y, vanilla.sun.y),
            result.mSunDiscColor.z * ratio(out.sun.z, vanilla.sun.z), result.mSunDiscColor.w);
        set(result.mSkyColor, out.sky);
        set(result.mAmbientColor, out.ambient);
        set(result.mSunColor, out.sun);
        set(result.mFogColor, out.horizon);
        result.mCloudTint = out.cloudTint;
        result.mSunElevation = sunHeight;
    }

    // OpenEden (task 0037): the ground's wetness. It heads for the rain's strength while the weather has a rain effect
    // (rain, thunderstorm; snow and ash do not wet) and dries over `wetness dry time` seconds of game time otherwise,
    // indoors too. It is a property of the weather and the clock only: records, scripts, sounds and saves are
    // untouched. The renderer is told 0 indoors, where nothing rains.
    void WeatherManager::updateWetness(float duration, bool paused, bool isExterior)
    {
        if (!Settings::rendering().mWetness)
            return;
        if (!paused)
        {
            const float gameSeconds
                = duration * MWBase::Environment::get().getWorld()->getTimeManager()->getGameTimeScale();
            const float heading
                = isExterior ? Render::Wetness::target(!mResult.mRainEffect.empty(), mResult.mPrecipitationAlpha) : 0.f;
            mWetness = Render::Wetness::step(mWetness, heading, gameSeconds, Settings::rendering().mWetnessDryTime);
        }
        mRendering.setWetness(isExterior ? mWetness : 0.f);
    }

    // OpenEden (task 0039): the snow and ash on the ground. It builds while the weather's particles are snow (snow,
    // blizzard) or ash (ash storm, blight), heads for nothing otherwise, melts over `cover melt time` (ash fades four
    // times slower), indoors too. Like the wetness it is a property of the weather and the clock only: records,
    // scripts, sounds and saves are untouched. The renderer is told nothing is there indoors.
    void WeatherManager::updateCover(float duration, bool paused, bool isExterior)
    {
        if (!Settings::rendering().mCover)
            return;
        if (!paused)
        {
            const float gameSeconds
                = duration * MWBase::Environment::get().getWorld()->getTimeManager()->getGameTimeScale();
            const std::string& particles = mResult.mParticleEffect;
            const auto& models = Settings::models();
            const bool snowy = isExterior && !particles.empty()
                && (particles == models.mWeathersnow.get().value()
                    || particles == models.mWeatherblizzard.get().value());
            const bool ashy = isExterior && !particles.empty()
                && (particles == models.mWeatherashcloud.get().value()
                    || particles == models.mWeatherblightcloud.get().value());
            mCover = Render::Cover::step(mCover, Render::Cover::kindOf(snowy, ashy), mResult.mPrecipitationAlpha,
                gameSeconds, Settings::rendering().mCoverBuildTime, Settings::rendering().mCoverMeltTime);
        }
        const Render::Cover::Dominant shown = Render::Cover::dominant(mCover);
        mRendering.setCover(isExterior ? static_cast<int>(shown.kind) : 0, isExterior ? shown.amount : 0.f);
    }

    // OpenEden (task 0040): lightning as light. The vanilla thunder schedule (Weather::calculateThunder) still decides
    // when a strike happens and plays the sound; each strike it reports starts a flash here, unless one started less
    // than Render::Lightning::minInterval seconds ago. The flash is the capped, cold, double-flicker curve of
    // components/render/lightningmath.hpp, and its direction a hash of the seed and the strike count. Time is the
    // frame time added up while the game is not paused, never a wall clock. Like the vanilla flash it is not saved.
    vsg::vec3 WeatherManager::updateLightning(const float elapsedSeconds, const bool isPaused)
    {
        if (!mRendering.lightningEnabled())
            return vsg::vec3(0.f, 0.f, 0.f);
        if (!isPaused)
            mLightningTime += elapsedSeconds;
        // A float of seconds loses precision over hours of play: take an hour off both clocks now and then.
        constexpr float rebaseSeconds = 3600.f;
        if (mLightningTime > 2.f * rebaseSeconds)
        {
            mLightningTime -= rebaseSeconds;
            mFlasher.shift(rebaseSeconds);
        }
        for (int type : { mCurrentWeather, mNextWeather })
        {
            if (type < 0 || static_cast<size_t>(type) >= mWeatherSettings.size())
                continue;
            float strength = 0.f;
            if (mWeatherSettings[type].takeStrike(strength))
                mFlasher.strike(mLightningTime, strength);
        }
        const float setting = mRendering.lightningStrength();
        const float debugAge = mRendering.lightningDebugAge();
        float amount = 0.f;
        vsg::vec3 direction;
        if (debugAge >= 0.f)
        {
            // A test aid: one flash held at this age, in any exterior weather.
            amount = Render::Lightning::peak(setting, 1.f) * Render::Lightning::envelope(debugAge);
            direction = Render::Lightning::direction(Render::Lightning::defaultSeed, 0u);
        }
        else
        {
            amount = mFlasher.intensity(mLightningTime, setting);
            direction = mFlasher.flashDirection();
        }
        mRendering.setLightning(amount, direction);
        return Render::Lightning::colourAdd(amount);
    }

    void WeatherManager::update(float duration, bool paused, const TimeStamp& time, bool isExterior)
    {
        MWWorld::ConstPtr player = MWMechanics::getPlayer();

        if (!paused || mFastForward)
        {
            // Add new transitions when either the player's current external region changes.
            if (updateWeatherTime() || updateWeatherRegion(regionOf(*player.getCell())))
            {
                auto it = mRegions.find(mCurrentRegion);
                if (it != mRegions.end())
                {
                    addWeatherTransition(it->second.getWeather());
                }
            }

            updateWeatherTransitions(duration);
        }

        bool isDay = time.getHour() >= mSunriseTime && time.getHour() <= mTimeSettings.mNightStart;
        if (isExterior && !isDay)
            mNightDayMode = ExteriorNight;
        else if (!isExterior && isDay && mWeatherSettings[mCurrentWeather].mGlareView >= 0.5f)
            mNightDayMode = InteriorDay;
        else
            mNightDayMode = Default;

        if (!isExterior)
        {
            updateWetness(duration, paused, false);
            updateCover(duration, paused, false);
            mRendering.setLightning(0.f, vsg::vec3(0.f, 0.f, 1.f));
            mRendering.setSkyEnabled(false);
            stopSounds();
            mWindSpeed = 0.f;
            mCurrentWindSpeed = 0.f;
            mNextWindSpeed = 0.f;
            return;
        }

        calculateWeatherResult(time.getHour(), duration, paused);
        updateWetness(duration, paused, true);
        updateCover(duration, paused, true);

        if (!paused)
        {
            mWindSpeed = mResult.mWindSpeed;
            mCurrentWindSpeed = mResult.mCurrentWindSpeed;
            mNextWindSpeed = mResult.mNextWindSpeed;
            // rotate the stars by 360 degrees every 4 days
            mAtmosphereNightRoll += MWBase::Environment::get().getWorld()->getTimeManager()->getGameTimeScale()
                * duration * Misc::VsgMath::inDegrees(360.f) / (3600 * 96.f);
            mRendering.getSky()->setAtmosphereNightRoll(mAtmosphereNightRoll);
        }

        mIsStorm = mResult.mIsStorm;

        // For some reason Ash Storm is not considered as a precipitation weather in game
        mPrecipitation = !(mResult.mParticleEffect.empty() && mResult.mRainEffect.empty())
            && mResult.mParticleEffect != Settings::models().mWeatherashcloud.get();

        mStormDirection = calculateStormDirection(mResult.mParticleEffect);
        mRendering.getSky()->setStormParticleDirection(mStormDirection);

        // disable sun during night
        if (time.getHour() >= mTimeSettings.mNightStart || time.getHour() <= mSunriseTime)
            mRendering.getSky()->sunDisable();
        else
            mRendering.getSky()->sunEnable();

        // OpenEden (task 0034): the sun's height above the horizon, for the atmosphere model.
        float sunHeight = 0.f;

        // Update the sun direction.  Run it east to west at a fixed angle from overhead.
        // The sun's speed at day and night may differ, since mSunriseTime and mNightStart
        // mark when the sun is level with the horizon.
        {
            // Shift times into a 24-hour window beginning at mSunriseTime...
            float adjustedHour = time.getHour();
            float adjustedNightStart = mTimeSettings.mNightStart;
            if (time.getHour() < mSunriseTime)
                adjustedHour += 24.f;
            if (mTimeSettings.mNightStart < mSunriseTime)
                adjustedNightStart += 24.f;

            const bool isNight = adjustedHour >= adjustedNightStart;
            mRendering.setNight(isNight);
            const float dayDuration = adjustedNightStart - mSunriseTime;
            const float nightDuration = 24.f - dayDuration;

            double theta;
            if (!isNight)
            {
                theta = static_cast<float>(Misc::VsgMath::PI) * (adjustedHour - mSunriseTime) / dayDuration;
            }
            else
            {
                theta = static_cast<float>(Misc::VsgMath::PI)
                    - static_cast<float>(Misc::VsgMath::PI) * (adjustedHour - adjustedNightStart) / nightDuration;
            }

            vsg::vec3 final(static_cast<float>(cos(theta)),
                -0.268f, // approx tan( -15 degrees )
                static_cast<float>(sin(theta)));
            mRendering.setSunDirection(final * -1);
            sunHeight = Render::Atmosphere::sunElevation(final, isNight);
            // Decision 0010 answer 4: the colours follow the colour tables' hour. Only the model's input changes; the
            // drawn sun and the light direction (set above) keep the orbit.
            if (Settings::rendering().mSunFollowsColourTables)
            {
                // The sky's windows (the sky colour is what the model mostly drives): the same numbers as
                // TimeOfDayInterpolator::getValue uses for "Sky", pre and post times included.
                const WeatherSetting sky = mTimeSettings.getSetting("Sky");
                const Render::Sun::TimeOfDayWindows windows{ mTimeSettings.mNightEnd - sky.mPreSunriseTime,
                    mTimeSettings.mDayStart + sky.mPostSunriseTime, mTimeSettings.mDayEnd - sky.mPreSunsetTime,
                    mTimeSettings.mNightStart + sky.mPostSunsetTime };
                sunHeight = Render::Sun::tableElevation(time.getHour(), windows);
            }
        }

        float underwaterFog = mUnderwaterFog.getValue(time.getHour(), mTimeSettings, "Fog");

        float peakHour = mSunriseTime + (mTimeSettings.mNightStart - mSunriseTime) / 2;
        float glareFade = 1.f;
        if (time.getHour() < mSunriseTime || time.getHour() > mTimeSettings.mNightStart)
            glareFade = 0.f;
        else if (time.getHour() < peakHour)
            glareFade = 1.f - (peakHour - time.getHour()) / (peakHour - mSunriseTime);
        else
            glareFade = 1.f - (time.getHour() - peakHour) / (mTimeSettings.mNightStart - peakHour);

        mRendering.getSky()->setGlareTimeOfDayFade(glareFade);

        mRendering.getSky()->setMasserState(mMasser.calculateState(time));
        mRendering.getSky()->setSecundaState(mSecunda.calculateState(time));

        // OpenEden (task 0034): with the atmosphere on, the colours handed to the renderer come from the model; the
        // weather's own result (mResult) is not changed. Off, rendered is mResult itself.
        const MWRender::WeatherResult* rendered = &mResult;
        if (Render::Atmosphere::useAtmosphere(Settings::rendering().mAtmosphere, Settings::rendering().mHdr))
        {
            mAtmosphereResult = mResult;
            applyAtmosphere(mAtmosphereResult, sunHeight);
            rendered = &mAtmosphereResult;
        }

        mRendering.configureFog(rendered->mFogDepth, underwaterFog, rendered->mDLFogFactor,
            rendered->mDLFogOffset / 100.0f, rendered->mFogColor);
        mRendering.setAmbientColour(rendered->mAmbientColor);
        mRendering.setSunColour(rendered->mSunColor, rendered->mSunColor * rendered->mGlareView * glareFade);

        mRendering.getSky()->setWeather(*rendered);

        // Play sounds
        if (mPlayingAmbientSoundID != mResult.mAmbientLoopSoundID)
        {
            if (mAmbientSound)
            {
                MWBase::Environment::get().getSoundManager()->stopSound(mAmbientSound);
                mAmbientSound = nullptr;
            }
            if (!mResult.mAmbientLoopSoundID.empty())
                mAmbientSound = MWBase::Environment::get().getSoundManager()->playSound(mResult.mAmbientLoopSoundID,
                    mResult.mAmbientSoundVolume, 1.0, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
            mPlayingAmbientSoundID = mResult.mAmbientLoopSoundID;
        }
        else if (mAmbientSound)
            mAmbientSound->setVolume(mResult.mAmbientSoundVolume);

        if (mPlayingRainSoundID != mResult.mRainLoopSoundID)
        {
            if (mRainSound)
            {
                MWBase::Environment::get().getSoundManager()->stopSound(mRainSound);
                mRainSound = nullptr;
            }
            if (!mResult.mRainLoopSoundID.empty())
                mRainSound = MWBase::Environment::get().getSoundManager()->playSound(mResult.mRainLoopSoundID,
                    mResult.mAmbientSoundVolume, 1.0, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
            mPlayingRainSoundID = mResult.mRainLoopSoundID;
        }
        else if (mRainSound)
            mRainSound->setVolume(mResult.mAmbientSoundVolume);
    }

    void WeatherManager::stopSounds()
    {
        MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
        if (mAmbientSound)
        {
            sndMgr->stopSound(mAmbientSound);
            mAmbientSound = nullptr;
        }
        mPlayingAmbientSoundID = ESM::RefId();

        if (mRainSound)
        {
            sndMgr->stopSound(mRainSound);
            mRainSound = nullptr;
        }
        mPlayingRainSoundID = ESM::RefId();

        for (ESM::RefId soundId : mWeatherSettings[mCurrentWeather].mThunderSoundID)
            if (!soundId.empty() && sndMgr->getSoundPlaying(MWWorld::ConstPtr(), soundId))
                sndMgr->stopSound3D(MWWorld::ConstPtr(), soundId);

        if (inTransition())
            for (ESM::RefId soundId : mWeatherSettings[mNextWeather].mThunderSoundID)
                if (!soundId.empty() && sndMgr->getSoundPlaying(MWWorld::ConstPtr(), soundId))
                    sndMgr->stopSound3D(MWWorld::ConstPtr(), soundId);
    }

    float WeatherManager::getWindSpeed() const
    {
        return mWindSpeed;
    }

    bool WeatherManager::isInStorm() const
    {
        return mIsStorm;
    }

    vsg::vec3 WeatherManager::getStormDirection() const
    {
        return mStormDirection;
    }

    void WeatherManager::advanceTime(double hours, bool incremental)
    {
        // In Morrowind, when the player sleeps/waits, serves jail time, travels, or trains, all weather transitions are
        // immediately applied, regardless of whatever transition time might have been remaining.
        mTimePassed += static_cast<float>(hours);
        mFastForward = !incremental ? true : mFastForward;
    }

    NightDayMode WeatherManager::getNightDayMode() const
    {
        return mNightDayMode;
    }

    bool WeatherManager::useTorches(float hour) const
    {
        bool isDark = hour < mSunriseTime || hour > mTimeSettings.mNightStart;

        return isDark && !mPrecipitation;
    }

    float WeatherManager::getSunPercentage(float hour) const
    {
        if (hour <= mTimeSettings.mNightEnd || hour >= mTimeSettings.mNightStart)
            return 0.f;
        else if (hour <= mTimeSettings.mDayStart)
            return (hour - mTimeSettings.mNightEnd) / mSunriseDuration;
        else if (hour > mTimeSettings.mDayEnd)
            return 1.f - ((hour - mTimeSettings.mDayEnd) / mSunsetDuration);
        return 1.f;
    }

    float WeatherManager::getSunVisibility() const
    {
        if (inTransition() && mTransitionFactor < mWeatherSettings[mNextWeather].mCloudsMaximumPercent)
        {
            float t = mTransitionFactor / mWeatherSettings[mNextWeather].mCloudsMaximumPercent;
            return (1.f - t) * mWeatherSettings[mCurrentWeather].mGlareView
                + t * mWeatherSettings[mNextWeather].mGlareView;
        }
        return mWeatherSettings[mCurrentWeather].mGlareView;
    }

    void WeatherManager::write(ESM::ESMWriter& writer, Loading::Listener& progress)
    {
        ESM::WeatherState state;
        state.mCurrentRegion = mCurrentRegion;
        state.mTimePassed = mTimePassed;
        state.mFastForward = mFastForward;
        state.mWeatherUpdateTime = mWeatherUpdateTime;
        state.mTransitionFactor = mTransitionFactor;
        state.mCurrentWeather = mCurrentWeather;
        state.mNextWeather = mNextWeather;
        state.mQueuedWeather = mQueuedWeather;

        auto it = mRegions.begin();
        for (; it != mRegions.end(); ++it)
        {
            state.mRegions.insert(std::make_pair(it->first, it->second));
        }

        writer.startRecord(ESM::REC_WTHR);
        state.save(writer);
        writer.endRecord(ESM::REC_WTHR);
    }

    bool WeatherManager::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (ESM::REC_WTHR == type)
        {
            ESM::WeatherState state;
            state.load(reader);

            std::swap(mCurrentRegion, state.mCurrentRegion);
            mTimePassed = state.mTimePassed;
            mFastForward = state.mFastForward;
            mWeatherUpdateTime = state.mWeatherUpdateTime;
            mTransitionFactor = state.mTransitionFactor;
            mCurrentWeather = state.mCurrentWeather;
            mNextWeather = state.mNextWeather;
            mQueuedWeather = state.mQueuedWeather;

            mRegions.clear();
            importRegions();

            for (auto it = state.mRegions.begin(); it != state.mRegions.end(); ++it)
            {
                auto found = mRegions.find(it->first);
                if (found != mRegions.end())
                {
                    found->second = RegionWeather(it->second);
                }
            }

            return true;
        }

        return false;
    }

    void WeatherManager::clear()
    {
        stopSounds();

        mCurrentRegion = ESM::RefId();
        mTimePassed = 0.0f;
        mWeatherUpdateTime = 0.0f;
        forceWeather(0);
        mRegions.clear();
        importRegions();
    }

    inline void WeatherManager::addWeather(
        const std::string& name, float dlFactor, float dlOffset, const std::string& particleEffect)
    {
        static const float fStromWindSpeed = mStore.get<ESM::GameSetting>().find("fStromWindSpeed")->mValue.getFloat();
        ESM::StringRefId id(name);
        Weather weather(id, static_cast<int>(mWeatherSettings.size()), name, fStromWindSpeed, mRainSpeed, dlFactor,
            dlOffset, particleEffect);

        mWeatherSettings.push_back(std::move(weather));
    }

    void WeatherManager::addWeather(const WeatherEntry& entry)
    {
        // OpenEden (task 0127): a pack's weather. It is a storm only when the pack says so.
        const float stormWindSpeed = entry.forceStorm ? -1.f : std::numeric_limits<float>::infinity();
        ESM::StringRefId id(entry.name);
        mWeatherSettings.push_back(Weather(id, static_cast<int>(mWeatherSettings.size()), entry.name, stormWindSpeed,
            mRainSpeed, entry.dlFactor, entry.dlOffset, entry.particleEffect, entry.params));
    }

    ESM::RefId WeatherManager::regionOf(const CellStore& cell)
    {
        if (mRegionProvider == nullptr)
            return cell.getCell()->getRegion();
        const ESM::RefId key = mRegionProvider->regionOf(cell);
        if (!key.empty() && mRegions.find(key) == mRegions.end())
        {
            std::vector<uint8_t> chances;
            if (mRegionProvider->chancesOf(key, chances))
            {
                auto inserted = mRegions.insert(std::make_pair(key, RegionWeather(std::move(chances))));
                // Developer aid (OPENEDEN_NEUTRAL_FORCE_WEATHER): a new region starts in the forced weather.
                if (mForcedWeather >= 0)
                    inserted.first->second.setWeather(mForcedWeather);
            }
            else
                return ESM::RefId();
        }
        return key;
    }

    inline void WeatherManager::importRegions()
    {
        if (mRegionProvider != nullptr)
            return; // a pack's regions are made when a cell asks (regionOf)
        for (const ESM::Region& region : mStore.get<ESM::Region>())
        {
            mRegions.insert(std::make_pair(region.mId, RegionWeather(region)));
        }
    }

    inline void WeatherManager::regionalWeatherChanged(const ESM::RefId& regionID, RegionWeather& region)
    {
        // If the region is current, then add a weather transition for it.
        MWWorld::ConstPtr player = MWMechanics::getPlayer();
        if (player.isInCell())
        {
            if (regionID == mCurrentRegion)
            {
                addWeatherTransition(region.getWeather());
            }
        }
    }

    inline bool WeatherManager::updateWeatherTime()
    {
        mWeatherUpdateTime -= mTimePassed;
        mTimePassed = 0.0f;
        if (mWeatherUpdateTime <= 0.0f)
        {
            // Expire all regional weather, so that any call to getWeather() will return a new weather ID.
            auto it = mRegions.begin();
            for (; it != mRegions.end(); ++it)
            {
                it->second.setWeather(mForcedWeather >= 0 ? mForcedWeather : invalidWeatherID);
            }

            mWeatherUpdateTime += mHoursBetweenWeatherChanges;

            return true;
        }

        return false;
    }

    inline bool WeatherManager::updateWeatherRegion(const ESM::RefId& playerRegion)
    {
        if (!playerRegion.empty() && playerRegion != mCurrentRegion)
        {
            mCurrentRegion = playerRegion;

            return true;
        }

        return false;
    }

    inline void WeatherManager::updateWeatherTransitions(const float elapsedRealSeconds)
    {
        // When a player chooses to train, wait, or serves jail time, any transitions will be fast forwarded to the last
        // weather type set, regardless of the remaining transition time.
        if (!mFastForward && inTransition())
        {
            const float delta = mWeatherSettings[mNextWeather].transitionDelta();
            mTransitionFactor -= elapsedRealSeconds * delta;
            if (mTransitionFactor <= 0.0f)
            {
                mCurrentWeather = mNextWeather;
                mNextWeather = mQueuedWeather;
                mQueuedWeather = invalidWeatherID;

                // We may have begun processing the queued transition, so we need to apply the remaining time towards
                // it.
                if (inTransition())
                {
                    const float newDelta = mWeatherSettings[mNextWeather].transitionDelta();
                    const float remainingSeconds = -(mTransitionFactor / delta);
                    mTransitionFactor = 1.0f - (remainingSeconds * newDelta);
                }
                else
                {
                    mTransitionFactor = 0.0f;
                }
            }
        }
        else
        {
            if (mQueuedWeather != invalidWeatherID)
            {
                mCurrentWeather = mQueuedWeather;
            }
            else if (mNextWeather != invalidWeatherID)
            {
                mCurrentWeather = mNextWeather;
            }

            mNextWeather = invalidWeatherID;
            mQueuedWeather = invalidWeatherID;
            mFastForward = false;
        }
    }

    inline void WeatherManager::forceWeather(const int weatherID)
    {
        mTransitionFactor = 0.0f;
        mCurrentWeather = weatherID;
        mNextWeather = invalidWeatherID;
        mQueuedWeather = invalidWeatherID;
    }

    inline bool WeatherManager::inTransition() const
    {
        return mNextWeather != invalidWeatherID;
    }

    inline void WeatherManager::addWeatherTransition(const int weatherID)
    {
        // In order to work like ChangeWeather expects, this method begins transitioning to the new weather immediately
        // if no transition is in progress, otherwise it queues it to be transitioned.

        assert(weatherID >= 0 && static_cast<size_t>(weatherID) < mWeatherSettings.size());

        if (!inTransition() && (weatherID != mCurrentWeather))
        {
            mNextWeather = weatherID;
            mTransitionFactor = 1.0f;
        }
        else if (inTransition() && (weatherID != mNextWeather))
        {
            mQueuedWeather = weatherID;
        }
    }

    inline void WeatherManager::calculateWeatherResult(
        const float gameHour, const float elapsedSeconds, const bool isPaused)
    {
        float flash = 0.0f;
        if (!inTransition())
        {
            calculateResult(mCurrentWeather, gameHour);
            flash = mWeatherSettings[mCurrentWeather].calculateThunder(1.0f, elapsedSeconds, isPaused);
        }
        else
        {
            calculateTransitionResult(1 - mTransitionFactor, gameHour);
            float currentFlash
                = mWeatherSettings[mCurrentWeather].calculateThunder(mTransitionFactor, elapsedSeconds, isPaused);
            float nextFlash
                = mWeatherSettings[mNextWeather].calculateThunder(1 - mTransitionFactor, elapsedSeconds, isPaused);
            flash = currentFlash + nextFlash;
        }
        vsg::vec4 flashColor(flash, flash, flash, 0.0f);
        // OpenEden (task 0040): with `lightning` on, the flash is the renderer's capped, cold, double-flicker one.
        const vsg::vec3 lightningColour = updateLightning(elapsedSeconds, isPaused);
        if (mRendering.lightningEnabled())
            flashColor = vsg::vec4(lightningColour.x, lightningColour.y, lightningColour.z, 0.0f);

        mResult.mFogColor += flashColor;
        mResult.mAmbientColor += flashColor;
        mResult.mSunColor += flashColor;
    }

    inline void WeatherManager::calculateResult(const int weatherID, const float gameHour)
    {
        const Weather& current = mWeatherSettings[weatherID];

        mResult.mCloudTexture = current.mCloudTexture;
        mResult.mCloudBlendFactor = 0;
        mResult.mNextWindSpeed = 0;
        mResult.mWindSpeed = mResult.mCurrentWindSpeed = calculateWindSpeed(weatherID, mWindSpeed);
        mResult.mBaseWindSpeed = mWeatherSettings[weatherID].mWindSpeed;

        mResult.mCloudSpeed = current.mCloudSpeed;
        mResult.mGlareView = current.mGlareView;
        mResult.mAmbientLoopSoundID = current.mAmbientLoopSoundID;
        mResult.mRainLoopSoundID = current.mRainLoopSoundID;
        mResult.mAmbientSoundVolume = 1.f;
        mResult.mPrecipitationAlpha = 1.f;

        mResult.mIsStorm = current.mIsStorm;

        mResult.mRainSpeed = current.mRainSpeed;
        mResult.mRainEntranceSpeed = current.mRainEntranceSpeed;
        mResult.mRainDiameter = current.mRainDiameter;
        mResult.mRainMinHeight = current.mRainMinHeight;
        mResult.mRainMaxHeight = current.mRainMaxHeight;
        mResult.mRainMaxRaindrops = current.mRainMaxRaindrops;

        mResult.mParticleEffect = current.mParticleEffect;
        mResult.mRainEffect = current.mRainEffect;

        mResult.mNight = (gameHour < mSunriseTime
            || gameHour
                > mTimeSettings.mNightStart + mTimeSettings.mStarsPostSunsetStart - mTimeSettings.mStarsFadingDuration);

        mResult.mFogDepth = current.mLandFogDepth.getValue(gameHour, mTimeSettings, "Fog");
        mResult.mFogColor = current.mFogColor.getValue(gameHour, mTimeSettings, "Fog");
        mResult.mAmbientColor = current.mAmbientColor.getValue(gameHour, mTimeSettings, "Ambient");
        mResult.mSunColor = current.mSunColor.getValue(gameHour, mTimeSettings, "Sun");
        mResult.mSkyColor = current.mSkyColor.getValue(gameHour, mTimeSettings, "Sky");
        mResult.mNightFade = mNightFade.getValue(gameHour, mTimeSettings, "Stars");
        mResult.mDLFogFactor = current.mDL.FogFactor;
        mResult.mDLFogOffset = current.mDL.FogOffset;

        WeatherSetting setting = mTimeSettings.getSetting("Sun");
        float preSunsetTime = setting.mPreSunsetTime;

        if (gameHour >= mTimeSettings.mDayEnd - preSunsetTime)
        {
            float factor = 1.f;
            if (preSunsetTime > 0)
                factor = (gameHour - (mTimeSettings.mDayEnd - preSunsetTime)) / preSunsetTime;
            factor = std::min(1.f, factor);
            mResult.mSunDiscColor = lerp(vsg::vec4(1, 1, 1, 1), current.mSunDiscSunsetColor, factor);
            // The SunDiscSunsetColor in the INI isn't exactly the resulting color on screen, most likely because
            // MW applied the color to the ambient term as well. After the ambient and emissive terms are added
            // together, the fixed pipeline would then clamp the total lighting to (1,1,1). A noticeable change in color
            // tone can be observed when only one of the color components gets clamped. Unfortunately that means we
            // can't use the INI color as is, have to replicate the above nonsense.
            mResult.mSunDiscColor = mResult.mSunDiscColor
                + Misc::VsgMath::componentMultiply(mResult.mSunDiscColor, mResult.mAmbientColor);
            for (int i = 0; i < 3; ++i)
                mResult.mSunDiscColor[i] = std::min(1.f, mResult.mSunDiscColor[i]);
        }
        else
            mResult.mSunDiscColor = vsg::vec4(1, 1, 1, 1);

        if (gameHour >= mTimeSettings.mDayEnd)
        {
            // sunset
            float fade = std::min(
                1.f, (gameHour - mTimeSettings.mDayEnd) / (mTimeSettings.mNightStart - mTimeSettings.mDayEnd));
            fade = fade * fade;
            mResult.mSunDiscColor.a = 1.f - fade;
        }
        else if (gameHour >= mTimeSettings.mNightEnd && gameHour <= mTimeSettings.mNightEnd + mSunriseDuration / 2.f)
        {
            // sunrise
            mResult.mSunDiscColor.a = gameHour - mTimeSettings.mNightEnd;
        }
        else
            mResult.mSunDiscColor.a = 1;

        mResult.mStormDirection = calculateStormDirection(mResult.mParticleEffect);
    }

    inline void WeatherManager::calculateTransitionResult(const float factor, const float gameHour)
    {
        calculateResult(mCurrentWeather, gameHour);
        const MWRender::WeatherResult current = mResult;
        calculateResult(mNextWeather, gameHour);
        const MWRender::WeatherResult other = mResult;

        mResult.mStormDirection = current.mStormDirection;
        mResult.mNextStormDirection = other.mStormDirection;

        mResult.mCloudTexture = current.mCloudTexture;
        mResult.mNextCloudTexture = other.mCloudTexture;
        mResult.mCloudBlendFactor = mWeatherSettings[mNextWeather].cloudBlendFactor(factor);

        mResult.mFogColor = lerp(current.mFogColor, other.mFogColor, factor);
        mResult.mSunColor = lerp(current.mSunColor, other.mSunColor, factor);
        mResult.mSkyColor = lerp(current.mSkyColor, other.mSkyColor, factor);

        mResult.mAmbientColor = lerp(current.mAmbientColor, other.mAmbientColor, factor);
        mResult.mSunDiscColor = lerp(current.mSunDiscColor, other.mSunDiscColor, factor);
        mResult.mFogDepth = lerp(current.mFogDepth, other.mFogDepth, factor);
        mResult.mDLFogFactor = lerp(current.mDLFogFactor, other.mDLFogFactor, factor);
        mResult.mDLFogOffset = lerp(current.mDLFogOffset, other.mDLFogOffset, factor);

        mResult.mCurrentWindSpeed = calculateWindSpeed(mCurrentWeather, mCurrentWindSpeed);
        mResult.mNextWindSpeed = calculateWindSpeed(mNextWeather, mNextWindSpeed);
        mResult.mBaseWindSpeed = lerp(current.mBaseWindSpeed, other.mBaseWindSpeed, factor);

        mResult.mWindSpeed = lerp(mResult.mCurrentWindSpeed, mResult.mNextWindSpeed, factor);
        mResult.mCloudSpeed = lerp(current.mCloudSpeed, other.mCloudSpeed, factor);
        mResult.mGlareView = lerp(current.mGlareView, other.mGlareView, factor);
        mResult.mNightFade = lerp(current.mNightFade, other.mNightFade, factor);

        mResult.mNight = current.mNight;

        float threshold = mWeatherSettings[mNextWeather].mRainThreshold;
        if (threshold <= 0.f)
            threshold = 0.5f;

        if (factor < threshold)
        {
            mResult.mIsStorm = current.mIsStorm;
            mResult.mParticleEffect = current.mParticleEffect;
            mResult.mRainEffect = current.mRainEffect;
            mResult.mRainSpeed = current.mRainSpeed;
            mResult.mRainEntranceSpeed = current.mRainEntranceSpeed;
            mResult.mAmbientSoundVolume = 1.f - factor / threshold;
            mResult.mPrecipitationAlpha = mResult.mAmbientSoundVolume;
            mResult.mAmbientLoopSoundID = current.mAmbientLoopSoundID;
            mResult.mRainLoopSoundID = current.mRainLoopSoundID;
            mResult.mRainDiameter = current.mRainDiameter;
            mResult.mRainMinHeight = current.mRainMinHeight;
            mResult.mRainMaxHeight = current.mRainMaxHeight;
            mResult.mRainMaxRaindrops = current.mRainMaxRaindrops;
        }
        else
        {
            mResult.mIsStorm = other.mIsStorm;
            mResult.mParticleEffect = other.mParticleEffect;
            mResult.mRainEffect = other.mRainEffect;
            mResult.mRainSpeed = other.mRainSpeed;
            mResult.mRainEntranceSpeed = other.mRainEntranceSpeed;
            mResult.mAmbientSoundVolume = (factor - threshold) / (1 - threshold);
            mResult.mPrecipitationAlpha = mResult.mAmbientSoundVolume;
            mResult.mAmbientLoopSoundID = other.mAmbientLoopSoundID;
            mResult.mRainLoopSoundID = other.mRainLoopSoundID;

            mResult.mRainDiameter = other.mRainDiameter;
            mResult.mRainMinHeight = other.mRainMinHeight;
            mResult.mRainMaxHeight = other.mRainMaxHeight;
            mResult.mRainMaxRaindrops = other.mRainMaxRaindrops;
        }
    }
}
