#ifndef OPENMW_COMPONENTS_RESOURCE_RESOURCESYSTEM_H
#define OPENMW_COMPONENTS_RESOURCE_RESOURCESYSTEM_H

#include <vsg/core/ref_ptr.h>

#include <memory>
#include <string>
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

namespace Render::Pbr
{
    class OverrideTable;
}

namespace Resource
{
    /// The options every shader of the game is read with, over the folder `shaderPath` (the build's or install's
    /// resources/shaders): the `*.settings` reader (Resource::ShaderSettings, made now from the settings), glsl and
    /// spirv readers, shared objects, and `[Rendering] slang shaders` as vsgUtil::preferSlangKey. Task 0072, review of
    /// the stage f and g rounds, finding 1: public, so that tests compile the shaders with the game's own options.
    vsg::ref_ptr<const vsg::Options> createShaderOptions(
        const std::string& shaderPath, const std::string& cacheDirectory = {});

    /*
     * Assembles vsg::Options for reading files.
     */
    class ResourceSystem
    {
    public:
        explicit ResourceSystem(const VFS::Manager* vfs, const std::string& shaderPath,
            vsg::ref_ptr<vsg::Sampler> defaultSampler, bool supportsCompressedImages, int computeBin,
            int depthSortedBin, const std::string& shaderCacheDirectory = {});
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
        // OpenEden (task 0027): the material overrides, made by the renderer when `[Rendering] pbr` is on; null
        // otherwise, and then nothing is looked up or marked.
        std::shared_ptr<Render::Pbr::OverrideTable> materialTable;
    };

}

#endif
