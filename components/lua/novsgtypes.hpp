#ifndef COMPONENTS_LUA_NOVSGTYPES_H
#define COMPONENTS_LUA_NOVSGTYPES_H

#include <sol/sol.hpp>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

// OpenEden V5 transition, removed by task 0013. Scripts know util.vector3 and friends as the OSG types until the Lua
// bindings convert. sol2 would accept a VSG type as an unnamed userdata, which a script can only fail on at run time, as
// `.x` on such a value does. An empty traits struct makes passing one to Lua a compile error instead.
namespace sol
{
    template <>
    struct usertype_traits<vsg::vec2>
    {
    };
    template <>
    struct usertype_traits<vsg::vec3>
    {
    };
    template <>
    struct usertype_traits<vsg::vec4>
    {
    };
    template <>
    struct usertype_traits<vsg::dvec2>
    {
    };
    template <>
    struct usertype_traits<vsg::dvec3>
    {
    };
    template <>
    struct usertype_traits<vsg::dvec4>
    {
    };
    template <>
    struct usertype_traits<vsg::ivec2>
    {
    };
    template <>
    struct usertype_traits<vsg::quat>
    {
    };
    template <>
    struct usertype_traits<vsg::dquat>
    {
    };
    template <>
    struct usertype_traits<vsg::mat4>
    {
    };
    template <>
    struct usertype_traits<vsg::dmat4>
    {
    };
}

#endif
