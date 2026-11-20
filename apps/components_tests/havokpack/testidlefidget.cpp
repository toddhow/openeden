#include <components/havokpack/idlefidget.hpp>
#include <components/havokpack/locomotion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <set>

// Task 0361: the idle variants an actor plays now and then, and the walk to run switch speed. The numbers are worked out
// by hand from the documented ranges.
namespace
{
    using namespace HavokPack;

    TEST(HavokIdleFidget, a_schedule_stays_inside_its_documented_ranges)
    {
        for (std::uint64_t id = 0; id < 2000; ++id)
        {
            const Fidget::Schedule s = Fidget::scheduleFor(id * 7919u + 3u);
            EXPECT_GE(s.mFirst, 2.f);
            EXPECT_LT(s.mFirst, 10.f);
            EXPECT_GE(s.mPeriod, 12.f);
            EXPECT_LT(s.mPeriod, 24.f);
        }
    }

    TEST(HavokIdleFidget, the_same_reference_always_gets_the_same_schedule_and_variant)
    {
        const Fidget::Schedule a = Fidget::scheduleFor(864594);
        const Fidget::Schedule b = Fidget::scheduleFor(864594);
        EXPECT_EQ(a.mFirst, b.mFirst);
        EXPECT_EQ(a.mPeriod, b.mPeriod);
        EXPECT_EQ(Fidget::variantFor(864594), Fidget::variantFor(864594));
    }

    TEST(HavokIdleFidget, neighbouring_references_do_not_move_as_one)
    {
        // Six guards one after another in the plugin: their first variants must not all start within one second, and
        // at least three different variants must be among them.
        float lo = 1e9f, hi = -1e9f;
        std::set<std::size_t> variants;
        for (std::uint64_t id = 864590; id < 864596; ++id)
        {
            const float first = Fidget::scheduleFor(id).mFirst;
            lo = std::min(lo, first);
            hi = std::max(hi, first);
            variants.insert(Fidget::variantFor(id));
        }
        EXPECT_GT(hi - lo, 1.f);
        EXPECT_GE(variants.size(), 3u);
    }

    TEST(HavokIdleFidget, every_variant_index_is_in_the_list_and_all_are_used)
    {
        std::set<std::size_t> used;
        for (std::uint64_t id = 0; id < 5000; ++id)
        {
            const std::size_t v = Fidget::variantFor(id);
            ASSERT_LT(v, Fidget::kVariants.size());
            used.insert(v);
        }
        EXPECT_EQ(used.size(), Fidget::kVariants.size());
    }

    TEST(HavokIdleFidget, nothing_plays_before_the_first_start)
    {
        const Fidget::Schedule s{ 5.f, 15.f };
        EXPECT_EQ(Fidget::at(s, 0.f, 4.f).mWeight, 0.f);
        EXPECT_EQ(Fidget::at(s, 4.99f, 4.f).mWeight, 0.f);
    }

    TEST(HavokIdleFidget, the_weight_rises_holds_and_falls_over_the_clip)
    {
        const Fidget::Schedule s{ 5.f, 15.f };
        // clip 4 s long, ramp 0.35: at 0 s into the clip 0, at 0.175 s half, from 0.35 to 3.65 s one, at 4 s zero.
        EXPECT_NEAR(Fidget::at(s, 5.f, 4.f).mWeight, 0.f, 1e-5f);
        EXPECT_NEAR(Fidget::at(s, 5.175f, 4.f).mWeight, 0.5f, 1e-4f);
        EXPECT_NEAR(Fidget::at(s, 7.f, 4.f).mWeight, 1.f, 1e-5f);
        EXPECT_NEAR(Fidget::at(s, 7.f, 4.f).mClipTime, 2.f, 1e-5f);
        EXPECT_NEAR(Fidget::at(s, 8.825f, 4.f).mWeight, 0.5f, 1e-4f);
        EXPECT_EQ(Fidget::at(s, 9.5f, 4.f).mWeight, 0.f);
    }

    TEST(HavokIdleFidget, the_next_variant_starts_one_period_later)
    {
        const Fidget::Schedule s{ 5.f, 15.f };
        const Fidget::Play p = Fidget::at(s, 5.f + 15.f + 2.f, 4.f);
        EXPECT_NEAR(p.mWeight, 1.f, 1e-5f);
        EXPECT_NEAR(p.mClipTime, 2.f, 1e-4f);
        EXPECT_EQ(Fidget::at(s, 5.f + 30.f + 6.f, 4.f).mWeight, 0.f);
    }

    TEST(HavokIdleFidget, a_clip_longer_than_the_period_or_bad_numbers_play_nothing)
    {
        const Fidget::Schedule s{ 5.f, 15.f };
        EXPECT_EQ(Fidget::at(s, 8.f, 0.f).mWeight, 0.f);
        EXPECT_EQ(Fidget::at(s, std::nanf(""), 4.f).mWeight, 0.f);
        EXPECT_EQ(Fidget::at({ 5.f, 0.f }, 8.f, 4.f).mWeight, 0.f);
    }

    TEST(HavokIdleFidget, a_clip_too_long_for_the_period_stretches_it_and_leaves_a_gap)
    {
        const Fidget::Schedule s{ 0.f, 12.f };
        // a 16 s clip: the period becomes 19 s, so at 16.5 s the clip is over and nothing plays until 19 s
        EXPECT_NEAR(Fidget::at(s, 8.f, 16.f).mWeight, 1.f, 1e-5f);
        EXPECT_EQ(Fidget::at(s, 16.5f, 16.f).mWeight, 0.f);
        EXPECT_NEAR(Fidget::at(s, 19.f + 8.f, 16.f).mClipTime, 8.f, 1e-4f);
    }

    TEST(HavokGaitSwitch, the_balanced_switch_speed_is_the_geometric_mean_of_the_clips_speeds)
    {
        Gait::Params p;
        EXPECT_NEAR(Gait::balancedSwitchSpeed(p), std::sqrt(82.45f * 350.87f), 1e-3f);
    }

    TEST(HavokGaitSwitch, a_chosen_switch_speed_moves_where_the_run_starts_and_the_default_does_not)
    {
        Gait::Params plain;
        EXPECT_FALSE(Gait::wantRun(200.f, false, plain)); // 216.66 + 10 percent for the plain half way edge
        Gait::Params balanced;
        balanced.mSwitchSpeed = Gait::balancedSwitchSpeed(balanced);
        EXPECT_TRUE(Gait::wantRun(200.f, false, balanced)); // 170 + 10 percent = 187
        EXPECT_FALSE(Gait::wantRun(180.f, false, balanced));
        EXPECT_TRUE(Gait::wantRun(160.f, true, balanced)); // it stays a run above 170 - 10 percent = 153
        EXPECT_FALSE(Gait::wantRun(150.f, true, balanced));
    }
}
