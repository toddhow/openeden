#include "game_skyrimse/grass.hpp"

#include "exterior_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace game_skyrimse
{
    namespace sdk = openeden::sdk::game;
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        // GRAS DATA is 32 bytes (VERIFIED by testskyrimsegrassreal.cpp on the five masters: every record that has one):
        //  0 density, 1 minimum slope, 2 maximum slope, 3 unused, 4 units from water (u16), 6 unused (u16),
        //  8 units from water type (u32), 12 position range, 16 height range, 20 color range, 24 wave period (floats),
        //  28 flags, 29 unused (3).
        constexpr std::size_t kDataBytes = 32;

        constexpr float kPi = 3.14159265358979f;
        constexpr float kRadiansPerDegree = kPi / 180.0f;
        // Layers of one quadrant that the winner search looks at (the pack's ExteriorLimits allow 32).
        constexpr std::size_t kMaxLayersLooked = 64;

        std::uint64_t splitmix(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        float unit(std::uint64_t& state)
        {
            return static_cast<float>(splitmix(state) >> 40) * (1.0f / 16777216.0f); // 24 bits, [0, 1)
        }

        std::string meshPath(const std::string& stored)
        {
            std::string out = stored;
            for (char& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '/')
                    c = '\\';
            }
            while (!out.empty() && out.front() == '\\')
                out.erase(out.begin());
            if (out.rfind("meshes\\", 0) != 0)
                out = "meshes\\" + out;
            return out;
        }

        // The Units From Water rule against the water surface for ground at `z`. Types are UESP's: 0 above at least,
        // 1 above at most, 2 below at least, 3 below at most, 4 either at least, 5 either at most, 6 either at most
        // above, 7 either at most below (RECALLED-VERIFY; the real-data test counts which occur). Any other value only
        // asks that the ground is not under water.
        bool waterAllows(const GrassRecord& kind, float z, const sdk::CellWater& water)
        {
            if (!water.present)
                return true;
            const float d = z - water.height; // positive: above the water
            const float u = static_cast<float>(kind.mUnitsFromWater);
            switch (kind.mUnitsFromWaterType)
            {
                case 0: return d >= u;
                case 1: return d >= 0.0f && d <= u;
                case 2: return -d >= u;
                case 3: return d <= 0.0f && -d <= u;
                case 4: return std::fabs(d) >= u;
                case 5: return std::fabs(d) <= u;
                case 6: return d <= u;
                case 7: return -d <= u;
                default: return d >= 0.0f;
            }
        }

        struct Candidate
        {
            FormId mKind;
            const GrassRecord* mRecord;
        };

        float height(const sdk::TerrainCell& cell, std::uint32_t row, std::uint32_t col)
        {
            return cell.heights[static_cast<std::size_t>(row) * cell.vertsPerSide + col];
        }
    }

    bool decodeGrass(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        GrassRecord& out, std::string& why)
    {
        const Fields fields{ context, "GRAS", why };
        out = GrassRecord();
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
                case Content::fourCC("MODL"):
                    if (!fields.zstring(type, data, out.mModel))
                        return false;
                    break;
                case Content::fourCC("DATA"):
                {
                    if (data.size() != kDataBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    const std::byte* p = data.data();
                    GrassRecord parsed;
                    parsed.mDensity = std::to_integer<std::uint8_t>(p[0]);
                    parsed.mMinSlope = std::to_integer<std::uint8_t>(p[1]);
                    parsed.mMaxSlope = std::to_integer<std::uint8_t>(p[2]);
                    parsed.mUnitsFromWater = readLe<std::uint16_t>(p + 4);
                    parsed.mUnitsFromWaterType = readLe<std::uint32_t>(p + 8);
                    parsed.mPositionRange = readLe<float>(p + 12);
                    parsed.mHeightRange = readLe<float>(p + 16);
                    parsed.mColorRange = readLe<float>(p + 20);
                    parsed.mWavePeriod = readLe<float>(p + 24);
                    parsed.mFlags = std::to_integer<std::uint8_t>(p[28]);
                    // A float that is not finite would reach the scale arithmetic: the DATA is then not used.
                    if (!std::isfinite(parsed.mPositionRange) || !std::isfinite(parsed.mHeightRange)
                        || !std::isfinite(parsed.mColorRange) || !std::isfinite(parsed.mWavePeriod))
                    {
                        fields.odd(type);
                        break;
                    }
                    out.mHasData = true;
                    out.mDensity = parsed.mDensity;
                    out.mMinSlope = parsed.mMinSlope;
                    out.mMaxSlope = parsed.mMaxSlope;
                    out.mUnitsFromWater = parsed.mUnitsFromWater;
                    out.mUnitsFromWaterType = parsed.mUnitsFromWaterType;
                    out.mPositionRange = parsed.mPositionRange;
                    out.mHeightRange = parsed.mHeightRange;
                    out.mColorRange = parsed.mColorRange;
                    out.mWavePeriod = parsed.mWavePeriod;
                    out.mFlags = parsed.mFlags;
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

    sdk::WorldId dominantTexture(const sdk::TerrainCell& cell, std::uint32_t quadrant, float u, float v)
    {
        if (quadrant >= 4)
            return 0;
        const std::uint32_t side = sdk::kTerrainLayerSide;
        const float fu = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(side - 1);
        const float fv = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(side - 1);
        const std::uint32_t c0 = std::min<std::uint32_t>(static_cast<std::uint32_t>(fu), side - 2);
        const std::uint32_t r0 = std::min<std::uint32_t>(static_cast<std::uint32_t>(fv), side - 2);
        const float tu = fu - static_cast<float>(c0);
        const float tv = fv - static_cast<float>(r0);

        // Each layer is drawn over what lies under it: it takes `a` of the picture and everything below keeps 1 - a.
        // The winner is the texture with the largest share; on a tie the one drawn later (on top) wins.
        const auto& layers = cell.layers[quadrant];
        const std::size_t count = std::min(layers.size(), kMaxLayersLooked);
        std::array<float, kMaxLayersLooked + 1> weights{};
        weights[0] = 1.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            float a = 0.0f;
            const sdk::TerrainLayer& layer = layers[i];
            if (layer.opacity.size() == static_cast<std::size_t>(side) * side)
            {
                const auto at = [&](std::uint32_t r, std::uint32_t c) {
                    return static_cast<float>(layer.opacity[static_cast<std::size_t>(r) * side + c]) / 255.0f;
                };
                const float south = at(r0, c0) * (1.0f - tu) + at(r0, c0 + 1) * tu;
                const float north = at(r0 + 1, c0) * (1.0f - tu) + at(r0 + 1, c0 + 1) * tu;
                a = south * (1.0f - tv) + north * tv;
            }
            for (std::size_t j = 0; j <= i; ++j)
                weights[j] *= 1.0f - a;
            weights[i + 1] = a;
        }
        std::size_t best = 0;
        for (std::size_t i = 1; i <= count; ++i)
            if (weights[i] >= weights[best])
                best = i;
        return best == 0 ? cell.baseTextures[quadrant] : layers[best - 1].texture;
    }

    void placeGrass(const sdk::TerrainCell& cell, float cellSize, const GrassTables& tables, const sdk::CellWater& water,
        const GrassPlacementOptions& options, sdk::GrassCell& out)
    {
        out = sdk::GrassCell();
        const std::uint32_t side = cell.vertsPerSide;
        if (side < 3 || !cell.hasHeights || cell.heights.size() != static_cast<std::size_t>(side) * side
            || !(cellSize > 0.0f) || (side - 1) % 2 != 0 || tables.mGrassOfTexture.empty())
            return;
        const std::uint32_t squares = side - 1;
        const std::uint32_t half = squares / 2; // squares per quadrant side
        const float squareSize = cellSize / static_cast<float>(squares);
        const float originX = static_cast<float>(cell.gridX) * cellSize;
        const float originY = static_cast<float>(cell.gridY) * cellSize;
        const float inv = 1.0f / static_cast<float>(half);

        const auto grassOf = [&](sdk::WorldId texture) -> const std::vector<FormId>* {
            const auto found = tables.mGrassOfTexture.find(texture);
            return found == tables.mGrassOfTexture.end() ? nullptr : &found->second;
        };
        const auto usable = [&](FormId id) -> const GrassRecord* {
            const auto found = tables.mKinds.find(id);
            if (found == tables.mKinds.end())
                return nullptr;
            const GrassRecord& g = found->second;
            return g.mHasData && g.mDensity > 0 && !g.mModel.empty() ? &g : nullptr;
        };
        const auto quadrantOf = [&](std::uint32_t row, std::uint32_t col) {
            return (row >= half ? 2u : 0u) + (col >= half ? 1u : 0u);
        };

        // The kinds a square may hold: those of the textures that dominate at its corners and middle.
        const auto candidates = [&](std::uint32_t row, std::uint32_t col, std::vector<Candidate>& list) {
            list.clear();
            const std::uint32_t quadrant = quadrantOf(row, col);
            const float lr = static_cast<float>(row % half);
            const float lc = static_cast<float>(col % half);
            const float us[5] = { 0.0f, 1.0f, 0.0f, 1.0f, 0.5f };
            const float vs[5] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.5f };
            for (int i = 0; i < 5; ++i)
            {
                const sdk::WorldId texture = dominantTexture(cell, quadrant, (lc + us[i]) * inv, (lr + vs[i]) * inv);
                const std::vector<FormId>* ids = grassOf(texture);
                if (!ids)
                    continue;
                for (const FormId id : *ids)
                    if (const GrassRecord* g = usable(id))
                        list.push_back({ id, g });
            }
            std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.mKind < b.mKind; });
            list.erase(std::unique(list.begin(), list.end(),
                           [](const Candidate& a, const Candidate& b) { return a.mKind == b.mKind; }),
                list.end());
        };

        // Pass 1: how many tufts the data asks for, to scale every density down together when a cell would hold too many.
        std::vector<Candidate> list;
        double requested = 0.0;
        for (std::uint32_t row = 0; row < squares; ++row)
            for (std::uint32_t col = 0; col < squares; ++col)
            {
                candidates(row, col, list);
                for (const Candidate& c : list)
                    requested += static_cast<double>(c.mRecord->mDensity) * options.mTuftsPerDensityPoint;
            }
        out.requested = static_cast<std::uint32_t>(std::min(requested, 4.0e9));
        if (requested <= 0.0)
            return;
        const double thin = requested > options.mMaxInstancesPerCell
            ? static_cast<double>(options.mMaxInstancesPerCell) / requested
            : 1.0;

        std::map<FormId, std::uint32_t> modelIndex;
        out.instances.reserve(static_cast<std::size_t>(std::min(requested * thin, 1.0e6)) + 16);
        for (std::uint32_t row = 0; row < squares; ++row)
        {
            for (std::uint32_t col = 0; col < squares; ++col)
            {
                candidates(row, col, list);
                if (list.empty())
                    continue;
                const std::uint32_t quadrant = quadrantOf(row, col);
                const float h00 = height(cell, row, col);
                const float h10 = height(cell, row, col + 1);
                const float h01 = height(cell, row + 1, col);
                const float h11 = height(cell, row + 1, col + 1);
                // The slope of the square, from its mean gradient.
                const float gx = ((h10 - h00) + (h11 - h01)) * 0.5f / squareSize;
                const float gy = ((h01 - h00) + (h11 - h10)) * 0.5f / squareSize;
                const float slopeDegrees = std::atan(std::sqrt(gx * gx + gy * gy)) / kRadiansPerDegree;
                for (const Candidate& c : list)
                {
                    const GrassRecord& kind = *c.mRecord;
                    if (slopeDegrees < static_cast<float>(kind.mMinSlope) || slopeDegrees > static_cast<float>(kind.mMaxSlope))
                        continue;
                    std::uint64_t state = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.world)) << 32)
                        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.gridX)) * 0x9E3779B1ull)
                        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.gridY)) << 16)
                        ^ (static_cast<std::uint64_t>(c.mKind) * 0x85EBCA6Bull)
                        ^ (static_cast<std::uint64_t>(row * squares + col) << 40);
                    const double expected = static_cast<double>(kind.mDensity) * options.mTuftsPerDensityPoint * thin;
                    const std::uint32_t whole = static_cast<std::uint32_t>(expected);
                    const std::uint32_t count = whole + (unit(state) < static_cast<float>(expected - whole) ? 1u : 0u);
                    for (std::uint32_t j = 0; j < count; ++j)
                    {
                        const float su = unit(state);
                        const float sv = unit(state);
                        const float yaw = unit(state) * 2.0f * kPi;
                        const float variation = unit(state) * 2.0f - 1.0f;
                        // Only the tufts standing on a ground that names this grass are kept (the corner test above is
                        // a superset: a texture can lose the square's middle).
                        const sdk::WorldId texture = dominantTexture(cell, quadrant,
                            (static_cast<float>(col % half) + su) * inv, (static_cast<float>(row % half) + sv) * inv);
                        const std::vector<FormId>* ids = grassOf(texture);
                        if (!ids || std::find(ids->begin(), ids->end(), c.mKind) == ids->end())
                            continue;
                        const float z = (h00 * (1.0f - su) + h10 * su) * (1.0f - sv) + (h01 * (1.0f - su) + h11 * su) * sv;
                        if (!waterAllows(kind, z, water))
                            continue;
                        auto found = modelIndex.find(c.mKind);
                        if (found == modelIndex.end())
                        {
                            found = modelIndex.emplace(c.mKind, static_cast<std::uint32_t>(out.models.size())).first;
                            sdk::GrassModel model;
                            model.modelPath = meshPath(kind.mModel);
                            model.vertexLighting = (kind.mFlags & GrassFlags::VertexLighting) != 0;
                            out.models.push_back(std::move(model));
                        }
                        sdk::GrassInstance instance;
                        instance.model = found->second;
                        instance.x = originX + (static_cast<float>(col) + su) * squareSize;
                        instance.y = originY + (static_cast<float>(row) + sv) * squareSize;
                        instance.z = z;
                        instance.yaw = yaw;
                        instance.scale = std::clamp(1.0f + variation * std::clamp(kind.mHeightRange, 0.0f, 1.0f),
                            options.mMinScale, options.mMaxScale);
                        out.instances.push_back(instance);
                    }
                }
            }
        }
    }
}
