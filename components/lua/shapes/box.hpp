#ifndef COMPONENTS_LUA_SHAPES_BOX_H
#define COMPONENTS_LUA_SHAPES_BOX_H

#include <array>

#include <components/misc/vsgmath.hpp>

namespace LuaUtil
{
    class Box
    {
    public:
        Box(const vsg::vec3& center, const vsg::vec3& halfSize, const vsg::dquat& rotation = vsg::dquat());
        Box(const vsg::dmat4& transform);

        std::array<vsg::vec3, 8> vertices() const;

        vsg::dmat4 asTransform() const;

        // TODO: Add `contains` and `intersects` methods

        bool operator==(const Box& other) const;

        vsg::vec3 mCenter;
        vsg::vec3 mHalfSize;
        vsg::dquat mRotation;
    };
}
#endif // COMPONENTS_LUA_SHAPES_BOX_H
