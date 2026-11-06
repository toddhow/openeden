// Task 0069 stage e: the regions of a merged body that equipment covers, and the index ranges a draw keeps when they
// are left out (components/body/mergedruntime.*). Written before the implementation and run red against stubs.
// Synthetic data only.

#include <components/body/mergedruntime.hpp>
#include <components/esm3/loadarmo.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Body;

    constexpr int slotCount = ESM::PRT_Count;

    std::vector<int> allSlots()
    {
        std::vector<int> slots(slotCount);
        for (int i = 0; i < slotCount; ++i)
            slots[static_cast<std::size_t>(i)] = i;
        return slots;
    }

    TEST(MergedBodyRegions, a_robe_hides_what_its_reservations_cover_and_nothing_else)
    {
        // Npc::updateParts reserves these for a robe (Slot_Robe, group 0 in the inventory store, priority 24).
        const int robeGroup = 0;
        const int robePriority = 24;
        const std::array<int, 11> reserved{ ESM::PRT_Groin, ESM::PRT_Skirt, ESM::PRT_RLeg, ESM::PRT_LLeg,
            ESM::PRT_RUpperarm, ESM::PRT_LUpperarm, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm,
            ESM::PRT_LForearm, ESM::PRT_Cuirass };
        std::vector<int> partslots(slotCount, -1);
        std::vector<int> priorities(slotCount, 0);
        for (const int slot : reserved)
        {
            partslots[static_cast<std::size_t>(slot)] = robeGroup;
            priorities[static_cast<std::size_t>(slot)] = robePriority;
        }
        const std::vector<int> merged{ ESM::PRT_Neck, ESM::PRT_Cuirass, ESM::PRT_Groin, ESM::PRT_RHand, ESM::PRT_LHand,
            ESM::PRT_RWrist, ESM::PRT_LWrist, ESM::PRT_RForearm, ESM::PRT_LForearm, ESM::PRT_RUpperarm,
            ESM::PRT_LUpperarm, ESM::PRT_RFoot, ESM::PRT_LFoot, ESM::PRT_RAnkle, ESM::PRT_LAnkle, ESM::PRT_RKnee,
            ESM::PRT_LKnee, ESM::PRT_RLeg, ESM::PRT_LLeg };
        std::vector<int> sortedMerged = merged;
        std::sort(sortedMerged.begin(), sortedMerged.end());
        std::vector<int> expected;
        for (const int slot : sortedMerged)
        {
            if (std::find(reserved.begin(), reserved.end(), slot) != reserved.end())
                expected.push_back(slot);
        }
        EXPECT_EQ(hiddenMergedSlots(sortedMerged, partslots, priorities), expected);
    }

    TEST(MergedBodyRegions, nothing_worn_hides_nothing)
    {
        std::vector<int> partslots(slotCount, -1);
        std::vector<int> priorities(slotCount, 0);
        EXPECT_TRUE(hiddenMergedSlots(allSlots(), partslots, priorities).empty());
        // The body parts themselves (group -1, priority 1, left from an earlier update) hide nothing either.
        std::fill(priorities.begin(), priorities.end(), 1);
        EXPECT_TRUE(hiddenMergedSlots(allSlots(), partslots, priorities).empty());
    }

    TEST(MergedBodyRegions, a_slot_outside_the_arrays_counts_as_covered)
    {
        const std::vector<int> partslots(4, -1);
        const std::vector<int> priorities(4, 0);
        const std::vector<int> merged{ 1, 3, 4, 40 };
        EXPECT_EQ(hiddenMergedSlots(merged, partslots, priorities), (std::vector<int>{ 4, 40 }));
        const std::vector<int> negative{ -2, 1 };
        EXPECT_EQ(hiddenMergedSlots(negative, partslots, priorities), (std::vector<int>{ -2 }));
    }

    TEST(MergedBodyRegions, hidden_slots_are_ascending_whatever_the_order_of_the_merged_slots)
    {
        std::vector<int> partslots(slotCount, 3);
        std::vector<int> priorities(slotCount, 5);
        const std::vector<int> merged{ 9, 2, 14 };
        EXPECT_EQ(hiddenMergedSlots(merged, partslots, priorities), (std::vector<int>{ 2, 9, 14 }));
    }

    std::vector<SlotRun> threeRuns()
    {
        // Slot 2: indices 0 to 5, slot 5: 6 to 14, slot 7: 15 to 17.
        return { { 2, 0, 6 }, { 5, 6, 9 }, { 7, 15, 3 } };
    }

    TEST(MergedBodyRegions, nothing_hidden_keeps_the_whole_draw_as_one_range)
    {
        EXPECT_EQ(visibleIndexRanges(threeRuns(), {}), (std::vector<IndexRange>{ { 0, 18 } }));
    }

    TEST(MergedBodyRegions, a_hidden_middle_slot_splits_the_draw)
    {
        const std::vector<int> hidden{ 5 };
        EXPECT_EQ(visibleIndexRanges(threeRuns(), hidden), (std::vector<IndexRange>{ { 0, 6 }, { 15, 3 } }));
    }

    TEST(MergedBodyRegions, hidden_ends_leave_the_middle)
    {
        const std::vector<int> hidden{ 2, 7 };
        EXPECT_EQ(visibleIndexRanges(threeRuns(), hidden), (std::vector<IndexRange>{ { 6, 9 } }));
    }

    TEST(MergedBodyRegions, everything_hidden_leaves_no_range)
    {
        const std::vector<int> hidden{ 2, 5, 7 };
        EXPECT_TRUE(visibleIndexRanges(threeRuns(), hidden).empty());
    }

    TEST(MergedBodyRegions, a_hidden_slot_the_draw_does_not_have_changes_nothing)
    {
        const std::vector<int> hidden{ 0, 3, 99 };
        EXPECT_EQ(visibleIndexRanges(threeRuns(), hidden), (std::vector<IndexRange>{ { 0, 18 } }));
    }

    TEST(MergedBodyRegions, empty_runs_are_dropped_and_do_not_split)
    {
        const std::vector<SlotRun> runs{ { 1, 0, 3 }, { 2, 3, 0 }, { 3, 3, 6 } };
        EXPECT_EQ(visibleIndexRanges(runs, {}), (std::vector<IndexRange>{ { 0, 9 } }));
        const std::vector<int> hidden{ 2 };
        EXPECT_EQ(visibleIndexRanges(runs, hidden), (std::vector<IndexRange>{ { 0, 9 } }));
    }

    MergedBody twoMaterialBody()
    {
        // Six triangles over four slots and two materials; vertices are not shared, so every index is new.
        MergedBody body;
        body.mMaterials = { "a", "b" };
        for (int i = 0; i < 18; ++i)
            body.mPositions.push_back({ static_cast<float>(i), 0.f, 0.f });
        for (std::uint32_t i = 0; i < 18; ++i)
            body.mTriangles.push_back(i);
        // Submeshes ordered by (slot, material): slot 1 material 0 (1 triangle), slot 1 material 1 (1), slot 3
        // material 0 (2), slot 4 material 1 (1), slot 6 material 0 (1).
        body.mSubmeshes = { { 0, 1, 0, 1 }, { 1, 1, 1, 1 }, { 0, 3, 2, 2 }, { 1, 4, 4, 1 }, { 0, 6, 5, 1 } };
        body.mRegions = { { 1, 0, 2 }, { 3, 2, 2 }, { 4, 4, 1 }, { 6, 5, 1 } };
        return body;
    }

    TEST(MergedBodyRegions, draws_by_material_record_their_slot_runs)
    {
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(twoMaterialBody());
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(draws[0].mMaterial, 0u);
        EXPECT_EQ(draws[0].mRuns, (std::vector<SlotRun>{ { 1, 0, 3 }, { 3, 3, 6 }, { 6, 9, 3 } }));
        EXPECT_EQ(draws[1].mMaterial, 1u);
        EXPECT_EQ(draws[1].mRuns, (std::vector<SlotRun>{ { 1, 0, 3 }, { 4, 3, 3 } }));
        for (const MergedDraw& draw : draws)
        {
            std::uint32_t covered = 0;
            for (const SlotRun& run : draw.mRuns)
            {
                EXPECT_EQ(run.mFirstIndex, covered);
                covered += run.mIndexCount;
            }
            EXPECT_EQ(covered, draw.mIndices.size());
        }
    }

    TEST(MergedBodyRegions, draws_per_submesh_record_one_run_each)
    {
        const std::vector<MergedDraw> draws = planMergedDraws(twoMaterialBody());
        ASSERT_EQ(draws.size(), 5u);
        for (const MergedDraw& draw : draws)
        {
            ASSERT_EQ(draw.mRuns.size(), 1u);
            EXPECT_EQ(draw.mRuns[0].mSlot, draw.mSlot);
            EXPECT_EQ(draw.mRuns[0].mFirstIndex, 0u);
            EXPECT_EQ(draw.mRuns[0].mIndexCount, draw.mIndices.size());
        }
    }

    TEST(MergedBodyRegions, hiding_by_slot_through_a_draw_leaves_the_other_slots_triangles)
    {
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(twoMaterialBody());
        ASSERT_EQ(draws.size(), 2u);
        const std::vector<int> hidden{ 3 };
        EXPECT_EQ(visibleIndexRanges(draws[0].mRuns, hidden), (std::vector<IndexRange>{ { 0, 3 }, { 9, 3 } }));
        EXPECT_EQ(visibleIndexRanges(draws[1].mRuns, hidden), (std::vector<IndexRange>{ { 0, 6 } }));
    }

    // Task 0069 stage e, second review (finding 1): which skins of an actor's copy restart their previous-frame
    // matrices after a region change. Written before drawsDrawnAgain and linksOfDraws, run red against stubs.

    using Indices = std::vector<std::size_t>;

    TEST(MergedBodyRestart, a_draw_hidden_before_and_drawn_now_is_drawn_again)
    {
        EXPECT_EQ(drawsDrawnAgain({ false, true, false, true }, { true, true, false, false }), (Indices{ 0 }));
        EXPECT_EQ(drawsDrawnAgain({ false, false, false }, { true, false, true }), (Indices{ 0, 2 }));
    }

    TEST(MergedBodyRestart, drawn_to_drawn_hidden_to_hidden_and_drawn_to_hidden_are_not)
    {
        EXPECT_TRUE(drawsDrawnAgain({ true, false, true }, { true, false, false }).empty());
        EXPECT_TRUE(drawsDrawnAgain({ true, true }, { true, true }).empty());
    }

    TEST(MergedBodyRestart, the_first_application_to_a_new_copy_draws_nothing_again)
    {
        // A new copy: nothing was applied before (its skins have never run, so they start without motion anyway).
        EXPECT_TRUE(drawsDrawnAgain({}, { true, true, true }).empty());
        EXPECT_TRUE(drawsDrawnAgain({}, {}).empty());
    }

    TEST(MergedBodyRestart, only_indices_both_lists_have_count)
    {
        EXPECT_EQ(drawsDrawnAgain({ false, false }, { true, true, true, true }), (Indices{ 0, 1 }));
        EXPECT_EQ(drawsDrawnAgain({ false, false, false, false }, { false, true }), (Indices{ 1 }));
    }

    TEST(MergedBodyRestart, links_are_matched_to_draws_by_identity_not_position)
    {
        int matrices[3];
        int other;
        const std::vector<const void*> draws{ &matrices[0], &matrices[1], &matrices[2] };
        // The copy's links: a controller that is not a skin (null), then the skins of draws 2, 0 and 1.
        const std::vector<const void*> links{ nullptr, &matrices[2], &other, &matrices[0], &matrices[1] };
        EXPECT_EQ(linksOfDraws(Indices{ 0 }, draws, links), (Indices{ 3 }));
        EXPECT_EQ(linksOfDraws(Indices{ 2 }, draws, links), (Indices{ 1 }));
        EXPECT_EQ(linksOfDraws(Indices{ 0, 1, 2 }, draws, links), (Indices{ 1, 3, 4 }));
        EXPECT_TRUE(linksOfDraws(Indices{}, draws, links).empty());
    }

    TEST(MergedBodyRestart, a_null_target_matches_nothing_and_a_bad_draw_index_is_ignored)
    {
        int matrices;
        // Draw 0 has no bone matrices: the null link must not match it.
        const std::vector<const void*> draws{ nullptr, &matrices };
        const std::vector<const void*> links{ nullptr, &matrices };
        EXPECT_TRUE(linksOfDraws(Indices{ 0 }, draws, links).empty());
        EXPECT_EQ(linksOfDraws(Indices{ 1, 7 }, draws, links), (Indices{ 1 }));
    }

    TEST(MergedBodyRestart, every_link_of_a_draw_drawn_again_is_named_once)
    {
        int matrices[2];
        const std::vector<const void*> draws{ &matrices[0], &matrices[1] };
        // Two links on draw 1's matrices (not made by the engine, but both must restart), and a draw listed twice.
        const std::vector<const void*> links{ &matrices[1], &matrices[0], &matrices[1] };
        EXPECT_EQ(linksOfDraws(Indices{ 1, 1 }, draws, links), (Indices{ 0, 2 }));
    }

    TEST(MergedBodyRestart, a_gauntlet_taken_off_restarts_the_hand_skin_only)
    {
        // Three draws as a copy has them (body, hands, feet); gauntlets hid the hands entirely, boots the feet partly.
        int body;
        int hands;
        int feet;
        const std::vector<const void*> draws{ &body, &hands, &feet };
        const std::vector<const void*> links{ &feet, &body, &hands };
        const Indices again = drawsDrawnAgain({ true, false, true }, { true, true, true });
        EXPECT_EQ(again, (Indices{ 1 }));
        EXPECT_EQ(linksOfDraws(again, draws, links), (Indices{ 2 }));
    }
}
