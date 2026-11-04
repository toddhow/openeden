#ifndef OPENEDEN_COMPONENTS_BODY_BODYINFLUENCES_H
#define OPENEDEN_COMPONENTS_BODY_BODYINFLUENCES_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage d): the two weight rules shared by the merged-body merger
// (bodymerge.cpp) and the skeleton mapping's weight remap (skeletonmapping.cpp), so that there is ONE implementation of
// "add duplicates, drop zeros, keep the four largest, renormalise" (task 0070 recommends exactly this for 0069).
//
// The rules, unchanged from the merger's:
//   normalisedWeights: sorts by bone index, adds the weights of equal bones, drops weights that are not above zero and
//     divides by the sum (in double) so the result sums to 1. An empty or all-zero input gives an empty result.
//   cutToFourWeights: the four largest weights (a tie goes to the LOWER bone index), renormalised to sum 1 in double,
//     largest first, rounded to float once. Unused entries are bone 0 with weight 0.
// Pure functions, no state, no allocation beyond the vectors they return.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Body
{
    /// A skin keeps at most this many influences per vertex (the engine's skin does the same).
    inline constexpr std::size_t maxSkinInfluences = 4;

    /// (bone index, weight) pairs.
    using WeightList = std::vector<std::pair<std::uint32_t, double>>;

    struct CutWeights
    {
        std::array<std::uint32_t, maxSkinInfluences> mBone{};
        std::array<float, maxSkinInfluences> mWeight{};
    };

    /// Adds duplicates, drops zeros and normalises to sum 1. Sorted by bone index.
    WeightList normalisedWeights(WeightList raw);

    /// The four largest (ties by lower bone index), renormalised to sum 1, largest first.
    CutWeights cutToFourWeights(const WeightList& weights);
}

#endif
