#ifndef OPENEDEN_COMPONENTS_BODY_MERGEDRUNTIME_H
#define OPENEDEN_COMPONENTS_BODY_MERGEDRUNTIME_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage d): the pure part of drawing a cached merged body in
// the game. The engine adapter (apps/openmw/mwrender/mergedbody.*) gathers the facts and builds the nodes; what is
// decided here has no engine type in it, so it is tested on its own.
//
// - decideMergedBody: whether an actor draws the merged body or the stock parts. Fail closed: any missing or doubtful
//   fact means the stock parts, with the reason that decided it (one log line per reason and body, by the caller).
// - mergedSlotsCovered: whether equipment or a reservation owns a slot the merged body would draw. In stage d an actor
//   that wears anything replacing a merged slot keeps the stock parts entirely (no mixing: stage e hides regions).
// - planMergedDraws: one draw per submesh with its own compact vertex list (arrays shared between draws drew later
//   draws wrong in the LOD clusters, mwrender/lodclusters.cpp).
// - invertAffine: the inverse bind matrix of a bone from its rest matrix.
//
// Threads: every function is pure and may run on any thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "bodymerge.hpp"

namespace Body
{
    enum class MergedBodyReason
    {
        Merged, // the merged body is drawn
        SettingOff,
        NotThirdPerson, // first person and head-only views keep their parts
        Werewolf,
        NoCache, // no cache folder, no `current`, a damaged pack or manifest
        NoEntry, // the manifest has no entry for this race and sex (or it failed in the bake)
        KeyNotComputed, // the key of the data now could not be computed (a part or the skeleton cannot be read)
        StaleKey, // the data changed since the bake (a load-order change, a replaced part): re-run the bake
        PayloadRefused, // the payload does not read (damaged, or written by another layout)
        BuildFailed, // the nodes could not be built (a part's look could not be matched, an exception)
        EquipmentCovers, // equipment or a reservation owns a merged slot: stock parts this time
    };

    /// The facts about one actor and its body. Every field defaults to the value that keeps the stock parts.
    struct MergedBodyFacts
    {
        bool mSettingOn = false;
        bool mThirdPerson = false;
        bool mWerewolf = false;
        bool mCacheLoaded = false;
        bool mEntryPresent = false;
        bool mKeyComputed = false;
        bool mKeyCurrent = false;
        bool mPayloadRead = false;
        bool mBuilt = false;
        bool mSlotsCovered = true;
    };

    /// The first fact that rules the merged body out, in the order of the struct; Merged when none does.
    MergedBodyReason decideMergedBody(const MergedBodyFacts& facts);

    /// A short English phrase for a log line ("the cache is stale: re-run openmw --bake-bodies").
    std::string_view describe(MergedBodyReason reason);

    /// True when any slot of `mergedSlots` has a priority of 1 or more in `priorities` (indexed by slot; a slot
    /// outside `priorities` counts as covered, so a bad slot number keeps the stock parts).
    bool mergedSlotsCovered(std::span<const int> mergedSlots, std::span<const int> priorities);

    /// The distinct slots of the body's regions, ascending.
    std::vector<int> mergedSlotsOf(const MergedBody& body);

    struct MergedDraw
    {
        std::uint32_t mMaterial = 0; // index into MergedBody::mMaterials
        int mSlot = 0;
        std::vector<std::uint32_t> mVertices; // the body's vertex for each draw vertex, in first-use order
        std::vector<std::uint32_t> mIndices; // three per triangle, into mVertices
        friend bool operator==(const MergedDraw&, const MergedDraw&) = default;
    };

    /// One draw per submesh, in submesh order. Empty when a submesh points outside the triangles or a triangle
    /// outside the vertices (the payload reader refuses such bodies; this is the second line).
    std::vector<MergedDraw> planMergedDraws(const MergedBody& body);

    /// A 3 x 4 matrix as components/bodynif's Matrix34: row-major [rotation and scale | translation], v' = M3 v + t.
    using Affine34 = std::array<double, 12>;

    /// The inverse of an affine matrix. False (and `out` untouched) when it is singular or not finite.
    bool invertAffine(const Affine34& in, Affine34& out);
}

#endif
