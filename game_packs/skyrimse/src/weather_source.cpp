#include "game_skyrimse/weather_source.hpp"

#include <algorithm>
#include <cctype>
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

    namespace
    {
        bool sameTexture(const std::string& a, const std::string& b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
                { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }

        bool endsWithBlank(const std::string& path)
        {
            const std::string blank = WeatherScale::kBlankCloudTexture;
            if (path.size() < blank.size())
                return false;
            const std::size_t at = path.size() - blank.size();
            if (!sameTexture(path.substr(at), blank))
                return false;
            return at == 0 || path[at - 1] == '\\' || path[at - 1] == '/';
        }

        bool isHorizonStrip(const std::string& path)
        {
            const std::string marker = WeatherScale::kHorizonCloudMarker;
            return std::search(path.begin(), path.end(), marker.begin(), marker.end(), [](char x, char y)
                       { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); })
                != path.end();
        }

        float cleanAlpha(float a)
        {
            return std::isfinite(a) ? std::clamp(a, 0.f, 1.f) : 0.f;
        }

        // Layers beyond the picture: the ones the record counts (LNAM, when it has one), that have a texture which
        // exists, is not blank and is not already drawn, and that are visible at some time of day.
        void addExtraLayers(const WeatherRecord& w, std::size_t picture, const TextureProbe& textureExists,
            openeden::sdk::game::WeatherDescription& d)
        {
            const std::size_t limit
                = w.mLayerCount ? std::min<std::size_t>(*w.mLayerCount, w.mCloudLayers.size()) : w.mCloudLayers.size();
            std::vector<std::string> taken;
            if (!d.cloudTexture.empty())
                taken.push_back(d.cloudTexture);
            for (std::size_t i = 0; i < limit && d.extraCloudLayers.size() < openeden::sdk::game::kMaxExtraCloudLayers;
                 ++i)
            {
                const CloudLayer& l = w.mCloudLayers[i];
                if (i == picture || !l.mHasTexture || l.mTexture.empty() || endsWithBlank(l.mTexture)
                    || isHorizonStrip(l.mTexture))
                    continue;
                if (textureExists && !textureExists(l.mTexture))
                    continue;
                if (std::any_of(
                        taken.begin(), taken.end(), [&](const std::string& t) { return sameTexture(t, l.mTexture); }))
                    continue;
                const std::array<float, 4> alpha = l.mAlpha ? *l.mAlpha : std::array<float, 4>{ 1.f, 1.f, 1.f, 1.f };
                float largest = 0.f;
                for (float a : alpha)
                    largest = std::max(largest, cleanAlpha(a));
                if (largest < WeatherScale::kMinExtraLayerAlpha)
                    continue;
                openeden::sdk::game::CloudLayerDescription layer;
                layer.texture = l.mTexture;
                layer.alpha = { cleanAlpha(alpha[0]), cleanAlpha(alpha[1]), cleanAlpha(alpha[2]), cleanAlpha(alpha[3]) };
                if (l.mColour)
                {
                    const auto c = [&](std::size_t t)
                    {
                        const Rgba8 v = (*l.mColour)[t];
                        return openeden::sdk::game::WeatherColour{ v.r / 255.f, v.g / 255.f, v.b / 255.f };
                    };
                    layer.tint = { c(WeatherConstants::Sunrise), c(WeatherConstants::Day), c(WeatherConstants::Sunset),
                        c(WeatherConstants::Night) };
                }
                else
                    layer.tint = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } };
                layer.speed = cloudSpeedOf(w, i);
                taken.push_back(layer.texture);
                d.extraCloudLayers.push_back(std::move(layer));
            }
        }

        // IMSP gives four image spaces in the time-of-day order (VERIFIED by their editor ids: DAWN, DAY, DUSK,
        // NIGHT). A missing record or one without CNAM is neutral for that time.
        void addImageSpace(
            const WeatherRecord& w, const ImageSpaceLookup& lookup, openeden::sdk::game::WeatherDescription& d)
        {
            if (!lookup || !w.mHasImageSpaces)
                return;
            openeden::sdk::game::ImageSpaceGrade grades[4];
            bool any = false;
            for (std::size_t t = 0; t < 4; ++t)
            {
                const ImageSpaceRecord* r = w.mImageSpaces[t] != kNullFormId ? lookup(w.mImageSpaces[t]) : nullptr;
                if (r == nullptr || !r->mCinematic)
                    continue;
                grades[t].saturation = r->mCinematic->mSaturation;
                grades[t].brightness = r->mCinematic->mBrightness;
                grades[t].contrast = r->mCinematic->mContrast;
                if (r->mTint)
                {
                    grades[t].tintAmount = std::clamp(r->mTint->mAmount, 0.f, 1.f);
                    grades[t].tint = { r->mTint->mR, r->mTint->mG, r->mTint->mB };
                }
                any = true;
            }
            if (!any)
                return;
            d.hasImageSpace = true;
            d.imageSpace = { grades[0], grades[1], grades[2], grades[3] };
        }
    }

    std::string normalizeSoundPath(std::string_view stored)
    {
        std::string path;
        path.reserve(stored.size() + 6);
        for (const char c : stored)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7f || c == ':')
                return {}; // a control character or a drive letter
            path.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(u)));
        }
        // Components: no empty one but the first, no "." or "..", a file name last.
        std::vector<std::string> parts;
        std::size_t begin = 0;
        while (begin <= path.size())
        {
            const std::size_t end = path.find('/', begin);
            std::string part = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            while (!part.empty() && part.back() == ' ')
                part.pop_back();
            while (!part.empty() && part.front() == ' ')
                part.erase(part.begin());
            if (part == "." || part == "..")
                return {};
            if (!part.empty())
                parts.push_back(std::move(part));
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        if (parts.empty() || path.back() == '/')
            return {};
        // "data/sound/..." and "sound/..." name the sound root; anything else is relative to it.
        std::size_t skip = 0;
        if (parts.size() > 1 && parts[0] == "data")
            skip = 1;
        if (parts.size() > skip + 1 && parts[skip] == "sound")
            ++skip;
        std::string out = "sound";
        for (std::size_t i = skip; i < parts.size(); ++i)
        {
            out.push_back('/');
            out += parts[i];
        }
        return parts.size() > skip ? out : std::string();
    }

    float soundGain(std::optional<std::uint16_t> attenuation)
    {
        if (!attenuation || *attenuation == 0)
            return 1.f;
        const float decibels = static_cast<float>(*attenuation) / 100.f;
        return std::max(SoundScale::kMinGain, std::pow(10.f, -decibels / 20.f));
    }

    namespace
    {
        // The four SNAM types of a WTHR (VERIFIED by what each descriptor holds in the installed data, task log).
        void addSounds(const WeatherRecord& w, const SoundLookup& lookup, const SoundProbe& exists,
            openeden::sdk::game::WeatherDescription& d)
        {
            if (!lookup)
                return;
            openeden::sdk::game::WeatherSoundDescription* slots[4]
                = { &d.ambientSound, &d.precipitationSound, &d.windSound, &d.thunderSound };
            bool taken[4] = {};
            for (const WeatherSound& entry : w.mSounds)
            {
                if (entry.mType > 3 || entry.mSound == kNullFormId || entry.mSound == kUnresolvedFormId)
                    continue;
                const SoundDescriptorRecord* record = lookup(entry.mSound);
                if (record == nullptr)
                    continue;
                openeden::sdk::game::WeatherSoundDescription& slot = *slots[entry.mType];
                for (const std::string& stored : record->mFiles)
                {
                    if (slot.files.size() >= openeden::sdk::game::kMaxWeatherSoundFiles)
                        break;
                    const std::string path = normalizeSoundPath(stored);
                    if (path.empty() || (exists && !exists(path))
                        || std::find(slot.files.begin(), slot.files.end(), path) != slot.files.end())
                        continue;
                    slot.files.push_back(path);
                }
                // The first entry that gave a file names the sound and sets its volume and loop.
                if (!taken[entry.mType] && !slot.files.empty())
                {
                    taken[entry.mType] = true;
                    slot.name = record->mCommon.mEditorId;
                    slot.volume = soundGain(record->mAttenuation);
                    slot.loop = record->mLoop;
                }
            }
        }
    }

    openeden::sdk::game::WeatherDescription describeWeather(const WeatherRecord& w, const TextureProbe& textureExists,
        const ImageSpaceLookup& imageSpaces, const SoundLookup& sounds, const SoundProbe& soundExists)
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
        addExtraLayers(w, picture, textureExists, d);
        addImageSpace(w, imageSpaces, d);
        addSounds(w, sounds, soundExists, d);
        d.cloudsMaximumPercent = 1.f;
        d.storm = false;
        return d;
    }

    bool describeTimeOfDay(const WeatherIndex& index, openeden::sdk::game::TimeOfDayDescription& out,
        const TextureProbe& textureExists)
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
        // The sky pictures and moons of the climate that gave the hours (the lowest form id of the winning timing).
        if (const ClimateRecord* c = index.climate(bestId))
        {
            const auto usable
                = [&](const std::string& path) { return !path.empty() && (!textureExists || textureExists(path)); };
            if (usable(c->mSunTexture))
                out.sunTexture = c->mSunTexture;
            if (usable(c->mSunGlareTexture))
                out.sunGlareTexture = c->mSunGlareTexture;
            if (c->mTiming)
            {
                out.masser = c->mTiming->hasMasser();
                out.secunda = c->mTiming->hasSecunda();
            }
        }
        return true;
    }

    WeatherTable::WeatherTable(const WeatherIndex& index, const TextureProbe& textureExists, const SoundProbe& soundExists)
        : mIndex(&index)
        , mIds(index.weatherIds())
    {
        mWeathers.reserve(mIds.size());
        for (const FormId id : mIds)
        {
            const WeatherRecord* w = index.weather(id);
            mWeathers.push_back(w != nullptr ? describeWeather(*w, textureExists, [this](FormId id) { return mIndex->imageSpace(id); },
                                             [this](FormId id) { return mIndex->sound(id); }, soundExists) : openeden::sdk::game::WeatherDescription{});
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
