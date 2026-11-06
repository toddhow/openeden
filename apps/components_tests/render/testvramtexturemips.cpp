// Tests for Render::VideoMemory::TextureMips (task 0080, stage d, first slice): which textures to drop top mip levels
// from, how many bytes that frees, and in which order. Written from the rules of the task spec before the code:
// only textures of 2048 texels or more on the long side take part, none goes below a floor of 1024, the ladder level
// is the number of top levels dropped, a texture changed recently is left alone (hysteresis), the ones not in use go
// first, and a restore brings the visible ones back first. Pure CPU.

#include <components/render/vramtexturemips.hpp>

#include <algorithm>
#include <gtest/gtest.h>

namespace
{
    using namespace Render::VideoMemory::TextureMips;

    // A BC1 texture (4x4 blocks of 8 bytes) with a full chain.
    Texture bc1(std::uint64_t id, std::uint32_t size)
    {
        Texture t;
        t.id = id;
        t.width = size;
        t.height = size;
        std::uint32_t levels = 1;
        for (std::uint32_t s = size; s > 1; s >>= 1)
            ++levels;
        t.mipLevels = levels;
        t.blockWidth = 4;
        t.blockHeight = 4;
        t.blockBytes = 8;
        return t;
    }

    TEST(VramTextureMips, level_bytes_of_a_bc1_chain)
    {
        const Texture t = bc1(1, 2048);
        EXPECT_EQ(levelBytes(t, 0), 2048 * 2048 / 2);
        EXPECT_EQ(levelBytes(t, 1), 1024 * 1024 / 2);
        EXPECT_EQ(levelBytes(t, 9), 8); // 4x4 texels is one block of 8 bytes
        EXPECT_EQ(levelBytes(t, 10), 8); // 2x2 and 1x1 still take one whole block
        EXPECT_EQ(levelBytes(t, 11), 8);
    }

    TEST(VramTextureMips, an_uncompressed_level_counts_its_texels)
    {
        Texture t;
        t.width = 64;
        t.height = 32;
        t.mipLevels = 7;
        t.blockBytes = 4;
        EXPECT_EQ(levelBytes(t, 0), 64 * 32 * 4);
        EXPECT_EQ(levelBytes(t, 5), 2 * 1 * 4);
    }

    TEST(VramTextureMips, a_level_past_the_chain_is_zero_bytes)
    {
        const Texture t = bc1(1, 256);
        EXPECT_EQ(levelBytes(t, 9), 0);
        EXPECT_EQ(levelBytes(t, -1), 0);
    }

    TEST(VramTextureMips, chain_bytes_sum_the_levels_from_the_first_kept)
    {
        const Texture t = bc1(1, 1024);
        std::int64_t all = 0;
        for (int i = 0; i < 11; ++i)
            all += levelBytes(t, i);
        EXPECT_EQ(chainBytes(t, 0), all);
        EXPECT_EQ(chainBytes(t, 1), all - levelBytes(t, 0));
        EXPECT_EQ(chainBytes(t, 11), 0);
    }

    TEST(VramTextureMips, only_textures_of_2048_and_up_are_eligible)
    {
        const Params p;
        EXPECT_FALSE(eligible(bc1(1, 1024), p));
        EXPECT_FALSE(eligible(bc1(1, 512), p));
        EXPECT_TRUE(eligible(bc1(1, 2048), p));
        EXPECT_TRUE(eligible(bc1(1, 4096), p));
    }

    TEST(VramTextureMips, the_long_side_decides)
    {
        const Params p;
        Texture t = bc1(1, 2048);
        t.height = 256;
        EXPECT_TRUE(eligible(t, p));
        t.width = 256;
        t.height = 2048;
        EXPECT_TRUE(eligible(t, p));
    }

    TEST(VramTextureMips, a_texture_with_one_level_is_not_eligible)
    {
        Texture t = bc1(1, 4096);
        t.mipLevels = 1;
        EXPECT_FALSE(eligible(t, Params{}));
    }

    TEST(VramTextureMips, a_pinned_texture_is_still_eligible_but_never_planned)
    {
        Texture t = bc1(1, 4096);
        t.pinned = true;
        EXPECT_TRUE(eligible(t, Params{}));
        EXPECT_TRUE(plan({ t }, 2, 1000, Params{}).empty());
    }

    TEST(VramTextureMips, no_texture_goes_below_the_floor)
    {
        const Params p; // floor 1024
        EXPECT_EQ(maxDrop(bc1(1, 2048), p), 1);
        EXPECT_EQ(maxDrop(bc1(1, 4096), p), 2);
        EXPECT_EQ(maxDrop(bc1(1, 8192), p), 3);
    }

    TEST(VramTextureMips, the_floor_follows_the_long_side)
    {
        Params p;
        p.floorLongSide = 2048;
        EXPECT_EQ(maxDrop(bc1(1, 4096), p), 1);
        Texture wide = bc1(1, 4096);
        wide.height = 512;
        EXPECT_EQ(maxDrop(wide, Params{}), 2); // 4096 x 512 -> 1024 x 128: the long side is what counts
    }

    TEST(VramTextureMips, the_drop_never_passes_the_chain)
    {
        Params p;
        p.floorLongSide = 0;
        EXPECT_EQ(maxDrop(bc1(1, 4096), p), 12); // 13 levels: the last one stays
    }

    TEST(VramTextureMips, a_short_chain_limits_the_drop)
    {
        Texture t = bc1(1, 4096);
        t.mipLevels = 3;
        EXPECT_EQ(maxDrop(t, Params{}), 2);
        t.mipLevels = 2;
        EXPECT_EQ(maxDrop(t, Params{}), 1);
    }

    TEST(VramTextureMips, an_ineligible_texture_has_no_drop)
    {
        EXPECT_EQ(maxDrop(bc1(1, 512), Params{}), 0);
        EXPECT_EQ(targetDrop(bc1(1, 512), 3, Params{}), 0);
        EXPECT_EQ(maxDrop(bc1(1, 1024), Params{}), 0);
    }

    TEST(VramTextureMips, the_target_is_the_level_up_to_the_largest_drop)
    {
        const Params p;
        const Texture big = bc1(1, 4096);
        EXPECT_EQ(targetDrop(big, 0, p), 0);
        EXPECT_EQ(targetDrop(big, 1, p), 1);
        EXPECT_EQ(targetDrop(big, 2, p), 2);
        EXPECT_EQ(targetDrop(big, 3, p), 2);
        const Texture mid = bc1(2, 2048);
        EXPECT_EQ(targetDrop(mid, 2, p), 1);
    }

    TEST(VramTextureMips, saved_bytes_are_the_dropped_levels)
    {
        const Texture t = bc1(1, 4096);
        EXPECT_EQ(savedBytes(t, 0, 1), levelBytes(t, 0));
        EXPECT_EQ(savedBytes(t, 0, 2), levelBytes(t, 0) + levelBytes(t, 1));
        EXPECT_EQ(savedBytes(t, 1, 2), levelBytes(t, 1));
        EXPECT_EQ(savedBytes(t, 2, 2), 0);
        EXPECT_EQ(savedBytes(t, 2, 0), -(levelBytes(t, 0) + levelBytes(t, 1))); // a restore costs the same
    }

    TEST(VramTextureMips, resident_bytes_follow_the_current_drop)
    {
        Texture t = bc1(1, 2048);
        const std::int64_t full = residentBytes(t);
        EXPECT_EQ(full, chainBytes(t, 0));
        EXPECT_GT(full, 0);
        t.drop = 1;
        EXPECT_EQ(residentBytes(t), full - levelBytes(t, 0));
    }

    TEST(VramTextureMips, a_step_down_frees_what_the_next_level_drops)
    {
        const std::vector<Texture> textures = { bc1(1, 4096), bc1(2, 2048), bc1(3, 512) };
        const Params p;
        // 0 -> 1: the top level of the 4096 and of the 2048; the 512 takes no part.
        EXPECT_EQ(stepBytes(textures, 0, true, p), levelBytes(textures[0], 0) + levelBytes(textures[1], 0));
        // 1 -> 2: only the 4096 has a level to give (the 2048 is at its floor).
        EXPECT_EQ(stepBytes(textures, 1, true, p), levelBytes(textures[0], 1));
        // 2 -> 3: nothing is left.
        EXPECT_EQ(stepBytes(textures, 2, true, p), 0);
    }

    TEST(VramTextureMips, a_step_up_costs_what_the_step_down_freed)
    {
        const std::vector<Texture> textures = { bc1(1, 4096), bc1(2, 2048) };
        const Params p;
        EXPECT_EQ(stepBytes(textures, 1, false, p), stepBytes(textures, 0, true, p));
        EXPECT_EQ(stepBytes(textures, 2, false, p), stepBytes(textures, 1, true, p));
        EXPECT_EQ(stepBytes(textures, 0, false, p), 0);
        EXPECT_GT(stepBytes(textures, 1, false, p), 0);
    }

    TEST(VramTextureMips, nothing_is_planned_when_every_texture_is_at_its_target)
    {
        std::vector<Texture> textures = { bc1(1, 4096), bc1(2, 2048) };
        EXPECT_TRUE(plan(textures, 0, 1000, Params{}).empty());
        textures[0].drop = 1;
        textures[1].drop = 1;
        EXPECT_TRUE(plan(textures, 1, 1000, Params{}).empty());
    }

    TEST(VramTextureMips, a_reduction_names_the_old_and_the_new_drop)
    {
        const auto changes = plan({ bc1(7, 4096) }, 2, 1000, Params{});
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].id, 7u);
        EXPECT_EQ(changes[0].fromDrop, 0);
        EXPECT_EQ(changes[0].toDrop, 2);
    }

    TEST(VramTextureMips, a_restore_goes_back_toward_zero)
    {
        Texture t = bc1(7, 4096);
        t.drop = 2;
        const auto changes = plan({ t }, 1, 1000, Params{});
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].fromDrop, 2);
        EXPECT_EQ(changes[0].toDrop, 1);
    }

    TEST(VramTextureMips, an_ineligible_texture_is_never_planned)
    {
        EXPECT_TRUE(plan({ bc1(1, 1024), bc1(2, 512) }, 3, 1000, Params{}).empty());
    }

    TEST(VramTextureMips, a_texture_changed_recently_is_left_alone)
    {
        Texture t = bc1(1, 4096);
        t.changedBefore = true;
        t.lastChangeFrame = 900;
        Params p;
        p.minFramesBetweenChanges = 300;
        EXPECT_TRUE(plan({ t }, 2, 1100, p).empty()); // 200 frames ago
        EXPECT_TRUE(plan({ t }, 2, 1199, p).empty());
        EXPECT_EQ(plan({ t }, 2, 1200, p).size(), 1u); // 300 frames ago
    }

    TEST(VramTextureMips, a_texture_never_changed_has_no_wait)
    {
        Texture t = bc1(1, 4096);
        t.changedBefore = false;
        t.lastChangeFrame = 1000; // ignored
        EXPECT_EQ(plan({ t }, 2, 1000, Params{}).size(), 1u);
    }

    TEST(VramTextureMips, the_hysteresis_holds_for_a_restore_too)
    {
        Texture t = bc1(1, 4096);
        t.drop = 2;
        t.changedBefore = true;
        t.lastChangeFrame = 990;
        EXPECT_TRUE(plan({ t }, 0, 1000, Params{}).empty());
    }

    TEST(VramTextureMips, a_frame_counter_that_went_back_does_not_underflow)
    {
        Texture t = bc1(1, 4096);
        t.changedBefore = true;
        t.lastChangeFrame = 5000;
        EXPECT_TRUE(plan({ t }, 2, 100, Params{}).empty());
    }

    TEST(VramTextureMips, a_reduction_takes_the_unused_textures_first)
    {
        Texture seen = bc1(1, 4096);
        seen.visible = true;
        seen.lastUsedFrame = 990;
        Texture old = bc1(2, 4096);
        old.lastUsedFrame = 100;
        Texture older = bc1(3, 4096);
        older.lastUsedFrame = 50;
        Params p;
        p.maxChanges = 3;
        const auto changes = plan({ seen, old, older }, 1, 1000, p);
        ASSERT_EQ(changes.size(), 3u);
        EXPECT_EQ(changes[0].id, 3u); // longest unused
        EXPECT_EQ(changes[1].id, 2u);
        EXPECT_EQ(changes[2].id, 1u); // visible last
    }

    TEST(VramTextureMips, among_the_unused_the_bigger_saving_goes_first)
    {
        Texture small = bc1(1, 2048);
        Texture big = bc1(2, 4096);
        small.lastUsedFrame = 100;
        big.lastUsedFrame = 100;
        Params p;
        p.maxChanges = 2;
        const auto changes = plan({ small, big }, 1, 1000, p);
        ASSERT_EQ(changes.size(), 2u);
        EXPECT_EQ(changes[0].id, 2u);
        EXPECT_EQ(changes[1].id, 1u);
    }

    TEST(VramTextureMips, equal_candidates_are_ordered_by_id)
    {
        Texture a = bc1(9, 4096);
        Texture b = bc1(3, 4096);
        Texture c = bc1(5, 4096);
        Params p;
        p.maxChanges = 3;
        const auto changes = plan({ a, b, c }, 1, 1000, p);
        ASSERT_EQ(changes.size(), 3u);
        EXPECT_EQ(changes[0].id, 3u);
        EXPECT_EQ(changes[1].id, 5u);
        EXPECT_EQ(changes[2].id, 9u);
    }

    TEST(VramTextureMips, the_plan_does_not_depend_on_the_input_order)
    {
        std::vector<Texture> textures;
        for (std::uint64_t id = 1; id <= 6; ++id)
        {
            Texture t = bc1(id, id % 2 ? 4096 : 2048);
            t.lastUsedFrame = id * 10 % 40;
            t.visible = id % 3 == 0;
            textures.push_back(t);
        }
        Params p;
        p.maxChanges = 6;
        const auto forward = plan(textures, 1, 1000, p);
        EXPECT_EQ(forward.size(), 6u);
        std::reverse(textures.begin(), textures.end());
        const auto backward = plan(textures, 1, 1000, p);
        ASSERT_EQ(forward.size(), backward.size());
        for (std::size_t i = 0; i < forward.size(); ++i)
        {
            EXPECT_EQ(forward[i].id, backward[i].id);
            EXPECT_EQ(forward[i].toDrop, backward[i].toDrop);
        }
    }

    TEST(VramTextureMips, a_restore_takes_the_visible_textures_first)
    {
        Texture hidden = bc1(1, 4096);
        hidden.drop = 1;
        hidden.lastUsedFrame = 10;
        Texture seen = bc1(2, 2048);
        seen.drop = 1;
        seen.visible = true;
        seen.lastUsedFrame = 995;
        Params p;
        p.maxChanges = 2;
        const auto changes = plan({ hidden, seen }, 0, 1000, p);
        ASSERT_EQ(changes.size(), 2u);
        EXPECT_EQ(changes[0].id, 2u);
        EXPECT_EQ(changes[1].id, 1u);
    }

    TEST(VramTextureMips, a_restore_takes_the_recently_used_first_among_the_unseen)
    {
        Texture a = bc1(1, 4096);
        a.drop = 1;
        a.lastUsedFrame = 10;
        Texture b = bc1(2, 4096);
        b.drop = 1;
        b.lastUsedFrame = 500;
        Params p;
        p.maxChanges = 2;
        const auto changes = plan({ a, b }, 0, 1000, p);
        ASSERT_EQ(changes.size(), 2u);
        EXPECT_EQ(changes[0].id, 2u);
    }

    TEST(VramTextureMips, a_plan_is_cut_at_the_change_limit)
    {
        std::vector<Texture> textures;
        for (std::uint64_t id = 1; id <= 10; ++id)
            textures.push_back(bc1(id, 4096));
        Params p;
        p.maxChanges = 4;
        EXPECT_EQ(plan(textures, 2, 1000, p).size(), 4u);
        p.maxChanges = 0;
        EXPECT_TRUE(plan(textures, 2, 1000, p).empty());
    }

    TEST(VramTextureMips, a_texture_goes_straight_to_its_target_in_one_swap)
    {
        const auto changes = plan({ bc1(1, 4096) }, 2, 1000, Params{});
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].toDrop, 2);
    }

    TEST(VramTextureMips, a_texture_above_its_target_after_a_lower_level_is_raised_to_it)
    {
        Texture t = bc1(1, 4096);
        t.drop = 2;
        const auto changes = plan({ t }, 0, 1000, Params{});
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].toDrop, 0);
    }

    TEST(VramTextureMips, a_texture_beyond_its_floor_is_brought_back_to_it)
    {
        // A texture at drop 2 whose largest drop is 1 (the floor changed): the plan raises it to 1 at any level.
        Texture t = bc1(1, 2048);
        t.drop = 2;
        const auto changes = plan({ t }, 3, 1000, Params{});
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].toDrop, 1);
    }

    // An array of layers (the terrain's layer arrays): every level is stored for all layers.
    TEST(VramTextureMips, a_layer_array_counts_every_layer_in_a_level)
    {
        Texture t = bc1(1, 2048);
        const std::int64_t single = levelBytes(t, 0);
        t.layers = 133;
        EXPECT_EQ(levelBytes(t, 0), single * 133);
        EXPECT_EQ(chainBytes(t, 1), chainBytes(bc1(1, 2048), 1) * 133);
    }

    TEST(VramTextureMips, a_layer_array_saves_all_layers_when_a_level_is_dropped)
    {
        Texture t = bc1(1, 2048);
        t.layers = 100;
        EXPECT_EQ(savedBytes(t, 0, 1), levelBytes(t, 0));
        EXPECT_EQ(residentBytes(t), chainBytes(t, 0));
        t.drop = 1;
        EXPECT_EQ(residentBytes(t), chainBytes(t, 1));
    }

    TEST(VramTextureMips, a_layer_array_follows_the_same_eligibility_and_floor)
    {
        Texture t = bc1(1, 2048);
        t.layers = 96;
        EXPECT_TRUE(eligible(t, Params{}));
        EXPECT_EQ(maxDrop(t, Params{}), 1);
        Texture small = bc1(2, 256);
        small.layers = 133;
        EXPECT_FALSE(eligible(small, Params{}));
    }

    TEST(VramTextureMips, a_step_of_the_ladder_counts_the_layers)
    {
        Texture t = bc1(1, 2048);
        t.layers = 10;
        const std::vector<Texture> textures = { t };
        EXPECT_EQ(stepBytes(textures, 0, true, Params{}), levelBytes(t, 0));
        EXPECT_EQ(levelBytes(t, 0), 10 * 2048 * 2048 / 2);
    }

    TEST(VramTextureMips, the_default_layer_count_is_one)
    {
        EXPECT_EQ(Texture{}.layers, 1u);
    }
}
