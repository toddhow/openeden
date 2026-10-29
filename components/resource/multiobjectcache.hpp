#ifndef OPENMW_COMPONENTS_MULTIOBJECTCACHE_H
#define OPENMW_COMPONENTS_MULTIOBJECTCACHE_H

#include <map>
#include <mutex>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>

#include <components/vfs/pathutil.hpp>

#include "cachestats.hpp"

namespace Resource
{

    /// @brief Cache for "non reusable" objects.
    class MultiObjectCache : public vsg::Object
    {
    public:
        void removeUnreferencedObjectsInCache();

        /** Remove all objects from the cache. */
        void clear();

        void addEntryToObjectCache(VFS::Path::NormalizedView filename, vsg::Object* object);

        /** Take an Object from cache. Return nullptr if no object found. */
        vsg::ref_ptr<vsg::Object> takeFromObjectCache(VFS::Path::NormalizedView fileName);

        CacheStats getStats() const;

    protected:
        typedef std::multimap<VFS::Path::Normalized, vsg::ref_ptr<vsg::Object>, std::less<>> ObjectCacheMap;

        ObjectCacheMap _objectCache;
        mutable std::mutex _objectCacheMutex;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        std::size_t mExpired = 0;
    };

}

#endif
