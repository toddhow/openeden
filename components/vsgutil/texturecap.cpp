#include "texturecap.hpp"

#include <algorithm>
#include <mutex>

#include <vsg/state/ImageInfo.h>

namespace vsgUtil::TextureCap
{
    namespace
    {
        constexpr int minimumCap = 16;
        constexpr int maximumCap = 8192;

        // The `auto` budget's table (task 0049, decision D8). A starting guess from the spec, not measured: stage d
        // measures real use and corrects it. A band starts at its lower bound (3 GiB is in the 3 to 6 band). A cap of
        // 0 means no cap.
        struct Band
        {
            uint64_t fromMiB; // device-local memory at or above this
            int objectCap;
            int terrainCap;
        };
        constexpr Band budgetBands[] = {
            { 0, 512, 256 }, // under 3 GiB
            { 3 * 1024, 1024, 512 }, // 3 to 6 GiB
            { 6 * 1024, 2048, 1024 }, // 6 to 10 GiB
            { 10 * 1024, 0, 2048 }, // over 10 GiB
        };

        std::mutex capsMutex;
        Caps currentCaps;

        int halved(int side, int levels)
        {
            // max(1, size / 2) per level, as detail::mipLevels does.
            for (int i = 0; i < levels && side > 1; ++i)
                side /= 2;
            return side;
        }
    }

    int sanitiseCap(int value)
    {
        if (value <= 0)
            return 0;
        value = std::clamp(value, minimumCap, maximumCap);
        int result = minimumCap;
        while (result * 2 <= value)
            result *= 2;
        return result;
    }

    Plan planLevels(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap)
    {
        Plan plan;
        if (cap <= 0 || width == 0 || height == 0)
            return plan;
        const int w = static_cast<int>(std::min<uint32_t>(width, 1u << 20));
        const int h = static_cast<int>(std::min<uint32_t>(height, 1u << 20));
        const int block = std::max(blockSize, 1);

        // The fewest halvings that bring the longest side to the cap...
        int needed = 0;
        while (std::max(halved(w, needed), halved(h, needed)) > cap && (halved(w, needed) > 1 || halved(h, needed) > 1))
            ++needed;

        // ...but no side may fall under one block (a side that started smaller is kept as it is).
        int allowed = 0;
        while (allowed < needed)
        {
            const int nextW = halved(w, allowed + 1);
            const int nextH = halved(h, allowed + 1);
            if (nextW < std::min(w, block) || nextH < std::min(h, block))
                break;
            ++allowed;
        }
        const int total = std::min(needed, allowed);
        plan.drop = std::min(total, std::max(levelsInChain - 1, 0));
        plan.shrink = total - plan.drop;
        return plan;
    }

    int levelsToDrop(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap)
    {
        return planLevels(width, height, levelsInChain, blockSize, cap).drop;
    }

    int levelsToShrink(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap)
    {
        return planLevels(width, height, levelsInChain, blockSize, cap).shrink;
    }

    int capFromBudget(uint64_t deviceLocalMiB, Category category)
    {
        if (deviceLocalMiB == 0 || (category != Category::Terrain && category != Category::Object))
            return 0;
        const Band* band = &budgetBands[0];
        for (const Band& candidate : budgetBands)
            if (deviceLocalMiB >= candidate.fromMiB)
                band = &candidate;
        return category == Category::Terrain ? band->terrainCap : band->objectCap;
    }

    int effectiveCap(Category category, const Caps& caps)
    {
        if (category != Category::Terrain && category != Category::Object)
            return 0;
        const int own = category == Category::Terrain ? caps.terrain : caps.object;
        const int explicitCap = sanitiseCap(own > 0 ? own : caps.global);
        const int budgetCap = caps.autoBudget ? capFromBudget(caps.deviceLocalMiB, category) : 0;
        if (explicitCap > 0 && budgetCap > 0)
            return std::min(explicitCap, budgetCap);
        return explicitCap > 0 ? explicitCap : budgetCap;
    }

    uint64_t bytesOfChain(uint32_t width, uint32_t height, VkFormat format, int levels)
    {
        const vsg::FormatTraits traits = vsg::getFormatTraits(format, false);
        if (levels <= 0 || traits.size <= 0)
            return 0;
        const uint32_t blockWidth = static_cast<uint32_t>(std::max(traits.blockWidth, 1));
        const uint32_t blockHeight = static_cast<uint32_t>(std::max(traits.blockHeight, 1));
        uint64_t total = 0;
        for (int level = 0; level < levels; ++level)
        {
            const uint64_t blocksX = (std::max(width, 1u) + blockWidth - 1) / blockWidth;
            const uint64_t blocksY = (std::max(height, 1u) + blockHeight - 1) / blockHeight;
            total += blocksX * blocksY * static_cast<uint64_t>(traits.size);
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        return total;
    }

    uint64_t deviceLocalMiB(const VkPhysicalDeviceMemoryProperties& memory)
    {
        uint64_t bytes = 0;
        const uint32_t count = std::min<uint32_t>(memory.memoryHeapCount, VK_MAX_MEMORY_HEAPS);
        for (uint32_t i = 0; i < count; ++i)
            if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                bytes += memory.memoryHeaps[i].size;
        return bytes / (1024 * 1024);
    }

    void setCaps(const Caps& caps)
    {
        std::scoped_lock lock(capsMutex);
        currentCaps = caps;
    }

    Caps getCaps()
    {
        std::scoped_lock lock(capsMutex);
        return currentCaps;
    }
}
