#include "skyrimshape.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <Jolt/Jolt.h>

#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <components/debug/debuglog.hpp>
#include <components/files/istreamptr.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/nifscene/header.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWPhysics::SkyrimShape
{
    namespace
    {
        Counters sCounters;

        // Task 0139: models read without a problem that have nothing to collide with (see modelReadWithNoCollision).
        std::mutex sCollisionFreeMutex;
        std::unordered_set<std::string> sCollisionFree;

        // The most the shape manager's hook asks of the scene reader's header: any Skyrim SE file is told apart by its
        // first bytes (NifScene::isSkyrimSeHeader).
        void raiseMax(std::atomic<std::uint64_t>& maximum, std::uint64_t value)
        {
            std::uint64_t seen = maximum.load(std::memory_order_relaxed);
            while (value > seen && !maximum.compare_exchange_weak(seen, value, std::memory_order_relaxed))
            {
            }
        }
    }

    Counters& counters()
    {
        return sCounters;
    }

    vsg::ref_ptr<Resource::PhysicsShape> makeShape(const NifScene::Collision::Mesh& mesh, std::string* error)
    {
        if (mesh.empty())
        {
            if (error)
                *error = "no collision triangles";
            return {};
        }

        // Lives on the stack, so no Jolt Ref may take ownership of it (as the shape manager's COLLADA path does).
        JPH::MeshShapeSettings settings;
        settings.SetEmbedded();
        settings.mBuildQuality = JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
        settings.mTriangleVertices.reserve(mesh.mVertices.size());
        for (const auto& v : mesh.mVertices)
            settings.mTriangleVertices.emplace_back(v[0], v[1], v[2]);
        settings.mIndexedTriangles.reserve(mesh.mTriangles.size());
        for (const auto& t : mesh.mTriangles)
            settings.mIndexedTriangles.emplace_back(t[0], t[1], t[2]);

        JPH::ShapeSettings::ShapeResult created = settings.Create();
        if (created.HasError())
        {
            // Remove degenerate and duplicate triangles, then try again (as Morrowind's loader does).
            settings.ClearCachedResult();
            settings.Sanitize();
            created = settings.Create();
            if (created.HasError())
            {
                if (error)
                    *error = created.GetError().c_str();
                return {};
            }
        }

        vsg::ref_ptr<Resource::PhysicsShape> shape(new Resource::PhysicsShape);
        shape->mCollisionShape = created.Get();
        const JPH::AABox bounds = shape->mCollisionShape->GetLocalBounds();
        const JPH::Vec3 extents = bounds.GetExtent();
        const JPH::Vec3 centre = bounds.GetCenter();
        shape->mCollisionBox.mExtents = vsg::vec3(extents.GetX(), extents.GetY(), extents.GetZ());
        shape->mCollisionBox.mCenter = vsg::vec3(centre.GetX(), centre.GetY(), centre.GetZ());
        return shape;
    }

    std::shared_ptr<const Resource::SceneShapeReader> makeSceneShapeReader()
    {
        auto reader = std::make_shared<Resource::SceneShapeReader>();
        reader->accepts = [](std::string_view fileStart) { return NifScene::isSkyrimSeHeader(fileStart); };
        reader->read = [](std::string data, const std::string& filename) -> vsg::ref_ptr<Resource::PhysicsShape> {
            Counters& totals = counters();
            const auto begin = std::chrono::steady_clock::now();
            totals.mModels.fetch_add(1, std::memory_order_relaxed);
            vsg::ref_ptr<Resource::PhysicsShape> shape;
            try
            {
                NifScene::SceneOptions sceneOptions;
                Files::IStreamPtr stream(new std::istringstream(std::move(data)));
                const NifScene::Scene scene
                    = NifScene::readScene(std::move(stream), VFS::Path::Normalized(filename), sceneOptions);
                const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene);
                NifScene::Collision::Report report;
                const NifScene::Collision::Mesh mesh = NifScene::Collision::buildMesh(flat, {}, &report);
                totals.mTrianglesDropped += report.mTrianglesBadIndex + report.mTrianglesRepeatedCorner
                    + report.mTrianglesBadVertex + report.mTrianglesTooThin;
                totals.mBlendedShapesLeftOut += report.mShapesBlended;
                if (report.mOverCap)
                {
                    totals.mOverCap.fetch_add(1, std::memory_order_relaxed);
                    Log(Debug::Warning) << "Skyrim collision for \"" << filename << "\": more than "
                                        << NifScene::Collision::Limits{}.mMaxTriangles
                                        << " triangles, using the object's bounds instead";
                }
                if (mesh.empty())
                {
                    totals.mNoMesh.fetch_add(1, std::memory_order_relaxed);
                    // Nothing collidable and not because of the triangle cap: Skyrim gives such a model no collision.
                    if (!report.mOverCap)
                        noteModelReadWithNoCollision(filename);
                }
                else
                {
                    std::string error;
                    shape = makeShape(mesh, &error);
                    if (!shape)
                    {
                        totals.mJoltRefused.fetch_add(1, std::memory_order_relaxed);
                        Log(Debug::Error)
                            << "Skyrim collision for \"" << filename << "\": Jolt refused the mesh: " << error;
                    }
                    else
                    {
                        const JPH::Shape::Stats stats = shape->mCollisionShape->GetStats();
                        totals.mShapes.fetch_add(1, std::memory_order_relaxed);
                        totals.mTriangles += mesh.mTriangles.size();
                        totals.mVertices += mesh.mVertices.size();
                        totals.mBytes += stats.mSizeBytes;
                    }
                }
            }
            catch (const std::exception& e)
            {
                totals.mFailed.fetch_add(1, std::memory_order_relaxed);
                Log(Debug::Error) << "Skyrim collision for \"" << filename << "\": " << e.what();
                shape = {};
            }
            const auto micros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin)
                    .count());
            totals.mMicroseconds += micros;
            raiseMax(totals.mMaxMicroseconds, micros);
            return shape;
        };
        return reader;
    }

    bool modelReadWithNoCollision(std::string_view path)
    {
        const std::string key = VFS::Path::Normalized(path).value();
        std::scoped_lock lock(sCollisionFreeMutex);
        return sCollisionFree.contains(key);
    }

    void noteModelReadWithNoCollision(std::string_view path)
    {
        std::string key = VFS::Path::Normalized(path).value();
        std::scoped_lock lock(sCollisionFreeMutex);
        sCollisionFree.insert(std::move(key));
    }

    bool standInBoxWanted(bool meshAdded, bool hasBounds, bool modelCollisionFree)
    {
        return !meshAdded && hasBounds && !modelCollisionFree;
    }

    std::string describeCounters()
    {
        const Counters& c = counters();
        std::ostringstream out;
        out << std::fixed << std::setprecision(1);
        out << "Skyrim collision: " << c.mModels.load() << " models read, " << c.mShapes.load() << " mesh shapes ("
            << c.mTriangles.load() << " triangles, " << c.mVertices.load() << " vertices, "
            << static_cast<double>(c.mBytes.load()) / (1024.0 * 1024.0) << " MiB in Jolt), " << c.mNoMesh.load()
            << " with no mesh (" << c.mOverCap.load() << " over the cap), " << c.mJoltRefused.load()
            << " refused by Jolt, " << c.mFailed.load() << " failed, left out: " << c.mBlendedShapesLeftOut.load()
            << " blended shapes and " << c.mTrianglesDropped.load() << " bad triangles, build time "
            << static_cast<double>(c.mMicroseconds.load()) / 1000.0 << " ms in all, "
            << static_cast<double>(c.mMaxMicroseconds.load()) / 1000.0 << " ms the slowest";
        return out.str();
    }
}
