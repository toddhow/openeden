// Tests for the point light shadow exclusion of a light's own model (task 0077, decision 0012): which traversal skips
// the model that carries a light. Written first, from the decision: the carrier is skipped in the shadow passes of
// the slot that holds its light, and nowhere else (not for other lights, not for the sun, not in the main view).

#include <components/animation/pointshadowcarrier.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include <vsg/core/Mask.h>

#include <components/render/pointshadowmath.hpp>

namespace
{
    using namespace Anim;

    const std::uint64_t lightA = 0x1000;
    const std::uint64_t lightB = 0x2000;
    constexpr vsg::Mask objectBit = 1ull << 10;

    struct Owners
    {
        std::array<std::uint64_t, pointShadowSlotCount> mIds{};
        std::uint64_t operator()(int slot) const { return mIds[static_cast<std::size_t>(slot)]; }
    };

    TEST(PointShadowCarrier, there_is_a_slot_bit_for_every_slot_of_the_shadow_system)
    {
        EXPECT_EQ(pointShadowSlotCount, Render::PointShadow::maxSlots);
    }

    TEST(PointShadowCarrier, slot_bits_are_distinct_and_leave_the_low_bits_alone)
    {
        vsg::Mask all = 0;
        for (int k = 0; k < pointShadowSlotCount; ++k)
        {
            const vsg::Mask bit = pointShadowSlotMask(k);
            EXPECT_NE(bit, 0u);
            EXPECT_EQ(bit & all, 0u);
            EXPECT_EQ(bit & 0xFFFFFFFFull, 0u) << "a slot bit must not collide with the renderer's masks";
            all |= bit;
        }
    }

    TEST(PointShadowCarrier, a_mask_with_exactly_one_slot_bit_names_that_slot)
    {
        for (int k = 0; k < pointShadowSlotCount; ++k)
            EXPECT_EQ(pointShadowSlotOfMask(objectBit | pointShadowSlotMask(k)), k);
    }

    TEST(PointShadowCarrier, no_slot_bit_or_several_name_no_slot)
    {
        EXPECT_EQ(pointShadowSlotOfMask(0), -1);
        EXPECT_EQ(pointShadowSlotOfMask(objectBit), -1);
        EXPECT_EQ(pointShadowSlotOfMask(vsg::MASK_ALL), -1) << "the main view's mask has every bit";
        EXPECT_EQ(pointShadowSlotOfMask(~(1ull << 15)), -1);
        EXPECT_EQ(pointShadowSlotOfMask(pointShadowSlotMask(1) | pointShadowSlotMask(2)), -1);
    }

    TEST(PointShadowCarrier, the_carrier_is_skipped_in_its_own_lights_slot)
    {
        Owners owners;
        owners.mIds[2] = lightA;
        EXPECT_TRUE(pointShadowCarrierSkipped(objectBit | pointShadowSlotMask(2), lightA, owners));
    }

    TEST(PointShadowCarrier, the_carrier_is_drawn_in_the_slots_of_other_lights)
    {
        Owners owners;
        owners.mIds[2] = lightA;
        owners.mIds[3] = lightB;
        EXPECT_FALSE(pointShadowCarrierSkipped(objectBit | pointShadowSlotMask(3), lightA, owners));
        EXPECT_FALSE(pointShadowCarrierSkipped(objectBit | pointShadowSlotMask(0), lightA, owners));
    }

    TEST(PointShadowCarrier, the_carrier_is_drawn_for_the_sun_and_the_main_view)
    {
        Owners owners;
        owners.mIds[0] = lightA;
        EXPECT_FALSE(pointShadowCarrierSkipped(objectBit, lightA, owners));
        EXPECT_FALSE(pointShadowCarrierSkipped(vsg::MASK_ALL, lightA, owners));
        EXPECT_FALSE(pointShadowCarrierSkipped(~(1ull << 15), lightA, owners));
    }

    TEST(PointShadowCarrier, a_model_without_a_light_is_never_skipped)
    {
        Owners owners; // free slots hold 0
        for (int k = 0; k < pointShadowSlotCount; ++k)
            EXPECT_FALSE(pointShadowCarrierSkipped(objectBit | pointShadowSlotMask(k), 0, owners));
    }

    TEST(PointShadowCarrier, the_shared_owner_table_is_set_and_read_by_slot)
    {
        setPointShadowOwner(5, lightB);
        EXPECT_EQ(pointShadowOwner(5), lightB);
        setPointShadowOwner(5, 0);
        EXPECT_EQ(pointShadowOwner(5), 0u);
        EXPECT_EQ(pointShadowOwner(-1), 0u);
        EXPECT_EQ(pointShadowOwner(pointShadowSlotCount), 0u);
        setPointShadowOwner(pointShadowSlotCount, lightA); // ignored
    }
}
