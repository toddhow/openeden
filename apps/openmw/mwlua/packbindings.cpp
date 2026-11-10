#include "packbindings.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include <components/debug/debuglog.hpp>
#include <components/extension/bindings_store.hpp>
#include <components/lua/luastate.hpp>

namespace MWLua
{
    namespace
    {
        using openeden::extension::BoundFunction;
        using openeden::sdk::BindingStatus;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

        const char* typeName(BindingType type)
        {
            switch (type)
            {
                case BindingType::Bool:
                    return "boolean";
                case BindingType::Int32:
                    return "integer";
                case BindingType::Double:
                    return "number";
                case BindingType::String:
                    return "string";
            }
            return "?";
        }

        const char* statusName(BindingStatus status)
        {
            switch (status)
            {
                case BindingStatus::Ok:
                    return "Ok";
                case BindingStatus::BadArgument:
                    return "BadArgument";
                case BindingStatus::NotFound:
                    return "NotFound";
                case BindingStatus::Refused:
                    return "Refused";
                case BindingStatus::Internal:
                    return "Internal";
            }
            return "Internal";
        }

        // What is wrong with the argument at `index` (1-based), or null if it converts into `out`. Plain data only: the
        // caller raises the Lua error, and a Lua error skips destructors.
        const char* convertArgument(lua_State* L, int index, BindingType expected, BindingValue& out)
        {
            const int luaType = lua_type(L, index);
            out = BindingValue{};
            out.type = expected;
            switch (expected)
            {
                case BindingType::Bool:
                    if (luaType != LUA_TBOOLEAN)
                        return "expected a boolean";
                    out.boolean = lua_toboolean(L, index) != 0;
                    return nullptr;
                case BindingType::Int32:
                {
                    if (luaType != LUA_TNUMBER)
                        return "expected an integer";
                    const double number = lua_tonumber(L, index);
                    if (!std::isfinite(number) || number != std::floor(number))
                        return "expected an integer, got a number that is not one";
                    if (number < static_cast<double>(std::numeric_limits<std::int32_t>::min())
                        || number > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                        return "integer out of the 32-bit range";
                    out.integer = static_cast<std::int32_t>(number);
                    return nullptr;
                }
                case BindingType::Double:
                    if (luaType != LUA_TNUMBER)
                        return "expected a number";
                    out.number = lua_tonumber(L, index);
                    return nullptr;
                case BindingType::String:
                {
                    if (luaType != LUA_TSTRING)
                        return "expected a string";
                    std::size_t length = 0;
                    const char* text = lua_tolstring(L, index, &length);
                    if (length > openeden::sdk::kMaxBindingStringBytes)
                        return "string longer than 64 KiB";
                    out.text = std::string_view(text, length);
                    return nullptr;
                }
            }
            return "unsupported argument type";
        }

        // Logs the facts that are reported once per function. Kept out of callBound so that no object with a destructor
        // is alive when the Lua error is raised.
        void reportOnce(const BoundFunction& function, const openeden::extension::Invoked& invoked)
        {
            if (invoked.firstEscapedException)
                Log(Debug::Error) << "core." << function.qualifiedName
                                  << ": an exception escaped the game pack's handler (reported once per function)";
            if (invoked.firstOverCost)
                Log(Debug::Warning) << "core." << function.qualifiedName << ": a call took longer than the declared "
                                    << function.maxCostMicroseconds << " microseconds (reported once per function)";
        }

        int callBound(lua_State* L)
        {
            auto* function = static_cast<BoundFunction*>(lua_touserdata(L, lua_upvalueindex(1)));

            const int given = lua_gettop(L);
            const int expected = static_cast<int>(function->params.size());
            if (given != expected)
                return luaL_error(
                    L, "core.%s: expected %d arguments, got %d", function->qualifiedName.c_str(), expected, given);

            BindingValue args[openeden::sdk::kMaxBindingParameters];
            for (int i = 0; i < expected; ++i)
            {
                const BindingType type = function->params[static_cast<std::size_t>(i)];
                if (const char* problem = convertArgument(L, i + 1, type, args[i]))
                    return luaL_error(L, "core.%s: argument #%d (%s): %s", function->qualifiedName.c_str(), i + 1,
                        typeName(type), problem);
            }

            const openeden::extension::Invoked invoked = openeden::extension::BindingsStore::invoke(
                *function, std::span<const BindingValue>(args, static_cast<std::size_t>(expected)));
            reportOnce(*function, invoked);

            if (invoked.result.status != BindingStatus::Ok)
            {
                lua_pushlstring(L, invoked.result.message.data(), invoked.result.message.size());
                return luaL_error(L, "core.%s: %s: %s", function->qualifiedName.c_str(),
                    statusName(invoked.result.status), lua_tostring(L, -1));
            }

            if (!function->hasResult)
                return 0;
            const BindingValue& value = invoked.result.value;
            switch (function->result)
            {
                case BindingType::Bool:
                    lua_pushboolean(L, value.boolean ? 1 : 0);
                    break;
                case BindingType::Int32:
                    lua_pushnumber(L, static_cast<lua_Number>(value.integer));
                    break;
                case BindingType::Double:
                    lua_pushnumber(L, value.number);
                    break;
                case BindingType::String:
                    lua_pushlstring(L, value.text.data(), value.text.size());
                    break;
            }
            return 1;
        }
    }

    std::map<std::string, sol::object> initPackPackages(sol::state_view lua, openeden::extension::BindingsStore& store)
    {
        std::map<std::string, sol::object> packages;
        lua_State* L = lua.lua_state();
        for (const auto& ns : store.namespaces())
        {
            lua_createtable(L, 0, static_cast<int>(ns->functions.size()));
            for (openeden::extension::BoundFunction& function : ns->functions)
            {
                lua_pushlstring(L, function.name.data(), function.name.size());
                lua_pushlightuserdata(L, &function);
                lua_pushcclosure(L, &callBound, 1);
                lua_rawset(L, -3);
            }
            sol::table table = sol::stack::pop<sol::table>(L);
            packages.emplace("core." + ns->name, LuaUtil::makeStrictReadOnly(table));
        }
        return packages;
    }
}
