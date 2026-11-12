#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// What the core needs to run a game's WEATHER and SKY (task 0127, stage W-S1; toddhow's answers of 2026-11-12, option 1
// of the task spec): a table of weathers described as plain values, the hours the sun rises and sets, and the weather
// list of a cell. The engine's weather manager (apps/openmw/mwworld/weather.cpp) is the one place that decides what
// weather is on, transitions, and hands the result to the sky renderer; this interface only supplies its INPUTS.
//
// ABI choice: a NEW interface found by dynamic_cast on the game's registered WorldCellSource (the precedent of
// ActorAppearanceSource in actor_appearance.hpp), so the frozen WorldCellSource and world_model.hpp are untouched. A
// pack that does not implement it leaves the weather as the engine's own (Morrowind's) table. Plain values only: no
// engine, renderer, Jolt, Qt, OSG or MyGUI type.
//
// Units: colours are linear-looking 0..1 floats exactly as the game stores them divided by 255 (no gamma conversion is
// done here); fogDepth is the engine's own fraction (see WeatherDescription::fogDepth); hours are 0..24 floats.
//
// Version policy: DRAFT 0.3 (0.2, task 0127 stage W-S2: precipitation and thunder fields added at the end of
// WeatherDescription, defaults mean "none"; 0.3, stage W-S3: extra cloud layers and the image space grade at the end of
// WeatherDescription, the sun and moon fields at the end of TimeOfDayDescription, defaults mean "none given"). Additive changes (a new trailing field with a default that means "not given", a new
// trailing virtual with a "not supported" default) raise the minor number; anything else raises the major number.
// Threading: main thread only, like WorldCellSource.

namespace openeden::sdk::game
{
    inline constexpr Version kWeatherSourceVersion{ 0, 3 };

    struct WeatherColour
    {
        float r = 0.f, g = 0.f, b = 0.f;
        friend bool operator==(const WeatherColour&, const WeatherColour&) = default;
    };

    /// A value for the four times of day the colour tables of both games use, in this order.
    template <class T>
    struct PerTimeOfDay
    {
        T sunrise{}, day{}, sunset{}, night{};
        friend bool operator==(const PerTimeOfDay&, const PerTimeOfDay&) = default;
    };

    /// What falls from the sky. The engine draws its own rain and snow for these (the particle data of the game is not
    /// decoded yet): Rain is the rain streaks and wets the ground, Snow is the snow effect and builds snow cover.
    enum class Precipitation : std::uint8_t
    {
        None = 0,
        Rain = 1,
        Snow = 2,
    };

    /// One more cloud picture drawn over the first (task 0127 stage W-S3). Alpha and tint are by time of day, as the
    /// game stores them; `speed` is the engine's cloud drift multiplier for this layer.
    struct CloudLayerDescription
    {
        std::string texture;
        PerTimeOfDay<float> alpha;
        PerTimeOfDay<WeatherColour> tint;
        float speed = 0.f;
        friend bool operator==(const CloudLayerDescription&, const CloudLayerDescription&) = default;
    };

    /// The cinematic part of a game's image space for one time of day (task 0127 stage W-S3): neutral values are
    /// saturation 1, brightness 1, contrast 1 and tint amount 0. The names follow the game's fields; the order of
    /// saturation, brightness and contrast in the data is RECALLED-VERIFY (see the Skyrim pack).
    struct ImageSpaceGrade
    {
        float saturation = 1.f;
        float brightness = 1.f;
        float contrast = 1.f;
        float tintAmount = 0.f;
        WeatherColour tint;
        friend bool operator==(const ImageSpaceGrade&, const ImageSpaceGrade&) = default;
    };

    /// The maximum number of extra cloud layers a description may carry.
    inline constexpr std::size_t kMaxExtraCloudLayers = 8;

    struct WeatherDescription
    {
        /// The game's own id of the weather (Skyrim SE: the load-order form id); only used to name it in logs.
        WorldId id = kNoWorldId;
        std::string editorId;
        /// The sky picture, relative to the game's texture root, with the game's own separators ("" for none).
        std::string cloudTexture;

        PerTimeOfDay<WeatherColour> sky, fog, ambient, sun;
        /// The engine's fog depth by time of day: the share of the view distance the fog ramp covers, 0 for no fog (the
        /// engine fogs from viewDistance * (1 - fogDepth) to viewDistance).
        PerTimeOfDay<float> fogDepth;
        /// The colour of the sun disc at sunset.
        WeatherColour sunDiscSunset;

        float windSpeed = 0.f; ///< the engine's scale: about 0 (calm) to 1 (storm)
        float cloudSpeed = 0.f; ///< the engine's cloud drift multiplier
        float glareView = 0.f; ///< 0..1, how clear the air is for the sun's glare
        float transitionDelta = 0.f; ///< fraction of a transition per real second
        float cloudsMaximumPercent = 1.f; ///< the share of a transition after which the clouds have changed
        bool storm = false; ///< an ash or blight style storm (wind direction and walking rules)

        // Added in 0.2. Defaults mean "no precipitation, no thunder".
        Precipitation precipitation = Precipitation::None;
        /// The share of a transition into this weather after which the precipitation has changed over (0..1). The
        /// engine keeps it between 0.1 and 0.9 itself.
        float precipitationThreshold = 0.5f;
        /// Thunder and lightning, in the engine's scale: 1 is about ten strikes a minute at the end of a transition. 0
        /// for none.
        float thunderFrequency = 0.f;
        /// The share of a transition after which the thunder starts (0..1).
        float thunderThreshold = 0.f;

        // Added in 0.3. Defaults mean "no extra layers, no image space".
        /// The cloud layers beyond the picture in `cloudTexture`, in draw order, at most kMaxExtraCloudLayers.
        std::vector<CloudLayerDescription> extraCloudLayers;
        bool hasImageSpace = false;
        PerTimeOfDay<ImageSpaceGrade> imageSpace;

        friend bool operator==(const WeatherDescription&, const WeatherDescription&) = default;
    };

    /// The hours the sun's phases begin and end, from the game's climate.
    struct TimeOfDayDescription
    {
        float sunriseBegin = 6.f, sunriseEnd = 10.f, sunsetBegin = 18.f, sunsetEnd = 22.f;
        // Added in 0.3 (the climate that gave the hours): the sun disc and sun glare pictures, relative to the game's
        // texture root ("" for none given), and which moons the sky has (true when the data does not say).
        std::string sunTexture;
        std::string sunGlareTexture;
        bool masser = true;
        bool secunda = true;
        friend bool operator==(const TimeOfDayDescription&, const TimeOfDayDescription&) = default;
    };

    struct WeatherChanceEntry
    {
        std::uint32_t weather = 0; ///< an index into the table describeWeathers returned
        std::uint32_t chance = 0; ///< a weight; the entries of a list need not add to 100
        friend bool operator==(const WeatherChanceEntry&, const WeatherChanceEntry&) = default;
    };

    /// The weathers a place can have. Two cells with the same `key` have the same list.
    struct CellWeatherList
    {
        WorldId key = kNoWorldId;
        std::vector<WeatherChanceEntry> entries;
        friend bool operator==(const CellWeatherList&, const CellWeatherList&) = default;
    };

    /// Implemented by a WorldCellSource that can describe its weather.
    class WeatherSource
    {
    public:
        virtual ~WeatherSource() = default;

        /// Every weather of the game, in a fixed order (the order is the index the lists refer to). False with `error`
        /// when the data cannot be read or holds no weather.
        [[nodiscard]] virtual bool describeWeathers(std::vector<WeatherDescription>& out, std::string& error) = 0;

        /// The hours of the game's sunrise and sunset. False when the data gives none (the engine keeps its own).
        [[nodiscard]] virtual bool describeTimeOfDay(TimeOfDayDescription& out) = 0;

        /// The weather list that applies to a cell. False when none applies (an interior without a sky region, a
        /// worldspace without a climate). Indices refer to the table of describeWeathers.
        [[nodiscard]] virtual bool weatherListForCell(WorldId cell, CellWeatherList& out) = 0;
    };
}
