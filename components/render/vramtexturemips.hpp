#ifndef OPENEDEN_RENDER_VRAMTEXTUREMIPS_H
#define OPENEDEN_RENDER_VRAMTEXTUREMIPS_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Render::VideoMemory::TextureMips
{
    /*
     * Which textures give up their top mip levels when video memory runs short (task 0080, stage d, first slice).
     * Pure: no Vulkan, no VSG, no clock; the caller passes the frame number. The ladder level L of the `texture mips`
     * adapter means "every eligible texture is at `min(L, maxDrop)` dropped top levels", so a step down drops one more
     * level from each texture that still may, and a step up gives it back.
     *
     * A texture takes part only when its long side is at least `minLongSide` (small textures return nothing to the
     * driver: the d0 spike), and its top is never dropped below `floorLongSide` on the long side.
     */

    struct Params
    {
        std::uint32_t minLongSide = 2048;
        std::uint32_t floorLongSide = 1024;
        /// A texture that was changed is left alone for this many frames (hysteresis against flicker).
        std::uint64_t minFramesBetweenChanges = 300;
        /// The most textures one plan changes (the caller swaps them in slices).
        int maxChanges = 4;
    };

    struct Texture
    {
        std::uint64_t id = 0;
        std::uint32_t width = 0; // of the full image (level 0)
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 1; // in the full chain
        std::uint32_t blockWidth = 1; // texels per block (4 for BC formats)
        std::uint32_t blockHeight = 1;
        std::uint32_t blockBytes = 4; // bytes per block (per texel when uncompressed)
        int drop = 0; // top levels dropped now
        std::uint64_t lastUsedFrame = 0; // the last frame something drawn with it was visible
        bool visible = false; // visible in the last frame
        bool pinned = false; // never changed
        bool changedBefore = false;
        std::uint64_t lastChangeFrame = 0; // meaningful when changedBefore
    };

    struct Change
    {
        std::uint64_t id = 0;
        int fromDrop = 0;
        int toDrop = 0;
    };

    /// Bytes of mip level `level` (0 is the top); 0 for a level outside the chain.
    inline std::int64_t levelBytes(const Texture& t, int level)
    {
        if (level < 0 || level >= static_cast<int>(t.mipLevels) || t.blockWidth == 0 || t.blockHeight == 0)
            return 0;
        const std::uint64_t w = std::max<std::uint32_t>(1, t.width >> level);
        const std::uint64_t h = std::max<std::uint32_t>(1, t.height >> level);
        const std::uint64_t bx = (w + t.blockWidth - 1) / t.blockWidth;
        const std::uint64_t by = (h + t.blockHeight - 1) / t.blockHeight;
        return static_cast<std::int64_t>(bx * by * t.blockBytes);
    }

    /// Bytes of the chain from level `first` to the end.
    inline std::int64_t chainBytes(const Texture& t, int first)
    {
        std::int64_t sum = 0;
        for (int level = std::max(first, 0); level < static_cast<int>(t.mipLevels); ++level)
            sum += levelBytes(t, level);
        return sum;
    }

    inline std::uint32_t longSide(const Texture& t)
    {
        return std::max(t.width, t.height);
    }

    inline bool eligible(const Texture& t, const Params& p)
    {
        return t.mipLevels > 1 && longSide(t) >= p.minLongSide;
    }

    /// The most top levels this texture may lose: the long side stays at or above the floor and the last level stays.
    inline int maxDrop(const Texture& t, const Params& p)
    {
        if (!eligible(t, p))
            return 0;
        int drop = 0;
        while (drop + 1 < static_cast<int>(t.mipLevels) && (longSide(t) >> (drop + 1)) >= p.floorLongSide)
            ++drop;
        return drop;
    }

    /// Where the ladder `level` puts this texture.
    inline int targetDrop(const Texture& t, int level, const Params& p)
    {
        return std::max(0, std::min(level, maxDrop(t, p)));
    }

    /// What going from `fromDrop` to `toDrop` frees (negative: costs).
    inline std::int64_t savedBytes(const Texture& t, int fromDrop, int toDrop)
    {
        return chainBytes(t, fromDrop) - chainBytes(t, toDrop);
    }

    inline std::int64_t residentBytes(const Texture& t)
    {
        return chainBytes(t, t.drop);
    }

    /// What one step of the ladder frees (`down`: from `level` to `level + 1`) or costs (`!down`: from `level` back to
    /// `level - 1`), over the textures given.
    inline std::int64_t stepBytes(const std::vector<Texture>& textures, int level, bool down, const Params& p)
    {
        if (!down && level <= 0)
            return 0;
        const int lower = down ? level : level - 1;
        std::int64_t sum = 0;
        for (const Texture& t : textures)
            if (eligible(t, p))
                sum += savedBytes(t, targetDrop(t, lower, p), targetDrop(t, lower + 1, p));
        return sum;
    }

    /// The textures to change now to bring them to the ladder `level`, at most `p.maxChanges`, in the order to swap
    /// them: reductions first (textures not in use, then the ones used longest ago, then the bigger saving), then
    /// restores (visible first, then the most recently used). Ties by id, so the result does not depend on the order
    /// of `textures`.
    inline std::vector<Change> plan(const std::vector<Texture>& textures, int level, std::uint64_t frame, const Params& p)
    {
        struct Candidate
        {
            const Texture* texture;
            int target;
            bool reduce;
            std::int64_t amount;
        };
        std::vector<Candidate> candidates;
        for (const Texture& t : textures)
        {
            if (t.pinned || !eligible(t, p))
                continue;
            const int target = targetDrop(t, level, p);
            if (target == t.drop)
                continue;
            if (t.changedBefore && (frame < t.lastChangeFrame || frame - t.lastChangeFrame < p.minFramesBetweenChanges))
                continue;
            const std::int64_t amount = savedBytes(t, t.drop, target);
            candidates.push_back({ &t, target, target > t.drop, amount < 0 ? -amount : amount });
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.reduce != b.reduce)
                return a.reduce;
            const Texture& x = *a.texture;
            const Texture& y = *b.texture;
            if (a.reduce)
            {
                if (x.visible != y.visible)
                    return !x.visible;
                if (x.lastUsedFrame != y.lastUsedFrame)
                    return x.lastUsedFrame < y.lastUsedFrame;
            }
            else
            {
                if (x.visible != y.visible)
                    return x.visible;
                if (x.lastUsedFrame != y.lastUsedFrame)
                    return x.lastUsedFrame > y.lastUsedFrame;
            }
            if (a.amount != b.amount)
                return a.amount > b.amount;
            return x.id < y.id;
        });
        std::vector<Change> changes;
        for (const Candidate& c : candidates)
        {
            if (static_cast<int>(changes.size()) >= p.maxChanges)
                break;
            changes.push_back({ c.texture->id, c.texture->drop, c.target });
        }
        return changes;
    }
}

#endif
