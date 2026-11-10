#ifndef MWLUA_PACKBINDINGS_H
#define MWLUA_PACKBINDINGS_H

#include <map>
#include <string>

#include <sol/sol.hpp>

namespace openeden::extension
{
    class BindingsStore;
}

namespace MWLua
{
    // Script packages for the namespaces a game pack declared (task 0097, stage A).
    //
    // Each namespace "<game>.<area>" becomes one read-only, strict package named "core.<game>.<area>" holding one
    // function per declared function, in declaration order. Calling one checks the arguments (count, type, int32 range,
    // 64 KiB strings) and raises an error that names the function and the argument; the handler's status other than Ok
    // is raised the same way. No handler runs for a namespace whose pack is gone: the call raises an error instead.
    //
    // The caller registers the result in the global, local and player script containers, never the menu or load ones.
    // With a store that holds no namespace the map is empty and nothing changes.
    //
    // Must be called from inside the Lua state's protected call (LuaManager::initPostLoad). The store must outlive the
    // Lua state (it belongs to the extension host, which the engine destroys last).
    std::map<std::string, sol::object> initPackPackages(sol::state_view lua, openeden::extension::BindingsStore& store);
}

#endif
