#ifndef OPENMW_PUDDLELAND_H
#define OPENMW_PUDDLELAND_H

// OpenEden (task 0095): the land of the loaded game data as the puddle code reads it. One class, shared by the baker
// (apps/openmw/puddlebaker.cpp) and the run-time map (apps/openmw/mwrender/puddlemap.cpp), so that the digest the
// runtime checks a baked cell against is made from exactly the heights the bake used.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <components/esm3/loadland.hpp>
#include <components/puddle/puddlebake.hpp>
#include <components/terrain/bounds.hpp>

#include "mwrender/terrainstorage.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    /// The land of the loaded game data through the terrain storage's vertex data: the heights the live terrain is
    /// built from (lod 0, one cell, 65 x 65 vertices, x fastest, south row first). Not thread safe by itself; it only
    /// reads the store and the storage, which the terrain's own worker threads also read.
    class StorageHeights final : public Puddle::LandHeightSource
    {
    public:
        StorageHeights(MWRender::TerrainStorage& storage, const MWWorld::ESMStore& store)
            : mStorage(storage)
            , mStore(store)
        {
        }

        bool cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights) override
        {
            if (mStore.get<ESM::Land>().search(cellX, cellY) == nullptr)
                return false;
            Terrain::Bounds bounds;
            bounds.size = 1.f;
            bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
            const Terrain::Storage::VertexData data = mStorage.getVertexData(0, bounds);
            const std::size_t count = static_cast<std::size_t>(data.heights->width()) * data.heights->height();
            heights.assign(data.heights->data(), data.heights->data() + count);
            return true;
        }

    private:
        MWRender::TerrainStorage& mStorage;
        const MWWorld::ESMStore& mStore;
    };
}

#endif
