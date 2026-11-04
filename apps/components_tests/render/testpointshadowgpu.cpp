// Tests for the layout of the point-light shadow data buffer, Render::PointShadow::pack (task 0077, stage a). Written
// from the task spec alone ("Binding layout" and the "Interfaces" section of docs/tasks/0077-point-light-shadows.md),
// without seeing any implementation. Pure CPU data: no device, window or game data.

#include <components/render/pointshadowgpu.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <vsg/maths/vec4.h>

namespace
{
    using namespace Render::PointShadow;

    void expectVec4(const vsg::vec4& actual, float x, float y, float z, float w, std::size_t index)
    {
        EXPECT_FLOAT_EQ(actual.x, x) << "vec4 " << index;
        EXPECT_FLOAT_EQ(actual.y, y) << "vec4 " << index;
        EXPECT_FLOAT_EQ(actual.z, z) << "vec4 " << index;
        EXPECT_FLOAT_EQ(actual.w, w) << "vec4 " << index;
    }

    // pack() with its size checked: an output of the wrong size is reported once and padded to the right size, so that
    // the test goes on to its real assertions instead of reading past the end of the vector.
    void packChecked(const Params& params, std::size_t maxLights, const std::vector<int>& slotOfLight,
        const std::vector<float>& fadeOfLight, const std::vector<vsg::vec4>& slotViewPositionAndFar,
        std::vector<vsg::vec4>& out)
    {
        pack(params, maxLights, slotOfLight, fadeOfLight, slotViewPositionAndFar, out);
        const std::size_t expected = 2u + maxLights + static_cast<std::size_t>(maxSlots);
        if (out.size() != expected)
        {
            ADD_FAILURE() << "pack left " << out.size() << " vec4s; expected " << expected;
            out.resize(expected);
        }
    }

    TEST(RenderPointShadowGpu, theHeaderIsTwoVec4s)
    {
        EXPECT_EQ(headerVec4s, 2u);
    }

    TEST(RenderPointShadowGpu, theOffsetsFollowTheLayoutOfTheSpec)
    {
        EXPECT_EQ(lightEntryOffset(), 2u);
        for (const std::size_t maxLights :
            { std::size_t(0), std::size_t(1), std::size_t(7), std::size_t(64), std::size_t(100) })
        {
            // header, then one entry per light, then maxSlots slot vec4s.
            EXPECT_EQ(slotOffset(maxLights), 2u + maxLights) << maxLights;
            EXPECT_EQ(bufferVec4s(maxLights), 2u + maxLights + static_cast<std::size_t>(maxSlots)) << maxLights;
        }
        EXPECT_EQ(bufferVec4s(64), 2u + 64u + 8u);
    }

    TEST(RenderPointShadowGpu, theDefaultParamsAreTheOnesOfTheSpec)
    {
        const Params p;
        EXPECT_EQ(p.slotCount, 0);
        EXPECT_EQ(p.resolution, 512.0);
        EXPECT_EQ(p.tanHalf, 1.0);
        EXPECT_EQ(p.near, nearPlane);
        EXPECT_EQ(p.normalOffsetTexels, 1.0);
        EXPECT_EQ(p.depthBiasTexels, 1.0);
        EXPECT_EQ(p.pcfRadiusTexels, 1.5);
        EXPECT_EQ(p.taps, 8);
        EXPECT_EQ(p.biasScale, 1.5);
    }

    TEST(RenderPointShadowGpu, theHeaderHoldsTheCountTheResolutionTheFieldOfViewAndTheNearPlane)
    {
        Params p;
        p.slotCount = 3;
        p.resolution = 1024.0;
        p.tanHalf = 1.0039;
        p.near = 4.0;
        std::vector<vsg::vec4> out;
        packChecked(p, 16, {}, {}, {}, out);
        ASSERT_EQ(out.size(), bufferVec4s(16));
        expectVec4(out[0], 3.f, 1024.f, 1.0039f, 4.f, 0);
    }

    TEST(RenderPointShadowGpu, theSecondHeaderVec4HasTheScaledBiasesTheRadiusAndTheTapCount)
    {
        Params p;
        p.normalOffsetTexels = 0.8;
        p.depthBiasTexels = 1.25;
        p.pcfRadiusTexels = 2.0;
        p.taps = 12;
        p.biasScale = 3.0;
        std::vector<vsg::vec4> out;
        packChecked(p, 8, {}, {}, {}, out);
        ASSERT_EQ(out.size(), bufferVec4s(8));
        // Both biases are multiplied by biasScale; the filter radius and the taps are not.
        expectVec4(out[1], 2.4f, 3.75f, 2.0f, 12.f, 1);
    }

    TEST(RenderPointShadowGpu, aBiasScaleOfOneLeavesTheBiasesAndZeroRemovesThem)
    {
        Params p;
        p.normalOffsetTexels = 0.5;
        p.depthBiasTexels = 2.5;
        p.biasScale = 1.0;
        std::vector<vsg::vec4> out;
        packChecked(p, 4, {}, {}, {}, out);
        expectVec4(out[1], 0.5f, 2.5f, 1.5f, 8.f, 1);
        p.biasScale = 0.0;
        packChecked(p, 4, {}, {}, {}, out);
        expectVec4(out[1], 0.f, 0.f, 1.5f, 8.f, 1);
    }

    TEST(RenderPointShadowGpu, aLightEntryIsTheSlotAndTheFadeIndexedByTheLightAndFollowsTheHeader)
    {
        const std::size_t maxLights = 6;
        const std::vector<int> slotOfLight = { -1, 2, 0, -1, 1, 3 };
        const std::vector<float> fadeOfLight = { 0.f, 0.25f, 1.f, 0.f, 0.5f, 0.875f };
        std::vector<vsg::vec4> out;
        packChecked(Params{}, maxLights, slotOfLight, fadeOfLight, {}, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        for (std::size_t light = 0; light < maxLights; ++light)
        {
            expectVec4(out[lightEntryOffset() + light], static_cast<float>(slotOfLight[light]), fadeOfLight[light], 0.f,
                0.f, lightEntryOffset() + light);
        }
    }

    TEST(RenderPointShadowGpu, lightsBeyondTheVectorsHaveNoSlotAndNoFade)
    {
        const std::size_t maxLights = 8;
        std::vector<vsg::vec4> out;
        packChecked(Params{}, maxLights, { 1, 0 }, { 0.5f, 1.f }, {}, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        expectVec4(out[lightEntryOffset() + 0], 1.f, 0.5f, 0.f, 0.f, 2);
        expectVec4(out[lightEntryOffset() + 1], 0.f, 1.f, 0.f, 0.f, 3);
        for (std::size_t light = 2; light < maxLights; ++light)
        {
            expectVec4(out[lightEntryOffset() + light], -1.f, 0.f, 0.f, 0.f, lightEntryOffset() + light);
        }
        // With no vectors at all every light is without a slot.
        packChecked(Params{}, maxLights, {}, {}, {}, out);
        for (std::size_t light = 0; light < maxLights; ++light)
        {
            expectVec4(out[lightEntryOffset() + light], -1.f, 0.f, 0.f, 0.f, lightEntryOffset() + light);
        }
    }

    TEST(RenderPointShadowGpu, vectorsLongerThanMaxLightsAreCutAtMaxLights)
    {
        const std::size_t maxLights = 3;
        const std::vector<int> slotOfLight = { 0, 1, 2, 3, 4, 5, 6 };
        const std::vector<float> fadeOfLight = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        std::vector<vsg::vec4> out;
        packChecked(Params{}, maxLights, slotOfLight, fadeOfLight, {}, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        for (std::size_t light = 0; light < maxLights; ++light)
        {
            expectVec4(out[lightEntryOffset() + light], static_cast<float>(light), 1.f, 0.f, 0.f, light);
        }
        // The slot vec4s that follow are untouched by the extra entries.
        for (std::size_t slot = 0; slot < static_cast<std::size_t>(maxSlots); ++slot)
        {
            expectVec4(out[slotOffset(maxLights) + slot], 0.f, 0.f, 0.f, 0.f, slotOffset(maxLights) + slot);
        }
    }

    TEST(RenderPointShadowGpu, theSlotsViewPositionAndFarSitAtTheSlotOffset)
    {
        const std::size_t maxLights = 5;
        const std::vector<vsg::vec4> slots = { vsg::vec4(1.f, 2.f, 3.f, 110.f), vsg::vec4(-4.f, 5.5f, -6.25f, 220.f),
            vsg::vec4(0.f, 0.f, -7.f, 330.f) };
        std::vector<vsg::vec4> out;
        packChecked(Params{}, maxLights, {}, {}, slots, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
        {
            expectVec4(out[slotOffset(maxLights) + slot], slots[slot].x, slots[slot].y, slots[slot].z, slots[slot].w,
                slotOffset(maxLights) + slot);
        }
    }

    TEST(RenderPointShadowGpu, everyUnusedSlotVec4IsZero)
    {
        const std::size_t maxLights = 5;
        const std::vector<vsg::vec4> slots = { vsg::vec4(1.f, 2.f, 3.f, 110.f), vsg::vec4(4.f, 5.f, 6.f, 220.f) };
        // The output starts full of garbage, longer than the result: nothing of it may survive in the slot area.
        std::vector<vsg::vec4> out(200, vsg::vec4(9.f, 9.f, 9.f, 9.f));
        packChecked(Params{}, maxLights, {}, {}, slots, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        for (std::size_t slot = slots.size(); slot < static_cast<std::size_t>(maxSlots); ++slot)
        {
            expectVec4(out[slotOffset(maxLights) + slot], 0.f, 0.f, 0.f, 0.f, slotOffset(maxLights) + slot);
        }
        // And the light entries and the header are rewritten, not left from the garbage.
        for (std::size_t light = 0; light < maxLights; ++light)
        {
            expectVec4(out[lightEntryOffset() + light], -1.f, 0.f, 0.f, 0.f, lightEntryOffset() + light);
        }
    }

    TEST(RenderPointShadowGpu, theOutputIsResizedToTheBufferSizeWhateverItWasBefore)
    {
        std::vector<vsg::vec4> out;
        packChecked(Params{}, 10, {}, {}, {}, out);
        EXPECT_EQ(out.size(), bufferVec4s(10));
        out.assign(3, vsg::vec4(1.f, 1.f, 1.f, 1.f));
        packChecked(Params{}, 10, {}, {}, {}, out);
        EXPECT_EQ(out.size(), bufferVec4s(10));
        packChecked(Params{}, 64, {}, {}, {}, out);
        EXPECT_EQ(out.size(), bufferVec4s(64));
        packChecked(Params{}, 0, {}, {}, {}, out);
        EXPECT_EQ(out.size(), bufferVec4s(0));
        EXPECT_EQ(out.size(), 2u + static_cast<std::size_t>(maxSlots));
    }

    TEST(RenderPointShadowGpu, moreSlotVec4sThanMaxSlotsAreIgnoredAndTheSizeStaysFixed)
    {
        const std::size_t maxLights = 2;
        std::vector<vsg::vec4> slots;
        for (int i = 0; i < maxSlots + 4; ++i)
        {
            slots.push_back(vsg::vec4(static_cast<float>(i + 1), 0.f, 0.f, 1.f));
        }
        std::vector<vsg::vec4> out;
        packChecked(Params{}, maxLights, {}, {}, slots, out);
        ASSERT_EQ(out.size(), bufferVec4s(maxLights));
        expectVec4(out[slotOffset(maxLights) + static_cast<std::size_t>(maxSlots) - 1], static_cast<float>(maxSlots),
            0.f, 0.f, 1.f, 0);
    }

    TEST(RenderPointShadowGpu, aFullBufferHasEveryPieceInItsPlace)
    {
        // A whole buffer as the game would write it: 4 slots in use, 6 lights, 3 of them shadowed.
        Params p;
        p.slotCount = 4;
        p.resolution = 512.0;
        p.tanHalf = 512.0 / 508.0;
        p.normalOffsetTexels = 1.0;
        p.depthBiasTexels = 1.0;
        p.pcfRadiusTexels = 1.5;
        p.taps = 8;
        p.biasScale = 1.5;
        const std::size_t maxLights = 6;
        const std::vector<int> slotOfLight = { 3, -1, 0, -1, 1, -1 };
        const std::vector<float> fadeOfLight = { 0.125f, 0.f, 1.f, 0.f, 0.5f, 0.f };
        const std::vector<vsg::vec4> slots = { vsg::vec4(1.f, 1.f, 1.f, 100.f), vsg::vec4(2.f, 2.f, 2.f, 200.f),
            vsg::vec4(3.f, 3.f, 3.f, 300.f), vsg::vec4(4.f, 4.f, 4.f, 400.f) };
        std::vector<vsg::vec4> out;
        packChecked(p, maxLights, slotOfLight, fadeOfLight, slots, out);
        ASSERT_EQ(out.size(), 2u + 6u + 8u);
        expectVec4(out[0], 4.f, 512.f, static_cast<float>(512.0 / 508.0), 4.f, 0);
        expectVec4(out[1], 1.5f, 1.5f, 1.5f, 8.f, 1);
        expectVec4(out[2], 3.f, 0.125f, 0.f, 0.f, 2);
        expectVec4(out[3], -1.f, 0.f, 0.f, 0.f, 3);
        expectVec4(out[4], 0.f, 1.f, 0.f, 0.f, 4);
        expectVec4(out[5], -1.f, 0.f, 0.f, 0.f, 5);
        expectVec4(out[6], 1.f, 0.5f, 0.f, 0.f, 6);
        expectVec4(out[7], -1.f, 0.f, 0.f, 0.f, 7);
        expectVec4(out[8], 1.f, 1.f, 1.f, 100.f, 8);
        expectVec4(out[9], 2.f, 2.f, 2.f, 200.f, 9);
        expectVec4(out[10], 3.f, 3.f, 3.f, 300.f, 10);
        expectVec4(out[11], 4.f, 4.f, 4.f, 400.f, 11);
        for (std::size_t i = 12; i < out.size(); ++i)
        {
            expectVec4(out[i], 0.f, 0.f, 0.f, 0.f, i);
        }
    }
}
