#ifndef OPENEDEN_VSGUTIL_TEXTUREMIPS_H
#define OPENEDEN_VSGUTIL_TEXTUREMIPS_H

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include <vsg/core/Data.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/utils/SharedObjects.h>

#include <components/render/vramtexturemips.hpp>

namespace vsgUtil::TextureMips
{
    /*
     * Task 0080, stage d, first slice: swaps the image of one texture for a copy without its top mip levels, and back.
     *
     * VSG compiles a BindDescriptorSet once (BindDescriptorSet::compile returns at once when it has a Vulkan handle,
     * and a set in flight must not be rewritten), so a swap builds a NEW Image, ImageView, ImageInfo, DescriptorImage,
     * DescriptorSet and BindDescriptorSet, compiles them through the hook, points every StateGroup that held the old
     * bind at the new one and gives the old bind to the `retire` hook (the deletion queue, so nothing is freed while a
     * frame in flight may use it). The users of a texture are found by scanning the scene graph (`scan`), not
     * registered by the loaders: a scan sees every group, also copies of a shared model, and the loaders are not
     * touched. The objects the SharedObjects cache holds (the full-size bind) become stale; a model loaded later
     * brings the full image back, and the next scan sees it at drop 0 and the planner reduces it again.
     *
     * Main thread only, between frames (the scene graph is edited). Owns: the reduced CPU copies (the full image is
     * kept alive by its entry, so a restore needs no reload) and, per texture, the groups seen at the last scan
     * (weakly).
     */

    struct Hooks
    {
        /// Compiles a new object (a BindDescriptorSet) for the device; false on failure.
        std::function<bool(const vsg::ref_ptr<vsg::Object>&)> compile;
        /// Gives an old object up; it must stay alive until no frame in flight can use it.
        std::function<void(vsg::ref_ptr<vsg::Object>)> retire;
    };

    struct SwapResult
    {
        bool ok = false;
        int groups = 0; // groups pointed at a new bind
        int binds = 0; // new binds built (groups sharing one bind share the new one)
        std::int64_t bytesBefore = 0; // the texture's resident bytes by the plan's numbers, per bind that changed
        std::int64_t bytesAfter = 0;
        double buildMs = 0.0; // the new data and objects
        double compileMs = 0.0; // the hook
        double replaceMs = 0.0; // pointing the groups at the new binds
    };

    class Swapper
    {
    public:
        Swapper(Hooks hooks, Render::VideoMemory::TextureMips::Params params);

        /// Walks the graph below `root`, updates the table and returns the eligible textures it holds.
        std::vector<Render::VideoMemory::TextureMips::Texture> scan(vsg::Node& root);

        /// As above, and feeds the plan's use signal: a texture is `visible` when a group that draws it lies inside a
        /// bounding sphere (vsg::CullNode or CullGroup, moved by the transforms above it) whose surface is within
        /// `params().visibleDistance` of `camera`; `lastUsedFrame` is the `frame` of the last scan where that held.
        /// Limits: a distance only (no frustum, so objects behind the camera count), the sphere's radius is not scaled
        /// by the transforms, groups with no sphere above them (the cached model templates) never count.
        std::vector<Render::VideoMemory::TextureMips::Texture> scan(
            vsg::Node& root, const vsg::dvec3& camera, std::uint64_t frame);

        /// Puts every bind of texture `id` at `toDrop` dropped top levels.
        SwapResult swap(std::uint64_t id, int toDrop, std::uint64_t frame);

        /// Resident bytes of all tracked textures at their current drops (by the plan's numbers; per distinct image).
        std::int64_t residentBytes() const;
        std::size_t textureCount() const { return mEntries.size(); }

        /// A diagnostic: of the binds given to `retire`, how many are still alive, and the reference counts they hold
        /// (a bind nothing else holds is destroyed when the deletion queue lets go of it; the shared object caches
        /// and a cached model's own groups keep it alive until pruned or swapped).
        struct RetiredReport
        {
            int retired = 0;
            int alive = 0;
            unsigned referenceCounts = 0;
            std::vector<std::uint64_t> aliveTextures; // the texture each live bind was retired for
            int imagesRetired = 0; // the images of the retired binds that were swapped away
            int imagesAlive = 0; // and how many of them still exist
            unsigned imageReferenceCounts = 0;
        };
        RetiredReport retiredReport();
        const Render::VideoMemory::TextureMips::Params& params() const { return mParams; }

    private:
        struct User
        {
            vsg::observer_ptr<vsg::StateGroup> group;
            int drop = 0;
            double distance = std::numeric_limits<double>::infinity(); // to the nearest sphere above it
        };
        struct Entry
        {
            std::uint64_t id = 0;
            vsg::ref_ptr<vsg::Data> full;
            std::map<int, vsg::ref_ptr<vsg::Data>> reduced;
            std::vector<User> users;
            bool changedBefore = false;
            std::uint64_t lastChangeFrame = 0;
            std::uint64_t lastNearFrame = 0;
        };

        friend struct ScanVisitor;
        std::vector<Render::VideoMemory::TextureMips::Texture> scanImpl(
            vsg::Node& root, const vsg::dvec3* camera, std::uint64_t frame);
        Hooks mHooks;
        Render::VideoMemory::TextureMips::Params mParams;
        std::vector<Entry> mEntries;
        std::unordered_map<const vsg::Data*, std::pair<std::size_t, int>> mDataIndex; // data -> (entry, drop)
        std::uint64_t mNextId = 1;
        struct Probe
        {
            vsg::observer_ptr<vsg::Object> object;
            std::uint64_t texture = 0;
        };
        std::vector<Probe> mRetiredProbes;
        std::vector<vsg::observer_ptr<vsg::Object>> mRetiredImages;
    };

    /// The reduced copy that images read from now on should be given in place of `full`, or null: while a texture is
    /// swapped down, a model loaded later (which shares the cached full image) would bring the full image back.
    /// Thread safe (the readers run on loader threads). Set and cleared by Swapper::swap and forgotten by scan.
    vsg::ref_ptr<vsg::Data> activeReduction(const vsg::Data* full);

    /// The models cached in a SharedObjects (its LoadedObject entries): they are the templates scene objects are
    /// cloned from, and they share the binds of the scene, so the swapper has to reach them too (task 0080: two binds
    /// stayed alive because their template, in this cache, still held them).
    std::vector<vsg::ref_ptr<vsg::Node>> cachedModels(vsg::SharedObjects& sharedObjects);

    /// True for an image the swapper can reduce: a plain 2D image with a mip chain of at least two levels.
    bool reducible(const vsg::Data& data);
}

#endif
