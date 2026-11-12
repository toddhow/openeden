#include "game_skyrimse/weather.hpp"

#include "exterior_common.hpp"
#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>

// Layout status of every subrecord read here (task 0127). VERIFIED: the five installed Skyrim SE masters (Skyrim.esm,
// Update.esm, Dawnguard.esm, HearthFires.esm, Dragonborn.esm) were walked by tools/skyrimse_weather_check/survey.py and by
// the gated test (aggregates in docs/tasks/0127-skyrim-weather-source.md): sizes, value ranges and the record type each
// reference resolves to. RECALLED-VERIFY: a meaning from memory of the Creation Kit wiki / xEdit definitions that no check
// of ours settled. Nothing is copied from either; the layouts below were found by measuring.
//
// WTHR (177 records in the five masters):
//   EDID zstring; DATA 19 (all 177); FNAM 32 (8 floats, all 177); NAM0 272, 224 or 208 (17, 14, 13 colour types x 4 times of
//   day x 4 bytes: 165, 11, 1 records); PNAM 512 (176) or 64 (1); JNAM 512 (176; absent in 1); QNAM and RNAM 32 (176);
//   LNAM 4 (29 in 164, 4 in 13); NAM1 4 (176); DALC 32 x4 (176 records) or 24 x4 (1); SNAM 8, repeated (55 records, 76 in
//   all: SNDR 67, SOUN 5, 4 dangling); TNAM 4, repeated (150 records, 15,984 in all, every one a STAT); MNAM 4 (SPGD in 36,
//   0 in 141); NNAM 4 (RFCT in 5, 0 in 172); IMSP 16 (four IMGS, all 177); HNAM 16 (four VOLI or 0, 93); NAM2 and NAM3 16
//   (11 records, not form ids: bit patterns 0x00FFFFFF); ONAM 4 (1 record, not a form id); MODL zstring (59) with MODT;
//   cloud textures as subrecords named c0TX, where c is the layer: '0' to '9', ':', ';' ... up to 'L' (layer 28).
// CLMT (9): EDID; WLST 12 per entry (one entry in 8 climates, two in 1); FNAM, GNAM, MODL zstrings; TNAM 6.
// REGN (389): WNAM 4, RDAT 8 (type u32, flags, priority, 2 pad; type 3 = weather in 91 of 198), RDWT 12 per entry (88
//   subrecords, every one directly after an RDAT of type 3; chances add to 100 in 87 and to 85 in 1).
// CELL: XCLR 4 per region (33,441 exterior cells), XCWT 4, XCCM 4 (314 cells, all interior, every one a REGN), XCIM 4, XCLC.

namespace game_skyrimse
{
    using namespace detail;

    namespace
    {
        constexpr std::uint32_t kLayerNameTail = Content::fourCC("\0" "0TX") & 0xFFFFFF00u;
        constexpr std::size_t kWeatherChanceBytes = 12;

        bool finite(float v)
        {
            return std::isfinite(v);
        }

        Rgba8 readRgba(const std::byte* p)
        {
            return { std::to_integer<std::uint8_t>(p[0]), std::to_integer<std::uint8_t>(p[1]),
                std::to_integer<std::uint8_t>(p[2]), std::to_integer<std::uint8_t>(p[3]) };
        }

        /// A decoder's working state: the context for form ids and odd notes, the weather caps and the failure message.
        struct Dec
        {
            const DecodeContext& mContext;
            const WeatherLimits& mLimits;
            const char* mRecord;
            std::string& mWhy;

            Fields fields() const { return Fields{ mContext, mRecord, mWhy }; }
            void odd(std::uint32_t sub) const { mContext.mNotes.odd(mRecord, tag(sub).c_str()); }

            bool text(std::uint32_t sub, std::span<const std::byte> data, std::string& out) const
            {
                if (data.size() > mLimits.mMaxStringBytes)
                {
                    mWhy = tag(sub) + ": " + std::to_string(data.size()) + " bytes, over the string limit of "
                        + std::to_string(mLimits.mMaxStringBytes);
                    return false;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                return true;
            }

            bool overCap(std::uint32_t sub, std::size_t have) const
            {
                if (have < mLimits.mMaxEntriesPerList)
                    return false;
                mWhy = tag(sub) + ": more than " + std::to_string(mLimits.mMaxEntriesPerList) + " entries";
                return true;
            }

            /// WLST and RDWT: whole 12 byte entries. false only when over the cap (mWhy set).
            bool chances(std::uint32_t sub, std::span<const std::byte> data, std::vector<WeatherChance>& out) const
            {
                if (data.size() % kWeatherChanceBytes != 0)
                {
                    odd(sub);
                    return true;
                }
                const std::size_t count = data.size() / kWeatherChanceBytes;
                if (out.size() + count > mLimits.mMaxEntriesPerList)
                {
                    mWhy = tag(sub) + ": more than " + std::to_string(mLimits.mMaxEntriesPerList) + " entries";
                    return false;
                }
                const Fields f = fields();
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::byte* p = data.data() + i * kWeatherChanceBytes;
                    WeatherChance c;
                    c.mWeather = f.resolved(readLe<std::uint32_t>(p));
                    c.mChance = readLe<std::int32_t>(p + 4);
                    c.mGlobal = readLe<std::uint32_t>(p + 8) == 0 ? kNullFormId : f.resolved(readLe<std::uint32_t>(p + 8));
                    out.push_back(c);
                }
                return true;
            }
        };

        /// '0TX' with a layer character in front: the layer index, or -1.
        int cloudLayerOf(std::uint32_t type)
        {
            if ((type & 0xFFFFFF00u) != kLayerNameTail)
                return -1;
            return static_cast<int>(type & 0xFFu) - static_cast<int>('0');
        }
    }

    // ---- WTHR -----------------------------------------------------------------------------------------------------

    bool decodeWeather(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        const WeatherLimits& limits, WeatherRecord& out, std::string& why)
    {
        const Dec d{ context, limits, "WTHR", why };
        const Fields fields = d.fields();
        out = WeatherRecord();
        out.mCommon = fields.common(record);

        std::array<CloudLayer, WeatherConstants::kMaxCloudLayers> layers{};
        std::uint32_t slots = 0;
        const auto slot = [&](std::uint32_t count) { slots = std::max(slots, count); };

        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            const int layer = cloudLayerOf(type);
            if (layer >= 0)
            {
                if (layer >= static_cast<int>(WeatherConstants::kMaxCloudLayers))
                {
                    d.odd(type);
                    continue;
                }
                std::string path;
                if (!d.text(type, data, path))
                    return false;
                layers[static_cast<std::size_t>(layer)].mTexture = std::move(path);
                layers[static_cast<std::size_t>(layer)].mHasTexture = true;
                slot(static_cast<std::uint32_t>(layer) + 1);
                continue;
            }
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!d.text(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("MODL"):
                    if (!d.text(type, data, out.mAuroraModel))
                        return false;
                    break;
                case Content::fourCC("LNAM"):
                    if (data.size() == 4)
                        out.mLayerCount = readLe<std::uint32_t>(data.data());
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("NAM1"):
                    if (data.size() == 4)
                        out.mNam1 = readLe<std::uint32_t>(data.data());
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("DATA"):
                    if (data.size() == 19)
                    {
                        WeatherData w;
                        for (std::size_t i = 0; i < 19; ++i)
                            w.mRaw[i] = std::to_integer<std::uint8_t>(data[i]);
                        w.mWindSpeed = w.mRaw[0];
                        w.mTransitionDelta = w.mRaw[3];
                        w.mSunGlare = w.mRaw[4];
                        w.mSunDamage = w.mRaw[5];
                        w.mPrecipitationBegin = w.mRaw[6];
                        w.mPrecipitationEnd = w.mRaw[7];
                        w.mThunderBegin = w.mRaw[8];
                        w.mThunderEnd = w.mRaw[9];
                        w.mThunderFrequency = w.mRaw[10];
                        w.mFlags = w.mRaw[11];
                        w.mLightningR = w.mRaw[12];
                        w.mLightningG = w.mRaw[13];
                        w.mLightningB = w.mRaw[14];
                        w.mWindDirection = w.mRaw[17];
                        w.mWindDirectionRange = w.mRaw[18];
                        out.mData = w;
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("FNAM"):
                    if (data.size() == 32)
                    {
                        float v[8];
                        std::memcpy(v, data.data(), sizeof(v));
                        if (std::all_of(std::begin(v), std::end(v), finite))
                            out.mFog = WeatherFog{ v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] };
                        else
                            d.odd(type);
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("NAM0"):
                    if (!data.empty() && data.size() % 16 == 0
                        && data.size() / 16 <= WeatherConstants::kMaxColourTypes)
                    {
                        out.mColourTypes = static_cast<std::uint32_t>(data.size() / 16);
                        for (std::uint32_t t = 0; t < out.mColourTypes; ++t)
                            for (std::uint32_t tod = 0; tod < WeatherConstants::kTimesOfDay; ++tod)
                                out.mColours[t][tod] = readRgba(data.data() + (t * 4 + tod) * 4);
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("PNAM"):
                    if (!data.empty() && data.size() % 16 == 0 && data.size() / 16 <= WeatherConstants::kMaxCloudLayers)
                    {
                        const auto n = static_cast<std::uint32_t>(data.size() / 16);
                        for (std::uint32_t l = 0; l < n; ++l)
                        {
                            std::array<Rgba8, 4> c;
                            for (std::uint32_t tod = 0; tod < 4; ++tod)
                                c[tod] = readRgba(data.data() + l * 16 + tod * 4);
                            layers[l].mColour = c;
                        }
                        slot(n);
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("JNAM"):
                    if (!data.empty() && data.size() % 16 == 0 && data.size() / 16 <= WeatherConstants::kMaxCloudLayers)
                    {
                        const auto n = static_cast<std::uint32_t>(data.size() / 16);
                        std::vector<std::array<float, 4>> alphas(n);
                        bool ok = true;
                        for (std::uint32_t l = 0; l < n && ok; ++l)
                            for (std::uint32_t tod = 0; tod < 4; ++tod)
                            {
                                alphas[l][tod] = readLe<float>(data.data() + l * 16 + tod * 4);
                                ok = ok && finite(alphas[l][tod]);
                            }
                        if (ok)
                        {
                            for (std::uint32_t l = 0; l < n; ++l)
                                layers[l].mAlpha = alphas[l];
                            slot(n);
                        }
                        else
                            d.odd(type);
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("QNAM"):
                case Content::fourCC("RNAM"):
                    if (!data.empty() && data.size() <= WeatherConstants::kMaxCloudLayers)
                    {
                        const bool q = type == Content::fourCC("QNAM");
                        for (std::size_t l = 0; l < data.size(); ++l)
                            (q ? layers[l].mSpeedQ : layers[l].mSpeedR) = std::to_integer<std::uint8_t>(data[l]);
                        slot(static_cast<std::uint32_t>(data.size()));
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("DALC"):
                    if ((data.size() == 32 || data.size() == 24) && out.mDirectionalAmbient.size() < 4)
                    {
                        DirectionalAmbient a;
                        for (std::size_t i = 0; i < 6; ++i)
                            a.mDirections[i] = readRgba(data.data() + i * 4);
                        if (data.size() == 32)
                        {
                            a.mHasSpecular = true;
                            a.mSpecular = readRgba(data.data() + 24);
                            a.mFresnelPower = readLe<float>(data.data() + 28);
                            if (!finite(a.mFresnelPower))
                                a.mFresnelPower = 0;
                        }
                        out.mDirectionalAmbient.push_back(a);
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("SNAM"):
                    if (data.size() == 8)
                    {
                        if (d.overCap(type, out.mSounds.size()))
                            return false;
                        out.mSounds.push_back({ fields.resolved(readLe<std::uint32_t>(data.data())),
                            readLe<std::uint32_t>(data.data() + 4) });
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("TNAM"):
                    if (data.size() == 4)
                    {
                        if (d.overCap(type, out.mSkyStatics.size()))
                            return false;
                        out.mSkyStatics.push_back(fields.resolved(readLe<std::uint32_t>(data.data())));
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("MNAM"):
                    fields.form(type, data, out.mPrecipitation);
                    break;
                case Content::fourCC("NNAM"):
                    fields.form(type, data, out.mVisualEffect);
                    break;
                case Content::fourCC("IMSP"):
                case Content::fourCC("HNAM"):
                    if (data.size() == 16)
                    {
                        const bool imsp = type == Content::fourCC("IMSP");
                        std::array<FormId, 4>& target = imsp ? out.mImageSpaces : out.mVolumetric;
                        for (std::size_t i = 0; i < 4; ++i)
                            target[i] = fields.resolved(readLe<std::uint32_t>(data.data() + i * 4));
                        (imsp ? out.mHasImageSpaces : out.mHasVolumetric) = true;
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("NAM2"):
                case Content::fourCC("NAM3"):
                    if (data.size() == 16)
                    {
                        std::array<std::uint8_t, 16> raw;
                        std::memcpy(raw.data(), data.data(), 16);
                        (type == Content::fourCC("NAM2") ? out.mNam2 : out.mNam3) = raw;
                    }
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("ONAM"):
                    if (data.size() == 4)
                    {
                        std::array<std::uint8_t, 4> raw;
                        std::memcpy(raw.data(), data.data(), 4);
                        out.mOnam = raw;
                    }
                    else
                        d.odd(type);
                    break;
                default:
                    break; // MODT, the old texture strings (ANAM BNAM CNAM DNAM of one record): not read
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        out.mCloudSlots = slots;
        out.mCloudLayers.assign(layers.begin(), layers.begin() + slots);
        return true;
    }

    // ---- IMGS -----------------------------------------------------------------------------------------------------

    bool decodeImageSpace(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, ImageSpaceRecord& out, std::string& why)
    {
        const Dec d{ context, limits, "IMGS", why };
        out = ImageSpaceRecord();
        out.mCommon = d.fields().common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        // A float that is not finite is a damaged subrecord: counted as odd and left out.
        const auto finite = [](const std::byte* p, std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
                if (!std::isfinite(readLe<float>(p + i * 4)))
                    return false;
            return true;
        };
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!d.text(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("CNAM"):
                    if (data.size() == 12 && finite(data.data(), 3))
                        out.mCinematic = ImageSpaceRecord::Cinematic{ readLe<float>(data.data()),
                            readLe<float>(data.data() + 4), readLe<float>(data.data() + 8) };
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("TNAM"):
                    if (data.size() == 16 && finite(data.data(), 4))
                        out.mTint = ImageSpaceRecord::Tint{ readLe<float>(data.data()), readLe<float>(data.data() + 4),
                            readLe<float>(data.data() + 8), readLe<float>(data.data() + 12) };
                    else
                        d.odd(type);
                    break;
                case Content::fourCC("HNAM"):
                    if (data.size() == 36 && finite(data.data(), 9))
                    {
                        std::array<float, 9> h{};
                        for (std::size_t i = 0; i < 9; ++i)
                            h[i] = readLe<float>(data.data() + i * 4);
                        out.mHdr = h;
                    }
                    else
                        d.odd(type);
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    // ---- CLMT -----------------------------------------------------------------------------------------------------

    bool decodeClimate(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        const WeatherLimits& limits, ClimateRecord& out, std::string& why)
    {
        const Dec d{ context, limits, "CLMT", why };
        out = ClimateRecord();
        out.mCommon = d.fields().common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!d.text(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("FNAM"):
                    if (!d.text(type, data, out.mSunTexture))
                        return false;
                    break;
                case Content::fourCC("GNAM"):
                    if (!d.text(type, data, out.mSunGlareTexture))
                        return false;
                    break;
                case Content::fourCC("MODL"):
                    if (!d.text(type, data, out.mSkyModel))
                        return false;
                    break;
                case Content::fourCC("WLST"):
                    if (!d.chances(type, data, out.mWeathers))
                        return false;
                    break;
                case Content::fourCC("TNAM"):
                    if (data.size() == 6)
                    {
                        const auto b = [&](std::size_t i) { return std::to_integer<std::uint8_t>(data[i]); };
                        out.mTiming = ClimateTiming{ b(0), b(1), b(2), b(3), b(4), b(5) };
                    }
                    else
                        d.odd(type);
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    // ---- REGN -----------------------------------------------------------------------------------------------------

    bool decodeRegionWeather(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, const WeatherLimits& limits, RegionWeatherRecord& out, std::string& why)
    {
        const Dec d{ context, limits, "REGN", why };
        out = RegionWeatherRecord();
        out.mCommon = d.fields().common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        std::optional<std::uint32_t> section; // the type of the last RDAT with a valid size
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!d.text(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("WNAM"):
                    d.fields().form(type, data, out.mWorld);
                    break;
                case Content::fourCC("RDAT"):
                    if (data.size() == 8)
                    {
                        section = readLe<std::uint32_t>(data.data());
                        if (*section == 3)
                        {
                            out.mHasWeather = true;
                            out.mFlags = std::to_integer<std::uint8_t>(data[4]);
                            out.mPriority = std::to_integer<std::uint8_t>(data[5]);
                        }
                    }
                    else
                    {
                        section.reset();
                        d.odd(type);
                    }
                    break;
                case Content::fourCC("RDWT"):
                    if (section && *section == 3)
                    {
                        if (!d.chances(type, data, out.mWeathers))
                            return false;
                    }
                    else
                        d.odd(type);
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    // ---- the walk --------------------------------------------------------------------------------------------------

    namespace
    {
        class Walker final : public Content::Visitor
        {
        public:
            Walker(const Content::Plugin& plugin, const FormIdResolver& resolver, const WeatherLimits& limits,
                WeatherRecords& out)
                : mPlugin(plugin)
                , mLimits(limits)
                , mOut(out)
                , mResolver(resolver)
                , mExterior(makeExteriorLimits(limits))
                , mContext{ resolver, mExterior, out.mNotes, plugin.header().isLocalized() }
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                mGroups.push_back(group);
                return true;
            }

            bool onGroupEnd(const Content::GroupInfo&) override
            {
                if (!mGroups.empty())
                    mGroups.pop_back();
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                switch (record.mType)
                {
                    case Content::fourCC("WTHR"):
                    case Content::fourCC("CLMT"):
                    case Content::fourCC("REGN"):
                    case Content::fourCC("WRLD"):
                    case Content::fourCC("CELL"):
                    case Content::fourCC("IMGS"):
                        break;
                    default:
                        return true;
                }
                TypeCount& count = mOut.mCounts[record.mType];
                ++count.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                    ++count.mDeleted;
                if (count.mParsed >= mLimits.mMaxRecordsPerType)
                {
                    fail(record, "more than " + std::to_string(mLimits.mMaxRecordsPerType) + " records of one type");
                    return true;
                }

                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error), record.mType);
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }

                std::string why;
                bool ok = false;
                switch (record.mType)
                {
                    case Content::fourCC("WTHR"):
                    {
                        WeatherRecord w;
                        ok = decodeWeather(bytes, record, mContext, mLimits, w, why);
                        if (ok)
                            mOut.mWeathers.push_back(std::move(w));
                        break;
                    }
                    case Content::fourCC("CLMT"):
                    {
                        ClimateRecord c;
                        ok = decodeClimate(bytes, record, mContext, mLimits, c, why);
                        if (ok)
                            mOut.mClimates.push_back(std::move(c));
                        break;
                    }
                    case Content::fourCC("IMGS"):
                    {
                        ImageSpaceRecord i;
                        ok = decodeImageSpace(bytes, record, mContext, mLimits, i, why);
                        if (ok)
                            mOut.mImageSpaces.push_back(std::move(i));
                        break;
                    }
                    case Content::fourCC("REGN"):
                    {
                        RegionWeatherRecord r;
                        ok = decodeRegionWeather(bytes, record, mContext, mLimits, r, why);
                        if (ok)
                            mOut.mRegions.push_back(std::move(r));
                        break;
                    }
                    case Content::fourCC("WRLD"):
                    {
                        WorldSpaceRecord w;
                        ok = decodeWorldSpace(bytes, record, mContext, w, why);
                        if (ok)
                            mOut.mWorlds.push_back(std::move(w));
                        break;
                    }
                    default:
                        ok = readCell(record, bytes, why);
                        break;
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, why);
                return true;
            }

        private:
            static ExteriorLimits makeExteriorLimits(const WeatherLimits& limits)
            {
                ExteriorLimits e;
                e.mMaxRecordBytes = limits.mMaxRecordBytes;
                e.mMaxStringBytes = limits.mMaxStringBytes;
                e.mMaxErrors = limits.mMaxErrors;
                e.mMaxRecordsPerType = limits.mMaxRecordsPerType;
                return e;
            }

            FormId worldOfGroups() const
            {
                for (auto it = mGroups.rbegin(); it != mGroups.rend(); ++it)
                    if (it->mType == 1)
                    {
                        const std::optional<FormId> id = mResolver.resolve(it->mLabel);
                        return id ? *id : kUnresolvedFormId;
                    }
                return kNullFormId;
            }

            void addError(Content::Error error, std::uint32_t type)
            {
                ++mOut.mCounts[type].mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                Content::Error error;
                error.mFile = mPlugin.name();
                error.mOffset = record.mOffset;
                error.mRecordType = record.mType;
                error.mMessage = std::move(message);
                addError(std::move(error), record.mType);
            }

            bool readCell(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                const Dec d{ mContext, mLimits, "CELL", why };
                const Fields fields = d.fields();
                CellWeatherLinks cell;
                cell.mCommon = fields.common(record);
                cell.mWorld = worldOfGroups();
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            if (!d.text(type, data, cell.mCommon.mEditorId))
                                return false;
                            break;
                        case Content::fourCC("XCLC"):
                            if (data.size() >= 8)
                            {
                                cell.mGrid = CellGrid{ readLe<std::int32_t>(data.data()),
                                    readLe<std::int32_t>(data.data() + 4) };
                                if (data.size() != 12)
                                    d.odd(type);
                            }
                            else
                                d.odd(type);
                            break;
                        case Content::fourCC("XCCM"):
                            fields.form(type, data, cell.mWeatherRegion);
                            break;
                        case Content::fourCC("XCWT"):
                            fields.form(type, data, cell.mWater);
                            break;
                        case Content::fourCC("XCIM"):
                            fields.form(type, data, cell.mImageSpace);
                            break;
                        case Content::fourCC("XCLR"):
                            if (data.size() % 4 != 0)
                            {
                                d.odd(type);
                                break;
                            }
                            if (data.size() / 4 > mLimits.mMaxEntriesPerList)
                            {
                                why = "XCLR: more than " + std::to_string(mLimits.mMaxEntriesPerList) + " entries";
                                return false;
                            }
                            for (std::size_t i = 0; i < data.size(); i += 4)
                                cell.mRegions.push_back(fields.resolved(readLe<std::uint32_t>(data.data() + i)));
                            break;
                        default:
                            break;
                    }
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                mOut.mCells.push_back(std::move(cell));
                return true;
            }

            const Content::Plugin& mPlugin;
            const WeatherLimits& mLimits;
            WeatherRecords& mOut;
            const FormIdResolver& mResolver;
            ExteriorLimits mExterior;
            DecodeContext mContext;
            std::vector<Content::GroupInfo> mGroups;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseWeatherRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const WeatherLimits& limits, WeatherRecords& out)
    {
        out = WeatherRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            Content::Error error;
            error.mFile = plugin.name();
            error.mMessage = "this plugin is not a Skyrim Special Edition one";
            out.mWalkError = error;
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Walker walker(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(walker);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseWeatherRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const WeatherLimits& limits, WeatherRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (!plugin)
            return false;
        if (!parseWeatherRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    bool parseWeatherRecordsFile(const std::filesystem::path& path, const LoadOrder& order,
        const WeatherLimits& limits, WeatherRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, error);
        if (!plugin)
            return false;
        if (!parseWeatherRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- WeatherIndex ----------------------------------------------------------------------------------------------

    namespace
    {
        template <class T, class Map>
        void put(Map& map, const std::vector<T>& records)
        {
            for (const T& r : records)
            {
                if (r.mCommon.mId == kNullFormId || r.mCommon.mId == kUnresolvedFormId)
                    continue;
                if (r.mCommon.deleted())
                    map.erase(r.mCommon.mId);
                else
                    map[r.mCommon.mId] = r;
            }
        }

        constexpr int kMaxWorldParentSteps = 16;
    }

    void WeatherIndex::add(const WeatherRecords& records)
    {
        put(mWeathers, records.mWeathers);
        put(mClimates, records.mClimates);
        put(mRegions, records.mRegions);
        put(mWorlds, records.mWorlds);
        put(mCells, records.mCells);
        put(mImageSpaces, records.mImageSpaces);
    }

    namespace
    {
        template <class T>
        std::vector<FormId> sortedKeys(const std::unordered_map<FormId, T>& map)
        {
            std::vector<FormId> ids;
            ids.reserve(map.size());
            for (const auto& entry : map)
                ids.push_back(entry.first);
            std::sort(ids.begin(), ids.end());
            return ids;
        }
    }

    std::vector<FormId> WeatherIndex::weatherIds() const
    {
        return sortedKeys(mWeathers);
    }

    std::vector<FormId> WeatherIndex::climateIds() const
    {
        return sortedKeys(mClimates);
    }

    std::optional<FormId> WeatherIndex::climateOfWorld(FormId worldId) const
    {
        FormId id = worldId;
        for (int step = 0; step <= kMaxWorldParentSteps; ++step)
        {
            const WorldSpaceRecord* w = world(id);
            if (w == nullptr)
                return std::nullopt;
            if (w->mClimate != kNullFormId && w->mClimate != kUnresolvedFormId)
                return w->mClimate;
            if (w->mParent == kNullFormId || w->mParent == kUnresolvedFormId || !w->mParentUsage
                || (*w->mParentUsage & ParentUsage::Climate) == 0)
                return std::nullopt;
            id = w->mParent;
        }
        return std::nullopt;
    }

    WeatherIndex::EffectiveList WeatherIndex::effectiveWeatherList(const CellWeatherLinks& cell) const
    {
        const RegionWeatherRecord* best = nullptr;
        if (cell.mWeatherRegion != kNullFormId)
        {
            const RegionWeatherRecord* r = region(cell.mWeatherRegion);
            if (r != nullptr && r->mHasWeather && !r->mWeathers.empty())
                best = r;
        }
        if (best == nullptr)
        {
            for (const FormId id : cell.mRegions)
            {
                const RegionWeatherRecord* r = region(id);
                if (r == nullptr || !r->mHasWeather || r->mWeathers.empty())
                    continue;
                if (best == nullptr || r->mPriority > best->mPriority)
                    best = r;
            }
        }
        EffectiveList list;
        if (best != nullptr)
        {
            list.mSource = EffectiveList::Source::Region;
            list.mFrom = best->mCommon.mId;
            list.mWeathers = &best->mWeathers;
            return list;
        }
        std::optional<FormId> climateId;
        if (cell.mWorld != kNullFormId)
            climateId = climateOfWorld(cell.mWorld);
        if (!climateId)
            return list;
        const ClimateRecord* c = climate(*climateId);
        if (c == nullptr || c->mWeathers.empty())
            return list;
        list.mSource = EffectiveList::Source::Climate;
        list.mFrom = c->mCommon.mId;
        list.mWeathers = &c->mWeathers;
        return list;
    }

    std::optional<FormId> WeatherIndex::pickWeather(const std::vector<WeatherChance>& list, double roll)
    {
        double total = 0;
        for (const WeatherChance& c : list)
            if (c.mChance > 0)
                total += static_cast<double>(c.mChance);
        if (total <= 0)
            return std::nullopt;
        if (!(roll >= 0.0)) // also a NaN
            roll = 0.0;
        if (roll >= 1.0)
            roll = std::nextafter(1.0, 0.0);
        const double target = roll * total;
        double cumulative = 0;
        std::optional<FormId> last;
        for (const WeatherChance& c : list)
        {
            if (c.mChance <= 0)
                continue;
            cumulative += static_cast<double>(c.mChance);
            last = c.mWeather;
            if (target < cumulative)
                return c.mWeather;
        }
        return last;
    }
}
