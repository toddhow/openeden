#include "terrainstorage.hpp"

#include <components/esm3/loadland.hpp>

#include "../mwworld/esmstore.hpp"

#include <components/misc/resourcehelpers.hpp>
#include <components/vfs/manager.hpp>

#include "landmanager.hpp"

namespace MWRender
{
    TerrainStorage::TerrainStorage(Resource::ResourceSystem* resourceSystem)
        : mLandManager(new LandManager(
              ESM::Land::DATA_VCLR | ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VTEX))
        , mResourceSystem(resourceSystem)
    {
    }

    TerrainStorage::~TerrainStorage()
    {
    }

    LandManager* TerrainStorage::getLandManager() const
    {
        return mLandManager.get();
    }

    void TerrainStorage::pruneCache()
    {
        mLandManager->pruneCache();
    }

    vsg::ref_ptr<const ESMTerrain::LandObject> TerrainStorage::getLand(ESM::ExteriorCellLocation cellLocation)
    {
        return mLandManager->getLand(cellLocation);
    }

    std::map<TerrainStorage::PluginTexture, std::string> TerrainStorage::enumerateLayers() const
    {
        std::map<PluginTexture, std::string> map;
        const auto& store = mEsmStore->get<ESM::LandTexture>();
        static constexpr char defaultTexture[] = "_land_default.dds";
        for (const auto& [pluginIndex, refId] : store.getMappings())
        {
            int plugin = pluginIndex.first;
            uint32_t texture = pluginIndex.second;
            map[std::make_pair(0, static_cast<short>(plugin))] = defaultTexture;

            const std::string* lt = store.search(texture, plugin);
            if (lt)
            {
                auto corrected = Misc::ResourceHelpers::correctTexturePath(VFS::Path::Normalized(*lt), *mResourceSystem->getVFS());
                if (mResourceSystem->getVFS()->exists(corrected))
                {
                    // NB: All vtex ids are +1 compared to the ltex ids
                    map[std::make_pair(static_cast<short>(texture + 1), static_cast<short>(plugin))] = *lt;
                }
            }
        }
        return map;
    }
}
