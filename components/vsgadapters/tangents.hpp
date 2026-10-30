#ifndef OPENEDEN_COMPONENTS_VSGADAPTERS_TANGENTS_H
#define OPENEDEN_COMPONENTS_VSGADAPTERS_TANGENTS_H

#include <cstdint>
#include <vector>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace vsgAdapters
{
    /// Per-vertex tangents for normal mapping (task 0012, V4h): a port of OSG 3.6.5's
    /// osgUtil::TangentSpaceGenerator, which OpenMW's ShaderVisitor used. xyz is the normalized tangent and w the
    /// handedness (-1 where (T x B) . N < 0) by which the shader scales the binormal N x T.
    ///
    /// Each index list is a triangle list, or for strip one strip whose odd windows swap their first two indices, as
    /// OSG did; OpenMW gave OSG each NIF strip separately. Normals may be empty, then each triangle uses its face
    /// normal. Mismatched sizes or out-of-range indices give an empty result.
    std::vector<vsg::vec4> generateTangents(const std::vector<vsg::vec3>& vertices,
        const std::vector<vsg::vec3>& normals, const std::vector<vsg::vec2>& texCoords,
        const std::vector<std::vector<uint16_t>>& indexLists, bool strip);
}

#endif
