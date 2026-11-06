#include "textureresize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#include <vsg/core/Allocator.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/MipmapLayout.h>

#include "arraytexture.hpp"

namespace vsgUtil::TextureResize
{
    namespace
    {
        // A plain 2D image (not a 3D one, an array or a cube map).
        bool isPlain2D(const vsg::Data& data)
        {
            if (data.dimensions() != 2 || data.depth() > 1)
                return false;
            const int8_t type = data.properties.imageViewType;
            return type < 0 || type == VK_IMAGE_VIEW_TYPE_2D;
        }

        template <class ArrayType>
        vsg::ref_ptr<vsg::Data> buildArray(uint32_t width, uint32_t height, const uint8_t* bytes, size_t size,
            vsg::Data::Properties properties, vsg::MipmapLayout* layout)
        {
            using Value = typename ArrayType::value_type;
            auto* memory = static_cast<Value*>(vsg::allocate(size, vsg::ALLOCATOR_AFFINITY_DATA));
            std::memcpy(static_cast<void*>(memory), bytes, size);
            properties.allocatorType = vsg::ALLOCATOR_TYPE_VSG_ALLOCATOR;
            properties.stride = 0; // the array sets it to the size of one value
            return ArrayType::create(width, height, memory, properties, layout);
        }

        // An Array2D whose value has the size of one texel (or one block), made to hold `size` bytes.
        vsg::ref_ptr<vsg::Data> buildData(size_t valueSize, bool blocks, uint32_t width, uint32_t height,
            const uint8_t* bytes, size_t size, const vsg::Data::Properties& properties, vsg::MipmapLayout* layout)
        {
            switch (valueSize)
            {
                case 1:
                    return buildArray<vsg::ubyteArray2D>(width, height, bytes, size, properties, layout);
                case 2:
                    return buildArray<vsg::ubvec2Array2D>(width, height, bytes, size, properties, layout);
                case 3:
                    return buildArray<vsg::ubvec3Array2D>(width, height, bytes, size, properties, layout);
                case 4:
                    return buildArray<vsg::ubvec4Array2D>(width, height, bytes, size, properties, layout);
                case 8:
                    return blocks ? buildArray<vsg::block64Array2D>(width, height, bytes, size, properties, layout)
                                  : buildArray<vsg::usvec4Array2D>(width, height, bytes, size, properties, layout);
                case 16:
                    return blocks ? buildArray<vsg::block128Array2D>(width, height, bytes, size, properties, layout)
                                  : buildArray<vsg::vec4Array2D>(width, height, bytes, size, properties, layout);
                default:
                    return {};
            }
        }

        uint32_t ceilDiv(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1) / divisor;
        }

        // The channel count of the formats shrinkUncompressed accepts, or 0.
        size_t shrinkableChannels(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R8_UNORM:
                    return 1;
                case VK_FORMAT_R8G8_UNORM:
                    return 2;
                case VK_FORMAT_R8G8B8_UNORM:
                case VK_FORMAT_B8G8R8_UNORM:
                    return 3;
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_B8G8R8A8_UNORM:
                    return 4;
                default:
                    return 0;
            }
        }

        uint8_t encodeUnit(double v)
        {
            return static_cast<uint8_t>(std::clamp(std::lround((v * 0.5 + 0.5) * 255.0), 0l, 255l));
        }

        // The first three channels as a vector in -1..1, scaled to length one again. A zero-length vector is left.
        void renormalise(uint8_t* texel)
        {
            double v[3];
            double length = 0;
            for (int c = 0; c < 3; ++c)
            {
                v[c] = texel[c] / 255.0 * 2.0 - 1.0;
                length += v[c] * v[c];
            }
            length = std::sqrt(length);
            if (length < 1e-6)
                return;
            for (int c = 0; c < 3; ++c)
                texel[c] = encodeUnit(v[c] / length);
        }

        // One halving by a box filter of the stored values: each new texel is the rounded mean of a 2 by 2 block, with
        // the block clamped to the image on an odd side.
        void halve(const uint8_t* in, uint32_t width, uint32_t height, size_t channels, uint8_t* out, uint32_t outWidth,
            uint32_t outHeight, bool normals)
        {
            for (uint32_t y = 0; y < outHeight; ++y)
            {
                const uint32_t y0 = std::min(2 * y, height - 1);
                const uint32_t y1 = std::min(2 * y + 1, height - 1);
                for (uint32_t x = 0; x < outWidth; ++x)
                {
                    const uint32_t x0 = std::min(2 * x, width - 1);
                    const uint32_t x1 = std::min(2 * x + 1, width - 1);
                    uint8_t* texel = out + (static_cast<size_t>(y) * outWidth + x) * channels;
                    for (size_t c = 0; c < channels; ++c)
                    {
                        const unsigned sum = in[(static_cast<size_t>(y0) * width + x0) * channels + c]
                            + in[(static_cast<size_t>(y0) * width + x1) * channels + c]
                            + in[(static_cast<size_t>(y1) * width + x0) * channels + c]
                            + in[(static_cast<size_t>(y1) * width + x1) * channels + c];
                        texel[c] = static_cast<uint8_t>((sum + 2) / 4);
                    }
                    if (normals)
                        renormalise(texel);
                }
            }
        }

        size_t coverageAbove(const uint8_t* texels, size_t count, size_t channels, int threshold)
        {
            size_t covered = 0;
            for (size_t i = 0; i < count; ++i)
                if (texels[i * channels + 3] >= threshold)
                    ++covered;
            return covered;
        }

        // The standard coverage preserving rule: scale the alpha of the reduced image so that as many of its texels
        // pass the test (alpha >= cutoff) as the share of the big image's texels did. The scale is chosen among the
        // thresholds 1..255 (a texel at `best` or more is brought to the cutoff), the first that comes closest, so the
        // result does not depend on anything but the two images.
        void preserveCoverage(
            const uint8_t* big, size_t bigCount, uint8_t* reduced, size_t reducedCount, size_t channels, float cutoff)
        {
            const int test = std::clamp(static_cast<int>(std::ceil(cutoff * 255.0f)), 1, 255);
            const size_t bigCovered = coverageAbove(big, bigCount, channels, test);
            if (bigCovered == 0 || bigCovered == bigCount)
                return;
            const double target = static_cast<double>(bigCovered) * static_cast<double>(reducedCount) / bigCount;

            std::array<size_t, 256> histogram{};
            for (size_t i = 0; i < reducedCount; ++i)
                ++histogram[reduced[i * channels + 3]];
            std::array<size_t, 257> atOrAbove{};
            for (int a = 255; a >= 0; --a)
                atOrAbove[a] = atOrAbove[a + 1] + histogram[a];

            int best = test;
            double bestDifference = std::abs(static_cast<double>(atOrAbove[test]) - target);
            for (int t = 1; t <= 255; ++t)
            {
                const double difference = std::abs(static_cast<double>(atOrAbove[t]) - target);
                if (difference < bestDifference)
                {
                    best = t;
                    bestDifference = difference;
                }
            }
            if (best == test)
                return;
            for (size_t i = 0; i < reducedCount; ++i)
            {
                uint8_t& alpha = reduced[i * channels + 3];
                alpha = static_cast<uint8_t>(std::min(255u, static_cast<unsigned>((alpha * test + best / 2) / best)));
            }
        }
    }

    vsg::ref_ptr<vsg::Data> dropMipLevels(const vsg::Data& source, int levels)
    {
        if (levels <= 0)
            return vsg::ref_ptr<vsg::Data>(const_cast<vsg::Data*>(&source));
        if (!isPlain2D(source))
            return {};
        const auto chain = detail::mipLevels(source);
        if (chain.size() <= static_cast<size_t>(levels))
            return {};

        const auto& first = chain[levels];
        const auto& last = chain.back();
        const size_t retained = last.offset + last.size - first.offset;
        if (first.offset + retained > source.dataSize())
            return {};

        const auto& props = source.properties;
        vsg::Data::Properties properties = props;
        properties.mipLevels = static_cast<uint8_t>(chain.size() - levels);

        vsg::ref_ptr<vsg::MipmapLayout> layout;
        if (source.getMipmapLayout())
        {
            layout = vsg::MipmapLayout::create(chain.size() - levels);
            for (size_t i = 0; i < layout->size(); ++i)
            {
                const auto& level = chain[levels + i];
                layout->set(
                    i, vsg::uivec4(level.width, level.height, 1u, static_cast<uint32_t>(level.offset - first.offset)));
            }
        }

        const auto* bytes = static_cast<const uint8_t*>(source.dataPointer()) + first.offset;
        return buildData(source.valueSize(), props.blockWidth > 1 || props.blockHeight > 1,
            ceilDiv(first.width, props.blockWidth), ceilDiv(first.height, props.blockHeight), bytes, retained,
            properties, layout);
    }

    vsg::ref_ptr<vsg::Data> dropArrayLevels(const vsg::Data& source, int levels)
    {
        if (levels <= 0)
            return vsg::ref_ptr<vsg::Data>(const_cast<vsg::Data*>(&source));
        if (source.dimensions() != 3 || source.properties.imageViewType != VK_IMAGE_VIEW_TYPE_2D_ARRAY
            || !detail::isBlockCompressed(source))
            return {};
        const vsg::MipmapLayout* layout = source.getMipmapLayout();
        if (!layout || layout->size() <= static_cast<size_t>(levels))
            return {};
        const vsg::uivec4 first = layout->at(static_cast<size_t>(levels));
        const size_t tailOffset = first.w;
        if (tailOffset >= source.dataSize())
            return {};
        const size_t tailSize = source.dataSize() - tailOffset;

        const size_t kept = layout->size() - static_cast<size_t>(levels);
        auto newLayout = vsg::MipmapLayout::create(kept);
        for (size_t i = 0; i < kept; ++i)
        {
            const vsg::uivec4 level = layout->at(static_cast<size_t>(levels) + i);
            if (level.w < tailOffset)
                return {};
            newLayout->set(i, vsg::uivec4(level.x, level.y, level.z, static_cast<uint32_t>(level.w - tailOffset)));
        }

        vsg::Data::Properties properties = source.properties;
        properties.mipLevels = static_cast<uint8_t>(kept);
        properties.allocatorType = vsg::ALLOCATOR_TYPE_VSG_ALLOCATOR;
        const uint32_t widthBlocks = ceilDiv(first.x, std::max<uint32_t>(1, properties.blockWidth));
        const uint32_t heightBlocks = ceilDiv(first.y, std::max<uint32_t>(1, properties.blockHeight));
        vsg::ref_ptr<vsg::Data> result;
        if (source.valueSize() == 8)
            result = detail::createArray<vsg::block64Array3D>(
                widthBlocks, heightBlocks, source.depth(), tailSize, properties, newLayout);
        else if (source.valueSize() == 16)
            result = detail::createArray<vsg::block128Array3D>(
                widthBlocks, heightBlocks, source.depth(), tailSize, properties, newLayout);
        if (!result)
            return {};
        std::memcpy(result->dataPointer(), static_cast<const uint8_t*>(source.dataPointer()) + tailOffset, tailSize);
        return result;
    }

    vsg::ref_ptr<vsg::Data> shrinkArray(const vsg::Data& source, int levels, Kind kind)
    {
        if (levels <= 0)
            return vsg::ref_ptr<vsg::Data>(const_cast<vsg::Data*>(&source));
        if (source.dimensions() != 3 || source.properties.imageViewType != VK_IMAGE_VIEW_TYPE_2D_ARRAY
            || source.getMipmapLayout() || detail::isBlockCompressed(source) || source.valueSize() != 4
            || shrinkableChannels(source.properties.format) != 4 || !source.dataPointer())
            return {};
        const uint32_t width = source.width();
        const uint32_t height = source.height();
        const uint32_t depth = source.depth();
        if (levels >= 31 || std::min(width, height) >> levels < 1)
            return {};
        uint32_t outWidth = width;
        uint32_t outHeight = height;
        for (int level = 0; level < levels; ++level)
        {
            outWidth = std::max(1u, outWidth / 2);
            outHeight = std::max(1u, outHeight / 2);
        }
        const bool normals = kind == Kind::Normal;
        vsg::Data::Properties properties = source.properties;
        properties.mipLevels = 0; // the GPU makes the chain
        properties.allocatorType = vsg::ALLOCATOR_TYPE_VSG_ALLOCATOR;
        const size_t layerBytes = size_t(outWidth) * outHeight * 4;
        auto result = detail::createArray<vsg::ubvec4Array3D>(
            outWidth, outHeight, depth, layerBytes * depth, properties, nullptr);
        auto* out = static_cast<uint8_t*>(result->dataPointer());
        const auto* in = static_cast<const uint8_t*>(source.dataPointer());
        const size_t sourceLayerBytes = size_t(width) * height * 4;
        std::vector<uint8_t> buffers[2];
        for (uint32_t layer = 0; layer < depth; ++layer)
        {
            const uint8_t* current = in + layer * sourceLayerBytes;
            uint32_t w = width;
            uint32_t h = height;
            for (int level = 0; level < levels; ++level)
            {
                const uint32_t nw = std::max(1u, w / 2);
                const uint32_t nh = std::max(1u, h / 2);
                uint8_t* target;
                if (level == levels - 1)
                    target = out + layer * layerBytes;
                else
                {
                    buffers[level % 2].resize(size_t(nw) * nh * 4);
                    target = buffers[level % 2].data();
                }
                halve(current, w, h, 4, target, nw, nh, normals);
                current = target;
                w = nw;
                h = nh;
            }
        }
        return result;
    }

    vsg::ref_ptr<vsg::Data> shrinkUncompressed(const vsg::Data& source, int levels, Kind kind, float alphaCutoff)
    {
        if (levels <= 0)
            return vsg::ref_ptr<vsg::Data>(const_cast<vsg::Data*>(&source));
        const size_t channels = shrinkableChannels(source.properties.format);
        if (channels == 0 || channels != source.valueSize() || !isPlain2D(source) || source.properties.blockWidth != 1
            || source.properties.blockHeight != 1 || !source.dataPointer())
            return {};

        const uint32_t width = source.width();
        const uint32_t height = source.height();
        const bool normals = kind == Kind::Normal && channels >= 3;

        // One level at a time: the input of a halving is the source (first) or the previous result, so no more than
        // two levels are alive at once.
        const uint8_t* in = static_cast<const uint8_t*>(source.dataPointer());
        uint32_t w = width;
        uint32_t h = height;
        std::vector<uint8_t> buffers[2];
        for (int level = 0; level < levels; ++level)
        {
            const uint32_t nw = std::max(1u, w / 2);
            const uint32_t nh = std::max(1u, h / 2);
            auto& out = buffers[level % 2];
            out.resize(static_cast<size_t>(nw) * nh * channels);
            halve(in, w, h, channels, out.data(), nw, nh, normals);
            in = out.data();
            w = nw;
            h = nh;
        }
        auto& result = buffers[(levels - 1) % 2];

        if (channels == 4 && alphaCutoff > 0.f)
            preserveCoverage(static_cast<const uint8_t*>(source.dataPointer()), static_cast<size_t>(width) * height,
                result.data(), static_cast<size_t>(w) * h, channels, std::min(alphaCutoff, 1.f));

        vsg::Data::Properties properties = source.properties;
        properties.mipLevels = 0; // the GPU makes the chain
        return buildData(channels, false, w, h, result.data(), result.size(), properties, nullptr);
    }

    vsg::ref_ptr<vsg::Data> applyCapUncounted(
        vsg::ref_ptr<vsg::Data> source, int cap, Kind kind, float alphaCutoff, CapOutcome& outcome)
    {
        outcome = CapOutcome{};
        if (!source || cap <= 0)
            return source;
        const size_t before = source->dataSize();
        auto leftBig = [&]() {
            outcome = { CapOutcome::Result::LeftBig, before, before };
            return source;
        };
        if (!isPlain2D(*source))
        {
            if (std::max(
                    source->width() * source->properties.blockWidth, source->height() * source->properties.blockHeight)
                > static_cast<uint32_t>(cap))
                return leftBig();
            return source;
        }
        const auto chain = detail::mipLevels(*source);
        if (chain.empty())
            return source;
        const auto& top = chain.front();
        const int blockSize = std::max<int>(source->properties.blockWidth, source->properties.blockHeight);
        const TextureCap::Plan plan
            = TextureCap::planLevels(top.width, top.height, static_cast<int>(chain.size()), blockSize, cap);
        if (plan.drop == 0 && plan.shrink == 0)
        {
            // Nothing to do, or nothing allowed: a block compressed image already at its smallest block count.
            if (std::max(top.width, top.height) > static_cast<uint32_t>(cap))
                return leftBig();
            return source;
        }

        vsg::ref_ptr<vsg::Data> result = source;
        if (plan.drop > 0)
        {
            result = dropMipLevels(*source, plan.drop);
            if (!result)
                return leftBig();
        }
        bool shrunk = false;
        if (plan.shrink > 0 && blockSize == 1)
        {
            if (auto smaller = shrinkUncompressed(*result, plan.shrink, kind, alphaCutoff))
            {
                result = smaller;
                shrunk = true;
            }
        }
        if (shrunk)
            outcome = { CapOutcome::Result::Shrunk, before, result->dataSize() };
        else if (plan.drop > 0)
            outcome = { CapOutcome::Result::Dropped, before, result->dataSize() };
        else
            return leftBig();
        return result;
    }

    void record(const CapOutcome& outcome)
    {
        switch (outcome.result)
        {
            case CapOutcome::Result::Dropped:
                report().addDropped(outcome.before, outcome.after);
                break;
            case CapOutcome::Result::Shrunk:
                report().addShrunk(outcome.before, outcome.after);
                break;
            case CapOutcome::Result::LeftBig:
                report().addLeftBig(outcome.before);
                break;
            case CapOutcome::Result::None:
                break;
        }
    }

    vsg::ref_ptr<vsg::Data> applyCap(vsg::ref_ptr<vsg::Data> source, int cap, Kind kind, float alphaCutoff)
    {
        CapOutcome outcome;
        auto result = applyCapUncounted(std::move(source), cap, kind, alphaCutoff, outcome);
        record(outcome);
        return result;
    }

    void ReportAccumulator::addDropped(uint64_t before, uint64_t after)
    {
        std::scoped_lock lock(mMutex);
        ++mReport.dropped;
        mReport.bytesBefore += before;
        mReport.bytesAfter += after;
    }

    void ReportAccumulator::addShrunk(uint64_t before, uint64_t after)
    {
        std::scoped_lock lock(mMutex);
        ++mReport.shrunk;
        mReport.bytesBefore += before;
        mReport.bytesAfter += after;
    }

    void ReportAccumulator::addLeftBig(uint64_t bytes)
    {
        std::scoped_lock lock(mMutex);
        ++mReport.leftBig;
        mReport.bytesBefore += bytes;
        mReport.bytesAfter += bytes;
    }

    Report ReportAccumulator::snapshot() const
    {
        std::scoped_lock lock(mMutex);
        return mReport;
    }

    void ReportAccumulator::reset()
    {
        std::scoped_lock lock(mMutex);
        mReport = Report{};
    }

    ReportAccumulator& report()
    {
        static ReportAccumulator accumulator;
        return accumulator;
    }

    std::string describe(const Report& summary)
    {
        constexpr uint64_t mebibyte = 1024 * 1024;
        std::ostringstream out;
        out << "texture downscale: " << summary.dropped << " textures lost top mip levels, " << summary.shrunk
            << " were shrunk by averaging, " << summary.leftBig << " over the cap could not be made smaller; "
            << summary.bytesBefore / mebibyte << " MiB before, " << summary.bytesAfter / mebibyte << " MiB after";
        return out.str();
    }
}
