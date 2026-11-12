#include "game_skyrimse/land.hpp"

#include "exterior_common.hpp"

#include <cmath>

// Layout status (see land.hpp): VERIFIED means tools/skyrimse_land_check ran over the installed Skyrim SE masters (the
// numbers are in docs/tasks/0117-skyrim-exteriors-e0-decoding.md); RECALLED-VERIFY means not checked. The layouts
// agree with the inherited ESM4 reader (components/esm4/loadland.hpp: VHGT, BTXT, ATXT, VTXT structs, and
// components/esmbridge/esmterrain.cpp for the gradient walk), read as a reference only.

// SETTLED by the installed masters (five, merged, tools/skyrimse_land_check; numbers in the task log):
// - Heights: mHeights[row * 33 + col], col = +X (east), row = +Y (north). An east neighbour shares column 32 with this
//   cell's column 0 (11,067 of 11,092 Tamriel pairs agree to 0.01 unit; no other edge pairing reaches 5 percent), a
//   north neighbour row 32 with row 0 (11,032 of 11,067). The rest (0.2 to 0.3 percent) are real seams in the data.
// - Quadrants: 0 south-west, 1 south-east, 2 north-west, 3 north-east. VTXT position = row * 17 + col in the quadrant's
//   17 x 17 grid with the same axes: quadrant 0 column 16 matches quadrant 1 column 0 (69 percent of edge entries, the
//   transposed reading 2.4 percent), quadrant 0 row 16 matches quadrant 2 row 0; base textures agree most across the
//   same borders.
// - A null texture (0) in ATXT is a real layer (99.9 percent carry opacity entries) and in BTXT "no base": both stand for
//   the game's default ground texture (textures\landscape\dirt02.dds, found in the archives; upstream
//   components/esm4/loadland.cpp assignDefaultTextures names the same file). One id (0x844 of Skyrim.esm) used by 101
//   layers is in no record of its plugin: a dangling reference, to be treated like null.

namespace game_skyrimse
{
    using namespace detail;

    void DecodeNotes::odd(const char* record, const char* sub)
    {
        ++mOddSubrecords;
        ++mOddBy[std::string(record) + "." + sub];
    }

    std::size_t LandRecord::layerCount() const
    {
        std::size_t n = 0;
        for (const LandQuadrant& q : mQuadrants)
            n += q.mLayers.size();
        return n;
    }

    namespace
    {
        // A height offset beyond this is not a terrain (the installed data stays within a few thousand steps).
        constexpr float kMaxHeightOffset = 1.0e6f;
        constexpr std::size_t kVhgtBytes = 4 + LandConstants::kVertCount + 3; // VERIFIED: 1,096 in every record
        constexpr std::size_t kVertexTripleBytes = LandConstants::kVertCount * 3; // VERIFIED: VNML and VCLR, 3,267
        constexpr std::size_t kLayerHeaderBytes = 8; // VERIFIED: BTXT and ATXT
        constexpr std::size_t kOpacityEntryBytes = 8; // VERIFIED: VTXT, u16 position, 2 pad, f32 opacity
    }

    std::vector<float> decodeHeights(float offset, std::span<const std::int8_t> gradient)
    {
        if (gradient.size() != LandConstants::kVertCount)
            return {};
        // The first value of every row accumulates down the first column; the rest of the row accumulates along it.
        // Same walk as components/esmbridge/esmterrain.cpp, ESM::LandData(const ESM4::Land&, int).
        std::vector<float> heights(LandConstants::kVertCount);
        constexpr std::uint32_t n = LandConstants::kVertsPerSide;
        float rowStart = offset;
        for (std::uint32_t row = 0; row < n; ++row)
        {
            rowStart += static_cast<float>(gradient[row * n]);
            heights[row * n] = rowStart * LandConstants::kHeightScale;
            float running = rowStart;
            for (std::uint32_t col = 1; col < n; ++col)
            {
                running += static_cast<float>(gradient[row * n + col]);
                heights[row * n + col] = running * LandConstants::kHeightScale;
            }
        }
        return heights;
    }

    bool decodeLand(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        LandRecord& out, std::string& why)
    {
        const Fields fields{ context, "LAND", why };
        out = LandRecord();
        out.mCommon = fields.common(record);

        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        // The layer a following VTXT belongs to: nullptr after a VTXT, after a refused ATXT, and at the start.
        LandLayer* pending = nullptr;
        std::size_t total = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("DATA"):
                    if (data.size() == 4)
                    {
                        out.mHasDataFlags = true;
                        out.mDataFlags = readLe<std::uint32_t>(data.data());
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("VHGT"):
                    if (data.size() == kVhgtBytes)
                    {
                        const float offset = readLe<float>(data.data());
                        if (!std::isfinite(offset) || std::abs(offset) > kMaxHeightOffset)
                        {
                            why = "VHGT: the height offset is not a plausible value";
                            return false;
                        }
                        std::vector<std::int8_t> gradient(LandConstants::kVertCount);
                        std::memcpy(gradient.data(), data.data() + 4, gradient.size());
                        out.mHeights = decodeHeights(offset, gradient);
                        out.mHeightOffset = offset;
                        out.mHasHeights = true;
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("VNML"):
                    if (data.size() == kVertexTripleBytes)
                    {
                        out.mNormals.resize(kVertexTripleBytes);
                        std::memcpy(out.mNormals.data(), data.data(), kVertexTripleBytes);
                        out.mHasNormals = true;
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("VCLR"):
                    if (data.size() == kVertexTripleBytes)
                    {
                        out.mColours.resize(kVertexTripleBytes);
                        std::memcpy(out.mColours.data(), data.data(), kVertexTripleBytes);
                        out.mHasColours = true;
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("BTXT"):
                    if (data.size() == kLayerHeaderBytes)
                    {
                        const std::uint8_t quadrant = std::to_integer<std::uint8_t>(data[4]);
                        if (quadrant < LandConstants::kQuadrants)
                            out.mQuadrants[quadrant].mBase = fields.resolved(readLe<std::uint32_t>(data.data()));
                        else
                            fields.odd(type);
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("ATXT"):
                    pending = nullptr;
                    if (data.size() == kLayerHeaderBytes)
                    {
                        const std::uint8_t quadrant = std::to_integer<std::uint8_t>(data[4]);
                        if (quadrant >= LandConstants::kQuadrants)
                        {
                            fields.odd(type);
                            break;
                        }
                        LandQuadrant& q = out.mQuadrants[quadrant];
                        if (q.mLayers.size() >= context.mLimits.mMaxLayersPerQuadrant)
                        {
                            why = "ATXT: quadrant " + std::to_string(quadrant) + " has more than "
                                + std::to_string(context.mLimits.mMaxLayersPerQuadrant) + " layers";
                            return false;
                        }
                        if (total >= context.mLimits.mMaxLayersPerLand)
                        {
                            why = "ATXT: the record has more than " + std::to_string(context.mLimits.mMaxLayersPerLand)
                                + " layers";
                            return false;
                        }
                        LandLayer layer;
                        layer.mTexture = fields.resolved(readLe<std::uint32_t>(data.data()));
                        layer.mLayerIndex = readLe<std::uint16_t>(data.data() + 6);
                        q.mLayers.push_back(std::move(layer));
                        pending = &q.mLayers.back();
                        ++total;
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("VTXT"):
                {
                    LandLayer* layer = pending;
                    pending = nullptr;
                    if (layer == nullptr || data.size() % kOpacityEntryBytes != 0)
                    {
                        fields.odd(type);
                        break;
                    }
                    const std::size_t count = data.size() / kOpacityEntryBytes;
                    if (count > LandConstants::kQuadrantVertCount)
                    {
                        why = "VTXT: " + std::to_string(count) + " entries, more than the "
                            + std::to_string(LandConstants::kQuadrantVertCount) + " vertices of a quadrant";
                        return false;
                    }
                    layer->mOpacity.reserve(count);
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const std::byte* e = data.data() + i * kOpacityEntryBytes;
                        const std::uint16_t position = readLe<std::uint16_t>(e);
                        const float opacity = readLe<float>(e + 4);
                        if (position >= LandConstants::kQuadrantVertCount || !std::isfinite(opacity) || opacity < 0.0f
                            || opacity > 1.0f)
                        {
                            fields.odd(type);
                            continue;
                        }
                        layer->mOpacity.push_back({ position, opacity });
                    }
                    break;
                }
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

    bool decodeGroundTexture(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, GroundTextureRecord& out, std::string& why)
    {
        const Fields fields{ context, "LTEX", why };
        out = GroundTextureRecord();
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
                case Content::fourCC("TNAM"):
                    fields.form(type, data, out.mTextureSet);
                    break;
                case Content::fourCC("MNAM"):
                    fields.form(type, data, out.mMaterial);
                    break;
                case Content::fourCC("HNAM"):
                    if (data.size() == 2)
                    {
                        out.mFriction = std::to_integer<std::uint8_t>(data[0]);
                        out.mRestitution = std::to_integer<std::uint8_t>(data[1]);
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("SNAM"):
                    if (data.size() == 1)
                        out.mSpecularExponent = std::to_integer<std::uint8_t>(data[0]);
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("GNAM"):
                    if (data.size() != 4)
                    {
                        fields.odd(type);
                        break;
                    }
                    if (out.mGrass.size() >= context.mLimits.mMaxGrassPerGroundTexture)
                    {
                        why = "GNAM: more than " + std::to_string(context.mLimits.mMaxGrassPerGroundTexture)
                            + " entries";
                        return false;
                    }
                    out.mGrass.push_back(fields.resolved(readLe<std::uint32_t>(data.data())));
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

    bool decodeTextureSet(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, TextureSetRecord& out, std::string& why)
    {
        const Fields fields{ context, "TXST", why };
        out = TextureSetRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        const std::uint32_t tx00 = Content::fourCC("TX00");
        while ((status = reader.next(type, data)) == 1)
        {
            if (type == Content::fourCC("EDID"))
            {
                if (!fields.zstring(type, data, out.mCommon.mEditorId))
                    return false;
            }
            else if (type >= tx00 && type <= tx00 + 7 * 0x01000000u && (type & 0x00FFFFFFu) == (tx00 & 0x00FFFFFFu))
            {
                // TX00 to TX07: the last character is the digit ('0' + n) in the fourth byte of the little-endian id.
                const std::uint32_t index = (type >> 24) - '0';
                if (index < out.mPaths.size() && !fields.zstring(type, data, out.mPaths[index]))
                    return false;
            }
            else if (type == Content::fourCC("DNAM"))
            {
                if (data.size() == 2)
                    out.mFlags = readLe<std::uint16_t>(data.data());
                else
                    fields.odd(type);
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }
}
