#include "multiobjectcache.hpp"

#include <iostream>
#include <vector>

namespace Resource
{
    void MultiObjectCache::removeUnreferencedObjectsInCache()
    {
        std::vector<vsg::ref_ptr<vsg::Object>> objectsToRemove;
        {
            std::lock_guard<std::mutex> lock(_objectCacheMutex);

            // Remove unreferenced entries from object cache
            ObjectCacheMap::iterator oitr = _objectCache.begin();
            while (oitr != _objectCache.end())
            {
                if (oitr->second->referenceCount() <= 1)
                {
                    objectsToRemove.push_back(oitr->second);
                    _objectCache.erase(oitr++);
                    ++mExpired;
                }
                else
                {
                    ++oitr;
                }
            }
        }

        // note, actual unref happens outside of the lock
        objectsToRemove.clear();
    }

    void MultiObjectCache::clear()
    {
        std::lock_guard<std::mutex> lock(_objectCacheMutex);
        _objectCache.clear();
    }

    void MultiObjectCache::addEntryToObjectCache(VFS::Path::NormalizedView filename, vsg::Object* object)
    {
        if (!object)
        {
            std::cerr << " trying to add NULL object to cache for " << filename << std::endl;
            return;
        }
        std::lock_guard<std::mutex> lock(_objectCacheMutex);
        _objectCache.emplace(filename, object);
    }

    vsg::ref_ptr<vsg::Object> MultiObjectCache::takeFromObjectCache(VFS::Path::NormalizedView fileName)
    {
        std::lock_guard<std::mutex> lock(_objectCacheMutex);
        ++mGet;
        const auto it = _objectCache.find(fileName);
        if (it != _objectCache.end())
        {
            vsg::ref_ptr<vsg::Object> object = std::move(it->second);
            _objectCache.erase(it);
            ++mHit;
            return object;
        }

        return nullptr;
    }

    CacheStats MultiObjectCache::getStats() const
    {
        std::lock_guard<std::mutex> lock(_objectCacheMutex);
        return CacheStats{
            .mSize = _objectCache.size(),
            .mGet = mGet,
            .mHit = mHit,
            .mExpired = mExpired,
        };
    }

}
