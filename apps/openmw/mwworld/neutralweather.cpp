#include "neutralweather.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"

#include <components/worldmodel/store.hpp>

#include "cellstore.hpp"

namespace MWWorld
{
    namespace
    {
        vsg::vec4 colour(const openeden::sdk::game::WeatherColour& c)
        {
            return vsg::vec4(c.r, c.g, c.b, 1.f);
        }

        std::array<vsg::vec4, 4> colours(const openeden::sdk::game::PerTimeOfDay<openeden::sdk::game::WeatherColour>& t)
        {
            return { colour(t.sunrise), colour(t.day), colour(t.sunset), colour(t.night) };
        }

        std::string hex(std::uint64_t value)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out;
            do
            {
                out.insert(out.begin(), digits[value & 0xf]);
                value >>= 4;
            } while (value != 0);
            return out;
        }
    }

    namespace
    {
        float clampShare(float value, float low, float high)
        {
            if (!std::isfinite(value))
                return NeutralWeatherConstants::kThresholdDefault;
            return std::clamp(value, low, high);
        }

        void applyPrecipitation(const openeden::sdk::game::WeatherDescription& w, WeatherEntry& entry)
        {
            using openeden::sdk::game::Precipitation;
            WeatherParams& p = entry.params;
            if (w.precipitation == Precipitation::None)
                return;
            p.rainThreshold = clampShare(w.precipitationThreshold, NeutralWeatherConstants::kThresholdMin,
                NeutralWeatherConstants::kThresholdMax);
            if (w.precipitation == Precipitation::Rain)
            {
                // The manager draws the rain when `usingPrecip` is set; a thunder storm gets the thunderstorm's density.
                const bool storm = w.thunderFrequency > 0.f && std::isfinite(w.thunderFrequency);
                p.usingPrecip = true;
                p.rainEntranceSpeed
                    = storm ? NeutralWeatherConstants::kStormEntranceSpeed : NeutralWeatherConstants::kRainEntranceSpeed;
                p.rainMaxRaindrops
                    = storm ? NeutralWeatherConstants::kStormMaxRaindrops : NeutralWeatherConstants::kRainMaxRaindrops;
                p.rainDiameter = NeutralWeatherConstants::kRainDiameter;
                p.rainMinHeight = NeutralWeatherConstants::kRainMinHeight;
                p.rainMaxHeight = NeutralWeatherConstants::kRainMaxHeight;
                // No rain sound yet: "None" is how the INI says "no loop" (the manager would default to "rain").
                p.rainLoopSoundId = "None";
            }
            else
            {
                const auto& models = Settings::models();
                const bool blizzard = w.windSpeed >= NeutralWeatherConstants::kBlizzardWind;
                entry.particleEffect = blizzard ? models.mWeatherblizzard.get().value() : models.mWeathersnow.get().value();
            }
        }

        void applyThunder(const openeden::sdk::game::WeatherDescription& w, WeatherEntry& entry)
        {
            if (!(w.thunderFrequency > 0.f) || !std::isfinite(w.thunderFrequency))
                return;
            WeatherParams& p = entry.params;
            p.thunderFrequency = w.thunderFrequency;
            p.thunderThreshold = std::clamp(std::isfinite(w.thunderThreshold) ? w.thunderThreshold : 0.f, 0.f,
                NeutralWeatherConstants::kThresholdMax);
            p.flashDecrement = NeutralWeatherConstants::kFlashDecrement;
        }
    }

    namespace
    {
        // Stage W-S3. The layers keep the pack's order; those past the cap, past the SDK's maximum, or with no picture
        // are dropped. Alpha and tint are cleaned of values that are not finite.
        void applyCloudLayersAndImageSpace(const openeden::sdk::game::WeatherDescription& w,
            std::size_t maxExtraCloudLayers, WeatherParams& p)
        {
            const std::size_t cap = std::min(maxExtraCloudLayers, openeden::sdk::game::kMaxExtraCloudLayers);
            const auto alpha = [](float a) { return std::isfinite(a) ? std::clamp(a, 0.f, 1.f) : 0.f; };
            for (const openeden::sdk::game::CloudLayerDescription& l : w.extraCloudLayers)
            {
                if (p.extraClouds.size() >= cap)
                    break;
                if (l.texture.empty())
                    continue;
                ExtraCloudParams e;
                e.texture = l.texture;
                e.alpha = { alpha(l.alpha.sunrise), alpha(l.alpha.day), alpha(l.alpha.sunset), alpha(l.alpha.night) };
                e.tint = colours(l.tint);
                e.speed = std::isfinite(l.speed) ? std::max(l.speed, 0.f) : 0.f;
                p.extraClouds.push_back(std::move(e));
            }
            if (w.hasImageSpace)
            {
                const auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
                ImageSpaceParams g;
                const openeden::sdk::game::ImageSpaceGrade* in[4]
                    = { &w.imageSpace.sunrise, &w.imageSpace.day, &w.imageSpace.sunset, &w.imageSpace.night };
                for (std::size_t t = 0; t < 4; ++t)
                {
                    g.grade[t] = vsg::vec4(std::max(finite(in[t]->brightness, 1.f), 0.f),
                        std::max(finite(in[t]->saturation, 1.f), 0.f), std::max(finite(in[t]->contrast, 1.f), 0.f),
                        std::clamp(finite(in[t]->tintAmount, 0.f), 0.f, 1.f));
                    g.tint[t] = colour(in[t]->tint);
                }
                p.imageSpace = g;
            }
        }
    }

    namespace
    {
        // Stage W-S4: the pack's sounds as the weather's slots. Files are kept in order up to the SDK's maximum, empty
        // names are dropped, the volume is cleaned to 0..1 (a value that is not a number is the full volume).
        NeutralWeatherSound::Slot soundSlot(const openeden::sdk::game::WeatherSoundDescription& s)
        {
            NeutralWeatherSound::Slot slot;
            for (const std::string& file : s.files)
            {
                if (slot.files.size() >= openeden::sdk::game::kMaxWeatherSoundFiles)
                    break;
                if (!file.empty())
                    slot.files.push_back(file);
            }
            if (slot.files.empty())
                return slot;
            slot.name = s.name;
            slot.volume = std::isfinite(s.volume) ? std::clamp(s.volume, 0.f, 1.f) : 1.f;
            slot.loop = s.loop;
            return slot;
        }
    }

    int findWeatherByName(const std::vector<WeatherEntry>& weathers, const std::string& name)
    {
        const auto lower = [](std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        const std::string wanted = lower(name);
        for (std::size_t i = 0; i < weathers.size(); ++i)
            if (lower(weathers[i].name) == wanted)
                return static_cast<int>(i);
        return -1;
    }

    WeatherEntry weatherEntryFromDescription(const openeden::sdk::game::WeatherDescription& w, std::size_t maxExtraCloudLayers)
    {
        WeatherEntry entry;
        entry.name = !w.editorId.empty() ? w.editorId : "weather" + hex(w.id);
        entry.dlFactor = 1.f;
        entry.dlOffset = 0.f;
        entry.forceStorm = w.storm;

        WeatherParams& p = entry.params;
        p.cloudTexture = w.cloudTexture;
        p.sky = colours(w.sky);
        p.fog = colours(w.fog);
        p.ambient = colours(w.ambient);
        p.sun = colours(w.sun);
        p.landFogDepth = { w.fogDepth.sunrise, w.fogDepth.day, w.fogDepth.sunset, w.fogDepth.night };
        p.sunDiscSunsetColor = colour(w.sunDiscSunset);
        p.windSpeed = w.windSpeed;
        p.cloudSpeed = w.cloudSpeed;
        p.glareView = w.glareView;
        p.cloudsMaximumPercent
            = std::max(w.cloudsMaximumPercent, NeutralWeatherConstants::kMinimumCloudsMaximumPercent);
        p.transitionDelta = w.transitionDelta;
        applyPrecipitation(w, entry);
        applyThunder(w, entry);
        applyCloudLayersAndImageSpace(w, maxExtraCloudLayers, p);
        p.sounds.ambient = soundSlot(w.ambientSound);
        p.sounds.precipitation = soundSlot(w.precipitationSound);
        p.sounds.wind = soundSlot(w.windSound);
        p.sounds.thunder = soundSlot(w.thunderSound);
        // The sounds are files (WeatherParams::sounds, stage W-S4); the INI-style loop sound ids stay empty or "None".
        // The game's own particle data waits for a later stage.
        return entry;
    }

    void blendExtraCloudLayers(const std::vector<MWRender::ExtraCloudLayer>& current,
        const std::vector<MWRender::ExtraCloudLayer>& next, float blend, std::vector<MWRender::ExtraCloudLayer>& out)
    {
        blend = std::isfinite(blend) ? std::clamp(blend, 0.f, 1.f) : 0.f;
        out.clear();
        for (const MWRender::ExtraCloudLayer& layer : current)
        {
            if (out.size() >= MWRender::kMaxExtraCloudLayers)
                return;
            out.push_back(layer);
            out.back().mAlpha *= 1.f - blend;
        }
        for (const MWRender::ExtraCloudLayer& layer : next)
        {
            if (out.size() >= MWRender::kMaxExtraCloudLayers)
                return;
            out.push_back(layer);
            out.back().mAlpha *= blend;
        }
    }

    WeatherManagerParams neutralManagerParams(const openeden::sdk::game::TimeOfDayDescription* timing)
    {
        WeatherManagerParams out;
        const float minimum = NeutralWeatherConstants::kMinimumDuration;
        if (timing != nullptr)
        {
            out.sunriseTime = timing->sunriseBegin;
            out.sunriseDuration = std::max(timing->sunriseEnd - timing->sunriseBegin, minimum);
            out.sunsetTime = timing->sunsetBegin;
            out.sunsetDuration = std::max(timing->sunsetEnd - timing->sunsetBegin, minimum);
        }
        else
        {
            out.sunriseTime = 6.f;
            out.sunriseDuration = 2.f;
            out.sunsetTime = 18.f;
            out.sunsetDuration = 2.f;
        }
        out.sunPreSunsetTime = 0.f;
        if (timing != nullptr)
        {
            out.sunTexture = timing->sunTexture;
            out.sunGlareTexture = timing->sunGlareTexture;
            out.masser = timing->masser;
            out.secunda = timing->secunda;
        }
        out.hoursBetweenWeatherChanges = NeutralWeatherConstants::kHoursBetweenWeatherChanges;
        out.rainSpeed = NeutralWeatherConstants::kRainSpeed;
        // Morrowind's underwater fog values: the pack has no water fog source yet.
        out.underwaterFog = { 3.f, 2.5f, 3.f, 4.f };
        // The colour tables blend linearly from night to sunrise to day over the pack's own sunrise window, and the
        // same at sunset: no extra pre/post windows. UNVERIFIED against the game's own sky (task 0127, question 3).
        for (const char* type : { "Sky", "Ambient", "Fog", "Sun" })
            out.transitions[type] = { 0.f, 0.f, 0.f, 0.f };
        // The stars fade as Morrowind's do.
        out.starsPostSunsetStart = 1.f;
        out.starsPreSunriseFinish = 2.f;
        out.starsFadingDuration = 2.f;
        return out;
    }

    std::vector<std::uint8_t> normalizedChances(const openeden::sdk::game::CellWeatherList& list, std::size_t tableSize)
    {
        std::vector<std::uint8_t> out(tableSize, 0);
        std::vector<std::uint64_t> weights(tableSize, 0);
        std::uint64_t total = 0;
        for (const openeden::sdk::game::WeatherChanceEntry& e : list.entries)
        {
            if (e.weather >= tableSize)
                continue;
            weights[e.weather] += e.chance;
            total += e.chance;
        }
        if (total == 0)
            return out;
        // Round down, then give the remainder (at most one per entry) to the largest weight; a tie goes to the lowest
        // index. The sum is then exactly 100.
        unsigned sum = 0;
        std::size_t largest = 0;
        for (std::size_t i = 0; i < tableSize; ++i)
        {
            out[i] = static_cast<std::uint8_t>(weights[i] * 100 / total);
            sum += out[i];
            if (weights[i] > weights[largest])
                largest = i;
        }
        out[largest] = static_cast<std::uint8_t>(out[largest] + (100 - sum));
        return out;
    }

    NeutralRegionProvider::NeutralRegionProvider(openeden::sdk::game::WeatherSource& source, std::size_t tableSize)
        : mSource(source)
        , mTableSize(tableSize)
    {
    }

    ESM::RefId NeutralRegionProvider::regionOf(const CellStore& cell)
    {
        const openeden::sdk::game::WorldCell* neutral = cell.neutralCell();
        // Morrowind's interiors have no weather region, and an interior's weather is never asked for.
        if (neutral == nullptr || neutral->interior)
            return ESM::RefId();
        const auto known = mCellKeys.find(neutral->id);
        if (known != mCellKeys.end())
            return known->second;

        ESM::RefId key;
        openeden::sdk::game::CellWeatherList list;
        if (mSource.weatherListForCell(neutral->id, list))
        {
            std::vector<std::uint8_t> chances = normalizedChances(list, mTableSize);
            if (std::any_of(chances.begin(), chances.end(), [](std::uint8_t c) { return c != 0; }))
            {
                key = ESM::RefId::stringRefId(cell.neutralGameId() + ":weather:" + hex(list.key));
                if (mChances.find(key) == mChances.end())
                {
                    Log(Debug::Info) << "Neutral weather: region " << key.toDebugString() << " (first asked by cell "
                                     << neutral->editorId << ") has " << std::count_if(chances.begin(), chances.end(),
                                            [](std::uint8_t c) { return c != 0; })
                                     << " weathers";
                    mChances.emplace(key, std::move(chances));
                }
            }
        }
        mCellKeys.emplace(neutral->id, key);
        return key;
    }

    bool NeutralRegionProvider::chancesOf(const ESM::RefId& region, std::vector<std::uint8_t>& chances)
    {
        const auto it = mChances.find(region);
        if (it == mChances.end())
            return false;
        chances = it->second;
        return true;
    }

    bool makeNeutralWeatherSetup(
        openeden::sdk::game::WeatherSource& source, NeutralWeatherSetup& out, std::string& error)
    {
        std::vector<openeden::sdk::game::WeatherDescription> weathers;
        if (!source.describeWeathers(weathers, error))
            return false;
        if (weathers.empty())
        {
            error = "the game pack describes no weather";
            return false;
        }
        openeden::sdk::game::TimeOfDayDescription timing;
        const bool hasTiming = source.describeTimeOfDay(timing);
        out = {};
        out.manager = neutralManagerParams(hasTiming ? &timing : nullptr);
        out.weathers.reserve(weathers.size());
        const std::size_t layers = static_cast<std::size_t>(std::clamp(Settings::game().mNeutralWeatherCloudLayers.get(), 0, 8));
        for (const openeden::sdk::game::WeatherDescription& w : weathers)
            out.weathers.push_back(weatherEntryFromDescription(w, layers));
        out.regions = std::make_shared<NeutralRegionProvider>(source, out.weathers.size());
        return true;
    }

    std::unique_ptr<NeutralWeatherSetup> makeSessionNeutralWeather()
    {
        if (!Settings::game().mNeutralWorld.get() || !Settings::game().mNeutralWeather.get())
            return nullptr;
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        if (store == nullptr)
        {
            Log(Debug::Warning) << "Neutral weather: no neutral world store; the weather stays the engine's";
            return nullptr;
        }
        for (const std::string& gameId : store->gameIds())
        {
            auto* source = dynamic_cast<openeden::sdk::game::WeatherSource*>(store->source(gameId));
            if (source == nullptr)
                continue;
            auto setup = std::make_unique<NeutralWeatherSetup>();
            std::string error;
            if (!makeNeutralWeatherSetup(*source, *setup, error))
            {
                Log(Debug::Warning) << "Neutral weather: " << gameId << " gives no weather: " << error;
                return nullptr;
            }
            Log(Debug::Info) << "Neutral weather: " << setup->weathers.size() << " weathers from " << gameId
                             << ", sunrise " << setup->manager.sunriseTime << " h for "
                             << setup->manager.sunriseDuration << " h, sunset " << setup->manager.sunsetTime
                             << " h for " << setup->manager.sunsetDuration << " h";
            return setup;
        }
        Log(Debug::Warning) << "Neutral weather: no game pack describes weather; the weather stays the engine's";
        return nullptr;
    }
}
