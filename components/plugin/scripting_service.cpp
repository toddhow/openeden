#include "scripting_service.hpp"

#include <iostream>

namespace openeden::plugin
{
    void ScriptingServiceImpl::TableImpl::setString(std::string_view key, std::string_view value)
    {
        mStrings[std::string(key)] = std::string(value);
    }

    void ScriptingServiceImpl::TableImpl::setNumber(std::string_view key, double value)
    {
        mNumbers[std::string(key)] = value;
    }

    void ScriptingServiceImpl::TableImpl::setBool(std::string_view key, bool value)
    {
        mBools[std::string(key)] = value;
    }

    void ScriptingServiceImpl::TableImpl::setFunction(std::string_view key, std::function<int(void*)> fn)
    {
        mFunctions[std::string(key)] = std::move(fn);
    }

    std::string ScriptingServiceImpl::TableImpl::getString(std::string_view key) const
    {
        auto it = mStrings.find(key);
        return it != mStrings.end() ? it->second : "";
    }

    double ScriptingServiceImpl::TableImpl::getNumber(std::string_view key) const
    {
        auto it = mNumbers.find(key);
        return it != mNumbers.end() ? it->second : 0.0;
    }

    bool ScriptingServiceImpl::TableImpl::getBool(std::string_view key) const
    {
        auto it = mBools.find(key);
        return it != mBools.end() ? it->second : false;
    }

    bool ScriptingServiceImpl::TableImpl::hasFunction(std::string_view key) const
    {
        return mFunctions.find(key) != mFunctions.end();
    }

    void ScriptingServiceImpl::registerModule(std::string_view pluginId, openeden::sdk::LuaInitCallback callback)
    {
        mModules[std::string(pluginId)] = std::move(callback);
    }

    void ScriptingServiceImpl::unregisterModule(std::string_view pluginId)
    {
        mModules.erase(std::string(pluginId));
        mTables.erase(std::string(pluginId));
    }

    bool ScriptingServiceImpl::hasModule(std::string_view pluginId) const
    {
        return mModules.find(pluginId) != mModules.end();
    }

    std::string ScriptingServiceImpl::getCanonicalNamespace(std::string_view pluginId) const
    {
        return "openeden." + std::string(pluginId);
    }

    void ScriptingServiceImpl::setPluginLoadOrder(const std::vector<std::string>& loadOrder)
    {
        mLoadOrder = loadOrder;
    }

    void ScriptingServiceImpl::initializeModules()
    {
        // 1. Initialize registered modules in plugin load order
        std::map<std::string, bool, std::less<>> executed;

        for (const auto& pluginId : mLoadOrder)
        {
            auto it = mModules.find(pluginId);
            if (it != mModules.end())
            {
                auto table = std::make_unique<TableImpl>();
                auto* tablePtr = table.get();
                mTables[pluginId] = std::move(table);

                RegistrarImpl registrar(pluginId, getCanonicalNamespace(pluginId), *tablePtr);
                try
                {
                    it->second(registrar);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden Scripting: Exception in Lua module initializer for '" << pluginId
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden Scripting: Unknown exception in Lua module initializer for '" << pluginId
                              << "'\n";
                }
                executed[pluginId] = true;
            }
        }

        // 2. Initialize any remaining registered modules not in load order
        for (const auto& [pluginId, callback] : mModules)
        {
            if (executed.find(pluginId) == executed.end())
            {
                auto table = std::make_unique<TableImpl>();
                auto* tablePtr = table.get();
                mTables[pluginId] = std::move(table);

                RegistrarImpl registrar(pluginId, getCanonicalNamespace(pluginId), *tablePtr);
                try
                {
                    callback(registrar);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden Scripting: Exception in Lua module initializer for '" << pluginId
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden Scripting: Unknown exception in Lua module initializer for '" << pluginId
                              << "'\n";
                }
            }
        }
    }

    const openeden::sdk::LuaTable* ScriptingServiceImpl::getModuleTable(std::string_view pluginId) const
    {
        auto it = mTables.find(pluginId);
        if (it != mTables.end())
        {
            return it->second.get();
        }
        return nullptr;
    }
} // namespace openeden::plugin
