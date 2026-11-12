#ifndef OPENMW_MWWORLD_NEUTRALWEATHER_H
#define OPENMW_MWWORLD_NEUTRALWEATHER_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <openeden/game/weather_source.hpp>

#include "weatherparams.hpp"

// A game pack's weather in the engine's weather manager (OpenEden task 0127, stage W-S1; toddhow's answers of
// 2026-11-12). The pack describes its weathers, its sun hours and the weather list of a cell through
// openeden::sdk::game::WeatherSource; this file turns that into the same `WeatherParams`, `WeatherEntry` and
// `WeatherManagerParams` the Morrowind path fills from the INI keys, so `WeatherManager` runs one code for both.
// Only with `[Game] neutral world` and `[Game] neutral weather`; Morrowind and every other session are untouched.

namespace MWWorld
{
    /// The clock of a pack's weather when the data gives none, and the constants the conversions below add. GUESS
    /// values (nothing in the data settles them), each pinned by a test.
    namespace NeutralWeatherConstants
    {
        inline constexpr float kHoursBetweenWeatherChanges = 12.f;
        inline constexpr float kRainSpeed = 575.f; ///< Morrowind's Weather_Precip_Gravity: no precipitation is shown yet
        inline constexpr float kMinimumDuration = 1.f / 60.f; ///< hours; a phase never lasts less than a minute
        inline constexpr float kMinimumCloudsMaximumPercent = 0.01f; ///< it divides the transition ratio
    }

    [[nodiscard]] WeatherEntry weatherEntryFromDescription(const openeden::sdk::game::WeatherDescription& weather);

    /// `timing` null: the default hours (6 to 8 and 18 to 20, Morrowind's).
    [[nodiscard]] WeatherManagerParams neutralManagerParams(const openeden::sdk::game::TimeOfDayDescription* timing);

    /// A weather list as the manager's chances: one byte per table entry, summing to exactly 100 (the manager's roll
    /// takes 1..100; Skyrim's lists are weights and one adds to 85). The largest weight takes the rounding remainder
    /// (ties: the lowest index). Entries outside the table are ignored; an all-zero list gives all zero.
    [[nodiscard]] std::vector<std::uint8_t> normalizedChances(
        const openeden::sdk::game::CellWeatherList& list, std::size_t tableSize);

    /// The regions of a pack: a cell's weather list becomes a region keyed by the list's key. Main thread only.
    class NeutralRegionProvider final : public RegionWeatherProvider
    {
    public:
        NeutralRegionProvider(openeden::sdk::game::WeatherSource& source, std::size_t tableSize);

        ESM::RefId regionOf(const CellStore& cell) override;
        bool chancesOf(const ESM::RefId& region, std::vector<std::uint8_t>& chances) override;

    private:
        openeden::sdk::game::WeatherSource& mSource;
        std::size_t mTableSize;
        std::map<std::uint64_t, ESM::RefId> mCellKeys; ///< cell id -> region key ("" id: none)
        std::map<ESM::RefId, std::vector<std::uint8_t>> mChances;
    };

    /// The setup for a pack's source. False with `error` when the pack describes no weather.
    [[nodiscard]] bool makeNeutralWeatherSetup(
        openeden::sdk::game::WeatherSource& source, NeutralWeatherSetup& out, std::string& error);

    /// The setup for the session, or null: needs `[Game] neutral world` and `[Game] neutral weather` on and a
    /// registered source of a game pack that is a WeatherSource. Logs why not.
    [[nodiscard]] std::unique_ptr<NeutralWeatherSetup> makeSessionNeutralWeather();
}

#endif
