#ifndef OPENEDEN_COMPONENTS_BODY_MERGEDRUNTIME_H
#define OPENEDEN_COMPONENTS_BODY_MERGEDRUNTIME_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage d): the pure part of drawing a cached merged body in
// the game. The engine adapter (apps/openmw/mwrender/mergedbody.*) gathers the facts and builds the nodes; what is
// decided here has no engine type in it, so it is tested on its own.
//
// - decideMergedBody: whether an actor draws the merged body or the stock parts. Fail closed: any missing or doubtful
//   fact means the stock parts, with the reason that decided it (one log line per reason and body, by the caller).
// - mergedSlotsCovered: whether equipment or a reservation owns a slot the merged body would draw (stage d, when such
//   an actor kept the stock parts entirely; the engine no longer asks it).
// - hiddenMergedSlots and visibleIndexRanges (stage e): the regions equipment covers, and the index ranges of a draw
//   that remain without them, so a dressed actor keeps the merged body with the covered regions left out.
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
        OtherSkeleton, // the actor's skeleton is not its race's default one, which the bake placed the parts on
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
        bool mDefaultSkeleton = false;
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

    /// The indices of one slot inside a draw (stage e): a draw's indices are its submeshes' triangles in submesh order,
    /// so the triangles of one slot are contiguous and a region can be left out of the draw by its index range.
    struct SlotRun
    {
        int mSlot = 0;
        std::uint32_t mFirstIndex = 0;
        std::uint32_t mIndexCount = 0;
        friend bool operator==(const SlotRun&, const SlotRun&) = default;
    };

    struct MergedDraw
    {
        std::uint32_t mMaterial = 0; // index into MergedBody::mMaterials
        int mSlot = 0;
        std::vector<std::uint32_t> mVertices; // the body's vertex for each draw vertex, in first-use order
        std::vector<std::uint32_t> mIndices; // three per triangle, into mVertices
        std::vector<SlotRun> mRuns; // the slots of mIndices in order, together covering all of it
        friend bool operator==(const MergedDraw&, const MergedDraw&) = default;
    };

    /// Task 0069 stage e: the merged slots whose regions equipment or a reservation covers, ascending. The stock
    /// parts' rule (`Npc::updateParts`): a slot's body part is drawn when its priority is below 1 or the slot holds the
    /// body part itself (group -1); equipment and reservations take a slot with an inventory group (>= 0) and a
    /// priority of 1 or more, and then the body part of that slot is not drawn. `partslots` and `priorities` are
    /// indexed by part slot (ESM::PartReferenceType), as `MWRender::PartSlots::groups` and `priorities`. A merged slot
    /// outside them counts as covered (its region is hidden: a bad slot number never shows skin under a garment).
    std::vector<int> hiddenMergedSlots(
        std::span<const int> mergedSlots, std::span<const int> partslots, std::span<const int> priorities);

    struct IndexRange
    {
        std::uint32_t mFirstIndex = 0;
        std::uint32_t mIndexCount = 0;
        friend bool operator==(const IndexRange&, const IndexRange&) = default;
    };

    /// The index ranges of a draw that remain when the regions of `hiddenSlots` (ascending) are left out: the runs of
    /// the other slots, in order, with runs that touch merged into one range and empty runs dropped. Empty when nothing
    /// remains.
    std::vector<IndexRange> visibleIndexRanges(std::span<const SlotRun> runs, std::span<const int> hiddenSlots);

    /// Task 0069 stage e, second review (finding 1): the draws of an actor's copy that are drawn again after a region
    /// change, ascending: hidden before (`drawnBefore[i]` false) and drawn now (`drawnNow[i]` true). Their skins were
    /// left out while hidden, so their previous-frame matrices are old. Only indices both lists have count: a draw past
    /// the end of `drawnBefore` (the first application to a new copy, whose skins have never run) is not drawn again.
    std::vector<std::size_t> drawsDrawnAgain(const std::vector<bool>& drawnBefore, const std::vector<bool>& drawnNow);

    /// The links whose target is the bone matrices of one of `draws` (indices into `drawTargets`), ascending indices
    /// into `linkTargets`. Matched by identity, never by position: the copy's links come in the order its controllers
    /// were linked, the draws in the order of the copy's children. A null target matches nothing on either side, and a
    /// draw index past `drawTargets` is ignored. The engine restarts the previous-frame matrices of these skins.
    std::vector<std::size_t> linksOfDraws(std::span<const std::size_t> draws, std::span<const void* const> drawTargets,
        std::span<const void* const> linkTargets);

    /// One draw per submesh, in submesh order. Empty when a submesh points outside the triangles or a triangle
    /// outside the vertices (the payload reader refuses such bodies; this is the second line).
    std::vector<MergedDraw> planMergedDraws(const MergedBody& body);

    /// One draw per material key (decision H19, review finding 4): the submeshes of a material, in submesh order,
    /// become one draw whose look is taken from the slot of its first submesh. Materials in first-use order; a vertex
    /// shared by two submeshes of the material is listed once. Empty on the same bad input as planMergedDraws.
    std::vector<MergedDraw> planMergedDrawsByMaterial(const MergedBody& body);

    /// A 3 x 4 matrix as components/bodynif's Matrix34: row-major [rotation and scale | translation], v' = M3 v + t.
    using Affine34 = std::array<double, 12>;

    /// The inverse of an affine matrix. False (and `out` untouched) when it is singular or not finite.
    bool invertAffine(const Affine34& in, Affine34& out);
}

#endif
