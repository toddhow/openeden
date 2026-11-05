#ifndef OPENEDEN_TERRAIN_CHUNKFADE_H
#define OPENEDEN_TERRAIN_CHUNKFADE_H

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

#include "bounds.hpp"

// OpenEden (task 0088, stage b). How the live terrain's chunks dither in and out as a pure state machine (no GPU, no
// clock): Terrain::Paging runs it on the main thread and the tests run it alone. The live terrain's view is rebuilt
// from scratch when the view point moves, so this class remembers the chunks across rebuilds, by their Bounds.
//
// The rule that keeps the ground whole (the same as the tiles' of task 0087):
//  - A chunk that appears fades IN over what was there (nothing, or the chunks it replaces, which stay drawn).
//  - A chunk that is replaced (split into finer chunks, or merged into a coarser one) stays at FULL until every chunk
//    that overlaps it is solid, and is then dropped in one frame (the replacement hides it completely).
//  - A chunk that nothing replaces (it left the range) fades OUT and is then dropped.
//  - A chunk whose square something else already draws (a LOD tile) is dropped at once.
//  - A chunk that appears where something else drew just before (a tile handing its square back) starts solid.
//  - A fade can turn around in the middle; it goes on from the value it has.

namespace Terrain
{
    class ChunkFader
    {
    public:
        /// Declares the chunks the view wants now. `instant`: nothing fades (a new view: a teleport, the first build;
        /// every old chunk is forgotten). `covered(bounds)`: a leaving chunk with this answer is dropped at once.
        /// `startsSolid(bounds)`: a new chunk with this answer is drawn in full from its first frame.
        void setDesired(std::span<const Bounds> desired, bool instant,
            const std::function<bool(const Bounds&)>& covered, const std::function<bool(const Bounds&)>& startsSolid);

        /// Moves every fade by `seconds` of a fade that takes `fadeSeconds` (above 0). Returns true when a chunk was
        /// dropped, so that the draws must be rebuilt.
        bool step(float seconds, float fadeSeconds);

        /// The share of the dither a wanted or leaving chunk keeps (1: fully drawn); 1 for a chunk it does not know.
        float fadeOf(const Bounds& bounds) const;

        /// The chunks that are still drawn although the view no longer wants them, in a fixed order.
        std::vector<Bounds> leaving() const;

        /// True when any chunk the fader holds (wanted or leaving) shares area with one of `bounds`: false for a view
        /// with nothing in common with the last one (a teleport), which then starts solid.
        bool sharesAreaWith(std::span<const Bounds> bounds) const;

        /// True when any chunk is not at 1 (the draws need updating each frame) or is leaving.
        bool active() const;

        void clear() { mEntries.clear(); }
        std::size_t size() const { return mEntries.size(); }

    private:
        struct Entry
        {
            float mFade = 1.f;
            bool mWanted = false;
        };
        std::map<Bounds, Entry> mEntries;
    };

    /// True when the two squares share area (touching edges do not count).
    bool boundsOverlap(const Bounds& a, const Bounds& b);

    /// A chunk's draw packs lod (byte 0), shift (byte 1) and its grid position (bytes 2 and 3) into firstInstance (see
    /// Builder::createDraw). Lod and shift never pass 15, so the high nibble of bytes 0 and 1 holds the share of the
    /// dither that is DROPPED (255 minus the fade in 255ths): a solid chunk keeps the old value exactly, and so does
    /// every other user of the terrain pipeline. The vertex stage (terrain/main.vert) decodes it. An instance whose
    /// nibbles are not free is returned unchanged (the chunk then draws solid).
    inline std::uint32_t encodeChunkFade(std::uint32_t instance, float fade)
    {
        if ((instance & 0xf0f0u) != 0)
            return instance;
        const float clamped = fade < 0.f ? 0.f : (fade > 1.f ? 1.f : fade);
        const std::uint32_t dropped = 255u - static_cast<std::uint32_t>(clamped * 255.f + 0.5f);
        return instance | ((dropped & 0xfu) << 4) | (((dropped >> 4) & 0xfu) << 12);
    }

    /// What the vertex stage reads back: 1 for a solid chunk.
    inline float decodeChunkFade(std::uint32_t instance)
    {
        const std::uint32_t dropped = ((instance >> 4) & 0xfu) | (((instance >> 12) & 0xfu) << 4);
        return 1.f - static_cast<float>(dropped) / 255.f;
    }
}

#endif
