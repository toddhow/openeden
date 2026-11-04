// Task 0069 stage e: the regions of a merged body that equipment covers, and the index ranges a draw keeps when they
// are left out (components/body/mergedruntime.*). Written before the implementation and run red against stubs.
// Synthetic data only.

#include <components/body/mergedruntime.hpp>
#include <components/esm3/loadarmo.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
    using namespace Body;

    constexpr int slotCount = ESM::PRT_Count;

    // The stock rule, written from `Npc::updateParts` (apps/openmw/mwrender/npc.cpp): equipment and reservations are
    // added first, with an inventory group (>= 0) and a priority of 1 or more; then, for every slot, the race's body
    // part is added when `mPartPriorities[slot] < 1`, and a body part kept from an earlier update has group -1 and
    // priority 1. So a slot shows skin exactly when no equipment or reservation owns it.
    bool stockShowsSkin(int partslot, int priority)
    {
        if (priority < 1)
            return true; // nothing there: the body part is added
        return partslot < 0; // the body part itself (group -1, priority 1)
    }

    std::vector<int> referenceHidden(
        const std::vector<int>& merged, const std::vector<int>& partslots, const std::vector<int>& priorities)
    {
        std::vector<int> hidden;
        for (const int slot : merged)
        {
            if (!stockShowsSkin(partslots[static_cast<std::size_t>(slot)], priorities[static_cast<std::size_t>(slot)]))
                hidden.push_back(slot);
        }
        return hidden;
    }

    std::vector<int> allSlots()
    {
        std::vector<int> slots(slotCount);
        for (int i = 0; i < slotCount; ++i)
            slots[static_cast<std::size_t>(i)] = i;
        return slots;
    }

    TEST(MergedBodyRegions, every_state_of_one_slot_hides_as_the_stock_parts_do)
    {
        // Every state a slot can be in: empty, the body part, and equipment or a reservation of every inventory group
        // (0 to 18) at every priority the slot list produces (clothing and armour of base 0, 3 and 11: 2 to 25).
        std::vector<std::pair<int, int>> states{ { -1, 0 }, { -1, 1 } };
        for (int group = 0; group <= 18; ++group)
        {
            for (int priority = 1; priority <= 25; ++priority)
                states.emplace_back(group, priority);
        }
        const std::vector<int> merged = allSlots();
        for (int slot = 0; slot < slotCount; ++slot)
        {
            for (const auto& [group, priority] : states)
            {
                std::vector<int> partslots(slotCount, -1);
                std::vector<int> priorities(slotCount, 0);
                partslots[static_cast<std::size_t>(slot)] = group;
                priorities[static_cast<std::size_t>(slot)] = priority;
                EXPECT_EQ(
                    hiddenMergedSlots(merged, partslots, priorities), referenceHidden(merged, partslots, priorities))
                    << "slot " << slot << " group " << group << " priority " << priority;
            }
        }
    }

    TEST(MergedBodyRegions, random_combinations_of_all_slots_hide_as_the_stock_parts_do)
    {
        // The rule is per slot, so the single-slot table above covers it; this checks the slots do not interact, over
        // 20,000 seeded combinations of all 27 slots and of merged-slot subsets.
        std::mt19937 random(12345);
        std::uniform_int_distribution<int> groupOf(-1, 18);
        std::uniform_int_distribution<int> priorityOf(0, 25);
        std::bernoulli_distribution mergedOf(0.6);
        for (int round = 0; round < 20000; ++round)
        {
            std::vector<int> partslots(slotCount);
            std::vector<int> priorities(slotCount);
            std::vector<int> merged;
            for (int slot = 0; slot < slotCount; ++slot)
            {
                partslots[static_cast<std::size_t>(slot)] = groupOf(random);
                priorities[static_cast<std::size_t>(slot)] = priorityOf(random);
                if (mergedOf(random))
                    merged.push_back(slot);
            }
            ASSERT_EQ(hiddenMergedSlots(merged, partslots, priorities), referenceHidden(merged, partslots, priorities))
                << "round " << round;
        }
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
}
