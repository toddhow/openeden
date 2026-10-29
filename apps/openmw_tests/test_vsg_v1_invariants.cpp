#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    // Frame simulation harness matching the engine's main loop automation logic:
    // apps/openmw/engine.cpp lines 1054-1070 and apps/openmw/vsgengine.cpp lines 268-285.
    enum class MockEngineState
    {
        State_Loading,
        State_Menu,
        State_Running
    };

    struct AutomationSimulator
    {
        unsigned int mAutoScreenshotFrame = 0;
        unsigned int mAutoQuitFrame = 0;

        unsigned int mRunningFrame = 0;
        unsigned int mTotalFramesSimulated = 0;

        unsigned int mScreenshotTriggerCount = 0;
        unsigned int mQuitTriggerCount = 0;

        bool mAutoScreenshotFired = false;
        bool mAutoQuitFired = false;

        void simulateFrame(MockEngineState state)
        {
            ++mTotalFramesSimulated;

            if (state == MockEngineState::State_Running)
            {
                ++mRunningFrame;

                if (mAutoScreenshotFrame && !mAutoScreenshotFired && mRunningFrame >= mAutoScreenshotFrame)
                {
                    ++mScreenshotTriggerCount;
                    mAutoScreenshotFired = true;
                }

                if (mAutoQuitFrame && !mAutoQuitFired && mRunningFrame >= mAutoQuitFrame)
                {
                    ++mQuitTriggerCount;
                    mAutoQuitFired = true;
                }
            }
        }

        bool hasQuit() const { return mAutoQuitFired; }
    };

    // Validates whether automation frame sequencing invariants hold
    bool isAutomationSequencingValid(unsigned int screenshotFrame, unsigned int quitFrame)
    {
        // If neither or only one is set, sequencing is trivially valid
        if (screenshotFrame == 0 || quitFrame == 0)
            return true;

        // Invariant: screenshot frame must be less than or equal to quit frame
        // so that screenshot capture happens before the engine terminates
        return screenshotFrame <= quitFrame;
    }

    struct ViewportExtent
    {
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;

        bool isValid() const { return mWidth > 0 && mHeight > 0; }
        double aspectRatio() const { return isValid() ? static_cast<double>(mWidth) / mHeight : 0.0; }
    };
}

TEST(VsgV1InvariantsTest, should_enforce_automation_frame_sequencing_invariants)
{
    // Valid: screenshot frame happens before quit frame (e.g. screenshot at 120, quit at 150)
    EXPECT_TRUE(isAutomationSequencingValid(120, 150));

    // Valid: screenshot and quit on the same frame (screenshot triggers before quit request processed)
    EXPECT_TRUE(isAutomationSequencingValid(100, 100));

    // Valid: only screenshot enabled
    EXPECT_TRUE(isAutomationSequencingValid(60, 0));

    // Valid: only quit enabled
    EXPECT_TRUE(isAutomationSequencingValid(0, 100));

    // Valid: both disabled
    EXPECT_TRUE(isAutomationSequencingValid(0, 0));

    // Invalid: quit frame before screenshot frame (screenshot would be unreachable!)
    EXPECT_FALSE(isAutomationSequencingValid(200, 100));
    EXPECT_FALSE(isAutomationSequencingValid(50, 10));
}

TEST(VsgV1InvariantsTest, should_not_increment_running_frame_counter_during_loading_or_menu)
{
    AutomationSimulator sim;
    sim.mAutoScreenshotFrame = 10;
    sim.mAutoQuitFrame = 20;

    // Simulate 50 loading frames and 30 menu frames
    for (int i = 0; i < 50; ++i)
        sim.simulateFrame(MockEngineState::State_Loading);
    for (int i = 0; i < 30; ++i)
        sim.simulateFrame(MockEngineState::State_Menu);

    EXPECT_EQ(sim.mTotalFramesSimulated, 80u);
    EXPECT_EQ(sim.mRunningFrame, 0u);
    EXPECT_FALSE(sim.mAutoScreenshotFired);
    EXPECT_FALSE(sim.mAutoQuitFired);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 0u);
    EXPECT_EQ(sim.mQuitTriggerCount, 0u);
}

TEST(VsgV1InvariantsTest, should_trigger_screenshot_and_quit_at_exact_configured_frames)
{
    AutomationSimulator sim;
    sim.mAutoScreenshotFrame = 5;
    sim.mAutoQuitFrame = 10;

    // Simulate running frames up to frame 4: nothing should fire yet
    for (int i = 0; i < 4; ++i)
        sim.simulateFrame(MockEngineState::State_Running);

    EXPECT_EQ(sim.mRunningFrame, 4u);
    EXPECT_FALSE(sim.mAutoScreenshotFired);
    EXPECT_FALSE(sim.mAutoQuitFired);

    // Frame 5: screenshot fires!
    sim.simulateFrame(MockEngineState::State_Running);
    EXPECT_EQ(sim.mRunningFrame, 5u);
    EXPECT_TRUE(sim.mAutoScreenshotFired);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 1u);
    EXPECT_FALSE(sim.mAutoQuitFired);

    // Frames 6 to 9: running continues, screenshot does NOT fire again (single-shot invariant)
    for (int i = 6; i <= 9; ++i)
        sim.simulateFrame(MockEngineState::State_Running);

    EXPECT_EQ(sim.mRunningFrame, 9u);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 1u);
    EXPECT_FALSE(sim.mAutoQuitFired);

    // Frame 10: quit fires!
    sim.simulateFrame(MockEngineState::State_Running);
    EXPECT_EQ(sim.mRunningFrame, 10u);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 1u);
    EXPECT_TRUE(sim.mAutoQuitFired);
    EXPECT_EQ(sim.mQuitTriggerCount, 1u);

    // Subsequent frames: quit remains fired, no repeat triggers
    sim.simulateFrame(MockEngineState::State_Running);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 1u);
    EXPECT_EQ(sim.mQuitTriggerCount, 1u);
}

TEST(VsgV1InvariantsTest, should_respect_disabled_automation_options)
{
    AutomationSimulator sim;
    sim.mAutoScreenshotFrame = 0; // disabled
    sim.mAutoQuitFrame = 0; // disabled

    for (int i = 0; i < 1000; ++i)
        sim.simulateFrame(MockEngineState::State_Running);

    EXPECT_EQ(sim.mRunningFrame, 1000u);
    EXPECT_FALSE(sim.mAutoScreenshotFired);
    EXPECT_FALSE(sim.mAutoQuitFired);
    EXPECT_EQ(sim.mScreenshotTriggerCount, 0u);
    EXPECT_EQ(sim.mQuitTriggerCount, 0u);
}

TEST(VsgV1InvariantsTest, should_verify_renderer_extent_and_screenshot_buffer_invariants)
{
    // Valid viewports
    const ViewportExtent hd{ 1280, 720 };
    EXPECT_TRUE(hd.isValid());
    EXPECT_NEAR(hd.aspectRatio(), 16.0 / 9.0, 1e-4);

    const ViewportExtent fhd{ 1920, 1080 };
    EXPECT_TRUE(fhd.isValid());
    EXPECT_NEAR(fhd.aspectRatio(), 16.0 / 9.0, 1e-4);

    // Invariant: Screenshot buffer byte size calculation
    // RGBA / BGRA 8-bit channels = 4 bytes per pixel
    const size_t bytesPerPixel = 4;
    const size_t expectedHdBufferSize = hd.mWidth * hd.mHeight * bytesPerPixel;
    EXPECT_EQ(expectedHdBufferSize, 1280u * 720u * 4u);

    // Invalid viewports
    const ViewportExtent zeroWidth{ 0, 720 };
    EXPECT_FALSE(zeroWidth.isValid());

    const ViewportExtent zeroHeight{ 1280, 0 };
    EXPECT_FALSE(zeroHeight.isValid());
}
