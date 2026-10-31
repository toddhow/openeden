#ifndef CSV_RENDER_PICKING_H
#define CSV_RENDER_PICKING_H

#include <algorithm>
#include <cmath>
#include <limits>

#include <vsg/app/ProjectionMatrix.h>
#include <vsg/core/Array.h>
#include <vsg/core/Mask.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/ArrayState.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include <components/vsgutil/intersectnormal.hpp>

#include "tagbase.hpp"

/// What's under a point of a view, keeping the rules of the OSG editor's WorldspaceWidget::mousePick. No window and no GPU,
/// so that openmw-cs-tests can check it: this reads the vertex arrays that nodes keep after they upload.
namespace CSVRender::Picking
{
    inline constexpr const char* sTagKey = "editor tag";

    /// Marks a node, and so whatever a ray hits beneath it, as an element of the scene. The node shares the tag.
    inline void setTag(vsg::Object& node, vsg::ref_ptr<TagBase> tag)
    {
        node.setObject(sTagKey, tag);
    }

    struct Ray
    {
        vsg::dvec3 mStart;
        vsg::dvec3 mEnd;
    };

    struct Result
    {
        bool mHit = false;
        vsg::ref_ptr<TagBase> mTag;
        /// The triangle's vertices.
        unsigned int mIndex0 = 0;
        unsigned int mIndex1 = 0;
        unsigned int mIndex2 = 0;
        vsg::dvec3 mWorldPos;
    };

    /// From the near plane to the far plane through a point of the picture, where (0, 0) is its top left and (1, 1) its bottom
    /// right. VSG's projection matrices flip Y and may reverse depth, and the inverse undoes both: reverse depth is what
    /// vsg::LineSegmentIntersector's own test for it reads.
    inline Ray viewRay(const vsg::dmat4& projection, const vsg::dmat4& view, double x, double y)
    {
        const bool reverseDepth = projection(2, 2) > 0.0;
        const vsg::dmat4 inverseProjection = vsg::inverse(projection);
        const vsg::dmat4 eyeToWorld = vsg::inverse(view);
        const vsg::dvec3 ndcNear(x * 2.0 - 1.0, y * 2.0 - 1.0, reverseDepth ? 1.0 : 0.0);
        const vsg::dvec3 ndcFar(x * 2.0 - 1.0, y * 2.0 - 1.0, reverseDepth ? 0.0 : 1.0);
        return { eyeToWorld * (inverseProjection * ndcNear), eyeToWorld * (inverseProjection * ndcFar) };
    }

    /// vsg::LineSegmentIntersector reads triangle lists only. Water and markers are unindexed strips.
    class Intersector : public vsg::LineSegmentIntersector
    {
    public:
        using vsg::LineSegmentIntersector::LineSegmentIntersector;

        bool intersectDraw(uint32_t firstVertex, uint32_t vertexCount, uint32_t firstInstance, uint32_t instanceCount) override
        {
            auto& arrayState = *arrayStateStack.back();
            if (arrayState.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
                return vsg::LineSegmentIntersector::intersectDraw(firstVertex, vertexCount, firstInstance, instanceCount);
            if (vertexCount < 3)
                return false;

            const auto& segment = _lineSegmentStack.back();
            const auto before = intersections.size();
            const uint32_t lastInstance = instanceCount > 1 ? firstInstance + instanceCount : firstInstance + 1;
            for (uint32_t instance = firstInstance; instance < lastInstance; ++instance)
            {
                const auto vertices = arrayState.vertexArray(instance);
                if (!vertices)
                    continue;
                for (uint32_t i = firstVertex; i + 2 < firstVertex + vertexCount; ++i)
                {
                    // Every second triangle of a strip has its first two the other way round, to keep its facing.
                    const bool odd = (i - firstVertex) % 2 != 0;
                    intersectTriangle(*vertices, segment, odd ? i + 1 : i, odd ? i : i + 1, i + 2, instance);
                }
            }
            return intersections.size() != before;
        }

    private:
        void intersectTriangle(const vsg::vec3Array& vertices, const LineSegment& segment, uint32_t i0, uint32_t i1,
            uint32_t i2, uint32_t instance)
        {
            // Moeller and Trumbore's test, on either side of the triangle: which side counts is for the caller.
            const vsg::dvec3 v0(vertices[i0]);
            const vsg::dvec3 edge1 = vsg::dvec3(vertices[i1]) - v0;
            const vsg::dvec3 edge2 = vsg::dvec3(vertices[i2]) - v0;
            const vsg::dvec3 direction = segment.end - segment.start;
            const vsg::dvec3 p = vsg::cross(direction, edge2);
            const double determinant = vsg::dot(edge1, p);
            if (std::abs(determinant) < std::numeric_limits<double>::epsilon())
                return;
            const vsg::dvec3 s = segment.start - v0;
            const double u = vsg::dot(s, p) / determinant;
            const vsg::dvec3 q = vsg::cross(s, edge1);
            const double v = vsg::dot(direction, q) / determinant;
            const double ratio = vsg::dot(edge2, q) / determinant;
            if (u < 0.0 || v < 0.0 || u + v > 1.0 || ratio < 0.0 || ratio > 1.0)
                return;
            add(segment.start + direction * ratio, ratio,
                vsg::IndexRatios{ { i0, 1.0 - u - v }, { i1, u }, { i2, v } }, instance);
        }
    };

    /// Does the hit's triangle face away from the ray? Terrain and skinned models compute their vertices, so this reads what
    /// the intersector saw, not the bind pose: a pick from below terrain does not stop at it, as the OSG editor's did not.
    inline bool isBackface(const vsg::LineSegmentIntersector::Intersection& hit, const vsg::dvec3& worldDirection)
    {
        if (hit.indexRatios.size() < 3)
            return false;
        const auto vertices = vsgUtil::hitVertices(hit);
        if (!vertices)
            return false;
        const vsg::dvec3 v0((*vertices)[hit.indexRatios[0].index]);
        const vsg::dvec3 v1((*vertices)[hit.indexRatios[1].index]);
        const vsg::dvec3 v2((*vertices)[hit.indexRatios[2].index]);
        const vsg::dvec3 localNormal = vsg::cross(v1 - v0, v2 - v0);
        // The direction in the triangle's own space, which needs no inverse transpose: the dot product is the same there
        // up to a mirror in the transform, and the OSG editor's world normal gave way there too.
        const vsg::dvec3 localDirection = vsg::inverse(hit.localToWorld) * (worldDirection + hit.worldIntersection)
            - vsg::inverse(hit.localToWorld) * hit.worldIntersection;
        return vsg::dot(localDirection, localNormal) > 0;
    }

    /// As WorldspaceWidget::mousePick did.
    /// \param mask which elements of mask.hpp's list count.
    /// \param placementDistance how far along the ray a miss places mWorldPos, the preference 3D Scene Editing/distance.
    inline Result pick(const vsg::Node& root, const Ray& ray, vsg::Mask mask, double placementDistance)
    {
        Intersector intersector(ray.mStart, ray.mEnd);
        intersector.traversalMask = mask;
        root.accept(intersector);

        const vsg::dvec3 direction = ray.mEnd - ray.mStart;
        const vsg::LineSegmentIntersector::Intersection* first = nullptr;
        for (const auto& hit : intersector.intersections)
            if ((first == nullptr || hit->ratio < first->ratio) && !isBackface(*hit, direction))
                first = hit.get();

        Result result;
        if (first == nullptr)
        {
            result.mWorldPos = ray.mStart + vsg::normalize(direction) * placementDistance;
            return result;
        }

        result.mHit = true;
        result.mWorldPos = first->worldIntersection;
        for (const vsg::Node* node : first->nodePath)
        {
            if (const auto* tag = node->getObject<TagBase>(sTagKey))
            {
                result.mTag = vsg::ref_ptr<TagBase>(const_cast<TagBase*>(tag));
                break;
            }
        }
        if (first->indexRatios.size() >= 3)
        {
            result.mIndex0 = first->indexRatios[0].index;
            result.mIndex1 = first->indexRatios[1].index;
            result.mIndex2 = first->indexRatios[2].index;
        }
        return result;
    }

    /// The nearest thing on the ray, either side of a triangle, as the orbit camera's first centre was
    /// (LineSegmentIntersector::LIMIT_NEAREST with no test of facing).
    inline bool pickNearest(const vsg::Node& root, const Ray& ray, vsg::Mask mask, vsg::dvec3& position)
    {
        Intersector intersector(ray.mStart, ray.mEnd);
        intersector.traversalMask = mask;
        root.accept(intersector);
        const vsg::LineSegmentIntersector::Intersection* first = nullptr;
        for (const auto& hit : intersector.intersections)
            if (first == nullptr || hit->ratio < first->ratio)
                first = hit.get();
        if (first == nullptr)
            return false;
        position = first->worldIntersection;
        return true;
    }
}

#endif
