#include "game_skyrimse/worldspace.hpp"

#include "exterior_common.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <cfloat>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

// Layout status as in land.cpp. The sentinel values of XCLW and the bits of PNAM are settled (or not) by the data in
// docs/tasks/0117-skyrim-exteriors-e0-decoding.md.

// SETTLED by the installed masters (tools/skyrimse_land_check, five masters merged, 49 worldspaces):
// - PNAM bits: 0x04 map, 0x08 water, 0x10 climate are certain (every child with the bit set has no MNAM, no NAM2, no
//   CNAM; 0x08 clear: all 37 have NAM2). 0x01 (land) and 0x02 (LOD) are never apart in the data (7 worlds have both,
//   37 neither), so their order is UESP's and cannot be told apart here. 0x20 and 0x40 have no matching subrecord in
//   the fields decoded: RECALLED-VERIFY. Worlds with 0x01 set have little LAND of their own (none has 90 percent of its
//   cells).
// - XCLW: 72,117 exterior cells hold the maximum float (the world's default); 128 hold -FLT_MAX (23), -2^31 (77) or a
// value
//   within a few steps of 2^32 (28): no water; 675 hold a real height (-17,000 to 12,608). The cell flag 0x02 is set in
//   all but 5 exterior cells whatever the water, so it does not say whether water exists.
// - MNAM is 28 bytes (the 16 byte form plus camera min height, max height and initial pitch) in every record that has
//   one (11 over the five masters; RECALLED-VERIFY the names of the last three floats).

namespace game_skyrimse
{
    using namespace detail;

    CellWater classifyCellWater(std::optional<float> raw)
    {
        CellWater water;
        if (!raw)
            return water;
        water.mRaw = *raw;
        if (!std::isfinite(*raw))
        {
            water.mKind = CellWaterKind::Invalid;
            return water;
        }
        // The maximum float is "use the world's default" (10,630 of 11,187 Tamriel cells); the two integer-looking
        // values (-2^31, 2^32) are "no water". A height beyond 2^31 units is not a height.
        if (*raw >= 3.0e38f)
            water.mKind = CellWaterKind::UseWorldDefault;
        else if (*raw <= -2147483648.0f || *raw >= 2147483648.0f)
            water.mKind = CellWaterKind::NoWater;
        else
        {
            water.mKind = CellWaterKind::Height;
            water.mHeight = *raw;
        }
        return water;
    }

    bool decodeWorldSpace(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, WorldSpaceRecord& out, std::string& why)
    {
        const Fields fields{ context, "WRLD", why };
        out = WorldSpaceRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        const auto f32 = [&](std::size_t at) { return readLe<float>(data.data() + at); };
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("FULL"):
                    if (!fields.lstring(type, data, out.mName))
                        return false;
                    break;
                case Content::fourCC("WNAM"):
                    fields.form(type, data, out.mParent);
                    break;
                case Content::fourCC("PNAM"):
                    if (data.size() == 2)
                        out.mParentUsage = readLe<std::uint16_t>(data.data());
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("CNAM"):
                    fields.form(type, data, out.mClimate);
                    break;
                case Content::fourCC("NAM2"):
                    fields.form(type, data, out.mWater);
                    break;
                case Content::fourCC("NAM3"):
                    fields.form(type, data, out.mLodWater);
                    break;
                case Content::fourCC("NAM4"):
                    if (data.size() == 4)
                        out.mLodWaterHeight = f32(0);
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("DNAM"):
                    if (data.size() == 8)
                    {
                        out.mDefaultLandHeight = f32(0);
                        out.mDefaultWaterHeight = f32(4);
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("MNAM"):
                    if (data.size() == 16 || data.size() == 28)
                    {
                        WorldMapData map;
                        map.mSize = static_cast<std::uint32_t>(data.size());
                        if (data.size() == 28)
                        {
                            map.mHasCamera = true;
                            map.mCameraMinHeight = f32(16);
                            map.mCameraMaxHeight = f32(20);
                            map.mCameraInitialPitch = f32(24);
                        }
                        map.mUsableWidth = readLe<std::int32_t>(data.data());
                        map.mUsableHeight = readLe<std::int32_t>(data.data() + 4);
                        map.mNorthWestX = readLe<std::int16_t>(data.data() + 8);
                        map.mNorthWestY = readLe<std::int16_t>(data.data() + 10);
                        map.mSouthEastX = readLe<std::int16_t>(data.data() + 12);
                        map.mSouthEastY = readLe<std::int16_t>(data.data() + 14);
                        out.mMap = map;
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("DATA"):
                    if (data.size() == 1)
                        out.mFlags = std::to_integer<std::uint8_t>(data[0]);
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("NAM0"):
                    if (data.size() == 8)
                        out.mMinBounds = Vec2{ f32(0), f32(4) };
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("NAM9"):
                    if (data.size() == 8)
                        out.mMaxBounds = Vec2{ f32(0), f32(4) };
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("ONAM"):
                    if (data.size() == 16)
                        out.mMapOffset = std::array<float, 4>{ f32(0), f32(4), f32(8), f32(12) };
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("WCTR"):
                    if (data.size() == 4)
                        out.mCenterCell = std::array<std::int16_t, 2>{ readLe<std::int16_t>(data.data()),
                            readLe<std::int16_t>(data.data() + 2) };
                    else
                        fields.odd(type);
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

    bool decodeWater(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        WaterRecord& out, std::string& why)
    {
        const Fields fields{ context, "WATR", why };
        out = WaterRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("FULL"):
                    if (!fields.lstring(type, data, out.mName))
                        return false;
                    break;
                case Content::fourCC("NNAM"):
                    if (!fields.zstring(type, data, out.mNoiseMap))
                        return false;
                    break;
                case Content::fourCC("ANAM"):
                    if (data.size() == 1)
                        out.mOpacity = std::to_integer<std::uint8_t>(data[0]);
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("FNAM"):
                    if (data.size() == 1)
                        out.mFlags = std::to_integer<std::uint8_t>(data[0]);
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("SNAM"):
                    fields.form(type, data, out.mSound);
                    break;
                case Content::fourCC("DNAM"):
                    out.mDnamSize = static_cast<std::uint32_t>(data.size());
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
            Walker(const Content::Plugin& plugin, const FormIdResolver& resolver, const ExteriorLimits& limits,
                ExteriorData& out)
                : mPlugin(plugin)
                , mLimits(limits)
                , mOut(out)
                , mContext{ resolver, limits, out.mNotes, plugin.header().isLocalized() }
                , mResolver(resolver)
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
                    case Content::fourCC("WRLD"):
                    case Content::fourCC("LAND"):
                    case Content::fourCC("LTEX"):
                    case Content::fourCC("TXST"):
                    case Content::fourCC("WATR"):
                    case Content::fourCC("GRAS"):
                        break;
                    case Content::fourCC("CELL"):
                        if (!inWorldGroup())
                            return true; // interior cells are not this stage's
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
                    case Content::fourCC("WRLD"):
                    {
                        WorldSpaceRecord w;
                        ok = decodeWorldSpace(bytes, record, mContext, w, why);
                        if (ok)
                            mOut.mWorlds.push_back(std::move(w));
                        break;
                    }
                    case Content::fourCC("CELL"):
                        ok = readCell(record, bytes, why);
                        break;
                    case Content::fourCC("LAND"):
                    {
                        LandRecord land;
                        ok = decodeLand(bytes, record, mContext, land, why);
                        if (ok)
                            placeLand(land);
                        break;
                    }
                    case Content::fourCC("LTEX"):
                    {
                        GroundTextureRecord t;
                        ok = decodeGroundTexture(bytes, record, mContext, t, why);
                        if (ok)
                            mOut.mGroundTextures.push_back(std::move(t));
                        break;
                    }
                    case Content::fourCC("GRAS"):
                    {
                        GrassRecord g;
                        ok = decodeGrass(bytes, record, mContext, g, why);
                        if (ok)
                            mOut.mGrass.push_back(std::move(g));
                        break;
                    }
                    case Content::fourCC("TXST"):
                    {
                        TextureSetRecord t;
                        ok = decodeTextureSet(bytes, record, mContext, t, why);
                        if (ok)
                            mOut.mTextureSets.push_back(std::move(t));
                        break;
                    }
                    default:
                    {
                        WaterRecord w;
                        ok = decodeWater(bytes, record, mContext, w, why);
                        if (ok)
                            mOut.mWaters.push_back(std::move(w));
                        break;
                    }
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, why);
                return true;
            }

        private:
            bool inWorldGroup() const
            {
                for (const Content::GroupInfo& g : mGroups)
                    if (g.mType == 1)
                        return true;
                return false;
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
                const Fields fields{ mContext, "CELL", why };
                ExteriorCellInfo cell;
                cell.mCommon = fields.common(record);
                cell.mWorld = worldOfGroups();
                std::optional<float> water;
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            if (!fields.zstring(type, data, cell.mCommon.mEditorId))
                                return false;
                            break;
                        case Content::fourCC("DATA"):
                            if (data.size() == 1)
                                cell.mFlags = std::to_integer<std::uint8_t>(data[0]);
                            else if (data.size() == 2)
                                cell.mFlags = readLe<std::uint16_t>(data.data());
                            else
                                fields.odd(type);
                            break;
                        case Content::fourCC("XCLC"):
                            if (data.size() >= 8)
                            {
                                cell.mGrid = CellGrid{ readLe<std::int32_t>(data.data()),
                                    readLe<std::int32_t>(data.data() + 4) };
                                if (data.size() != 12)
                                    fields.odd(type);
                            }
                            else
                                fields.odd(type);
                            break;
                        case Content::fourCC("XCLW"):
                            if (data.size() == 4)
                                water = readLe<float>(data.data());
                            else
                                fields.odd(type);
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
                cell.mWater = classifyCellWater(water);
                mCellByStored[record.mFormId] = mOut.mCells.size();
                mOut.mCells.push_back(std::move(cell));
                return true;
            }

            void placeLand(LandRecord& land)
            {
                land.mWorld = worldOfGroups();
                for (auto it = mGroups.rbegin(); it != mGroups.rend(); ++it)
                {
                    if (it->mType != 6)
                        continue;
                    const auto found = mCellByStored.find(it->mLabel);
                    if (found != mCellByStored.end())
                    {
                        ExteriorCellInfo& cell = mOut.mCells[found->second];
                        land.mCell = cell.mCommon.mId;
                        land.mGrid = cell.mGrid;
                        cell.mHasLand = true;
                    }
                    break;
                }
                if (mLimits.mDiscardLandData)
                {
                    std::vector<float>().swap(land.mHeights);
                    std::vector<std::int8_t>().swap(land.mNormals);
                    std::vector<std::uint8_t>().swap(land.mColours);
                    for (LandQuadrant& quadrant : land.mQuadrants)
                        std::vector<LandLayer>().swap(quadrant.mLayers);
                }
                mOut.mLands.push_back(std::move(land));
            }

            const Content::Plugin& mPlugin;
            const ExteriorLimits& mLimits;
            ExteriorData& mOut;
            DecodeContext mContext;
            const FormIdResolver& mResolver;
            std::vector<Content::GroupInfo> mGroups;
            std::vector<std::byte> mInflated;
            std::map<std::uint32_t, std::size_t> mCellByStored;
        };
    }

    bool parseExterior(
        const Content::Plugin& plugin, const LoadOrder& order, const ExteriorLimits& limits, ExteriorData& out)
    {
        out = ExteriorData();
        out.mName = plugin.name();
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

    bool parseExteriorBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ExteriorLimits& limits, ExteriorData& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (!plugin)
            return false;
        return parseExterior(*plugin, order, limits, out);
    }

    bool parseExteriorFile(const std::filesystem::path& path, const LoadOrder& order, const ExteriorLimits& limits,
        ExteriorData& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, error);
        if (!plugin)
            return false;
        return parseExterior(*plugin, order, limits, out);
    }
}
