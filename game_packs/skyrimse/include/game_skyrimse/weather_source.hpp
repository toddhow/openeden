#ifndef OPENEDEN_GAME_SKYRIMSE_WEATHER_SOURCE_H
#define OPENEDEN_GAME_SKYRIMSE_WEATHER_SOURCE_H

// Skyrim SE weather as the SDK's plain description (task 0127, stage W-S1): turns the pack's decoded WTHR/CLMT/REGN
// records (weather.hpp) into openeden::sdk::game::WeatherDescription values and answers "which weathers can this cell
// have". Pure: no engine type, no file access (the view reads the plugins and builds the WeatherIndex).
//
// Every number that turns a Skyrim value into the engine's scale is a named constant below with how it was chosen. They
// are the FIRST slice's choices, marked RECALLED-VERIFY or GUESS where nothing in the data settles them; a change is a
// deliberate one (each is pinned by a test).

#include "game_skyrimse/weather.hpp"

#include <openeden/game/weather_source.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace WeatherScale
    {
        /// The distance the fog depth is worked out at: the engine's default camera viewing distance ([Camera] viewing
        /// distance 7168). GUESS: the engine fogs linearly to the view distance, Skyrim fogs by pow(distance / far,
        /// power) up to a maximum, so the two cannot agree at every distance; they are made to agree here.
        inline constexpr float kFogReferenceDistance = 7168.f;
        /// The wind byte (0..178 in the data) over this is the engine's wind scale (clear 25 -> 0.098, storm 102 -> 0.4;
        /// Morrowind's clear is about 0.1). GUESS for the unit.
        inline constexpr float kWindByteScale = 255.f;
        /// The sun glare byte over this is the engine's glare view. GUESS for the unit.
        inline constexpr float kGlareByteScale = 255.f;
        /// DATA byte 3 (125 in 134 of 177 records) times this is the fraction of a transition per real second: 125 gives
        /// 0.015, Morrowind's clear-to-cloudy delta, so a transition takes about a minute. GUESS: Skyrim's own rule is
        /// a game-time one.
        inline constexpr float kTransitionDeltaPerUnit = 0.00012f;
        /// QNAM and RNAM (127 means still in the common case): the drift multiplier is the larger distance of a byte from
        /// 127 over 127 times this, but never below the floor so that clouds always move a little. GUESS.
        inline constexpr float kCloudSpeedScale = 2.f;
        inline constexpr float kCloudSpeedFloor = 0.25f;
        inline constexpr std::uint8_t kCloudStillByte = 127;
        /// DATA byte 10 (thunder/lightning frequency) is 255 for "none" and counts down for more thunder (VERIFIED on
        /// the installed data: 164 of 177 weathers have 255 and only stormy ones differ, for example 246 on the stormy
        /// rains, 15 on a magic storm; the unit is RECALLED-VERIFY). The engine's frequency is (255 - byte) times this,
        /// at most kThunderFrequencyMax: 246 gives 0.45, about four and a half strikes a minute in the engine's formula
        /// (Morrowind's thunderstorm has 0.4); a magic storm (15) and a snow storm (203) would give 12 and 2.6, which
        /// the maximum holds at one strike in six seconds. GUESS for the unit.
        inline constexpr std::uint8_t kNoThunderByte = 255;
        inline constexpr float kThunderFrequencyPerStep = 0.05f;
        inline constexpr float kThunderFrequencyMax = 1.f;
        /// Extra cloud layers (task 0127 stage W-S3). A layer is drawn over the picture only when its largest alpha over
        /// the four times of day reaches this (JNAM is 0 to 1 in the data; layers that are never visible are left out).
        inline constexpr float kMinExtraLayerAlpha = 0.05f;
        /// A texture of this name (case-insensitive) is a blank picture the data uses as a placeholder layer (several
        /// installed weathers name Sky\Alpha.dds on layer 2): nothing is drawn for it.
        inline constexpr const char* kBlankCloudTexture = "alpha.dds";
    }

    /// Whether a texture the data names exists (path as stored, "Sky\SkyrimClouds01.dds"). Empty: every texture counts as
    /// existing.
    using TextureProbe = std::function<bool(const std::string&)>;

    /// One weather as the SDK describes it. NAM0 types used: SkyUpper (sky), FogFar (fog), Ambient, Sunlight (sun), Sun
    /// (the disc at sunset); the picture is the first cloud layer with a texture that exists (some weathers name a
    /// leftover Oblivion texture on layer 0 that the game does not ship: the first frame drew the engine's purple
    /// fallback).
    /// The record an IMGS form id names, or null (the index's `imageSpace`).
    using ImageSpaceLookup = std::function<const ImageSpaceRecord*(FormId)>;

    [[nodiscard]] openeden::sdk::game::WeatherDescription describeWeather(const WeatherRecord& weather,
        const TextureProbe& textureExists = {}, const ImageSpaceLookup& imageSpaces = {});

    /// The sun hours of the climate timing most climates share (ties: the lowest climate form id). False when no climate
    /// has a timing.
    /// Also (stage W-S3) the sun and sun glare pictures of that climate when `textureExists` (if given) finds them, and
    /// which moons the climate has.
    [[nodiscard]] bool describeTimeOfDay(const WeatherIndex& index, openeden::sdk::game::TimeOfDayDescription& out,
        const TextureProbe& textureExists = {});

    /// The table of every weather of an index in ascending form id order, and the lists of cells over it. Holds a
    /// pointer to the index, which must outlive it.
    class WeatherTable
    {
    public:
        explicit WeatherTable(const WeatherIndex& index, const TextureProbe& textureExists = {});

        [[nodiscard]] const std::vector<openeden::sdk::game::WeatherDescription>& weathers() const { return mWeathers; }
        /// The table position of a weather form id, or -1.
        [[nodiscard]] int indexOf(FormId weather) const;
        /// The list that applies to a cell (form id). False when none applies or no listed weather is in the table.
        [[nodiscard]] bool listForCell(FormId cell, openeden::sdk::game::CellWeatherList& out) const;

    private:
        const WeatherIndex* mIndex;
        std::vector<FormId> mIds; ///< ascending, parallel to mWeathers
        std::vector<openeden::sdk::game::WeatherDescription> mWeathers;
    };
}

#endif
