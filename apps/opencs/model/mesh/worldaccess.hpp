#ifndef CSM_MESH_WORLDACCESS_H
#define CSM_MESH_WORLDACCESS_H

#include <string>

namespace NeutralWorld
{
    class NeutralWorldStore;
}

namespace CSMMesh
{
    /// The world the cell viewer reads (task 0173, stage d): the neutral world store of the session's game pack and
    /// the game id its source was registered under. Set by whoever started the game pack (the editor, a capture);
    /// read by MeshScene::loadSkyrimCell and the "Open Cell" menu. Main (GUI) thread only.
    struct ActiveWorld
    {
        NeutralWorld::NeutralWorldStore* mStore = nullptr;
        /// The game id of the registered source; empty when the store has none.
        std::string mGameId;
        /// The game pack's name, for menus and messages ("Skyrim Special Edition").
        std::string mGameName;
    };

    /// Replaces the active world. A store must stay alive until it is replaced or cleared (setActiveWorld({})).
    void setActiveWorld(ActiveWorld world);
    const ActiveWorld& activeWorld();

    /// The world to use from a store: its first game id (NeutralWorldStore::gameIds) and the given name; an empty
    /// game id when `store` is null or holds no source.
    ActiveWorld worldFromStore(NeutralWorld::NeutralWorldStore* store, const std::string& gameName);

    /// True when the active world has a store with a source for its game id: cells can be opened.
    bool hasActiveWorld();
}

#endif
