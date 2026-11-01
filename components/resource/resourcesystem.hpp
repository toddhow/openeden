#ifndef OPENMW_COMPONENTS_RESOURCE_RESOURCESYSTEM_H
#define OPENMW_COMPONENTS_RESOURCE_RESOURCESYSTEM_H

#include <vsg/core/ref_ptr.h>

#include <memory>
#include <vector>

namespace vsg
{
    class Options;
    class Sampler;
}

namespace VFS
{
    class Manager;
}

namespace Misc
{
    class Stats;
}
namespace Pipeline
{
    class Builder;
}

namespace vsgAdapters
{
    class nif;
}
namespace vsg
{
    class Descriptor;
}

namespace Resource
{
    /*
     * Assembles vsg::Options for reading files.
     */
    class ResourceSystem
    {
    public:
        explicit ResourceSystem(const VFS::Manager* vfs, const std::string& shaderPath,
            vsg::ref_ptr<vsg::Sampler> defaultSampler, bool supportsCompressedImages, int computeBin,
            int depthSortedBin);
        ~ResourceSystem();

        /// OpenEden: the weather particle occlusion map, for the model the setting names (task 0012, V4l). Set before
        /// that model is loaded.
        void setOccludedParticleMap(vsg::ref_ptr<vsg::Descriptor> map);

        /// Indicates to each resource manager to clear the cache, i.e. to drop cached objects that are no longer
        /// referenced.
        /// @note May be called from any thread if you do not add or remove resource managers at that point.
        void updateCache(double referenceTime);

        /// Indicates to each resource manager to clear the entire cache.
        /// @note May be called from any thread if you do not add or remove resource managers at that point.
        void clearCache();

        /// @note May be called from any thread.
        const VFS::Manager* getVFS() const;

        void reportStats(unsigned int frameNumber, Misc::Stats* stats) const;

    private:
        const VFS::Manager* mVFS;

        ResourceSystem(const ResourceSystem&);
        void operator=(const ResourceSystem&);

    public:
        const vsg::ref_ptr<const vsg::Options> shaderOptions;
        const std::unique_ptr<const Pipeline::Builder> builder;
        const vsg::ref_ptr<const vsg::Options> imageOptions;
        const vsg::ref_ptr<const vsg::Options> textureOptions;
        // Declared before nodeOptions, which creates it.
        vsg::ref_ptr<vsgAdapters::nif> nifReader;
        const vsg::ref_ptr<const vsg::Options> nodeOptions;
        const vsg::ref_ptr<const vsg::Options> animationOptions;
    };

}

#endif
