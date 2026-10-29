#include "physicsshapemanager.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <components/nifjolt/joltnifloader.hpp>

#include "multiobjectcache.hpp"
#include "niffilemanager.hpp"
#include "objectcache.hpp"
#include "physicsshape.hpp"

namespace Resource
{
    PhysicsShapeManager::PhysicsShapeManager(
        const VFS::Manager* vfs, NifFileManager* nifFileManager, double expiryDelay)
        : ResourceManager(vfs, expiryDelay)
        , mInstanceCache(new MultiObjectCache)
        , mNifFileManager(nifFileManager)
    {
    }

    PhysicsShapeManager::~PhysicsShapeManager() = default;

    vsg::ref_ptr<const PhysicsShape> PhysicsShapeManager::getShape(VFS::Path::NormalizedView name)
    {
        if (vsg::ref_ptr<vsg::Object> obj = mCache->getRefFromObjectCache(name))
            return vsg::ref_ptr<PhysicsShape>(static_cast<PhysicsShape*>(obj.get()));

        vsg::ref_ptr<PhysicsShape> shape;

        if (Misc::getFileExtension(name.value()) == "nif")
        {
            NifJolt::JoltNifLoader loader(getVFS());
            shape = loader.load(*mNifFileManager->get(name));
        }
        else
        {
            // openeden-vsg: shapes for other mesh formats were built from the OSG scene graph, which is gone.
            // They return when the mesh-to-shape visitor is ported to VSG scene graphs (task 0010, stage V2).
            Log(Debug::Warning) << "No collision shape for \"" << name
                                << "\": only NIF collision is supported while the VSG port is in progress";
        }

        mCache->addEntryToObjectCache(name.value(), shape);

        return shape;
    }

    vsg::ref_ptr<PhysicsShapeInstance> PhysicsShapeManager::cacheInstance(VFS::Path::NormalizedView name)
    {
        vsg::ref_ptr<PhysicsShapeInstance> instance = createInstance(name);
        if (instance != nullptr)
            mInstanceCache->addEntryToObjectCache(name, instance.get());
        return instance;
    }

    vsg::ref_ptr<PhysicsShapeInstance> PhysicsShapeManager::getInstance(VFS::Path::NormalizedView name)
    {
        if (vsg::ref_ptr<vsg::Object> obj = mInstanceCache->takeFromObjectCache(name))
            return vsg::ref_ptr<PhysicsShapeInstance>(static_cast<PhysicsShapeInstance*>(obj.get()));
        return createInstance(name);
    }

    vsg::ref_ptr<PhysicsShapeInstance> PhysicsShapeManager::createInstance(VFS::Path::NormalizedView name)
    {
        if (vsg::ref_ptr<const PhysicsShape> shape = getShape(name))
            return makeInstance(std::move(shape));
        return vsg::ref_ptr<PhysicsShapeInstance>();
    }

    void PhysicsShapeManager::updateCache(double referenceTime)
    {
        ResourceManager::updateCache(referenceTime);

        mInstanceCache->removeUnreferencedObjectsInCache();
    }

    void PhysicsShapeManager::clearCache()
    {
        ResourceManager::clearCache();

        mInstanceCache->clear();
    }

}
