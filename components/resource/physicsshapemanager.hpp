#ifndef OPENMW_COMPONENTS_JOLTSHAPEMANAGER_H
#define OPENMW_COMPONENTS_JOLTSHAPEMANAGER_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <Jolt/Jolt.h>

#include <vsg/core/ref_ptr.h>

#include "physicsshape.hpp"
#include "resourcemanager.hpp"

namespace Resource
{
    class NifFileManager;

    struct PhysicsShape;
    class PhysicsShapeInstance;

    class MultiObjectCache;

    /// Task 0110 (neutral world stage N3a-4b): a format-blind hook for a model file that the Morrowind NIF loader
    /// cannot read (a Skyrim SE file). `accepts` looks at the first bytes of a .nif file; for a file it accepts,
    /// `read` is the whole answer (a shape, or null for "no collision"): the Morrowind loader is never tried on it.
    /// The host sets the hook (apps/openmw/mwphysics/skyrimshape.hpp) when `[Game] neutral world` is on; without it
    /// nothing changes.
    /// Both functions may run on any thread the manager is used from.
    struct SceneShapeReader
    {
        std::function<bool(std::string_view fileStart)> accepts;
        std::function<vsg::ref_ptr<PhysicsShape>(std::string data, const std::string& filename)> read;
    };

    /// Handles loading, caching and "instancing" of Jolt physics shapes.
    /// A shape 'instance' is a clone of another shape, with the goal of setting a different scale on this instance.
    /// @note May be used from any thread.
    class PhysicsShapeManager : public ResourceManager
    {
    public:
        PhysicsShapeManager(const VFS::Manager* vfs, NifFileManager* nifFileManager, double expiryDelay);
        ~PhysicsShapeManager();

        /// Sets the hook for model files the Morrowind loader cannot read (see SceneShapeReader). Call it once,
        /// before the manager is used from other threads; null (the default) means every .nif goes to the Morrowind
        /// loader.
        void setSceneShapeReader(std::shared_ptr<const SceneShapeReader> reader)
        {
            mSceneShapeReader = std::move(reader);
        }

        /// @note May return a null pointer if the object has no shape.
        vsg::ref_ptr<const PhysicsShape> getShape(VFS::Path::NormalizedView name);

        /// Create an instance of the given shape and cache it for later use, so that future calls to getInstance() can
        /// simply return the cached instance instead of having to create a new one.
        /// @note The returned ref_ptr may be kept by the caller to ensure that the instance stays in cache for as long
        /// as needed.
        vsg::ref_ptr<PhysicsShapeInstance> cacheInstance(VFS::Path::NormalizedView name);

        /// @note May return a null pointer if the object has no shape.
        vsg::ref_ptr<PhysicsShapeInstance> getInstance(VFS::Path::NormalizedView name);

        /// @see ResourceManager::updateCache
        void updateCache(double referenceTime) override;

        void clearCache() override;

    private:
        vsg::ref_ptr<PhysicsShapeInstance> createInstance(VFS::Path::NormalizedView name);

        /// Builds the shape of a non-NIF model (COLLADA through assimp); returns null and logs a warning when the model
        /// cannot be read, and null when it has no triangles.
        vsg::ref_ptr<PhysicsShape> loadModelShape(VFS::Path::NormalizedView name) const;

        /// With a hook set: opens the file, and when the hook accepts its first bytes returns true and the hook's
        /// shape in `shape`. False (the file untouched) for every file the hook does not accept.
        bool readSceneShape(VFS::Path::NormalizedView name, vsg::ref_ptr<PhysicsShape>& shape) const;

        vsg::ref_ptr<MultiObjectCache> mInstanceCache;
        NifFileManager* mNifFileManager;
        std::shared_ptr<const SceneShapeReader> mSceneShapeReader;
    };

}

#endif
