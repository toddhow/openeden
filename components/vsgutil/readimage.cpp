#include "readimage.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

#include <vsg/core/Data.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/io/read.h>

#include "texturemips.hpp"
#include "textureresize.hpp"

namespace vsgUtil
{
    void removeSrgb(vsg::Data& data)
    {
        // openeden-vsg: vsgXchange 1.1's stb_image reader (TGA, PNG, JPG, BMP) tags 8-bit images as sRGB, so the GPU
        // converted them to linear when sampling and textures came out far too dark. vsgopenmw's shaders work on the
        // stored values, as OpenMW's did, so the same bytes are read as UNORM.
        VkFormat& format = data.properties.format;
        switch (format)
        {
            case VK_FORMAT_R8_SRGB:
                format = VK_FORMAT_R8_UNORM;
                break;
            case VK_FORMAT_R8G8_SRGB:
                format = VK_FORMAT_R8G8_UNORM;
                break;
            case VK_FORMAT_R8G8B8_SRGB:
                format = VK_FORMAT_R8G8B8_UNORM;
                break;
            case VK_FORMAT_B8G8R8_SRGB:
                format = VK_FORMAT_B8G8R8_UNORM;
                break;
            case VK_FORMAT_R8G8B8A8_SRGB:
                format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case VK_FORMAT_B8G8R8A8_SRGB:
                format = VK_FORMAT_B8G8R8A8_UNORM;
                break;
            // Task 0044: a DDS with a DX10 header can be tagged sRGB in a block-compressed format (Skyrim's True PBR
            // colour files are). The sampler would then convert to linear and the shader would do it again.
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
                format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                break;
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
                format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                break;
            case VK_FORMAT_BC2_SRGB_BLOCK:
                format = VK_FORMAT_BC2_UNORM_BLOCK;
                break;
            case VK_FORMAT_BC3_SRGB_BLOCK:
                format = VK_FORMAT_BC3_UNORM_BLOCK;
                break;
            case VK_FORMAT_BC7_SRGB_BLOCK:
                format = VK_FORMAT_BC7_UNORM_BLOCK;
                break;
            default:
                break;
        }
    }

    namespace
    {
        // Task 0049: the capped copy of an image that is in the shared object cache. vsg::read caches the full image
        // under its file name; a capped read keeps its smaller copy here, keyed by the full image and the cap, so that
        // every user of one file gets one capped image (and one GPU image) and two categories with different caps do
        // not collide. Both sides are weak: an entry lives while something uses the capped image, and the cache never
        // keeps a texture alive that the engine has let go of.
        struct CapKey
        {
            const vsg::Data* original;
            int cap;
            int kind;
            int cutoffMilli;

            bool operator<(const CapKey& rhs) const
            {
                return std::tie(original, cap, kind, cutoffMilli)
                    < std::tie(rhs.original, rhs.cap, rhs.kind, rhs.cutoffMilli);
            }
        };

        struct CapEntry
        {
            vsg::observer_ptr<vsg::Data> original;
            vsg::observer_ptr<vsg::Data> capped;
        };

        std::mutex capCacheMutex;
        std::map<CapKey, CapEntry> capCache;

        vsg::ref_ptr<vsg::Data> findCapped(const CapKey& key, const vsg::ref_ptr<vsg::Data>& original)
        {
            const auto found = capCache.find(key);
            if (found == capCache.end() || found->second.original.ref_ptr() != original)
                return {};
            return found->second.capped.ref_ptr();
        }

        vsg::ref_ptr<vsg::Data> capImage(vsg::ref_ptr<vsg::Data> data, int cap, const ImageUse& use, bool shared)
        {
            // Only an image the options share can come back as the same object, so only that one is worth keeping.
            if (!shared)
                return TextureResize::applyCap(data, cap, use.kind, use.alphaCutoff);
            // The report is written only by the thread whose result is kept, or two loaders racing on one image
            // would count it twice.
            TextureResize::CapOutcome outcome;

            const CapKey key{ data.get(), cap, static_cast<int>(use.kind), static_cast<int>(use.alphaCutoff * 1000.f) };
            {
                std::scoped_lock lock(capCacheMutex);
                if (auto capped = findCapped(key, data))
                    return capped;
            }
            auto capped = TextureResize::applyCapUncounted(data, cap, use.kind, use.alphaCutoff, outcome);
            std::scoped_lock lock(capCacheMutex);
            if (auto other = findCapped(key, data))
                return other; // another loader thread was first
            // Entries whose images are gone are removed now and then, so the map does not grow without limit.
            if (capCache.size() % 256 == 255)
                for (auto it = capCache.begin(); it != capCache.end();)
                    it = it->second.original.valid() ? std::next(it) : capCache.erase(it);
            capCache[key] = CapEntry{ vsg::observer_ptr<vsg::Data>(data), vsg::observer_ptr<vsg::Data>(capped) };
            TextureResize::record(outcome);
            return capped;
        }

        // The cap a read with this use gets, or 0.
        int capFor(const ImageUse& use)
        {
            if (use.category == TextureCap::Category::None)
                return 0;
            int cap = TextureCap::effectiveCap(use.category, TextureCap::getCaps());
            if (use.maxSize > 0)
                cap = cap > 0 ? std::min(cap, use.maxSize) : use.maxSize;
            if (cap > 0 && use.floor > 0)
                cap = std::max(cap, use.floor);
            return cap;
        }
    }

    vsg::ref_ptr<vsg::Data> readImage(
        const std::string& path, vsg::ref_ptr<const vsg::Options> options, const ImageUse& use)
    {
        if (auto data = readOptionalImage(path, options, use))
            return data;
        throw std::runtime_error("!readImage(\"" + path + "\")");
    }

    vsg::ref_ptr<vsg::Data> readOptionalImage(
        const std::string& path, vsg::ref_ptr<const vsg::Options> options, const ImageUse& use)
    {
        auto data = vsg::read_cast<vsg::Data>(path, options);
        if (data)
            removeSrgb(*data);
        // Task 0049: only a read that names a category can be capped; any other is returned as it was read.
        if (data)
        {
            const int cap = capFor(use);
            if (cap > 0)
                data = capImage(data, cap, use, options && options->sharedObjects);
        }
        // Task 0080 stage d: a texture that is swapped down is read at its reduced size, or a model loaded later would
        // bring the full image back (the cache holds the full one).
        if (data)
            if (auto reduced = TextureMips::activeReduction(data.get()))
                data = std::move(reduced);
        return data;
    }

    size_t arrayReadThreadCount(size_t imageCount, size_t requested)
    {
        // Named constant: past this the disk, not the decoder, is the limit and the thread stacks are wasted.
        constexpr size_t maxArrayReadThreads = 8;
        size_t count = requested;
        if (count == 0)
            count = std::min<size_t>(maxArrayReadThreads, std::max(1u, std::thread::hardware_concurrency()));
        return std::max<size_t>(1, std::min(count, imageCount));
    }

    std::vector<vsg::ref_ptr<vsg::Data>> readArrayImages(const std::vector<std::string>& paths,
        vsg::ref_ptr<const vsg::Options> options, const ImageUse& use, bool required, size_t threads)
    {
        auto readAll = [&](const ImageUse& readUse) {
            std::vector<vsg::ref_ptr<vsg::Data>> images(paths.size());
            std::vector<std::exception_ptr> failures(paths.size());
            auto readOne = [&](size_t n) {
                try
                {
                    images[n] = required ? readImage(paths[n], options, readUse)
                                         : readOptionalImage(paths[n], options, readUse);
                }
                catch (...)
                {
                    failures[n] = std::current_exception();
                }
            };
            const size_t count = arrayReadThreadCount(paths.size(), threads);
            if (count <= 1)
            {
                for (size_t n = 0; n < paths.size(); ++n)
                    readOne(n);
            }
            else
            {
                // Every path is read by exactly one thread; the calling thread works too. The readers share the
                // options' object cache, which locks, as the model loader threads already rely on.
                std::atomic<size_t> next{ 0 };
                auto work = [&] {
                    for (size_t n = next++; n < paths.size(); n = next++)
                        readOne(n);
                };
                std::vector<std::thread> workers;
                workers.reserve(count - 1);
                for (size_t t = 1; t < count; ++t)
                    workers.emplace_back(work);
                work();
                for (auto& worker : workers)
                    worker.join();
            }
            for (const auto& failure : failures)
                if (failure)
                    std::rethrow_exception(failure);
            return images;
        };
        auto images = readAll(use);
        const int cap = capFor(use);
        if (cap <= 0)
            return images;
        for (const auto& image : images)
        {
            if (!image)
                continue;
            const auto [width, height, depth, faces] = image->pixelExtents();
            if (std::max(width, height) > static_cast<uint32_t>(cap))
            {
                // One layer could not be brought down (a block compressed image with one level). Layers of different
                // sizes would leave it grey in the array, so the array keeps its full sizes: the cap never makes an
                // array worse than no cap.
                std::cerr << "Texture cap: an image of an array cannot be made smaller, so the whole array is left at "
                             "full size"
                          << std::endl;
                return readAll(ImageUse{});
            }
        }
        return images;
    }
}
