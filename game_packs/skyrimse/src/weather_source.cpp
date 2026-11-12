#include "game_skyrimse/weather_source.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::PerTimeOfDay;
        using openeden::sdk::game::WeatherColour;

        WeatherColour colourOf(const WeatherRecord& w, std::uint32_t type, std::uint32_t time)
        {
            if (type >= w.mColourTypes || type >= WeatherConstants::kMaxColourTypes)
                return {};
            const Rgba8 c = w.mColours[type][time];
            return { c.r / 255.f, c.g / 255.f, c.b / 255.f };
        }

        PerTimeOfDay<WeatherColour> tableOf(const WeatherRecord& w, std::uint32_t type)
        {
            return { colourOf(w, type, WeatherConstants::Sunrise), colourOf(w, type, WeatherConstants::Day),
                colourOf(w, type, WeatherConstants::Sunset), colourOf(w, type, WeatherConstants::Night) };
        }

        /// The engine fogs linearly from viewDistance * (1 - depth) to viewDistance. Skyrim fogs by pow(share, power)
        /// where share = (distance - near) / (far - near), up to `max`. The depth is that fog amount at the reference
        /// distance: the engine's ramp then starts where a fraction `depth` of the view distance is left.
        float fogDepthOf(float nearD, float farD, float power, float maxFog)
        {
            if (!(farD > 0.f) || !std::isfinite(farD) || !std::isfinite(nearD) || !(farD > nearD))
                return 0.f;
            const float share
                = std::clamp((WeatherScale::kFogReferenceDistance - nearD) / (farD - nearD), 0.f, 1.f);
            const float amount = power > 0.f && std::isfinite(power) ? std::pow(share, power) : share;
            const float cap = std::isfinite(maxFog) ? std::clamp(maxFog, 0.f, 1.f) : 1.f;
            return std::clamp(std::min(amount, cap), 0.f, 1.f);
        }

        float cloudSpeedOf(const WeatherRecord& w, std::size_t layer)
        {
            if (layer >= w.mCloudLayers.size())
                return WeatherScale::kCloudSpeedFloor;
            const CloudLayer& l = w.mCloudLayers[layer];
            const auto away = [](const std::optional<std::uint8_t>& b)
            { return b ? std::abs(static_cast<int>(*b) - WeatherScale::kCloudStillByte) : 0; };
            const float drift = static_cast<float>(std::max(away(l.mSpeedQ), away(l.mSpeedR))) / WeatherScale::kCloudStillByte;
            return std::max(WeatherScale::kCloudSpeedFloor, drift * WeatherScale::kCloudSpeedScale);
        }
    }

    openeden::sdk::game::WeatherDescription describeWeather(const WeatherRecord& w, const TextureProbe& textureExists)
    {
        openeden::sdk::game::WeatherDescription d;
        d.id = w.mCommon.mId;
        d.editorId = w.mCommon.mEditorId;

        std::size_t picture = w.mCloudLayers.size();
        for (std::size_t i = 0; i < w.mCloudLayers.size(); ++i)
            if (w.mCloudLayers[i].mHasTexture && !w.mCloudLayers[i].mTexture.empty()
                && (!textureExists || textureExists(w.mCloudLayers[i].mTexture)))
            {
                picture = i;
                d.cloudTexture = w.mCloudLayers[i].mTexture;
                break;
            }
        d.cloudSpeed = cloudSpeedOf(w, picture);

        d.sky = tableOf(w, SkyColourType::SkyUpper);
        d.fog = tableOf(w, SkyColourType::FogFar);
        d.ambient = tableOf(w, SkyColourType::Ambient);
        d.sun = tableOf(w, SkyColourType::Sunlight);
        d.sunDiscSunset = colourOf(w, SkyColourType::Sun, WeatherConstants::Sunset);

        if (w.mFog)
        {
            const WeatherFog& f = *w.mFog;
            const float day = fogDepthOf(f.mDayNear, f.mDayFar, f.mDayPower, f.mDayMax);
            const float night = fogDepthOf(f.mNightNear, f.mNightFar, f.mNightPower, f.mNightMax);
            d.fogDepth = { day, day, day, night };
        }
        if (w.mData)
        {
            d.windSpeed = w.mData->mWindSpeed / WeatherScale::kWindByteScale;
            d.glareView = w.mData->mSunGlare / WeatherScale::kGlareByteScale;
            d.transitionDelta = w.mData->mTransitionDelta * WeatherScale::kTransitionDeltaPerUnit;

            // Task 0127 W-S2. The kind is the DATA flags' Snow or Rainy bit (snow wins when both are set) and only a
            // weather that names precipitation particles (MNAM, an SPGD) falls at all: the installed data has rainy
            // weathers without one (SoulCairnAmb01_Rain, TESTCloudyRain) that show nothing in the game. Byte 6 over 255
            // is the share of a transition after which the precipitation has changed over (153 / 255 = 0.6, the same
            // number as Morrowind's rain threshold; RECALLED-VERIFY).
            using openeden::sdk::game::Precipitation;
            if (w.mPrecipitation != kNullFormId)
            {
                if ((w.mData->mFlags & WeatherFlags::Snow) != 0)
                    d.precipitation = Precipitation::Snow;
                else if ((w.mData->mFlags & WeatherFlags::Rainy) != 0)
                    d.precipitation = Precipitation::Rain;
            }
            d.precipitationThreshold = w.mData->mPrecipitationBegin / 255.f;

            // Thunder exists when the frequency is below "none" and the end fade is not zero (the default weather has
            // frequency 0 and both fades 0). Byte 8 over 255 is when it starts within a transition.
            if (w.mData->mThunderFrequency < WeatherScale::kNoThunderByte && w.mData->mThunderEnd != 0)
            {
                d.thunderFrequency = std::min(WeatherScale::kThunderFrequencyMax,
                    (WeatherScale::kNoThunderByte - w.mData->mThunderFrequency)
                        * WeatherScale::kThunderFrequencyPerStep);
                d.thunderThreshold = w.mData->mThunderBegin / 255.f;
            }
        }
        d.cloudsMaximumPercent = 1.f;
        d.storm = false;
        return d;
    }

    bool describeTimeOfDay(const WeatherIndex& index, openeden::sdk::game::TimeOfDayDescription& out)
    {
        using Key = std::tuple<unsigned, unsigned, unsigned, unsigned>;
        std::map<Key, std::pair<int, FormId>> votes; // timing -> (climates, lowest id)
        for (const FormId id : index.climateIds())
        {
            const ClimateRecord* c = index.climate(id);
            if (c == nullptr || !c->mTiming)
                continue;
            const ClimateTiming& t = *c->mTiming;
            // A climate with no day (all four zero) gives no timing.
            if (t.mSunriseBegin == 0 && t.mSunriseEnd == 0 && t.mSunsetBegin == 0 && t.mSunsetEnd == 0)
                continue;
            auto& v = votes[Key{ t.sunriseBeginMinutes(), t.sunriseEndMinutes(), t.sunsetBeginMinutes(),
                t.sunsetEndMinutes() }];
            if (v.first == 0)
                v.second = id;
            ++v.first;
        }
        if (votes.empty())
            return false;
        const Key* best = nullptr;
        int bestCount = 0;
        FormId bestId = 0;
        for (const auto& [key, v] : votes)
            if (v.first > bestCount || (v.first == bestCount && v.second < bestId))
            {
                best = &key;
                bestCount = v.first;
                bestId = v.second;
            }
        out.sunriseBegin = std::get<0>(*best) / 60.f;
        out.sunriseEnd = std::get<1>(*best) / 60.f;
        out.sunsetBegin = std::get<2>(*best) / 60.f;
        out.sunsetEnd = std::get<3>(*best) / 60.f;
        return true;
    }

    WeatherTable::WeatherTable(const WeatherIndex& index, const TextureProbe& textureExists)
        : mIndex(&index)
        , mIds(index.weatherIds())
    {
        mWeathers.reserve(mIds.size());
        for (const FormId id : mIds)
        {
            const WeatherRecord* w = index.weather(id);
            mWeathers.push_back(w != nullptr ? describeWeather(*w, textureExists) : openeden::sdk::game::WeatherDescription{});
        }
    }

    int WeatherTable::indexOf(FormId weather) const
    {
        const auto it = std::lower_bound(mIds.begin(), mIds.end(), weather);
        if (it == mIds.end() || *it != weather)
            return -1;
        return static_cast<int>(it - mIds.begin());
    }

    bool WeatherTable::listForCell(FormId cell, openeden::sdk::game::CellWeatherList& out) const
    {
        out = {};
        const CellWeatherLinks* links = mIndex->cell(cell);
        if (links == nullptr)
            return false;
        const WeatherIndex::EffectiveList list = mIndex->effectiveWeatherList(*links);
        if (list.mSource == WeatherIndex::EffectiveList::Source::None || list.mWeathers == nullptr)
            return false;
        for (const WeatherChance& c : *list.mWeathers)
        {
            const int index = indexOf(c.mWeather);
            if (index < 0)
                continue;
            out.entries.push_back({ static_cast<std::uint32_t>(index),
                static_cast<std::uint32_t>(std::max<std::int32_t>(c.mChance, 0)) });
        }
        if (out.entries.empty())
        {
            out = {};
            return false;
        }
        out.key = list.mFrom;
        return true;
    }
}
