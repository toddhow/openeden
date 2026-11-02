#ifndef OPENEDEN_VSGUTIL_TEXTURECAP_H
#define OPENEDEN_VSGUTIL_TEXTURECAP_H

#include <cstdint>
#include <string>

#include <vsg/vk/vulkan.h>

/*
 * Task 0049: the decisions of the runtime texture downscaler, as plain functions on numbers. "How big may this texture
 * be, and how many mip levels must be dropped (or pixels shrunk) to get there?" Nothing here touches an image, a device
 * or a setting: the settings are mapped to `Caps` by the game (vsgutil may not depend on other components), and the
 * data operations are in textureresize.hpp.
 */
namespace vsgUtil::TextureCap
{
    // Who is asking. Only Terrain and Object are ever capped; None (a call that names no category), Ui and Effect never
    // are, whatever the settings say.
    enum class Category
    {
        None,
        Terrain,
        Object,
        Ui,
        Effect,
    };

    // What the texels mean. Dropping mip levels needs no kind (the authored levels are already right); a CPU shrink
    // does.
    enum class Kind
    {
        Colour, // averaged as stored
        Normal, // xyz in the first three channels, renormalised after averaging; alpha averaged plainly
        Data, // averaged as stored (roughness, height and other data maps)
    };

    // The settings, as numbers. A cap of 0 means "no cap".
    struct Caps
    {
        int global = 0; // [Rendering] texture max size
        int terrain = 0; // texture max size terrain (0: use the global one)
        int object = 0; // texture max size objects (0: use the global one)
        bool autoBudget = false; // texture memory budget = auto
        uint64_t deviceLocalMiB = 0; // device-local memory; 0 means unknown, so `auto` finds no cap
    };

    // A height map (`_p`, task 0045) is not capped below this: it is one channel and already small.
    constexpr int heightMapFloor = 256;

    // How many levels to take off a texture to bring its longest side to the cap.
    struct Plan
    {
        int drop = 0; // levels of the existing chain that are not uploaded
        int shrink = 0; // further halvings by averaging, for what the chain cannot give (a single-level image)
    };

    // 0 stays 0; otherwise clamped to 16..8192 and rounded down to a power of two. Negative values count as 0.
    int sanitiseCap(int value);

    // Level sizes follow max(1, size / 2) per level. The levels needed are the fewest that bring the longest side to
    // the cap, never leaving either side of the top level under one block (blockSize 4 for BC1 to BC7, 1 for an
    // uncompressed image; a side already under a block is kept as it is). `levelsInChain` counts the top level, so at
    // most levelsInChain - 1 can be dropped; the rest of the need is the `shrink` part.
    Plan planLevels(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap);
    int levelsToDrop(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap);
    int levelsToShrink(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap);

    // The `auto` budget's table (a starting guess, not measured; see the constants in texturecap.cpp). Terrain and
    // Object only; any other category, and unknown memory (0), give 0.
    int capFromBudget(uint64_t deviceLocalMiB, Category category);

    // The cap for a category: its own setting, else the global one, else the budget; `auto` never raises a cap the
    // user set explicitly (the smaller of the two wins). Settings are sanitised first.
    int effectiveCap(Category category, const Caps& caps);

    // Bytes of `levels` levels of a width by height image in `format`, level sizes as above, blocks rounded up. 0 for a
    // format the function does not know.
    uint64_t bytesOfChain(uint32_t width, uint32_t height, VkFormat format, int levels);

    // The sum of the device-local heaps, in MiB (as the device report's memory.deviceLocalMiB).
    uint64_t deviceLocalMiB(const VkPhysicalDeviceMemoryProperties& memory);

    // `texture memory budget`: "off" (or empty), "auto" (the device's own memory) or a number of MiB, which behaves as
    // auto on a card with that much device-local memory (to see what a smaller card would get). Anything else is
    // not valid and counts as off.
    struct BudgetSetting
    {
        bool autoBudget = false;
        uint64_t simulatedMiB = 0; // 0: ask the device
        bool valid = true;
    };
    BudgetSetting parseBudget(const std::string& text);

    // The process-wide settings, written once at start (RenderManager) before the loader threads read them.
    void setCaps(const Caps& caps);
    Caps getCaps();
}

#endif
