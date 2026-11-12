#ifndef OPENEDEN_GAME_SKYRIMSE_WEATHER_H
#define OPENEDEN_GAME_SKYRIMSE_WEATHER_H

// Skyrim SE weather, climate and the links that choose them, as plain data (task 0127, TBD-E of
// docs/design/skyrim-se-implementation-plan.md section 8, milestone M-S2): WTHR (a weather), CLMT (a climate), REGN
// (the weather list of a region), the worldspace's climate link (CNAM, already decoded by worldspace.hpp) and the
// cell-side links XCLR (regions), XCCM (climate), XCWT (water) and XCIM (image space). Pack-own types only: no SDK type,
// no engine, renderer, Jolt or Qt type. Nothing here is wired into the engine (the engine half is a proposal in the
// task spec).
//
// Layout status of every field is marked in weather.cpp: VERIFIED (sizes, ranges and the record type each reference
// resolves to, measured on the five installed Skyrim SE masters by tools/skyrimse_weather_check; aggregates in the task
// log) or RECALLED-VERIFY (a meaning from memory of the Creation Kit wiki / xEdit definitions that the data does not
// settle). Same hostile-input discipline as land.hpp: every size is checked against the bytes that remain and against a
// cap before anything is allocated, nothing throws, a bad record is a located error and the walk goes on.

#include "game_skyrimse/worldspace.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace WeatherConstants
    {
        /// Times of day in every colour table, in this order (VERIFIED by plausibility on the 165 full weathers: the
        /// stars and the moon glare are brightest in slot 3, the sunlight in slot 1).
        inline constexpr std::uint32_t kTimesOfDay = 4;
        enum TimeOfDay : std::uint32_t
        {
            Sunrise = 0,
            Day = 1,
            Sunset = 2,
            Night = 3,
        };
        /// Cloud layers are named by the first character of their texture subrecord: '0' is layer 0, then 1..9, ':',
        /// ';' ... up to 'L' = layer 28 in the installed data (VERIFIED: contiguous character codes 0x30 to 0x4C).
        inline constexpr std::uint32_t kMaxCloudLayers = 32;
        /// NAM0 holds up to this many colour types (VERIFIED: 17, 14 or 13 in the installed data).
        inline constexpr std::uint32_t kMaxColourTypes = 17;
    }

    struct Rgba8
    {
        std::uint8_t r = 0, g = 0, b = 0, a = 0;
        friend bool operator==(const Rgba8&, const Rgba8&) = default;
    };

    /// NAM0 colour types, in the order they are stored (type-major, then time of day). The names are RECALLED-VERIFY; the
    /// data supports the order where it can be tested (stars and moon glare brightest at night, fog near and far alike,
    /// sunlight brightest by day).
    namespace SkyColourType
    {
        enum Value : std::uint32_t
        {
            SkyUpper = 0,
            FogNear = 1,
            Unknown2 = 2,
            Ambient = 3,
            Sunlight = 4,
            Sun = 5,
            Stars = 6,
            SkyLower = 7,
            Horizon = 8,
            EffectLighting = 9,
            CloudLodDiffuse = 10,
            CloudLodAmbient = 11,
            FogFar = 12,
            SkyStatics = 13,
            WaterMultiplier = 14,
            SunGlare = 15,
            MoonGlare = 16,
        };
    }

    /// One cloud layer. Slots exist up to the largest of the arrays the record holds (mCloudSlots).
    struct CloudLayer
    {
        std::string mTexture; ///< the NNTX-style subrecord ("Sky\\SkyrimClouds04.dds"); empty when the layer has none
        bool mHasTexture = false;
        /// PNAM: 4 colours of 4 bytes per layer (VERIFIED: 512 bytes = 32 layers; 64 bytes in one old record).
        std::optional<std::array<Rgba8, 4>> mColour;
        /// JNAM: 4 floats per layer, 0 to 1 in the installed data (VERIFIED: 512 bytes).
        std::optional<std::array<float, 4>> mAlpha;
        /// QNAM and RNAM: one byte per layer; 127 means "no movement" in the installed data (the common value). Which
        /// is X and which is Y is RECALLED-VERIFY, so they keep the subrecord names.
        std::optional<std::uint8_t> mSpeedQ;
        std::optional<std::uint8_t> mSpeedR;
    };

    /// DATA, 19 bytes (VERIFIED size in all 177 records). Bytes 1, 2, 15 and 16 are zero in every installed record and
    /// are kept raw in mRaw, not named. The meanings of the others are RECALLED-VERIFY, except where noted.
    struct WeatherData
    {
        std::array<std::uint8_t, 19> mRaw{};
        std::uint8_t mWindSpeed = 0; ///< byte 0, 0 to 178 in the installed data
        std::uint8_t mTransitionDelta = 0; ///< byte 3 (125 in 134 of 177 records)
        std::uint8_t mSunGlare = 0; ///< byte 4
        std::uint8_t mSunDamage = 0; ///< byte 5
        std::uint8_t mPrecipitationBegin = 0; ///< byte 6, fade in
        std::uint8_t mPrecipitationEnd = 0; ///< byte 7, fade out
        std::uint8_t mThunderBegin = 0; ///< byte 8
        std::uint8_t mThunderEnd = 0; ///< byte 9
        std::uint8_t mThunderFrequency = 0; ///< byte 10 (255 in 164 of 177 records)
        std::uint8_t mFlags = 0; ///< byte 11, see WeatherFlags
        std::uint8_t mLightningR = 0, mLightningG = 0, mLightningB = 0; ///< bytes 12 to 14
        std::uint8_t mWindDirection = 0; ///< byte 17, an angle scaled to a byte (43 in 157 of 177)
        std::uint8_t mWindDirectionRange = 0; ///< byte 18
    };

    /// WeatherData::mFlags bits. The installed data uses 0 to 50 (bits 0 to 5 only: VERIFIED); the names are
    /// RECALLED-VERIFY (the data supports them: bit 1 is set on the clear, 2 on the cloudy, 4 on the rainy, 8 on the snowy
    /// weathers the editor ids name).
    namespace WeatherFlags
    {
        inline constexpr std::uint8_t Pleasant = 0x01;
        inline constexpr std::uint8_t Cloudy = 0x02;
        inline constexpr std::uint8_t Rainy = 0x04;
        inline constexpr std::uint8_t Snow = 0x08;
        inline constexpr std::uint8_t AuroraPermanent = 0x10;
        inline constexpr std::uint8_t AuroraFollowsSun = 0x20;
    }

    /// FNAM, 8 floats (VERIFIED size 32 in all records): day near, day far, night near, night far, day power, night
    /// power, day max, night max. The names are RECALLED-VERIFY; the ranges agree (distances 0 to 300,000; the rest 0.2
    /// to 1.0).
    struct WeatherFog
    {
        float mDayNear = 0, mDayFar = 0, mNightNear = 0, mNightFar = 0;
        float mDayPower = 0, mNightPower = 0, mDayMax = 0, mNightMax = 0;
    };

    /// DALC: directional ambient lighting for one time of day. VERIFIED layout (one fresnel float of 1.0 closes the
    /// 32 byte form): six colours of 4 bytes, a specular colour, a float. One installed record has the 24 byte form
    /// (colours only). The direction order of the six is RECALLED-VERIFY (+X, -X, +Y, -Y, +Z, -Z).
    struct DirectionalAmbient
    {
        std::array<Rgba8, 6> mDirections{};
        bool mHasSpecular = false;
        Rgba8 mSpecular;
        float mFresnelPower = 0;
    };

    struct WeatherSound
    {
        FormId mSound = kNullFormId; ///< SNAM, 8 bytes: a SNDR or SOUN (VERIFIED types), kUnresolvedFormId if unresolved
        std::uint32_t mType = 0; ///< 0 to 3 in the installed data (RECALLED-VERIFY: default, precipitation, wind, thunder)
    };

    struct WeatherRecord
    {
        RecordCommon mCommon;
        std::uint32_t mCloudSlots = 0; ///< the number of layer slots any array holds
        std::vector<CloudLayer> mCloudLayers; ///< mCloudSlots entries
        std::optional<std::uint32_t> mLayerCount; ///< LNAM: 29 in 164 records, 4 in 13 (VERIFIED), meaning RECALLED-VERIFY
        /// NAM1 as stored. NOT a "bit set means no texture" mask (tested: 142 of 176 records disagree); meaning unsettled.
        std::optional<std::uint32_t> mNam1;
        std::optional<WeatherData> mData;
        std::optional<WeatherFog> mFog;
        /// NAM0: mColourTypes types of 4 times of day (VERIFIED: size / 16 = 17, 14 or 13); mColours[type][time of day].
        std::uint32_t mColourTypes = 0;
        std::array<std::array<Rgba8, WeatherConstants::kTimesOfDay>, WeatherConstants::kMaxColourTypes> mColours{};
        std::vector<DirectionalAmbient> mDirectionalAmbient; ///< DALC, in file order: sunrise, day, sunset, night
        std::vector<WeatherSound> mSounds;
        std::vector<FormId> mSkyStatics; ///< TNAM, one subrecord each (VERIFIED: STAT, up to several hundred per record)
        FormId mPrecipitation = kNullFormId; ///< MNAM, an SPGD (VERIFIED type), 0 in 141 of 177
        FormId mVisualEffect = kNullFormId; ///< NNAM, an RFCT (VERIFIED type), 0 in 172 of 177
        std::array<FormId, 4> mImageSpaces{}; ///< IMSP, four IMGS in time-of-day order
        bool mHasImageSpaces = false;
        std::array<FormId, 4> mVolumetric{}; ///< HNAM, four VOLI or 0
        bool mHasVolumetric = false;
        std::string mAuroraModel; ///< MODL (RECALLED-VERIFY meaning: an aurora/sky model path)
        /// NAM2, NAM3 (16 bytes) and ONAM (4 bytes) are NOT form ids (VERIFIED: bit patterns, not ids); kept raw and
        /// never resolved. NAM2 and NAM3 occur only in the eleven "FX" weathers.
        std::optional<std::array<std::uint8_t, 16>> mNam2;
        std::optional<std::array<std::uint8_t, 16>> mNam3;
        std::optional<std::array<std::uint8_t, 4>> mOnam;
    };

    /// IMGS (task 0127 stage W-S3): the parts of an image space the engine can use. Sizes VERIFIED on the 268 records of
    /// the five masters that have them: CNAM 12 bytes (three floats), TNAM 16 bytes (four floats), HNAM 36 bytes (nine
    /// floats), DNAM 16 (or 12 in two records). The ORDER of the CNAM floats (saturation, brightness, contrast) is
    /// RECALLED-VERIFY (xEdit's definition); the data supports it: the third value is 1.0 to 2.0 (a contrast never below
    /// 1), the second 0.9 to 1.5 (brightness dims and brightens), the first 0.4 to 2.45. TNAM is amount then a colour
    /// (all four 0 to 1: VERIFIED range). HNAM is kept raw: its meaning (eye adaptation, bloom) needs an auto-exposure
    /// pass the engine does not have.
    struct ImageSpaceRecord
    {
        RecordCommon mCommon;
        struct Cinematic
        {
            float mSaturation = 1.f, mBrightness = 1.f, mContrast = 1.f;
        };
        struct Tint
        {
            float mAmount = 0.f, mR = 0.f, mG = 0.f, mB = 0.f;
        };
        std::optional<Cinematic> mCinematic;
        std::optional<Tint> mTint;
        std::optional<std::array<float, 9>> mHdr;
    };

    /// One entry of a weather list (CLMT WLST, REGN RDWT): VERIFIED 12 bytes, a WTHR, a signed chance and a global
    /// (always 0 in the installed data).
    struct WeatherChance
    {
        FormId mWeather = kNullFormId;
        std::int32_t mChance = 0;
        FormId mGlobal = kNullFormId;
    };

    /// CLMT TNAM, 6 bytes (VERIFIED size in all 9 climates). Bytes 0 to 3 are times in units of ten minutes (33, 60, 96,
    /// 123 are 5:30, 10:00, 16:00, 20:30 in every installed climate that has them: VERIFIED values, RECALLED-VERIFY
    /// that they are sunrise begin and end and sunset begin and end).
    struct ClimateTiming
    {
        std::uint8_t mSunriseBegin = 0, mSunriseEnd = 0, mSunsetBegin = 0, mSunsetEnd = 0;
        std::uint8_t mVolatility = 0; ///< byte 4: 50 or 0 in the installed data
        std::uint8_t mMoonByte = 0; ///< byte 5: 195 (0xC3) in the two climates of the sky with both moons, 3 elsewhere
        [[nodiscard]] unsigned sunriseBeginMinutes() const { return mSunriseBegin * 10u; }
        [[nodiscard]] unsigned sunriseEndMinutes() const { return mSunriseEnd * 10u; }
        [[nodiscard]] unsigned sunsetBeginMinutes() const { return mSunsetBegin * 10u; }
        [[nodiscard]] unsigned sunsetEndMinutes() const { return mSunsetEnd * 10u; }
        /// RECALLED-VERIFY: the top two bits are Masser and Secunda, the low six the phase length in days.
        [[nodiscard]] bool hasMasser() const { return (mMoonByte & 0x40) != 0; }
        [[nodiscard]] bool hasSecunda() const { return (mMoonByte & 0x80) != 0; }
        [[nodiscard]] unsigned phaseLength() const { return mMoonByte & 0x3Fu; }
    };

    struct ClimateRecord
    {
        RecordCommon mCommon;
        std::vector<WeatherChance> mWeathers; ///< WLST
        std::string mSunTexture; ///< FNAM ("Sky\\Sun.dds"); empty when absent
        std::string mSunGlareTexture; ///< GNAM
        std::string mSkyModel; ///< MODL ("Sky\\Stars.nif")
        std::optional<ClimateTiming> mTiming;
    };

    /// A region's weather: RDAT of type 3 (VERIFIED: 91 of the 198 RDAT with data; every RDWT follows one) and the RDWT
    /// that follows it. Only the weather part of a REGN is decoded here.
    struct RegionWeatherRecord
    {
        RecordCommon mCommon;
        FormId mWorld = kNullFormId; ///< WNAM
        bool mHasWeather = false; ///< an RDAT of type 3 was present
        std::uint8_t mFlags = 0; ///< RDAT byte 4 (0 or 1 in the installed data)
        std::uint8_t mPriority = 0; ///< RDAT byte 5 (50 to 100 in the installed data); meaning RECALLED-VERIFY
        std::vector<WeatherChance> mWeathers;
    };

    /// Weather-related links of one CELL. Exterior cells carry XCLR (VERIFIED: 33,441 of the exterior cells, a list of 4
    /// byte region ids, every one a REGN except 30 dangling) and XCWT (a WATR); interior cells carry XCCM (314 cells,
    /// VERIFIED: interior only, every one a REGN, so it is a region to take the sky and weather from, not a climate)
    /// and XCIM (an IMGS, in both kinds).
    struct CellWeatherLinks
    {
        RecordCommon mCommon;
        FormId mWorld = kNullFormId; ///< kNullFormId for an interior cell
        std::optional<CellGrid> mGrid; ///< XCLC
        FormId mWeatherRegion = kNullFormId; ///< XCCM, a REGN (VERIFIED type)
        FormId mWater = kNullFormId; ///< XCWT, a WATR
        FormId mImageSpace = kNullFormId; ///< XCIM, an IMGS
        std::vector<FormId> mRegions; ///< XCLR
    };

    struct WeatherLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxStringBytes = 64 * 1024;
        /// Entries of a list: sounds, sky statics, weather chances, cell regions. The installed data has at most a few
        /// hundred (TNAM) and 7 (RDWT).
        std::uint32_t mMaxEntriesPerList = 4096;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    // ---- the pure decoders: one record's inflated bytes in, a struct out ------------------------------------------
    // Each returns false with `why` set for a record the caps or the framing refuse; a subrecord of a wrong size or
    // with a hostile value is counted in the notes and skipped, not an error.

    [[nodiscard]] bool decodeWeather(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, WeatherRecord& out, std::string& why);
    [[nodiscard]] bool decodeClimate(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, ClimateRecord& out, std::string& why);
    [[nodiscard]] bool decodeImageSpace(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, ImageSpaceRecord& out, std::string& why);
    [[nodiscard]] bool decodeRegionWeather(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, RegionWeatherRecord& out, std::string& why);

    // ---- the walk ---------------------------------------------------------------------------------------------------

    struct WeatherRecords
    {
        std::string mName;
        std::vector<WorldSpaceRecord> mWorlds; ///< decoded by worldspace.hpp's decodeWorldSpace
        std::vector<WeatherRecord> mWeathers;
        std::vector<ClimateRecord> mClimates;
        std::vector<RegionWeatherRecord> mRegions;
        std::vector<CellWeatherLinks> mCells;
        std::vector<ImageSpaceRecord> mImageSpaces; ///< IMGS (task 0127 stage W-S3)
        DecodeNotes mNotes;
        std::map<std::uint32_t, TypeCount> mCounts; ///< by record type: seen, parsed, failed, deleted
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    /// Walks an opened plugin and decodes WTHR, CLMT, REGN, WRLD and the weather links of CELL. `order` must hold the
    /// plugin and its masters. False only when the walk itself failed (mWalkError); records before it are kept.
    [[nodiscard]] bool parseWeatherRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const WeatherLimits& limits, WeatherRecords& out);
    [[nodiscard]] bool parseWeatherRecordsBytes(const std::string& bytes, const std::string& name,
        const LoadOrder& order, const WeatherLimits& limits, WeatherRecords& out, Content::Error& error);
    [[nodiscard]] bool parseWeatherRecordsFile(const std::filesystem::path& path, const LoadOrder& order,
        const WeatherLimits& limits, WeatherRecords& out, Content::Error& error);

    // ---- resolution -------------------------------------------------------------------------------------------------

    /// The records of every plugin of a load order, later plugins overriding earlier ones by form id (whole-record
    /// replacement). Holds copies.
    class WeatherIndex
    {
    public:
        void add(const WeatherRecords& records);

        [[nodiscard]] const WeatherRecord* weather(FormId id) const { return find(mWeathers, id); }
        [[nodiscard]] const ClimateRecord* climate(FormId id) const { return find(mClimates, id); }
        [[nodiscard]] const RegionWeatherRecord* region(FormId id) const { return find(mRegions, id); }
        [[nodiscard]] const WorldSpaceRecord* world(FormId id) const { return find(mWorlds, id); }
        [[nodiscard]] const CellWeatherLinks* cell(FormId id) const { return find(mCells, id); }
        [[nodiscard]] const ImageSpaceRecord* imageSpace(FormId id) const { return find(mImageSpaces, id); }

        [[nodiscard]] std::size_t weatherCount() const { return mWeathers.size(); }
        [[nodiscard]] std::size_t climateCount() const { return mClimates.size(); }
        [[nodiscard]] std::size_t regionCount() const { return mRegions.size(); }

        /// The form ids of all weathers (task 0127 stage W-S1) / climates, ascending: a fixed order whatever the hash
        /// map holds, so a table built from them is the same on every run.
        [[nodiscard]] std::vector<FormId> weatherIds() const;
        [[nodiscard]] std::vector<FormId> climateIds() const;

        /// The climate of a worldspace: its own CNAM, else its parent's while PNAM says the climate is inherited
        /// (ParentUsage::Climate), up to 16 levels (a loop gives nullopt). The inheritance rule is VERIFIED on the
        /// installed data (WhiterunWorld, PNAM 127, takes Tamriel's climate); 42 of the 49 worlds end with a climate.
        [[nodiscard]] std::optional<FormId> climateOfWorld(FormId world) const;

        /// The weather list that applies to a cell, and where it came from. RECALLED-VERIFY rule: the cell's XCCM region
        /// if it has a weather list (interior cells); else, among the regions of the cell (XCLR) that have a weather list,
        /// the one with the highest priority, ties to the earlier region; else the climate of the cell's world. Empty
        /// `mWeathers` when nothing applies.
        struct EffectiveList
        {
            enum class Source
            {
                None,
                Region,
                Climate,
            };
            Source mSource = Source::None;
            FormId mFrom = kNullFormId; ///< the region or climate record
            const std::vector<WeatherChance>* mWeathers = nullptr;
        };
        [[nodiscard]] EffectiveList effectiveWeatherList(const CellWeatherLinks& cell) const;

        /// A weighted pick: negative chances count as 0, the entries' chances are summed (they need not add to 100: one
        /// installed list adds to 85), `roll` in [0, 1) selects by the cumulative share. Deterministic: the caller
        /// supplies the number. nullopt when the list is empty or every chance is 0.
        [[nodiscard]] static std::optional<FormId> pickWeather(const std::vector<WeatherChance>& list, double roll);

    private:
        template <class T>
        static const T* find(const std::unordered_map<FormId, T>& map, FormId id)
        {
            const auto it = map.find(id);
            return it == map.end() ? nullptr : &it->second;
        }

        std::unordered_map<FormId, WeatherRecord> mWeathers;
        std::unordered_map<FormId, ClimateRecord> mClimates;
        std::unordered_map<FormId, RegionWeatherRecord> mRegions;
        std::unordered_map<FormId, WorldSpaceRecord> mWorlds;
        std::unordered_map<FormId, CellWeatherLinks> mCells;
        std::unordered_map<FormId, ImageSpaceRecord> mImageSpaces;
    };
}

#endif
