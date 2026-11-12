#ifndef OPENMW_MWWORLD_NEUTRALWEATHER_H
#define OPENMW_MWWORLD_NEUTRALWEATHER_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <openeden/game/weather_source.hpp>

#include "../mwrender/weatherdata.hpp"
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
        inline constexpr float kRainSpeed = 575.f; ///< Morrowind's Weather_Precip_Gravity (the rain fall speed)
        inline constexpr float kMinimumDuration = 1.f / 60.f; ///< hours; a phase never lasts less than a minute
        inline constexpr float kMinimumCloudsMaximumPercent = 0.01f; ///< it divides the transition ratio

        // Stage W-S2. The game's particle data (SPGD) is not decoded yet (toddhow, 2026-11-12: later), so the engine's
        // own rain, snow and lightning are drawn with Morrowind's numbers (files/openmw.cfg, Weather_Rain_* and
        // Weather_Thunderstorm_*, whose engine code these are). Each is a named stand-in, pinned by a test.
        inline constexpr float kRainEntranceSpeed = 7.f; ///< Weather_Rain_Rain_Entrance_Speed
        inline constexpr int kRainMaxRaindrops = 450; ///< Weather_Rain_Max_Raindrops
        inline constexpr float kStormEntranceSpeed = 5.f; ///< Weather_Thunderstorm_Rain_Entrance_Speed
        inline constexpr int kStormMaxRaindrops = 650; ///< Weather_Thunderstorm_Max_Raindrops
        inline constexpr float kRainDiameter = 600.f; ///< Weather_Rain_Rain_Diameter (also the thunderstorm's)
        inline constexpr float kRainMinHeight = 200.f; ///< Weather_Rain_Rain_Height_Min
        inline constexpr float kRainMaxHeight = 700.f; ///< Weather_Rain_Rain_Height_Max
        inline constexpr float kFlashDecrement = 4.f; ///< Weather_Thunderstorm_Flash_Decrement
        /// A snow weather with at least this wind (the SDK's scale) is a blizzard (Skyrim's snow storm has wind byte
        /// 178 = 0.7, its plain snow 76 = 0.3). GUESS.
        inline constexpr float kBlizzardWind = 0.5f;
        /// The manager takes a precipitation or thunder share of a transition between these (the manager divides by
        /// one minus the threshold and treats 0 as "unset").
        inline constexpr float kThresholdMin = 0.1f;
        inline constexpr float kThresholdMax = 0.9f;
        inline constexpr float kThresholdDefault = 0.5f;
    }

    /// `maxExtraCloudLayers`: how many of the description's extra cloud layers are kept (stage W-S3; 0 keeps none, the
    /// single picture of W-S1). The image space is always carried; the manager applies it only when asked to.
    [[nodiscard]] WeatherEntry weatherEntryFromDescription(
        const openeden::sdk::game::WeatherDescription& weather, std::size_t maxExtraCloudLayers = 0);

    /// During a transition the layers of the weather that is going fade with the first picture's blend (`blend` 0 to 1:
    /// 0 is all the current weather's), and the next one's fade in. The result holds at most
    /// MWRender::kMaxExtraCloudLayers, the current weather's first. `out` is cleared first.
    void blendExtraCloudLayers(const std::vector<MWRender::ExtraCloudLayer>& current,
        const std::vector<MWRender::ExtraCloudLayer>& next, float blend, std::vector<MWRender::ExtraCloudLayer>& out);

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

    /// The index in `weathers` of the weather named `name` (case-insensitive), or -1. A developer aid:
    /// OPENEDEN_NEUTRAL_FORCE_WEATHER=<editor id> makes the weather manager start the player's region in that weather
    /// (docs/debug-environment-variables.md).
    [[nodiscard]] int findWeatherByName(const std::vector<WeatherEntry>& weathers, const std::string& name);

    /// The setup for a pack's source. False with `error` when the pack describes no weather.
    [[nodiscard]] bool makeNeutralWeatherSetup(
        openeden::sdk::game::WeatherSource& source, NeutralWeatherSetup& out, std::string& error);

    /// The setup for the session, or null: needs `[Game] neutral world` and `[Game] neutral weather` on and a
    /// registered source of a game pack that is a WeatherSource. Logs why not.
    [[nodiscard]] std::unique_ptr<NeutralWeatherSetup> makeSessionNeutralWeather();
}

#endif
