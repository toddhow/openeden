#include "game_skyrimse/terrain_source.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace game_skyrimse
{
    namespace sdk = openeden::sdk::game;

    namespace
    {
        constexpr std::size_t kRecordHeaderSize = 24; // Skyrim SE: type, size, flags, id, version control, form version

        std::string vfsPath(const std::string& path)
        {
            std::string out = path;
            for (char& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '/')
                    c = '\\';
            }
            while (!out.empty() && out.front() == '\\')
                out.erase(out.begin());
            if (out.rfind("textures\\", 0) != 0)
                out = "textures\\" + out;
            return out;
        }

        template <typename T>
        void takeIf(std::optional<T>& dst, const std::optional<T>& src)
        {
            if (src)
                dst = src;
        }

        void mergeWorld(WorldSpaceRecord& dst, const WorldSpaceRecord& src)
        {
            if (!src.mCommon.mEditorId.empty())
                dst.mCommon.mEditorId = src.mCommon.mEditorId;
            if (src.mParent != kNullFormId)
                dst.mParent = src.mParent;
            takeIf(dst.mParentUsage, src.mParentUsage);
            takeIf(dst.mDefaultLandHeight, src.mDefaultLandHeight);
            takeIf(dst.mDefaultWaterHeight, src.mDefaultWaterHeight);
            if (src.mWater != kNullFormId)
                dst.mWater = src.mWater;
        }
    }

    SkyrimTerrainIndex::SkyrimTerrainIndex(ExteriorLimits limits)
        : mLimits(limits)
    {
        mLimits.mDiscardLandData = true;
    }
    SkyrimTerrainIndex::~SkyrimTerrainIndex() = default;

    bool SkyrimTerrainIndex::addPlugin(PluginSource source, const LoadOrder& order, std::string& error)
    {
        Content::Error openError;
        std::unique_ptr<std::istream> stream = source.mOpen ? source.mOpen() : nullptr;
        std::unique_ptr<Content::Plugin> plugin;
        if (stream && *stream)
            plugin = Content::Plugin::open(std::move(stream), source.mName, openError);
        else
            openError.mFile = source.mName, openError.mMessage = "cannot open the file";
        if (!plugin)
        {
            error = openError.mFile.empty() ? source.mName + ": " + openError.mMessage : openError.describe();
            return false;
        }
        source.mHeader = plugin->header();
        mOrder = order;
        ExteriorData data;
        if (!parseExterior(*plugin, mOrder, mLimits, data) && data.mWalkError)
            mErrors.push_back(*data.mWalkError);
        for (const Content::Error& e : data.mErrors)
            mErrors.push_back(e);
        mStats.mRecordErrors += data.mErrors.size() + data.mErrorsDropped;
        mStats.mUnresolvedFormIds += data.mNotes.mUnresolvedFormIds;
        mPlugins.push_back(std::move(source));
        merge(data, static_cast<std::uint32_t>(mPlugins.size() - 1));
        ++mStats.mPlugins;
        return true;
    }

    bool SkyrimTerrainIndex::addPluginFile(const std::filesystem::path& path, const LoadOrder& order, std::string& error)
    {
        PluginSource source;
        source.mName = path.filename().string();
        source.mOpen = [path]() -> std::unique_ptr<std::istream> {
            return std::make_unique<std::ifstream>(path, std::ios::binary);
        };
        return addPlugin(std::move(source), order, error);
    }

    bool SkyrimTerrainIndex::addPluginBytes(
        std::string bytes, const std::string& name, const LoadOrder& order, std::string& error)
    {
        const auto shared = std::make_shared<const std::string>(std::move(bytes));
        PluginSource source;
        source.mName = name;
        source.mOpen = [shared]() -> std::unique_ptr<std::istream> {
            return std::make_unique<std::istringstream>(*shared, std::ios::binary);
        };
        return addPlugin(std::move(source), order, error);
    }

    void SkyrimTerrainIndex::merge(ExteriorData& data, std::uint32_t pluginIndex)
    {
        for (const WorldSpaceRecord& w : data.mWorlds)
        {
            if (w.mCommon.deleted() || w.mCommon.mId == kNullFormId)
                continue;
            const auto found = mWorlds.find(w.mCommon.mId);
            if (found == mWorlds.end())
                mWorlds[w.mCommon.mId] = w;
            else
                mergeWorld(found->second, w);
        }
        for (const LandRecord& l : data.mLands)
        {
            if (!l.mGrid || l.mWorld == kNullFormId || l.mWorld == kUnresolvedFormId)
                continue;
            const LandKey key{ l.mWorld, l.mGrid->x, l.mGrid->y };
            if (l.mCommon.deleted())
                mLands.erase(key);
            else
                mLands[key] = LandPlace{ pluginIndex, l.mCommon.mOffset };
        }
        for (const ExteriorCellInfo& c : data.mCells)
        {
            if (!c.mGrid || c.mWorld == kNullFormId || c.mWorld == kUnresolvedFormId)
                continue;
            const LandKey key{ c.mWorld, c.mGrid->x, c.mGrid->y };
            if (c.mCommon.deleted())
                mCellWaters.erase(key);
            else
                mCellWaters[key] = c.mWater;
        }
        for (const GroundTextureRecord& t : data.mGroundTextures)
        {
            if (t.mCommon.deleted())
                mGround.erase(t.mCommon.mId);
            else
                mGround[t.mCommon.mId] = t;
        }
        for (const TextureSetRecord& t : data.mTextureSets)
        {
            if (t.mCommon.deleted())
                mSets.erase(t.mCommon.mId);
            else
                mSets[t.mCommon.mId] = t;
        }
        for (const GrassRecord& g : data.mGrass)
        {
            if (g.mCommon.deleted())
                mGrassTables.mKinds.erase(g.mCommon.mId);
            else
                mGrassTables.mKinds[g.mCommon.mId] = g;
        }
        mGrassTables.mGrassOfTexture.clear();
        for (const auto& [id, ltex] : mGround)
        {
            if (ltex.mGrass.empty())
                continue;
            std::vector<FormId>& list = mGrassTables.mGrassOfTexture[id];
            for (const FormId grass : ltex.mGrass)
                if (grass != kNullFormId && grass != kUnresolvedFormId)
                    list.push_back(grass);
        }
        mStats.mWorlds = mWorlds.size();
        mStats.mLands = mLands.size();
        mStats.mGroundTextures = mGround.size();
    }

    bool SkyrimTerrainIndex::textureUsable(FormId texture, sdk::GroundTexture* out) const
    {
        const auto ltex = mGround.find(texture);
        if (ltex == mGround.end() || ltex->second.mTextureSet == kNullFormId)
            return false;
        const auto set = mSets.find(ltex->second.mTextureSet);
        if (set == mSets.end() || set->second.mPaths[0].empty())
            return false;
        if (out)
        {
            out->id = texture;
            out->diffusePath = vfsPath(set->second.mPaths[0]);
        }
        return true;
    }

    sdk::WorldId SkyrimTerrainIndex::mapTexture(FormId texture) const
    {
        return texture != kNullFormId && textureUsable(texture, nullptr) ? texture : 0;
    }

    bool SkyrimTerrainIndex::worldInfo(sdk::WorldId world, sdk::WorldSpaceInfo& out) const
    {
        const auto found = mWorlds.find(world);
        if (found == mWorlds.end())
            return false;
        const WorldSpaceRecord& w = found->second;
        out = sdk::WorldSpaceInfo();
        out.id = w.mCommon.mId;
        out.editorId = w.mCommon.mEditorId;
        const bool hasParent = w.mParent != kNullFormId && w.mParent != kUnresolvedFormId;
        out.parent = hasParent ? w.mParent : sdk::kNoWorldId;
        out.parentUsage = hasParent ? w.mParentUsage.value_or(0) : 0;
        out.cellSizeUnits = WorldConstants::kCellSizeUnits;
        out.vertsPerSide = LandConstants::kVertsPerSide;
        out.defaultLandHeight = w.mDefaultLandHeight.value_or(0.0f);
        return true;
    }

    std::size_t SkyrimTerrainIndex::listWorlds(std::vector<sdk::WorldSpaceInfo>& out) const
    {
        std::size_t count = 0;
        for (const auto& entry : mWorlds)
        {
            sdk::WorldSpaceInfo info;
            if (worldInfo(entry.first, info))
            {
                out.push_back(std::move(info));
                ++count;
            }
        }
        return count;
    }

    std::size_t SkyrimTerrainIndex::listGroundTextures(std::vector<sdk::GroundTexture>& out) const
    {
        sdk::GroundTexture def;
        def.id = 0;
        def.diffusePath = TerrainDefaults::kDiffuse;
        out.push_back(def);
        std::size_t count = 1;
        for (const auto& entry : mGround)
        {
            sdk::GroundTexture t;
            if (textureUsable(entry.first, &t))
            {
                out.push_back(std::move(t));
                ++count;
            }
        }
        return count;
    }

    bool SkyrimTerrainIndex::defaultWater(FormId world, float& height) const
    {
        // A world with the water bit takes its parent's water (the bit is set only on worlds with no NAM2 of their own,
        // task 0117); the chain is short and capped so a cycle in hostile data ends.
        for (int hops = 0; hops < 8 && world != kNullFormId; ++hops)
        {
            const auto found = mWorlds.find(world);
            if (found == mWorlds.end())
                return false;
            const WorldSpaceRecord& w = found->second;
            const bool hasParent = w.mParent != kNullFormId && w.mParent != kUnresolvedFormId;
            const bool inherits = hasParent && (w.mParentUsage.value_or(0) & ParentUsage::Water) != 0;
            if (!inherits)
            {
                const CellWater water = classifyCellWater(w.mDefaultWaterHeight);
                if (water.mKind != CellWaterKind::Height)
                    return false;
                height = water.mHeight;
                return true;
            }
            world = w.mParent;
        }
        return false;
    }

    bool SkyrimTerrainIndex::cellWater(sdk::WorldId world, std::int32_t x, std::int32_t y, sdk::CellWater& out) const
    {
        out = sdk::CellWater();
        if (mWorlds.find(world) == mWorlds.end())
            return false;
        FormId current = world;
        for (int hops = 0; hops < 8 && current != kNullFormId; ++hops)
        {
            const auto cell = mCellWaters.find(LandKey{ current, x, y });
            if (cell != mCellWaters.end())
            {
                const CellWater& water = cell->second;
                if (water.mKind == CellWaterKind::Height)
                {
                    out.present = true;
                    out.height = water.mHeight;
                    return true;
                }
                if (water.mKind == CellWaterKind::NoWater)
                    return true;
                break; // the default, the absence of XCLW and an invalid value all mean the world's default
            }
            const auto w = mWorlds.find(current);
            if (w == mWorlds.end())
                break;
            const WorldSpaceRecord& rec = w->second;
            const bool lends = rec.mParent != kNullFormId && rec.mParent != kUnresolvedFormId
                && (rec.mParentUsage.value_or(0) & ParentUsage::Land) != 0;
            if (!lends)
                break;
            current = rec.mParent;
        }
        float height = 0.0f;
        if (defaultWater(world, height))
        {
            out.present = true;
            out.height = height;
        }
        return true;
    }

    sdk::TerrainResult SkyrimTerrainIndex::buildTerrain(
        sdk::WorldId world, std::int32_t x, std::int32_t y, sdk::TerrainCell& out, std::string& error) const
    {
        out = sdk::TerrainCell();
        if (mWorlds.find(world) == mWorlds.end())
        {
            error = "terrain: unknown world " + std::to_string(world);
            return sdk::TerrainResult::Failed;
        }
        const auto place = mLands.find(LandKey{ world, x, y });
        if (place == mLands.end())
            return sdk::TerrainResult::NoLand;
        const PluginSource& source = mPlugins[place->second.mPlugin];
        const std::string where = source.mName + ": LAND of cell (" + std::to_string(x) + ", " + std::to_string(y) + ")";
        const auto fail = [&](const std::string& why) {
            error = where + ": " + why;
            return sdk::TerrainResult::Failed;
        };

        std::unique_ptr<std::istream> stream = source.mOpen();
        if (!stream || !*stream)
            return fail("cannot reopen the file");
        stream->seekg(static_cast<std::streamoff>(place->second.mOffset));
        std::byte header[kRecordHeaderSize];
        stream->read(reinterpret_cast<char*>(header), static_cast<std::streamsize>(kRecordHeaderSize));
        if (!*stream)
            return fail("the record header is cut short");
        const auto u32 = [&](std::size_t at) {
            std::uint32_t v = 0;
            std::memcpy(&v, header + at, 4);
            return v;
        };
        if (u32(0) != Content::fourCC("LAND"))
            return fail("the file no longer has a LAND record at the indexed place");
        const std::uint32_t dataSize = u32(4);
        if (dataSize > mLimits.mMaxRecordBytes)
            return fail("the record is " + std::to_string(dataSize) + " bytes, over the limit");
        std::vector<std::byte> stored(dataSize);
        if (dataSize != 0)
        {
            stream->read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(dataSize));
            if (!*stream)
                return fail("the record data is cut short");
        }
        Content::RawRecord record;
        record.mType = u32(0);
        record.mFlags = u32(8);
        record.mFormId = u32(12);
        record.mOffset = place->second.mOffset;
        record.mDataSize = dataSize;
        record.mData = stored;
        std::vector<std::byte> inflated;
        std::span<const std::byte> bytes = stored;
        if (record.isCompressed())
        {
            if (stored.size() >= 4)
                std::memcpy(&record.mUncompressedSize, stored.data(), 4);
            Content::Error inflateError;
            if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                return fail("the record inflates to more than the limit");
            if (!inflateRecord(record, source.mName, inflated, inflateError))
                return fail(inflateError.mMessage);
            bytes = inflated;
        }

        ExteriorLimits readLimits = mLimits;
        readLimits.mDiscardLandData = false;
        const FormIdResolver resolver(source.mHeader, source.mName, mOrder);
        DecodeNotes notes;
        const DecodeContext context{ resolver, readLimits, notes, false };
        LandRecord land;
        std::string why;
        if (!decodeLand(bytes, record, context, land, why))
            return fail(why);
        ++mStats.mLandReads;

        out.world = world;
        out.gridX = x;
        out.gridY = y;
        out.vertsPerSide = LandConstants::kVertsPerSide;
        const std::size_t count = LandConstants::kVertCount;
        if (land.mHasHeights && land.mHeights.size() == count)
        {
            out.hasHeights = true;
            out.heights = std::move(land.mHeights);
        }
        if (land.mHasNormals && land.mNormals.size() == count * 3)
        {
            out.hasNormals = true;
            out.normals = std::move(land.mNormals);
        }
        if (land.mHasColours && land.mColours.size() == count * 3)
        {
            out.hasColours = true;
            out.colours = std::move(land.mColours);
        }
        for (std::size_t q = 0; q < LandConstants::kQuadrants; ++q)
        {
            out.baseTextures[q] = mapTexture(land.mQuadrants[q].mBase);

            // Task 0149: the alpha layers in layer number order (the file order breaks ties), each with its opacity
            // grid; a layer with no VTXT adds nothing and is left out.
            std::vector<const LandLayer*> ordered;
            for (const LandLayer& layer : land.mQuadrants[q].mLayers)
                if (!layer.mOpacity.empty())
                    ordered.push_back(&layer);
            std::stable_sort(ordered.begin(), ordered.end(),
                [](const LandLayer* a, const LandLayer* b) { return a->mLayerIndex < b->mLayerIndex; });
            for (const LandLayer* layer : ordered)
            {
                sdk::TerrainLayer converted;
                converted.texture = mapTexture(layer->mTexture);
                converted.opacity.assign(sdk::kTerrainLayerSide * sdk::kTerrainLayerSide, 0);
                for (const LandOpacity& entry : layer->mOpacity)
                    if (entry.mPosition < converted.opacity.size())
                    {
                        const float clamped = std::isfinite(entry.mOpacity) ? std::clamp(entry.mOpacity, 0.0f, 1.0f) : 0.0f;
                        converted.opacity[entry.mPosition] = static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
                    }
                out.layers[q].push_back(std::move(converted));
            }
        }
        return sdk::TerrainResult::Built;
    }

    bool SkyrimTerrainIndex::grassForCell(
        sdk::WorldId world, std::int32_t x, std::int32_t y, sdk::GrassCell& out, std::string& error) const
    {
        return grassForCell(world, x, y, out, error, GrassPlacementOptions());
    }

    bool SkyrimTerrainIndex::grassForCell(sdk::WorldId world, std::int32_t x, std::int32_t y, sdk::GrassCell& out,
        std::string& error, const GrassPlacementOptions& options) const
    {
        out = sdk::GrassCell();
        if (mGrassTables.mKinds.empty() || mGrassTables.mGrassOfTexture.empty())
            return mWorlds.find(world) != mWorlds.end();
        // The land: this world's own, else a parent's that lends it (the walk the core does for the terrain).
        FormId current = world;
        sdk::TerrainCell cell;
        bool found = false;
        for (int hops = 0; hops < 8 && current != kNullFormId; ++hops)
        {
            const auto w = mWorlds.find(current);
            if (w == mWorlds.end())
                return false;
            const sdk::TerrainResult result = buildTerrain(current, x, y, cell, error);
            if (result == sdk::TerrainResult::Failed)
                return false;
            if (result == sdk::TerrainResult::Built)
            {
                found = true;
                break;
            }
            const WorldSpaceRecord& rec = w->second;
            const bool lends = rec.mParent != kNullFormId && rec.mParent != kUnresolvedFormId
                && (rec.mParentUsage.value_or(0) & ParentUsage::Land) != 0;
            if (!lends)
                break;
            current = rec.mParent;
        }
        if (!found)
            return false;
        cell.world = world; // the seed follows the world the player is in, not the one that lent the land
        sdk::CellWater water;
        if (!cellWater(world, x, y, water))
            water = sdk::CellWater();
        placeGrass(cell, WorldConstants::kCellSizeUnits, mGrassTables, water, options, out);
        return true;
    }
}
