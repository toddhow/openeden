#ifndef OPENEDEN_GAME_SKYRIMSE_TERRAIN_SOURCE_H
#define OPENEDEN_GAME_SKYRIMSE_TERRAIN_SOURCE_H

// Skyrim SE exterior terrain for the neutral world model (task 0120, stage E1 of docs/research/0053): the load order's
// worldspaces, LAND records and ground textures, merged in load order and handed out as the SDK's plain values
// (openeden/game/world_terrain.hpp). Built on the stage E0 decoders (land.hpp, worldspace.hpp).
//
// Memory: the index keeps, per LAND record, only its place (world, cell grid, plugin, file offset); the heights,
// normals, colours and layers (about 11 KiB each, 54,000 records in the five official masters) are read and decoded
// again when buildTerrain asks for the cell. Worlds, ground textures and texture sets are small and are kept decoded.
//
// Overriding follows the load order: a later plugin's WRLD fields, LTEX, TXST and LAND replace an earlier one's for the
// same resolved id (or cell); a LAND flagged deleted removes the cell's land. The parent-world fallback is NOT done here
// (the SDK says the core walks WorldSpaceInfo::parent).
//
// Null and dangling ground textures: a stored null texture id, an id that is in no LTEX of the load order, and an LTEX
// with no usable diffuse path all become texture id 0, the game's default ground texture
// (textures\landscape\dirt02.dds; docs/tasks/0117 settled this from the data).
//
// Threading: the queries are const and read only the index and the plugin files (each read opens its own stream), so
// they may run on one thread at a time per object; the core calls them on the main thread.
// Hostile input: everything is decoded by the E0 decoders under ExteriorLimits; nothing throws.

#include "game_skyrimse/worldspace.hpp"

#include <openeden/game/world_terrain.hpp>
#include <openeden/game/world_water.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace game_skyrimse
{
    namespace TerrainDefaults
    {
        inline constexpr const char* kDiffuse = "textures\\landscape\\dirt02.dds";
        inline constexpr const char* kNormal = "textures\\landscape\\dirt02_n.dds";
    }

    class SkyrimTerrainIndex
    {
    public:
        struct Stats
        {
            std::size_t mPlugins = 0;
            std::size_t mWorlds = 0;
            std::size_t mLands = 0;
            std::size_t mGroundTextures = 0;
            std::uint64_t mLandReads = 0;
            std::uint64_t mRecordErrors = 0;
            std::uint64_t mUnresolvedFormIds = 0;
        };

        explicit SkyrimTerrainIndex(ExteriorLimits limits = {});
        ~SkyrimTerrainIndex();
        SkyrimTerrainIndex(const SkyrimTerrainIndex&) = delete;
        SkyrimTerrainIndex& operator=(const SkyrimTerrainIndex&) = delete;

        /// Indexes one plugin. `order` must hold the plugin and its masters, and every later call must pass a load
        /// order that extends the earlier ones. False with a located `error` for a file that cannot be opened or walked;
        /// the plugin is then not part of the index (records before a damaged place are kept when the walk reached them).
        [[nodiscard]] bool addPluginFile(const std::filesystem::path& path, const LoadOrder& order, std::string& error);
        /// The same on bytes in memory (the tests); the index keeps the bytes to read LAND records from.
        [[nodiscard]] bool addPluginBytes(
            std::string bytes, const std::string& name, const LoadOrder& order, std::string& error);

        // The SDK's terrain functions (world_source.hpp 1.1); listWorlds is the index's own (the SDK finds a world by its
        // editor id through findWorldByEditorId).
        [[nodiscard]] bool worldInfo(openeden::sdk::game::WorldId world, openeden::sdk::game::WorldSpaceInfo& out) const;
        std::size_t listWorlds(std::vector<openeden::sdk::game::WorldSpaceInfo>& out) const;
        [[nodiscard]] openeden::sdk::game::TerrainResult buildTerrain(openeden::sdk::game::WorldId world, std::int32_t x,
            std::int32_t y, openeden::sdk::game::TerrainCell& out, std::string& error) const;
        std::size_t listGroundTextures(std::vector<openeden::sdk::game::GroundTexture>& out) const;
        /// The water of one grid square (task 0129; sdk world_water.hpp). The cell's own XCLW wins (a real height is the
        /// surface, a "no water" sentinel means none); a cell with the world-default value, no XCLW or no CELL record
        /// takes the world's default water height (WRLD DNAM, second float). A world whose PNAM has the water bit takes
        /// its parent's default; a square the world does not list is looked up in the parent when the parent lends its
        /// land (the same rule as the land). False for an unknown world.
        [[nodiscard]] bool cellWater(
            openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y, openeden::sdk::game::CellWater& out) const;

        [[nodiscard]] Stats stats() const { return mStats; }
        [[nodiscard]] const std::vector<Content::Error>& errors() const { return mErrors; }

    private:
        struct PluginSource
        {
            std::string mName;
            Content::Header mHeader;
            std::function<std::unique_ptr<std::istream>()> mOpen;
        };
        struct LandPlace
        {
            std::uint32_t mPlugin = 0;
            std::uint64_t mOffset = 0;
        };
        using LandKey = std::tuple<FormId, std::int32_t, std::int32_t>;

        bool addPlugin(PluginSource source, const LoadOrder& order, std::string& error);
        void merge(ExteriorData& data, std::uint32_t pluginIndex);
        [[nodiscard]] openeden::sdk::game::WorldId mapTexture(FormId texture) const;
        [[nodiscard]] bool defaultWater(FormId world, float& height) const;
        [[nodiscard]] bool textureUsable(FormId texture, openeden::sdk::game::GroundTexture* out) const;

        ExteriorLimits mLimits;
        LoadOrder mOrder;
        std::vector<PluginSource> mPlugins;
        std::map<FormId, WorldSpaceRecord> mWorlds;
        std::map<LandKey, LandPlace> mLands;
        std::map<LandKey, game_skyrimse::CellWater> mCellWaters; ///< by the cell's world and grid; later plugins replace
        std::map<FormId, GroundTextureRecord> mGround;
        std::map<FormId, TextureSetRecord> mSets;
        std::vector<Content::Error> mErrors;
        mutable Stats mStats;
    };
}

#endif
