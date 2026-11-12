#ifndef OPENMW_MWWORLD_WEATHERPARAMS_H
#define OPENMW_MWWORLD_WEATHERPARAMS_H

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec4.h>

// The plain description of a weather and of the weather manager's clock (OpenEden task 0127, stage W-S1).
//
// OpenMW built every number of `MWWorld::Weather` and `WeatherManager` straight from `Fallback::Map` (Morrowind.ini
// keys). They are now built in two steps: a function fills these structs (`weatherParamsFromFallback`,
// `weatherManagerParamsFromFallback`: Morrowind, exactly the keys and defaults the constructors used), and the
// constructors read the structs. A game pack's neutral weather (mwworld/neutralweather.hpp) fills the same structs from
// the SDK's WeatherDescription, so both games run the same weather code. apps/openmw_tests/mwworld/testweatherparams.cpp
// pins the Fallback path against the values the old constructors read.

namespace MWWorld
{
    class CellStore;

    /// Everything one `Weather` reads from its source of numbers. The colour arrays are in the order sunrise, day,
    /// sunset, night.
    struct WeatherParams
    {
        std::string cloudTexture;
        std::array<vsg::vec4, 4> sky{}, fog{}, ambient{}, sun{};
        std::array<float, 4> landFogDepth{};
        vsg::vec4 sunDiscSunsetColor{ 0.f, 0.f, 0.f, 0.f };
        float windSpeed = 0.f;
        float cloudSpeed = 0.f;
        float glareView = 0.f;
        float rainEntranceSpeed = 0.f;
        int rainMaxRaindrops = 0;
        float rainDiameter = 0.f;
        float rainThreshold = 0.f;
        float rainMinHeight = 0.f;
        float rainMaxHeight = 0.f;
        bool usingPrecip = false;
        float cloudsMaximumPercent = 0.f;
        float transitionDelta = 0.f;
        float thunderFrequency = 0.f;
        float thunderThreshold = 0.f;
        float flashDecrement = 0.f;
        std::array<std::string, 4> thunderSoundId;
        std::string rainLoopSoundId;
        std::string ambientLoopSoundId;
    };

    /// One entry of the weather table: the weather's name (its script-visible id), its numbers and the three values the
    /// manager adds (distant land fog factor and offset, the particle effect model).
    struct WeatherEntry
    {
        std::string name;
        WeatherParams params;
        float dlFactor = 1.f;
        float dlOffset = 0.f;
        std::string particleEffect;
        /// An ash or blight style storm is decided from the wind speed against this (Morrowind: fStromWindSpeed). A
        /// weather whose pack says "storm" sets it below its wind speed; one that does not sets it to infinity.
        bool forceStorm = false;
    };

    /// The manager's clock and the two small tables it keeps (Morrowind: INI keys `Weather_Sunrise_Time` ...).
    struct WeatherManagerParams
    {
        float sunriseTime = 0.f;
        float sunsetTime = 0.f;
        float sunriseDuration = 0.f;
        float sunsetDuration = 0.f;
        float sunPreSunsetTime = 0.f;
        float hoursBetweenWeatherChanges = 0.f;
        float rainSpeed = 0.f;
        std::array<float, 4> underwaterFog{}; ///< sunrise, day, sunset, night
        /// The pre/post sunrise/sunset windows for "Sky", "Ambient", "Fog" and "Sun" (the order of four floats of
        /// `WeatherSetting`: pre-sunrise, post-sunrise, pre-sunset, post-sunset).
        std::map<std::string, std::array<float, 4>> transitions;
        float starsPostSunsetStart = 0.f;
        float starsPreSunriseFinish = 0.f;
        float starsFadingDuration = 0.f;
    };

    /// Morrowind's: the values the old constructors read from `Fallback::Map`.
    WeatherParams weatherParamsFromFallback(const std::string& name);
    WeatherManagerParams weatherManagerParamsFromFallback();

    /// Where the weather manager gets the weather region of a cell from, when the cells carry no `ESM::Region` (a
    /// neutral world). Main thread only.
    class RegionWeatherProvider
    {
    public:
        virtual ~RegionWeatherProvider() = default;

        /// The key of the weather region that applies to the cell, or an empty id when none does. Cells with the same
        /// weather list have the same key.
        virtual ESM::RefId regionOf(const CellStore& cell) = 0;

        /// The chance of each weather of the table (index = table position, the sum 100) for a key `regionOf` gave.
        /// False when the key is unknown.
        virtual bool chancesOf(const ESM::RefId& region, std::vector<std::uint8_t>& chances) = 0;
    };

    /// What `WeatherManager` takes instead of the Morrowind tables: a neutral world's weather (neutralweather.hpp).
    struct NeutralWeatherSetup
    {
        WeatherManagerParams manager;
        std::vector<WeatherEntry> weathers;
        std::shared_ptr<RegionWeatherProvider> regions;
    };
}

#endif
