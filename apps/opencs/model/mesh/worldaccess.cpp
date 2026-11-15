#include "worldaccess.hpp"

#include <utility>
#include <vector>

#include <components/worldmodel/store.hpp>

namespace CSMMesh
{
    namespace
    {
        ActiveWorld& current()
        {
            static ActiveWorld world;
            return world;
        }
    }

    void setActiveWorld(ActiveWorld world)
    {
        current() = std::move(world);
    }

    const ActiveWorld& activeWorld()
    {
        return current();
    }

    ActiveWorld worldFromStore(NeutralWorld::NeutralWorldStore* store, const std::string& gameName)
    {
        ActiveWorld world;
        world.mStore = store;
        world.mGameName = gameName;
        if (store != nullptr)
        {
            const std::vector<std::string> ids = store->gameIds();
            if (!ids.empty())
                world.mGameId = ids.front();
        }
        return world;
    }

    bool hasActiveWorld()
    {
        const ActiveWorld& world = current();
        return world.mStore != nullptr && !world.mGameId.empty() && world.mStore->source(world.mGameId) != nullptr;
    }
}
