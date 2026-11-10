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
        using openeden::sdk::BindingShape;
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
                case BindingType::Handle:
                    return "handle";
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

        // ---- handles: an opaque userdata holding a 64-bit value ----
        //
        // A full userdata (never light userdata: the save code gives that a different error), with a locked metatable:
        // equal by value, no fields, nothing assignable, the real metatable hidden. Two equal handles are still two
        // different table keys (a table compares userdata by identity); scripts compare with ==.

        constexpr const char* kHandleMeta = "openeden.pack.handle";

        struct HandleBox
        {
            std::uint64_t mId;
        };

        void pushHandle(lua_State* L, std::uint64_t id)
        {
            auto* box = static_cast<HandleBox*>(lua_newuserdata(L, sizeof(HandleBox)));
            box->mId = id;
            luaL_getmetatable(L, kHandleMeta);
            lua_setmetatable(L, -2);
        }

        bool testHandle(lua_State* L, int index, std::uint64_t& id)
        {
            if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))
                return false;
            luaL_getmetatable(L, kHandleMeta);
            const bool same = lua_rawequal(L, -1, -2) != 0;
            lua_pop(L, 2);
            if (!same)
                return false;
            id = static_cast<HandleBox*>(lua_touserdata(L, index))->mId;
            return true;
        }

        int handleEquals(lua_State* L)
        {
            std::uint64_t a = 0;
            std::uint64_t b = 0;
            const bool bothHandles = testHandle(L, 1, a) && testHandle(L, 2, b);
            lua_pushboolean(L, bothHandles && a == b ? 1 : 0);
            return 1;
        }

        int handleOpaque(lua_State* L)
        {
            return luaL_error(L, "pack handles are opaque: they have no fields and cannot be changed");
        }

        int handleToString(lua_State* L)
        {
            lua_pushstring(L, "pack handle");
            return 1;
        }

        void ensureHandleMetatable(lua_State* L)
        {
            if (luaL_newmetatable(L, kHandleMeta) == 0)
            {
                lua_pop(L, 1);
                return;
            }
            lua_pushcfunction(L, &handleEquals);
            lua_setfield(L, -2, "__eq");
            lua_pushcfunction(L, &handleOpaque);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, &handleOpaque);
            lua_setfield(L, -2, "__newindex");
            lua_pushcfunction(L, &handleToString);
            lua_setfield(L, -2, "__tostring");
            lua_pushstring(L, "opaque handle");
            lua_setfield(L, -2, "__metatable");
            lua_pop(L, 1);
        }

        // What is wrong with the value at `index`, or null if it converts into `out` as a single value of `expected`.
        // Plain data only: the caller raises the Lua error, and a Lua error skips destructors.
        const char* convertScalar(lua_State* L, int index, BindingType expected, BindingValue& out)
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
                case BindingType::Handle:
                    if (!testHandle(L, index, out.handle))
                        return "expected a handle";
                    return nullptr;
            }
            return "unsupported argument type";
        }

        // Room for the elements of the array arguments of one call (a call has at most 8 arguments). One per thread, so
        // a call allocates nothing and two threads never share it. Whoever keeps the elements copies them out.
        constexpr std::size_t kArrayStorage
            = openeden::sdk::kMaxBindingParameters * openeden::sdk::kMaxBindingArrayElements;
        thread_local BindingValue sArrayStorage[kArrayStorage];

        // An array argument: a table that is a sequence 1..n, n at most 256, with no other keys, read raw (no
        // metamethod runs). Returns the problem or null; `badElement` is the 1-based position of a bad element, else 0.
        const char* convertArray(
            lua_State* L, int index, BindingType element, BindingValue& out, std::size_t& storageUsed, int& badElement)
        {
            badElement = 0;
            out = BindingValue{};
            out.type = element;
            if (lua_type(L, index) != LUA_TTABLE)
                return "expected an array (a table)";

            // Count the entries (lua_next is raw), then check that 1..count are all there: a flat sequence.
            std::size_t entries = 0;
            lua_pushnil(L);
            while (lua_next(L, index) != 0)
            {
                lua_pop(L, 1);
                ++entries;
                if (entries > openeden::sdk::kMaxBindingArrayElements)
                {
                    lua_pop(L, 1); // the key
                    return "more than 256 elements";
                }
            }
            for (std::size_t i = 1; i <= entries; ++i)
            {
                lua_rawgeti(L, index, static_cast<int>(i));
                const bool missing = lua_isnil(L, -1);
                lua_pop(L, 1);
                if (missing)
                    return "expected a flat array: a sequence with no holes and no other keys";
            }

            BindingValue* slots = sArrayStorage + storageUsed;
            for (std::size_t i = 0; i < entries; ++i)
            {
                lua_rawgeti(L, index, static_cast<int>(i + 1));
                const char* problem = convertScalar(L, -1, element, slots[i]);
                lua_pop(L, 1); // strings stay alive in the table
                if (problem != nullptr)
                {
                    badElement = static_cast<int>(i + 1);
                    return problem;
                }
            }
            storageUsed += entries;
            out.elements = slots;
            out.count = entries;
            return nullptr;
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

        void pushScalar(lua_State* L, BindingType type, const BindingValue& value)
        {
            switch (type)
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
                case BindingType::Handle:
                    pushHandle(L, value.handle);
                    break;
            }
        }

        int callBound(lua_State* L)
        {
            auto* function = static_cast<BoundFunction*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto* store = static_cast<openeden::extension::BindingsStore*>(lua_touserdata(L, lua_upvalueindex(2)));

            const int given = lua_gettop(L);
            const int expected = static_cast<int>(function->params.size());
            // Trailing optional parameters may be left out
            bool countOk = given <= expected;
            for (int i = given; countOk && i < expected; ++i)
                countOk = function->shapes[static_cast<std::size_t>(i)] == BindingShape::Optional;
            if (!countOk)
                return luaL_error(
                    L, "core.%s: expected %d arguments, got %d", function->qualifiedName.c_str(), expected, given);

            BindingValue args[openeden::sdk::kMaxBindingParameters];
            std::size_t storageUsed = 0;
            for (int i = 0; i < expected; ++i)
            {
                const std::size_t slot = static_cast<std::size_t>(i);
                const BindingType type = function->params[slot];
                const BindingShape shape = function->shapes[slot];
                const char* problem = nullptr;
                int badElement = 0;
                if (shape == BindingShape::Optional && (i >= given || lua_isnil(L, i + 1)))
                {
                    args[i] = BindingValue{};
                    args[i].type = type;
                    args[i].present = false;
                }
                else if (shape == BindingShape::Array)
                    problem = convertArray(L, i + 1, type, args[i], storageUsed, badElement);
                else
                    problem = convertScalar(L, i + 1, type, args[i]);
                if (problem != nullptr && badElement != 0)
                    return luaL_error(L, "core.%s: argument #%d (array of %s): element %d: %s",
                        function->qualifiedName.c_str(), i + 1, typeName(type), badElement, problem);
                if (problem != nullptr)
                    return luaL_error(L, "core.%s: argument #%d (%s%s): %s", function->qualifiedName.c_str(), i + 1,
                        shape == BindingShape::Array          ? "array of "
                            : shape == BindingShape::Optional ? "optional "
                                                              : "",
                        typeName(type), problem);
            }
            const std::span<const BindingValue> argSpan(args, static_cast<std::size_t>(expected));

            // A call that changes the game is queued, not run: the main thread runs it later, in call order. It has no
            // result; a full queue drops the call (counted, logged at the drain), which is not a script error.
            if (function->access == openeden::sdk::BindingAccess::Mutate)
            {
                (void)store->mutateQueue().push(*function, argSpan);
                return 0;
            }

            const openeden::extension::Invoked invoked = openeden::extension::BindingsStore::invoke(*function, argSpan);
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
            switch (function->resultShape)
            {
                case BindingShape::Single:
                    pushScalar(L, function->result, value);
                    break;
                case BindingShape::Optional:
                    if (!value.present)
                        lua_pushnil(L);
                    else
                        pushScalar(L, function->result, value);
                    break;
                case BindingShape::Array:
                    lua_createtable(L, static_cast<int>(value.count), 0);
                    for (std::size_t i = 0; i < value.count; ++i)
                    {
                        pushScalar(L, function->result, value.elements[i]);
                        lua_rawseti(L, -2, static_cast<int>(i + 1));
                    }
                    break;
            }
            return 1;
        }
    }

    std::map<std::string, sol::object> initPackPackages(
        sol::state_view lua, openeden::extension::BindingsStore& store, PackContext context)
    {
        std::map<std::string, sol::object> packages;
        lua_State* L = lua.lua_state();
        if (!store.namespaces().empty())
            ensureHandleMetatable(L);
        for (const auto& ns : store.namespaces())
        {
            // The menu has no game loaded: only a namespace its pack marked menuSafe is given to it at all.
            if (context == PackContext::Menu && !ns->menuSafe)
                continue;
            lua_createtable(L, 0, static_cast<int>(ns->functions.size()));
            for (openeden::extension::BoundFunction& function : ns->functions)
            {
                // A function that changes the game exists in global scripts only.
                if (function.access == openeden::sdk::BindingAccess::Mutate && context != PackContext::Global)
                    continue;
                lua_pushlstring(L, function.name.data(), function.name.size());
                lua_pushlightuserdata(L, &function);
                lua_pushlightuserdata(L, &store);
                lua_pushcclosure(L, &callBound, 2);
                lua_rawset(L, -3);
            }
            sol::table table = sol::stack::pop<sol::table>(L);
            sol::table version(lua, sol::create);
            version["major"] = ns->version.major;
            version["minor"] = ns->version.minor;
            table["API_VERSION"] = LuaUtil::makeStrictReadOnly(version);
            packages.emplace("core." + ns->name, LuaUtil::makeStrictReadOnly(table));
        }
        return packages;
    }

    LuaUtil::ScriptsConfiguration::RequirementCheck makePackRequirementCheck(openeden::extension::BindingsStore* store)
    {
        return [store](const ESM::LuaScriptCfg::Requirement& requirement) -> std::string {
            if (store == nullptr)
                return "no game pack is loaded in this session";
            return store->unmetRequirement(requirement.mPackage, requirement.mMajor, requirement.mMinor);
        };
    }
}
