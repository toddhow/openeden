// Tests for MWRender::clampArea (apps/openmw/mwrender/extentsync.hpp), written from its header comment only: the render
// area for the next frame is, per axis, the smaller of the swapchain's extent and the requested one, and a zero result
// leaves the applied extent alone. No GPU is involved.

#include "apps/openmw/mwrender/extentsync.hpp"

#include <gtest/gtest.h>

namespace
{
    using MWRender::clampArea;

    bool same(VkExtent2D a, VkExtent2D b)
    {
        return a.width == b.width && a.height == b.height;
    }

    TEST(MWRenderClampArea, UnchangedWindowKeepsTheSwapchainExtent)
    {
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 1280, 720 }), { 1280, 720 }));
    }

    TEST(MWRenderClampArea, GrowWaitsForTheSwapchain)
    {
        // The event has asked for 1484x861 but the framebuffers are still 1280x720: no render area beyond them.
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 1484, 861 }), { 1280, 720 }));
    }

    TEST(MWRenderClampArea, GrowFollowsOnceTheSwapchainHasGrown)
    {
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1484, 861 }, { 1484, 861 }), { 1484, 861 }));
    }

    TEST(MWRenderClampArea, ShrinkTakesEffectBeforeTheSwapchainIsRebuilt)
    {
        // The rebuild that makes the framebuffers 884x481 comes after this call, in the same frame.
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 884, 481 }), { 884, 481 }));
    }

    TEST(MWRenderClampArea, ChoosesEachAxisSeparately)
    {
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 1500, 500 }), { 1280, 500 }));
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 900, 900 }), { 900, 720 }));
    }

    TEST(MWRenderClampArea, TwoRebuildsInARowEndAtTheLatestRequest)
    {
        // Grow, then shrink again before a frame was drawn: only the latest request is seen.
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 1000, 600 }), { 1000, 600 }));
        // A grow followed at once by a larger one: the swapchain is rebuilt once, to the last request.
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 1700, 950 }), { 1280, 720 }));
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1700, 950 }, { 1700, 950 }), { 1700, 950 }));
    }

    TEST(MWRenderClampArea, ZeroExtentChangesNothing)
    {
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 0, 0 }), { 1280, 720 }));
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 1280, 720 }, { 0, 400 }), { 1280, 720 }));
        EXPECT_TRUE(same(clampArea({ 1280, 720 }, { 400, 0 }, { 1280, 720 }), { 1280, 720 }));
    }
}
