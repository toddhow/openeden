#ifndef MWLUA_PACKBINDINGS_H
#define MWLUA_PACKBINDINGS_H

#include <map>
#include <string>

#include <components/lua/configuration.hpp>

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
    //
    // Stage B: what a package holds depends on the script type that gets it. Global: Read and Mutate functions. Local
    // (also used for player scripts): Read functions only. Menu: Read functions of namespaces declared menuSafe, and no
    // other namespace. A function that is not given is absent from the strict table: "Key not found".
    enum class PackContext
    {
        Global,
        Local,
        Menu,
    };

    // The check `ScriptsConfiguration::init` uses for a script's `REQUIRES=` tags (stage C): met if the store's game
    // pack declared that package in that major version with a minor version not lower. With a null store (no game pack)
    // every requirement is unmet. The store must outlive the returned function.
    LuaUtil::ScriptsConfiguration::RequirementCheck makePackRequirementCheck(openeden::extension::BindingsStore* store);

    std::map<std::string, sol::object> initPackPackages(
        sol::state_view lua, openeden::extension::BindingsStore& store, PackContext context = PackContext::Global);
}

#endif
