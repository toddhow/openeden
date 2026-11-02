#include "tilemodel.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

#include <components/lod/tilebake.hpp>

namespace CSMLod
{
    namespace
    {
        // The relief ramp's stops: water blue, lowland green, upland tan, snow white. Display colours only.
        struct RampStop
        {
            float mT;
            Rgba mColour;
        };
        constexpr RampStop rampStops[] = {
            { 0.f, reliefLow },
            { 0.25f, { 40, 140, 60, 255 } },
            { 0.6f, { 190, 170, 90, 255 } },
            { 1.f, reliefHigh },
        };

        // The camera cross is this many pixels from its centre to its tip, and the heading line this long.
        constexpr int cameraCrossRadius = 2;
        constexpr int cameraHeadingLength = 14;

        std::uint8_t lerpByte(std::uint8_t a, std::uint8_t b, float f) noexcept
        {
            return static_cast<std::uint8_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * f));
        }

        void putPixel(RgbaImage& image, int x, int y, Rgba colour) noexcept
        {
            if (x < 0 || y < 0 || x >= static_cast<int>(image.mWidth) || y >= static_cast<int>(image.mHeight))
                return;
            std::uint8_t* p = image.mPixels.data() + (static_cast<std::size_t>(y) * image.mWidth + x) * 4;
            p[0] = colour.mR;
            p[1] = colour.mG;
            p[2] = colour.mB;
            p[3] = colour.mA;
        }

        RgbaImage makeImage(std::uint32_t width, std::uint32_t height, Rgba fill)
        {
            RgbaImage image;
            image.mWidth = width;
            image.mHeight = height;
            image.mPixels.resize(static_cast<std::size_t>(width) * height * 4);
            for (std::size_t i = 0; i < image.mPixels.size(); i += 4)
            {
                image.mPixels[i] = fill.mR;
                image.mPixels[i + 1] = fill.mG;
                image.mPixels[i + 2] = fill.mB;
                image.mPixels[i + 3] = fill.mA;
            }
            return image;
        }

        bool gridIsValid(const Lod::TerrainTile& tile) noexcept
        {
            return tile.mGridSize >= 2
                && tile.mHeights.size() == static_cast<std::size_t>(tile.mGridSize) * tile.mGridSize;
        }

        float heightOf(const Lod::TerrainTile& tile, std::size_t index) noexcept
        {
            return tile.mMinHeight
                + static_cast<float>(tile.mHeights[index]) / 65535.f * (tile.mMaxHeight - tile.mMinHeight);
        }

        void normalOf(const Lod::TerrainTile& tile, std::size_t index, float& x, float& y, float& z) noexcept
        {
            if (tile.mHasNormals && tile.mNormals.size() >= (index + 1) * 2)
                Lod::decodeNormal(tile.mNormals[index * 2], tile.mNormals[index * 2 + 1], x, y, z);
            else
            {
                x = 0.f;
                y = 0.f;
                z = 1.f;
            }
        }

        Rgba darker(Rgba colour) noexcept
        {
            return { static_cast<std::uint8_t>(colour.mR / 2), static_cast<std::uint8_t>(colour.mG / 2),
                static_cast<std::uint8_t>(colour.mB / 2), colour.mA };
        }

        bool parseDouble(std::string_view text, double& value) noexcept
        {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, value);
            return ec == std::errc() && ptr == end && std::isfinite(value);
        }

        std::string_view trim(std::string_view text) noexcept
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            return text;
        }
    }

    Rgba reliefRamp(float t) noexcept
    {
        if (!(t > 0.f))
            return reliefLow;
        if (t >= 1.f)
            return reliefHigh;
        for (std::size_t i = 1; i < std::size(rampStops); ++i)
        {
            if (t <= rampStops[i].mT)
            {
                const RampStop& a = rampStops[i - 1];
                const RampStop& b = rampStops[i];
                const float f = (t - a.mT) / (b.mT - a.mT);
                return { lerpByte(a.mColour.mR, b.mColour.mR, f), lerpByte(a.mColour.mG, b.mColour.mG, f),
                    lerpByte(a.mColour.mB, b.mColour.mB, f), 255 };
            }
        }
        return reliefHigh;
    }

    Lod::LodError buildTileMesh(const Lod::TerrainTile& tile, TileMesh& mesh)
    {
        mesh = TileMesh{};
        if (!gridIsValid(tile))
            return { "gridSize", 0, "the grid is smaller than 2 or the height array has the wrong size" };

        const std::uint32_t n = tile.mGridSize;
        const float step = tile.mSide / static_cast<float>(n - 1);
        const std::size_t triangles = static_cast<std::size_t>(2) * (n - 1) * (n - 1);
        mesh.mGridSize = n;
        mesh.mPositions.reserve(triangles * 9);
        mesh.mNormals.reserve(triangles * 9);

        auto addVertex = [&](std::uint32_t i, std::uint32_t j) {
            const std::size_t index = static_cast<std::size_t>(j) * n + i;
            mesh.mPositions.push_back(tile.mOriginX + static_cast<float>(i) * step);
            mesh.mPositions.push_back(tile.mOriginY + static_cast<float>(j) * step);
            mesh.mPositions.push_back(heightOf(tile, index));
            float nx = 0.f, ny = 0.f, nz = 1.f;
            normalOf(tile, index, nx, ny, nz);
            mesh.mNormals.push_back(nx);
            mesh.mNormals.push_back(ny);
            mesh.mNormals.push_back(nz);
        };

        for (std::uint32_t j = 0; j + 1 < n; ++j)
        {
            for (std::uint32_t i = 0; i + 1 < n; ++i)
            {
                // a = (i, j), b = (i + 1, j), c = (i + 1, j + 1), d = (i, j + 1): (a, b, c) then (a, c, d).
                addVertex(i, j);
                addVertex(i + 1, j);
                addVertex(i + 1, j + 1);
                addVertex(i, j);
                addVertex(i + 1, j + 1);
                addVertex(i, j + 1);
            }
        }
        return {};
    }

    RgbaImage heightImage(const Lod::TerrainTile& tile)
    {
        if (!gridIsValid(tile))
            return {};
        const std::uint32_t n = tile.mGridSize;
        RgbaImage image = makeImage(n, n, reliefLow);
        const float range = tile.mMaxHeight - tile.mMinHeight;
        for (std::uint32_t row = 0; row < n; ++row)
        {
            const std::uint32_t j = n - 1 - row; // row 0 is the north row
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const float h = heightOf(tile, static_cast<std::size_t>(j) * n + i);
                const float t = range > 0.f ? (h - tile.mMinHeight) / range : 0.f;
                putPixel(image, static_cast<int>(i), static_cast<int>(row), reliefRamp(t));
            }
        }
        return image;
    }

    RgbaImage normalImage(const Lod::TerrainTile& tile)
    {
        if (!gridIsValid(tile))
            return {};
        const std::uint32_t n = tile.mGridSize;
        RgbaImage image = makeImage(n, n, { 0, 0, 0, 255 });
        auto channel = [](float c) {
            const long v = std::lround((0.5 + 0.5 * static_cast<double>(c)) * 255.0);
            return static_cast<std::uint8_t>(std::clamp(v, 0l, 255l));
        };
        for (std::uint32_t row = 0; row < n; ++row)
        {
            const std::uint32_t j = n - 1 - row;
            for (std::uint32_t i = 0; i < n; ++i)
            {
                float x = 0.f, y = 0.f, z = 1.f;
                normalOf(tile, static_cast<std::size_t>(j) * n + i, x, y, z);
                putPixel(
                    image, static_cast<int>(i), static_cast<int>(row), { channel(x), channel(y), channel(z), 255 });
            }
        }
        return image;
    }

    RgbaImage albedoImage(const Lod::TerrainTile& tile)
    {
        if (!tile.mHasAlbedo || tile.mAlbedoFormat != Lod::AlbedoFormat::Rgba8)
            return {};
        const std::size_t bytes = static_cast<std::size_t>(tile.mAlbedoWidth) * tile.mAlbedoHeight * 4;
        if (bytes == 0 || tile.mAlbedo.size() < bytes)
            return {};
        RgbaImage image;
        image.mWidth = tile.mAlbedoWidth;
        image.mHeight = tile.mAlbedoHeight;
        image.mPixels.assign(tile.mAlbedo.begin(), tile.mAlbedo.begin() + static_cast<std::ptrdiff_t>(bytes));
        return image;
    }

    std::string summarise(const WorldGridResult& result)
    {
        return "tiles " + std::to_string(result.mTiles.size()) + ", drawn " + std::to_string(result.mDrawn) + ", live "
            + std::to_string(result.mLive) + ", skipped " + std::to_string(result.mSkipped);
    }

    std::string parseCameraSpec(std::string_view text, WorldGridOptions& options)
    {
        static constexpr const char* names[] = { "x", "y", "z", "heading", "fov", "viewport", "threshold", "mode" };
        WorldGridOptions result = options;

        std::size_t field = 0;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const std::size_t comma = text.find(',', start);
            const std::string_view part
                = trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
            if (field >= std::size(names))
                return "too many fields (at most 8: x,y,z,heading,fov,viewport,threshold,mode)";

            if (!part.empty())
            {
                if (field == 7)
                {
                    if (part == "auto")
                        result.mMode = ForcedMode::Auto;
                    else if (part == "tile")
                        result.mMode = ForcedMode::AllTile;
                    else if (part == "live")
                        result.mMode = ForcedMode::AllLive;
                    else
                        return std::string("mode must be auto, tile or live, not \"") + std::string(part) + "\"";
                }
                else
                {
                    double v = 0;
                    if (!parseDouble(part, v))
                        return std::string("field ") + names[field] + " is not a number: \"" + std::string(part) + "\"";
                    switch (field)
                    {
                        case 0:
                            result.mCamera.mX = v;
                            break;
                        case 1:
                            result.mCamera.mY = v;
                            break;
                        case 2:
                            result.mCamera.mZ = v;
                            break;
                        case 3:
                        {
                            const double radians = v * std::numbers::pi / 180.0;
                            result.mCamera.mForwardX = std::sin(radians);
                            result.mCamera.mForwardY = std::cos(radians);
                            result.mCamera.mForwardZ = 0;
                            break;
                        }
                        case 4:
                            if (!(v > 0.0 && v < 180.0))
                                return "field fov must be between 0 and 180 degrees";
                            result.mCamera.mFovY = v * std::numbers::pi / 180.0;
                            break;
                        case 5:
                            if (!(v > 0.0))
                                return "field viewport must be above 0";
                            result.mCamera.mViewportHeight = v;
                            break;
                        default:
                            if (v < 0.0)
                                return "field threshold must not be negative";
                            result.mThreshold = static_cast<float>(v);
                            break;
                    }
                }
            }
            ++field;
            if (comma == std::string_view::npos)
                break;
            start = comma + 1;
        }
        options = result;
        return {};
    }

    std::unique_ptr<TileModel> TileModel::open(const std::filesystem::path& path, Lod::LodError& error)
    {
        std::unique_ptr<Lod::TilePack> pack = Lod::TilePack::open(path, error);
        if (!pack)
            return nullptr;
        return adopt(std::move(pack), error);
    }

    std::unique_ptr<TileModel> TileModel::adopt(std::unique_ptr<Lod::TilePack> pack, Lod::LodError& error)
    {
        error = {};
        if (!pack)
        {
            error = { "pack", 0, "no pack" };
            return nullptr;
        }
        std::unique_ptr<TileModel> model(new TileModel);
        model->mPack = std::move(pack);
        const auto& worldspaces = model->mPack->manifest().mWorldspaces;
        if (!worldspaces.empty() && worldspaces.front().mCellSize > 0)
            model->mCellSize = worldspaces.front().mCellSize;

        for (const Lod::TileId& id : model->mPack->tileIds())
        {
            Lod::TerrainTile tile;
            Lod::TerrainTileLayout layout;
            error = model->mPack->readHeader(id, tile, layout);
            if (error)
                return nullptr;
            const std::optional<std::uint64_t> key = Lod::packTileKey(id);
            if (!key)
            {
                error = { "key", 0, "a tile id does not pack" };
                return nullptr;
            }
            TileRow row;
            row.mId = id;
            row.mKey = *key;
            row.mLevel = id.mLevel;
            row.mTileX = id.mX;
            row.mTileY = id.mY;
            Lod::tileOriginCell(id, row.mCellX, row.mCellY);
            row.mOriginX = tile.mOriginX;
            row.mOriginY = tile.mOriginY;
            row.mSide = tile.mSide;
            row.mMinHeight = tile.mMinHeight;
            row.mMaxHeight = tile.mMaxHeight;
            row.mError = tile.mGeometricError;
            row.mSizeBytes = layout.mTotalSize;
            row.mFlags = static_cast<std::uint16_t>(
                (tile.mHasNormals ? Lod::flagHasNormals : 0) | (tile.mHasAlbedo ? Lod::flagHasAlbedo : 0));
            row.mGridSize = tile.mGridSize;
            model->mRows.push_back(row);
        }
        std::sort(model->mRows.begin(), model->mRows.end(),
            [](const TileRow& a, const TileRow& b) { return a.mKey < b.mKey; });
        return model;
    }

    Lod::LodError TileModel::readTile(std::size_t row, Lod::TerrainTile& tile) const
    {
        if (row >= mRows.size())
            return { "row", 0, "no such row" };
        return mPack->readTile(mRows[row].mId, tile);
    }

    WorldGridView::WorldGridView(const TileModel& model)
        : mModel(model)
    {
        mStates.resize(model.rows().size());
        for (std::size_t i = 0; i < mStates.size(); ++i)
        {
            const TileRow& row = model.rows()[i];
            Lod::TileBounds& b = mStates[i].mBounds;
            b.mOriginX = row.mOriginX;
            b.mOriginY = row.mOriginY;
            b.mSide = row.mSide;
            b.mMinHeight = row.mMinHeight;
            b.mMaxHeight = row.mMaxHeight;
            b.mGeometricError = row.mError;
        }
    }

    WorldGridResult WorldGridView::update(const WorldGridOptions& options)
    {
        Lod::selectTiles(mStates, options.mCamera, options.mThreshold);

        const auto& rows = mModel.rows();
        WorldGridResult result;
        result.mTiles.resize(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const Lod::TileState& state = mStates[i];
            WorldGridTile& out = result.mTiles[i];
            out.mRow = i;
            if (!state.mVisible)
            {
                out.mDraw = TileDraw::Skipped;
                ++result.mSkipped;
                continue;
            }
            out.mPixels = state.mPixels;
            bool asTile = state.mChoice == Lod::TileChoice::Tile;
            if (options.mMode == ForcedMode::AllTile)
                asTile = true;
            else if (options.mMode == ForcedMode::AllLive)
                asTile = false;
            out.mDraw = asTile ? TileDraw::Tile : TileDraw::Live;
            if (asTile)
                ++result.mDrawn;
            else
                ++result.mLive;
        }

        if (rows.empty())
            return result;

        // The map covers the bounding box of all tiles, in cells.
        std::int64_t minX = rows.front().mCellX, minY = rows.front().mCellY;
        std::int64_t maxX = minX, maxY = minY;
        for (const TileRow& row : rows)
        {
            const std::int64_t side = std::int64_t{ 1 } << row.mLevel;
            minX = std::min<std::int64_t>(minX, row.mCellX);
            minY = std::min<std::int64_t>(minY, row.mCellY);
            maxX = std::max<std::int64_t>(maxX, row.mCellX + side);
            maxY = std::max<std::int64_t>(maxY, row.mCellY + side);
        }
        const std::int64_t widthCells = maxX - minX;
        const std::int64_t heightCells = maxY - minY;
        result.mMinCellX = static_cast<std::int32_t>(minX);
        result.mMinCellY = static_cast<std::int32_t>(minY);

        // The map is at most mapMaxSide pixels a side: the scale is reduced to fit, and when even one pixel a cell
        // does not fit (a damaged pack with far-apart or huge tiles) there is no map. All sums are 64-bit.
        const std::int64_t longest = std::max(widthCells, heightCells);
        if (longest > mapMaxSide)
            return result;
        const std::int64_t scale
            = std::min<std::int64_t>(std::max<std::uint32_t>(options.mScale, 1), mapMaxSide / longest);
        result.mScale = static_cast<std::uint32_t>(scale);
        result.mMap = makeImage(static_cast<std::uint32_t>(widthCells * scale),
            static_cast<std::uint32_t>(heightCells * scale), mapBackground);

        // Coarser tiles first, so that finer ones lie on top.
        std::vector<std::size_t> order(rows.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
            [&rows](std::size_t a, std::size_t b) { return rows[a].mLevel > rows[b].mLevel; });

        for (std::size_t index : order)
        {
            const TileRow& row = rows[index];
            Rgba colour = mapSkipped;
            if (result.mTiles[index].mDraw == TileDraw::Tile)
                colour = mapTile;
            else if (result.mTiles[index].mDraw == TileDraw::Live)
                colour = mapLive;

            const std::int64_t sideCells = std::int64_t{ 1 } << row.mLevel;
            const int x0 = static_cast<int>((row.mCellX - minX) * scale);
            const int size = static_cast<int>(sideCells * scale);
            const int y0 = static_cast<int>((heightCells - (row.mCellY - minY) - sideCells) * scale);
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const bool ring = scale >= 2 && (x == 0 || y == 0 || x == size - 1 || y == size - 1);
                    putPixel(result.mMap, x0 + x, y0 + y, ring ? darker(colour) : colour);
                }
            }
        }

        // The camera: a cross and a line along its heading, when it is inside the map.
        const double cellSize = static_cast<double>(mModel.cellSize());
        const double px = (options.mCamera.mX / cellSize - static_cast<double>(minX)) * scale;
        const double py
            = (static_cast<double>(heightCells) - (options.mCamera.mY / cellSize - static_cast<double>(minY))) * scale;
        if (px >= 0.0 && py >= 0.0 && px < static_cast<double>(result.mMap.mWidth)
            && py < static_cast<double>(result.mMap.mHeight))
        {
            const int cx = static_cast<int>(std::floor(px));
            const int cy = static_cast<int>(std::floor(py));
            result.mCameraPixelX = cx;
            result.mCameraPixelY = cy;
            for (int d = -cameraCrossRadius; d <= cameraCrossRadius; ++d)
            {
                putPixel(result.mMap, cx + d, cy, mapCamera);
                putPixel(result.mMap, cx, cy + d, mapCamera);
            }
            for (int s = 1; s <= cameraHeadingLength; ++s)
            {
                putPixel(result.mMap, cx + static_cast<int>(std::lround(options.mCamera.mForwardX * s)),
                    cy - static_cast<int>(std::lround(options.mCamera.mForwardY * s)), mapCamera);
            }
        }
        return result;
    }
}
