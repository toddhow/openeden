#include "physicsshapemanager.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <components/debug/debuglog.hpp>
#include <components/files/hash.hpp>
#include <components/files/istreamptr.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <components/nifjolt/joltnifloader.hpp>

#include "assimpscene.hpp"
#include "multiobjectcache.hpp"
#include "niffilemanager.hpp"
#include "objectcache.hpp"
#include "physicsshape.hpp"

namespace Resource
{
    namespace
    {
        // openeden-vsg: port of OpenMW 0.51's NodeToShapeVisitor (components/resource/physicsshapemanager.cpp at
        // e3957be0b2^1), which built a triangle mesh from every drawable of a non-NIF model's OSG scene graph. The
        // model is now read with assimp, which also backs vsgXchange's model reader. As in 0.51 the shape is static
        // (animation is ignored), only triangles count, and a node named "Collision" is preferred when it has geometry.
        // Unlike 0.51, which started its second visitor at the "Collision" node, the transforms above that node are
        // applied too: assimp puts the file's up-axis and unit conversion at the root, so skipping it would rotate
        // the shape.
        class NodeToShape
        {
        public:
            NodeToShape(const aiScene& scene, bool collisionNodeOnly)
                : mScene(scene)
                , mCollisionNodeOnly(collisionNodeOnly)
            {
                // Lives on the stack, so no Jolt Ref may take ownership of it.
                mTriangleMesh.SetEmbedded();
                mTriangleMesh.mBuildQuality = JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
                visit(*scene.mRootNode, sAssimpYUpToZUp, false);
            }

            vsg::ref_ptr<PhysicsShape> getShape()
            {
                if (mTriangleMesh.mIndexedTriangles.empty())
                    return {};

                JPH::ShapeSettings::ShapeResult created = mTriangleMesh.Create();
                if (created.HasError())
                {
                    // Remove degenerate and duplicate triangles, then try again
                    mTriangleMesh.ClearCachedResult();
                    mTriangleMesh.Sanitize();
                    created = mTriangleMesh.Create();
                    if (created.HasError())
                    {
                        Log(Debug::Error) << "PhysicsShapeManager mesh error: " << created.GetError();
                        return {};
                    }
                }

                vsg::ref_ptr<PhysicsShape> shape(new PhysicsShape);
                shape->mCollisionShape = created.Get();

                const JPH::AABox bounds = shape->mCollisionShape->GetLocalBounds();
                const JPH::Vec3 extents = bounds.GetExtent();
                const JPH::Vec3 center = bounds.GetCenter();
                shape->mCollisionBox.mExtents = vsg::vec3(extents.GetX(), extents.GetY(), extents.GetZ());
                shape->mCollisionBox.mCenter = vsg::vec3(center.GetX(), center.GetY(), center.GetZ());
                return shape;
            }

        private:
            void visit(const aiNode& node, const aiMatrix4x4& parentMatrix, bool insideCollisionNode)
            {
                const aiMatrix4x4 matrix = parentMatrix * node.mTransformation;
                // As 0.51's FindByNameVisitor, only the first node named "Collision" (in document order) counts.
                const bool isCollisionNode = mCollisionNodeOnly && !insideCollisionNode && !mFoundCollisionNode
                    && Misc::StringUtils::ciEqual(std::string_view(node.mName.C_Str(), node.mName.length), "Collision");
                if (isCollisionNode)
                    insideCollisionNode = true;

                if (!mCollisionNodeOnly || insideCollisionNode)
                    for (unsigned int i = 0; i < node.mNumMeshes; ++i)
                        appendTriangles(*mScene.mMeshes[node.mMeshes[i]], matrix);

                for (unsigned int i = 0; i < node.mNumChildren; ++i)
                    visit(*node.mChildren[i], matrix, insideCollisionNode);

                if (isCollisionNode)
                    mFoundCollisionNode = true;
            }

            void appendTriangles(const aiMesh& mesh, const aiMatrix4x4& matrix)
            {
                const std::size_t baseVertex = mTriangleMesh.mTriangleVertices.size();
                if (mesh.mNumVertices > std::numeric_limits<std::uint32_t>::max() - baseVertex)
                    return;

                mTriangleMesh.mTriangleVertices.reserve(baseVertex + mesh.mNumVertices);
                for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
                {
                    const aiVector3D vertex = matrix * mesh.mVertices[i];
                    mTriangleMesh.mTriangleVertices.emplace_back(vertex.x, vertex.y, vertex.z);
                }

                const auto indexBase = static_cast<std::uint32_t>(baseVertex);
                for (unsigned int i = 0; i < mesh.mNumFaces; ++i)
                {
                    const aiFace& face = mesh.mFaces[i];
                    if (face.mNumIndices != 3 || face.mIndices[0] >= mesh.mNumVertices
                        || face.mIndices[1] >= mesh.mNumVertices || face.mIndices[2] >= mesh.mNumVertices)
                        continue;
                    mTriangleMesh.mIndexedTriangles.emplace_back(
                        indexBase + face.mIndices[0], indexBase + face.mIndices[1], indexBase + face.mIndices[2]);
                }
            }

            const aiScene& mScene;
            const bool mCollisionNodeOnly;
            bool mFoundCollisionNode = false;
            JPH::MeshShapeSettings mTriangleMesh;
        };

        vsg::ref_ptr<PhysicsShape> createShape(const aiScene& scene)
        {
            // Check first if there's a custom collision node
            if (vsg::ref_ptr<PhysicsShape> shape = NodeToShape(scene, true).getShape())
                return shape;

            // Generate a collision shape from the mesh
            return NodeToShape(scene, false).getShape();
        }
    }

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
            // Task 0110: a file the host's hook accepts (a Skyrim SE model) is the hook's alone.
            if (!mSceneShapeReader || !readSceneShape(name, shape))
            {
                NifJolt::JoltNifLoader loader(getVFS());
                shape = loader.load(*mNifFileManager->get(name));
            }
        }
        else
            shape = loadModelShape(name);

        mCache->addEntryToObjectCache(name.value(), shape);

        return shape;
    }

    bool PhysicsShapeManager::readSceneShape(VFS::Path::NormalizedView name, vsg::ref_ptr<PhysicsShape>& shape) const
    {
        if (!mSceneShapeReader || !mSceneShapeReader->accepts || !mSceneShapeReader->read || !getVFS()->exists(name))
            return false;
        // The first bytes decide; a file the hook does not accept is opened again by the Morrowind loader.
        constexpr std::size_t headerBytes = 256;
        {
            Files::IStreamPtr probe = getVFS()->get(name);
            char head[headerBytes];
            probe->read(head, static_cast<std::streamsize>(headerBytes));
            const auto got = static_cast<std::size_t>(std::max<std::streamsize>(probe->gcount(), 0));
            if (!mSceneShapeReader->accepts(std::string_view(head, got)))
                return false;
        }
        Files::IStreamPtr stream = getVFS()->get(name);
        std::string data((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        shape = mSceneShapeReader->read(std::move(data), std::string(name.value()));
        if (shape != nullptr)
        {
            shape->mFileName = name;
            shape->mFileHash.clear();
        }
        return true;
    }

    vsg::ref_ptr<PhysicsShape> PhysicsShapeManager::loadModelShape(VFS::Path::NormalizedView name) const
    {
        // openeden-vsg: OpenMW 0.51 read the model through SceneManager::getTemplate and OSG's loaders; the VSG
        // renderer has no shared template for non-NIF models, so the model is read here.
        if (!getVFS()->exists(name))
        {
            Log(Debug::Warning) << "No collision shape for \"" << name << "\": the file does not exist";
            return {};
        }

        Files::IStreamPtr stream = getVFS()->get(name);
        const std::array<std::uint64_t, 2> fileHash = Files::getHash(name.value(), *stream);
        const std::string content(std::istreambuf_iterator<char>(*stream), {});

        // One importer per call, so shapes can be loaded from any thread.
        Assimp::Importer importer;
        std::string error;
        const aiScene* scene
            = readAssimpScene(importer, content, Misc::getFileExtension(name.value()), aiProcess_Triangulate, error);
        if (scene == nullptr)
        {
            Log(Debug::Warning) << "No collision shape for \"" << name << "\": " << error;
            return {};
        }

        vsg::ref_ptr<PhysicsShape> shape = createShape(*scene);
        if (shape != nullptr)
        {
            shape->mFileName = name;
            shape->mFileHash.assign(
                reinterpret_cast<const char*>(fileHash.data()), fileHash.size() * sizeof(std::uint64_t));
        }
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
