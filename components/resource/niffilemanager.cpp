#include "niffilemanager.hpp"

#include <vsg/core/Object.h>

#include <components/vfs/manager.hpp>

#include "objectcache.hpp"

namespace Resource
{

    class NifFileHolder : public vsg::Object
    {
    public:
        NifFileHolder(const Nif::NIFFilePtr& file)
            : mNifFile(file)
        {
        }
        Nif::NIFFilePtr mNifFile;
    };

    NifFileManager::NifFileManager(const VFS::Manager* vfs, const ToUTF8::StatelessUtf8Encoder* encoder)
        // NIF files aren't needed any more once the converted objects are cached in PhysicsShapeManager,
        // so no point in using an expiry delay.
        : ResourceManager(vfs, 0)
        , mEncoder(encoder)
    {
    }

    NifFileManager::~NifFileManager() = default;

    Nif::NIFFilePtr NifFileManager::get(VFS::Path::NormalizedView name)
    {
        vsg::ref_ptr<vsg::Object> obj = mCache->getRefFromObjectCache(name);
        if (obj != nullptr)
            return static_cast<NifFileHolder*>(obj.get())->mNifFile;

        auto file = std::make_shared<Nif::NIFFile>(name);
        Nif::Reader reader(*file, mEncoder);
        reader.parse(mVFS->get(name));
        obj = new NifFileHolder(file);
        mCache->addEntryToObjectCache(name.value(), obj);
        return file;
    }

}
