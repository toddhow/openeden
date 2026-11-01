#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_RECASTMESHBUILDER_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_RECASTMESHBUILDER_H

#include "recastmesh.hpp"
#include "tilebounds.hpp"

#include <components/misc/vsgmath.hpp>
#include <components/resource/physicsshape.hpp>

#include <array>
#include <functional>
#include <memory>
#include <tuple>
#include <vector>

namespace JPH
{
    class Shape;
    class ConvexShape;
    class MeshShape;
    class CompoundShape;
    class HeightFieldShape;
    class RotatedTranslatedShape;
}

namespace DetourNavigator
{
    struct RecastMeshTriangle
    {
        AreaType mAreaType;
        std::array<vsg::vec3, 3> mVertices;

        friend inline bool operator<(const RecastMeshTriangle& lhs, const RecastMeshTriangle& rhs)
        {
            return std::tie(lhs.mAreaType, lhs.mVertices) < std::tie(rhs.mAreaType, rhs.mVertices);
        }
    };

    using TriangleProcessFunc = std::function<void(JPH::Float3* triangle, int partId, int triangleIndex)>;
    using TriangleWalkerFunc = std::function<void(JPH::Float3& v1, JPH::Float3& v2, JPH::Float3& v3, int)>;

    class RecastMeshBuilder
    {
    public:
        explicit RecastMeshBuilder(const TileBounds& bounds) noexcept;

        void addObject(const JPH::Shape& shape, const vsg::dmat4& transform, const AreaType areaType,
            vsg::ref_ptr<const Resource::PhysicsShape> source, const ObjectTransform& objectTransform);

        void addWater(const vsg::ivec2& cellPosition, const Water& water);

        void addHeightfield(const vsg::ivec2& cellPosition, int cellSize, float height);

        void addHeightfield(const vsg::ivec2& cellPosition, int cellSize, const float* heights, std::size_t size,
            float minHeight, float maxHeight);

        std::shared_ptr<RecastMesh> create(const Version& version) &&;

    private:
        const TileBounds mBounds;
        std::vector<RecastMeshTriangle> mTriangles;
        std::vector<CellWater> mWater;
        std::vector<Heightfield> mHeightfields;
        std::vector<FlatHeightfield> mFlatHeightfields;
        std::vector<MeshSource> mSources;

        inline void addObject(const JPH::Shape& shape, const vsg::dmat4& transform, const AreaType areaType);

        void addObject(const JPH::Shape& shape, const vsg::dmat4& transform, TriangleProcessFunc& processTriangle);

        void addObject(
            const JPH::HeightFieldShape& shape, const vsg::dmat4& transform, TriangleProcessFunc& processTriangle);

        void addObject(const JPH::RotatedTranslatedShape& shape, const vsg::dmat4& transform, const AreaType areaType);

        void addObject(const JPH::CompoundShape& shape, const vsg::dmat4& transform, const AreaType areaType);

        void addObject(const JPH::HeightFieldShape& shape, const vsg::dmat4& transform, const AreaType areaType);
    };

    Mesh makeMesh(std::vector<RecastMeshTriangle>&& triangles, const vsg::vec3& shift = vsg::vec3());

    Mesh makeMesh(const Heightfield& heightfield);
}

#endif
