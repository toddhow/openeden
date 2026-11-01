#include "tangents.hpp"

#include <vsg/maths/vec3.h>

namespace vsgAdapters
{
    namespace
    {
        // OSG 3.6.5 src/osgUtil/TangentSpaceGenerator.cpp, TangentSpaceGenerator::compute (OSGPL, a modified LGPL,
        // compatible with OpenEden's GPL-3.0-or-later).
        struct Basis
        {
            std::vector<vsg::vec3>& tangents;
            std::vector<vsg::vec3>& binormals;
            std::vector<vsg::vec3>& normals;
        };

        // Adds one axis's slope of u and v along that axis, from the triangle's position and texture deltas.
        void addAxis(int axis, const vsg::vec3& p1, const vsg::vec3& p2, const vsg::vec3& p3, const vsg::vec2& uv1,
            const vsg::vec2& uv2, const vsg::vec2& uv3, vsg::vec3& t, vsg::vec3& b)
        {
            vsg::vec3 v = vsg::cross(vsg::vec3(p2[axis] - p1[axis], uv2.x - uv1.x, uv2.y - uv1.y),
                vsg::vec3(p3[axis] - p1[axis], uv3.x - uv1.x, uv3.y - uv1.y));
            if (v.x == 0)
                return;
            v = vsg::normalize(v);
            t[axis] += -v.y / v.x;
            b[axis] += -v.z / v.x;
        }

        void computeTriangle(const std::vector<vsg::vec3>& vertices, const std::vector<vsg::vec3>& normals,
            const std::vector<vsg::vec2>& texCoords, uint16_t iA, uint16_t iB, uint16_t iC, Basis& basis)
        {
            const vsg::vec3& p1 = vertices[iA];
            const vsg::vec3& p2 = vertices[iB];
            const vsg::vec3& p3 = vertices[iC];
            const vsg::vec2& uv1 = texCoords[iA];
            const vsg::vec2& uv2 = texCoords[iB];
            const vsg::vec2& uv3 = texCoords[iC];

            vsg::vec3 n1, n2, n3;
            if (normals.empty())
                n1 = n2 = n3 = vsg::cross(p2 - p1, p3 - p1);
            else
            {
                n1 = normals[iA];
                n2 = normals[iB];
                n3 = normals[iC];
            }

            // All three corners get the same increments, as in OSG.
            vsg::vec3 t, b;
            for (int axis = 0; axis < 3; ++axis)
                addAxis(axis, p1, p2, p3, uv1, uv2, uv3, t, b);

            const uint16_t corners[3] = { iA, iB, iC };
            const vsg::vec3* cornerNormals[3] = { &n1, &n2, &n3 };
            for (int i = 0; i < 3; ++i)
            {
                const vsg::vec3& n = *cornerNormals[i];
                basis.tangents[corners[i]] += vsg::cross(vsg::cross(n, t), n);
                basis.binormals[corners[i]] += vsg::cross(n, vsg::cross(b, n));
                basis.normals[corners[i]] += n;
            }
        }
    }

    std::vector<vsg::vec4> generateTangents(const std::vector<vsg::vec3>& vertices,
        const std::vector<vsg::vec3>& normals, const std::vector<vsg::vec2>& texCoords,
        const std::vector<std::vector<uint16_t>>& indexLists, bool strip)
    {
        const size_t count = vertices.size();
        if (texCoords.size() != count || (!normals.empty() && normals.size() != count))
            return {};
        for (const auto& indices : indexLists)
            for (uint16_t index : indices)
                if (index >= count)
                    return {};

        std::vector<vsg::vec3> tangents(count), binormals(count), accumulatedNormals(count);
        Basis basis{ tangents, binormals, accumulatedNormals };
        for (const auto& indices : indexLists)
        {
            if (strip)
            {
                // TangentSpaceGenerator::generate's TRIANGLE_STRIP case: odd windows swap their first two corners.
                for (size_t i = 0; i + 2 < indices.size(); ++i)
                {
                    if (i % 2 == 0)
                        computeTriangle(
                            vertices, normals, texCoords, indices[i], indices[i + 1], indices[i + 2], basis);
                    else
                        computeTriangle(
                            vertices, normals, texCoords, indices[i + 1], indices[i], indices[i + 2], basis);
                }
            }
            else
            {
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    computeTriangle(vertices, normals, texCoords, indices[i], indices[i + 1], indices[i + 2], basis);
            }
        }

        // TangentSpaceGenerator::generate: the handedness is whether T x B points against the accumulated normal.
        std::vector<vsg::vec4> result(count);
        for (size_t i = 0; i < count; ++i)
        {
            const vsg::vec3 txb = vsg::cross(tangents[i], binormals[i]);
            const bool flipped = vsg::dot(txb, accumulatedNormals[i]) < 0;
            const float length = vsg::length(tangents[i]);
            // osg::Vec4::normalize leaves a zero vector as it is.
            const vsg::vec3 tangent = length > 0 ? tangents[i] / length : tangents[i];
            result[i] = vsg::vec4(tangent, flipped ? -1.f : 1.f);
        }
        return result;
    }
}
