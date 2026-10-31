#ifndef COMPONENTS_LUA_UTILPACKAGE_H
#define COMPONENTS_LUA_UTILPACKAGE_H

#include <sol/sol.hpp>

#include <components/misc/vsgmath.hpp>

namespace LuaUtil
{
    using Vec2 = vsg::vec2;
    using Vec3 = vsg::vec3;
    using Vec4 = vsg::vec4;

    // For performance reasons "Transform" is implemented as 2 types with the same interface.
    // Transform supports only composition, inversion, and applying to a 3d vector.
    struct TransformM
    {
        vsg::mat4 mM;
    };
    struct TransformQ
    {
        vsg::dquat mQ;
    };

    inline TransformM asTransform(const vsg::mat4& m)
    {
        return { m };
    }
    inline TransformQ asTransform(const vsg::dquat& q)
    {
        return { q };
    }

    inline bool isTransform(const sol::object& obj)
    {
        return obj.is<TransformM>() || obj.is<TransformQ>();
    }

    // Scripts can read this name, v.__type.name, and sol2 would otherwise give the C++ type's new name. It is the
    // name they have always seen, not a use of OSG.
    template <typename T>
    void keepTypeName(sol::usertype<T>& type, const char* name)
    {
        sol::table typeTable = type[sol::meta_function::type];
        typeTable["name"] = name;
    }

    sol::table initUtilPackage(lua_State* state);
}

#endif // COMPONENTS_LUA_UTILPACKAGE_H
