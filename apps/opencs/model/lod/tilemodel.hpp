#ifndef CSM_LOD_TILEMODEL_H
#define CSM_LOD_TILEMODEL_H

// Eden Studio's tile viewer, the model (task 0055, stage S1e). Plain data, no GPU, no Qt widgets: it opens a baked
// terrain tile pack (components/lod), lists its tiles, turns one tile into a triangle list and three RGBA8 pictures,
// and shows what Lod::selectTiles decides for a camera as a world map picture. The panel (view/lod) and the
// capture hook only draw what this returns. Units are Morrowind world units, Z up.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/lod/loderror.hpp>
#include <components/lod/terraintile.hpp>
#include <components/lod/tilekey.hpp>
#include <components/lod/tilepack.hpp>
#include <components/lod/tileselection.hpp>

namespace CSMLod
{
    /// A picture of 8-bit red, green, blue, alpha, four bytes a pixel, rows from the TOP of the picture (for a tile or
    /// the world map, the top is the north edge). Empty (width 0) when there is nothing to show.
    struct RgbaImage
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint8_t> mPixels; // mWidth * mHeight * 4 bytes when not empty
        bool empty() const noexcept { return mWidth == 0 || mHeight == 0; }
    };

    /// One line of the tile table. Everything comes from the pack's key and the tile's 88-byte header.
    struct TileRow
    {
        Lod::TileId mId;
        std::uint64_t mKey = 0; // Lod::packTileKey(mId)
        std::uint32_t mLevel = 0; // = mId.mLevel
        std::uint32_t mTileX = 0, mTileY = 0; // the biased tile coordinates, = mId.mX, mId.mY
        std::int32_t mCellX = 0, mCellY = 0; // the signed south-west cell (Lod::tileOriginCell)
        float mOriginX = 0, mOriginY = 0; // world units, south-west corner, from the header
        float mSide = 0; // world units
        float mMinHeight = 0, mMaxHeight = 0;
        float mError = 0; // geometric error in world units
        std::uint64_t mSizeBytes = 0; // the payload's total size (layout.mTotalSize)
        std::uint16_t mFlags = 0; // Lod::flagHasNormals | Lod::flagHasAlbedo
        std::uint32_t mGridSize = 0;
    };

    /// A triangle list. For each triangle, three vertices of three floats: `mPositions` and `mNormals` both hold 9
    /// floats a triangle. A grid of N x N vertices gives 2 * (N - 1)^2 triangles.
    ///
    /// Vertex (i, j) of the grid (i from the west, j from the south) is at x = originX + i * side / (N - 1), y =
    /// originY + j * side / (N - 1), z = minHeight + h / 65535 * (maxHeight - minHeight) where h is the stored 16-bit
    /// value at index j * N + i. Grid square (i, j) has the corners a = (i, j), b = (i + 1, j), c = (i + 1, j + 1), d =
    /// (i, j + 1) and gives the triangles (a, b, c) then (a, c, d); squares go west to east, then south to north.
    /// Both triangles are counter-clockwise seen from above (+Z). `mNormals` holds the decoded per-vertex normal
    /// (Lod::decodeNormal, unit length), or (0, 0, 1) at every vertex when the tile has no normals.
    struct TileMesh
    {
        std::uint32_t mGridSize = 0;
        std::vector<float> mPositions;
        std::vector<float> mNormals;
        std::size_t triangleCount() const noexcept { return mPositions.size() / 9; }
    };

    struct Rgba
    {
        std::uint8_t mR = 0, mG = 0, mB = 0, mA = 255;
        bool operator==(const Rgba&) const = default;
    };

    /// The relief colour ramp, t clamped to 0..1: t = 0 gives reliefLow, t = 1 gives reliefHigh, and between them it
    /// is piecewise linear through colours the implementation chooses. Alpha is 255.
    inline constexpr Rgba reliefLow{ 20, 40, 120, 255 };
    inline constexpr Rgba reliefHigh{ 250, 250, 250, 255 };
    Rgba reliefRamp(float t) noexcept;

    /// A tile as a triangle list (see TileMesh). Fails (empty mesh) when the tile's grid size is below 2 or its
    /// height array does not hold gridSize^2 values.
    Lod::LodError buildTileMesh(const Lod::TerrainTile& tile, TileMesh& mesh);

    /// N x N picture of the tile's heights, row 0 the NORTH row of the grid (j = N - 1), column 0 the west column.
    /// Pixel = reliefRamp((height - min) / (max - min)) from the stored 16-bit value (t = 0 when max == min).
    /// Empty when the height array does not hold gridSize^2 values.
    RgbaImage heightImage(const Lod::TerrainTile& tile);
    /// N x N picture of the normals, same row order: each component c of the decoded unit normal becomes
    /// std::lround((0.5 + 0.5 * c) * 255) clamped to 0..255, alpha 255. A tile without normals shows (0, 0, 1)
    /// everywhere, which is (128, 128, 255).
    RgbaImage normalImage(const Lod::TerrainTile& tile);
    /// The tile's albedo, first mip level only: its own width and height, rows as stored top to bottom. Empty when the
    /// tile has no albedo or the albedo format is not Rgba8 (block compressed pictures are not decoded here).
    RgbaImage albedoImage(const Lod::TerrainTile& tile);

    /// How the world map treats the selection.
    enum class ForcedMode
    {
        Auto, // what Lod::selectTiles chooses
        AllTile, // every visible tile is shown as Tile
        AllLive, // every visible tile is shown as Live
    };

    /// What a tile is on the map this call.
    enum class TileDraw
    {
        Tile,
        Live,
        Skipped, // not visible (outside the view distance or the view cone)
    };

    struct WorldGridOptions
    {
        Lod::SelectionCamera mCamera;
        float mThreshold = Lod::thresholdMedium; // pixels
        ForcedMode mMode = ForcedMode::Auto;
        std::uint32_t mScale = 2; // map pixels along one cell (at least 1; 0 is treated as 1)
    };

    struct WorldGridTile
    {
        std::size_t mRow = 0; // index into TileModel::rows()
        TileDraw mDraw = TileDraw::Skipped;
        float mPixels = 0; // error on screen this call (0 when skipped)
    };

    /// The map colours (alpha 255): the background where no tile lies, and the tile squares by state. A square is
    /// filled with its colour; when mScale is at least 2 the outermost pixel ring of each square may be darker, so only
    /// the interior pixels are promised.
    inline constexpr Rgba mapBackground{ 20, 20, 24, 255 };
    inline constexpr Rgba mapTile{ 60, 160, 60, 255 };
    inline constexpr Rgba mapLive{ 200, 140, 40, 255 };
    inline constexpr Rgba mapSkipped{ 90, 90, 90, 255 };
    inline constexpr Rgba mapCamera{ 230, 30, 30, 255 };

    inline constexpr std::int64_t mapMaxSide = 4096;

    struct WorldGridResult
    {
        std::vector<WorldGridTile> mTiles; // one per row, in row order
        std::size_t mDrawn = 0; // TileDraw::Tile
        std::size_t mLive = 0; // TileDraw::Live
        std::size_t mSkipped = 0; // TileDraw::Skipped
        /// The map is at most mapMaxSide pixels a side: the scale is reduced to fit, and when even one pixel a cell
        /// does not fit the map is empty (and mScale 0).
        RgbaImage mMap;
        std::uint32_t mScale = 0; // the map pixels a cell actually used
        /// The map's south-west cell, and where the camera is on the map in pixels from the left and the top (-1 when
        /// it lies outside the map).
        std::int32_t mMinCellX = 0, mMinCellY = 0;
        std::int32_t mCameraPixelX = -1, mCameraPixelY = -1;
    };

    /// "tiles N, drawn D, live L, skipped S".
    std::string summarise(const WorldGridResult& result);

    /// Parses "x,y,z,heading,fov,viewport,threshold,mode" (all fields optional from the left; a missing field keeps
    /// the value already in `options`). Heading is in degrees, 0 looking north (+Y), 90 east (+X), so the forward
    /// vector is (sin h, cos h, 0); fov is the vertical field of view in degrees; viewport is the height in pixels;
    /// mode is auto, tile or live. Returns an empty string on success, otherwise a message naming the bad field and
    /// leaves `options` unchanged.
    std::string parseCameraSpec(std::string_view text, WorldGridOptions& options);

    /// An open pack. Immutable after open.
    class TileModel
    {
    public:
        /// nullptr and a located error when the pack cannot be opened or a tile's header is bad.
        static std::unique_ptr<TileModel> open(const std::filesystem::path& path, Lod::LodError& error);
        static std::unique_ptr<TileModel> adopt(std::unique_ptr<Lod::TilePack> pack, Lod::LodError& error);

        const Lod::TilePack& pack() const noexcept { return *mPack; }
        /// One row per tile, in key order (ascending Lod::packTileKey).
        const std::vector<TileRow>& rows() const noexcept { return mRows; }
        /// The cell size of the manifest's first worldspace (world units), 8192 when the manifest has none.
        std::uint32_t cellSize() const noexcept { return mCellSize; }

        /// Reads one whole tile.
        Lod::LodError readTile(std::size_t row, Lod::TerrainTile& tile) const;

    private:
        TileModel() = default;
        std::unique_ptr<Lod::TilePack> mPack;
        std::vector<TileRow> mRows;
        std::uint32_t mCellSize = 8192;
    };

    /// The selection states of one pack, kept between calls so that hysteresis works as in the game: update() runs
    /// Lod::selectTiles exactly once per call on the stored states. The model must outlive the view.
    class WorldGridView
    {
    public:
        explicit WorldGridView(const TileModel& model);

        /// Runs the selection and draws the map. In AllTile / AllLive the reported draw of every visible tile is
        /// forced, and Skipped tiles stay skipped. The map covers the bounding box of all tiles (in cells), and draws
        /// the coarser tiles first so that finer ones lie on top. The camera is drawn in mapCamera as a small cross
        /// with a line along its heading when it lies inside the map.
        WorldGridResult update(const WorldGridOptions& options);

    private:
        const TileModel& mModel;
        std::vector<Lod::TileState> mStates;
    };
}

#endif
