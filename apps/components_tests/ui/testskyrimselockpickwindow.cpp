// Task 0203: the lock picking window (files/data/openeden/skyrimse/lockpick_window.lua) over the headless UI backend
// and a fake `core.skyrimse.lockbooks` package written in Lua. Written AFTER the module (a window is hard to specify
// before it exists); a one-line mutation check of the module is in the task's stage log.
#include "uiluafixture.hpp"

#include <algorithm>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimLockpickWindow : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                W = require('openeden.skyrimse.lockpick_window')
                state = 'none'
                placement = ''
                reason = ''
                picks = 3
                serial = 0
                progress, strain = 0, 0
                begins, inputs, cancels, closes = {}, {}, 0, 0
                lb = {
                  lock_state = function() return state end,
                  lock_reason = function() return reason end,
                  lock_placement = function() return placement end,
                  lock_progress = function() return progress end,
                  lock_strain = function() return strain end,
                  picks = function() return picks end,
                  lock_serial = function() return serial end,
                }
                env = { ui = ui, lb = lb,
                  begin = function(p, l) begins[#begins + 1] = { p, l }; end,
                  input = function(a, t, ms) inputs[#inputs + 1] = { a, t, ms } end,
                  cancel = function() cancels = cancels + 1 end,
                  close = function() closes = closes + 1 end }
                win = W.create(env)
            )"));
        }

        std::vector<ElementId> withText(const std::string& cls, const std::string& text) const
        {
            std::vector<ElementId> out;
            for (ElementId id : withClass(cls))
                if (backend.node(id)->mText == text)
                    out.push_back(id);
            return out;
        }

        bool hasText(const std::string& text) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText == text)
                    return true;
            return false;
        }

        bool hasTextStarting(const std::string& prefix) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText.rfind(prefix, 0) == 0)
                    return true;
            return false;
        }

        void frame(double dt)
        {
            ASSERT_TRUE(run("win.update(" + std::to_string(dt) + "); ui.update()"));
        }

        // The pack answers: attempt in progress on the window's lock.
        void picking() { ASSERT_TRUE(run("state = 'picking'; placement = '2d631'; serial = serial + 1")); }

        void click(ElementId id)
        {
            ASSERT_TRUE(backend.emit(id, EventKind::Click));
            ASSERT_TRUE(run("ui.update()"));
        }

        void open() { ASSERT_TRUE(run("win.open('0002d631', 50, 'Dawnstar Barracks'); ui.update()")); }
    };

    TEST_F(SkyrimLockpickWindow, openingAsksForAnAttemptAndShowsTheLock)
    {
        open();
        EXPECT_TRUE(get<bool>("win.isOpen()"));
        ASSERT_EQ(get<int>("#begins"), 1);
        EXPECT_EQ(get<std::string>("begins[1][1]"), "0002d631");
        EXPECT_EQ(get<int>("begins[1][2]"), 50);
        EXPECT_TRUE(hasText("Dawnstar Barracks"));
        EXPECT_TRUE(hasText("Lockpicks 3"));
        EXPECT_TRUE(hasText("Pick angle +0"));
        EXPECT_TRUE(get<bool>("win.waiting()"));
    }

    TEST_F(SkyrimLockpickWindow, nothingIsSentUntilThePackAnswers)
    {
        open();
        for (int i = 0; i < 10; ++i)
            frame(0.016);
        EXPECT_EQ(get<int>("#inputs"), 0);
        // an answer about another lock (or an old attempt) is not the answer
        ASSERT_TRUE(run("state = 'picking'; placement = 'ffff'; serial = serial + 1"));
        frame(0.016);
        EXPECT_EQ(get<int>("#inputs"), 0);
        picking();
        frame(0.016);
        EXPECT_EQ(get<int>("#inputs"), 1);
        EXPECT_FALSE(get<bool>("win.waiting()"));
    }

    TEST_F(SkyrimLockpickWindow, eachFrameSendsTheAngleTheTurnAndTheFrameTimeInMilliseconds)
    {
        open();
        picking();
        frame(0.016);
        ASSERT_EQ(get<int>("#inputs"), 1);
        EXPECT_NEAR(get<double>("inputs[1][1]"), 0.0, 1e-9);
        EXPECT_FALSE(get<bool>("inputs[1][2]"));
        EXPECT_NEAR(get<double>("inputs[1][3]"), 16.0, 1e-6);
        frame(0.032);
        EXPECT_NEAR(get<double>("inputs[2][3]"), 32.0, 1e-6);
    }

    TEST_F(SkyrimLockpickWindow, theKeysTurnThePickAtSixtyDegreesASecondAndStopAtNinety)
    {
        open();
        picking();
        ASSERT_TRUE(run("win.setKey('right', true)"));
        frame(0.5);
        EXPECT_NEAR(get<double>("win.angle()"), 30.0, 1e-6);
        EXPECT_TRUE(hasText("Pick angle +30"));
        frame(5.0);
        EXPECT_NEAR(get<double>("win.angle()"), 90.0, 1e-6);
        ASSERT_TRUE(run("win.setKey('right', false); win.setKey('left', true)"));
        frame(2.0);
        EXPECT_NEAR(get<double>("win.angle()"), -30.0, 1e-6);
        EXPECT_TRUE(hasText("Pick angle -30"));
        ASSERT_TRUE(run("win.setKey('left', true); win.setKey('right', true)"));
        frame(1.0);
        EXPECT_NEAR(get<double>("win.angle()"), -30.0, 1e-6); // both keys cancel
        ASSERT_TRUE(run("win.setKey('nonsense', true)")); // an unknown key is ignored
    }

    TEST_F(SkyrimLockpickWindow, theTurnKeyAndTheTurnButtonTurnTheLock)
    {
        open();
        picking();
        ASSERT_TRUE(run("win.setKey('turn', true)"));
        frame(0.016);
        EXPECT_TRUE(get<bool>("inputs[#inputs][2]"));
        ASSERT_TRUE(run("win.setKey('turn', false)"));
        frame(0.016);
        EXPECT_FALSE(get<bool>("inputs[#inputs][2]"));
        // one click turns for 0.4 s
        const auto turn = withText("button", "Turn");
        ASSERT_EQ(turn.size(), 1u);
        click(turn.front());
        frame(0.3);
        EXPECT_TRUE(get<bool>("inputs[#inputs][2]"));
        frame(0.3);
        EXPECT_TRUE(get<bool>("inputs[#inputs][2]"));
        frame(0.3);
        EXPECT_FALSE(get<bool>("inputs[#inputs][2]"));
    }

    TEST_F(SkyrimLockpickWindow, theArrowButtonsNudgeFiveDegrees)
    {
        open();
        picking();
        const auto right = withText("button", ">");
        const auto left = withText("button", "<");
        ASSERT_EQ(right.size(), 1u);
        ASSERT_EQ(left.size(), 1u);
        click(right.front());
        click(right.front());
        EXPECT_NEAR(get<double>("win.angle()"), 10.0, 1e-9);
        click(left.front());
        EXPECT_NEAR(get<double>("win.angle()"), 5.0, 1e-9);
        for (int i = 0; i < 40; ++i)
            click(left.front());
        EXPECT_NEAR(get<double>("win.angle()"), -90.0, 1e-9);
    }

    TEST_F(SkyrimLockpickWindow, anOpenedLockSaysSoAndClosesAfterAMoment)
    {
        open();
        picking();
        frame(0.016);
        ASSERT_TRUE(run("state = 'opened'; progress = 1"));
        frame(0.016);
        EXPECT_TRUE(hasText("Unlocked"));
        EXPECT_EQ(get<int>("closes"), 0);
        EXPECT_EQ(get<int>("#inputs"), 1); // no more input after the end
        frame(0.6);
        EXPECT_EQ(get<int>("closes"), 0);
        frame(0.7);
        EXPECT_EQ(get<int>("closes"), 1);
        frame(1.0);
        EXPECT_EQ(get<int>("closes"), 1); // once
    }

    TEST_F(SkyrimLockpickWindow, aBrokenPickStartsTheNextOneWhileThereAreMore)
    {
        open();
        picking();
        frame(0.016);
        ASSERT_TRUE(run("state = 'broke'; picks = 2"));
        frame(0.016);
        EXPECT_TRUE(hasText("The pick broke. A new pick."));
        ASSERT_EQ(get<int>("#begins"), 2);
        EXPECT_EQ(get<std::string>("begins[2][1]"), "0002d631");
        // the pack has not answered yet: still 'broke', no second begin and no input
        const int inputs = get<int>("#inputs");
        for (int i = 0; i < 5; ++i)
            frame(0.016);
        EXPECT_EQ(get<int>("#begins"), 2);
        EXPECT_EQ(get<int>("#inputs"), inputs);
        ASSERT_TRUE(run("state = 'picking'; serial = serial + 1"));
        frame(0.016);
        EXPECT_EQ(get<int>("#inputs"), inputs + 1);
    }

    TEST_F(SkyrimLockpickWindow, aBrokenLastPickEndsTheAttempt)
    {
        open();
        picking();
        frame(0.016);
        ASSERT_TRUE(run("state = 'broke'; picks = 0"));
        frame(0.016);
        EXPECT_TRUE(hasText("The pick broke. You have no more lockpicks."));
        EXPECT_EQ(get<int>("#begins"), 1);
        frame(1.5);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimLockpickWindow, aRefusalShowsThePacksReasonAndCloses)
    {
        open();
        ASSERT_TRUE(run("state = 'refused'; reason = 'you have no lockpick'; serial = serial + 1"));
        frame(0.016);
        EXPECT_TRUE(hasText("you have no lockpick"));
        EXPECT_EQ(get<int>("#inputs"), 0);
        frame(1.5);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimLockpickWindow, anOldRefusalIsNotTheAnswerToANewBegin)
    {
        // the pack still says 'refused' (with an old reason) from the last lock: a new window must wait for ITS answer
        ASSERT_TRUE(run("state = 'refused'; reason = 'this lock needs its key'; serial = 4"));
        open();
        for (int i = 0; i < 10; ++i)
            frame(0.016);
        EXPECT_FALSE(hasText("this lock needs its key"));
        EXPECT_EQ(get<int>("closes"), 0);
        EXPECT_TRUE(get<bool>("win.waiting()"));
        // the pack answers this begin: serial moved, the attempt is picking
        ASSERT_TRUE(run("serial = 5; state = 'picking'; placement = '2d631'"));
        frame(0.016);
        EXPECT_FALSE(get<bool>("win.waiting()"));
        EXPECT_EQ(get<int>("#inputs"), 1);
        EXPECT_EQ(get<int>("closes"), 0);
    }

    TEST_F(SkyrimLockpickWindow, aFreshRefusalIsShownWhenTheSerialMoved)
    {
        ASSERT_TRUE(run("state = 'refused'; reason = 'old'; serial = 4"));
        open();
        ASSERT_TRUE(run("serial = 5; reason = 'you have no lockpick'"));
        frame(0.016);
        EXPECT_TRUE(hasText("you have no lockpick"));
        frame(1.5);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimLockpickWindow, aPackWithoutTheSerialStillWorksWithThePlacementCheck)
    {
        ASSERT_TRUE(run("lb.lock_serial = nil"));
        open();
        picking();
        frame(0.016);
        EXPECT_EQ(get<int>("#inputs"), 1);
    }

    TEST_F(SkyrimLockpickWindow, aPackThatNeverAnswersIsGivenUpOn)
    {
        open();
        for (int i = 0; i < 245; ++i)
            frame(0.016);
        EXPECT_TRUE(hasText("The lock did not answer."));
        EXPECT_FALSE(get<bool>("win.waiting()"));
        frame(1.5);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimLockpickWindow, dismissingWhilePickingCancelsTheAttemptOnce)
    {
        open();
        picking();
        frame(0.016);
        ASSERT_TRUE(run("win.dismiss()"));
        EXPECT_EQ(get<int>("cancels"), 1);
        EXPECT_FALSE(get<bool>("win.isOpen()"));
        ASSERT_TRUE(run("win.dismiss()"));
        EXPECT_EQ(get<int>("cancels"), 1);
        EXPECT_TRUE(withClass("panel").empty()); // the document is gone
    }

    TEST_F(SkyrimLockpickWindow, theCancelButtonCancelsAndLeaves)
    {
        open();
        picking();
        frame(0.016);
        const auto cancel = withText("button", "Cancel");
        ASSERT_EQ(cancel.size(), 1u);
        click(cancel.front());
        EXPECT_EQ(get<int>("cancels"), 1);
        EXPECT_EQ(get<int>("closes"), 1);
    }

    TEST_F(SkyrimLockpickWindow, theLockpickCountFollowsThePack)
    {
        open();
        picking();
        ASSERT_TRUE(run("picks = 7"));
        frame(0.016);
        EXPECT_TRUE(hasText("Lockpicks 7"));
    }

    TEST_F(SkyrimLockpickWindow, openingAgainRebuildsAndAsksAgain)
    {
        open();
        ASSERT_TRUE(run("win.open('00000010', 25, 'Chest'); ui.update()"));
        EXPECT_EQ(get<int>("#begins"), 2);
        EXPECT_EQ(get<std::string>("begins[2][1]"), "00000010");
        EXPECT_EQ(withClass("panel").size(), 1u);
        EXPECT_TRUE(hasText("Chest"));
    }

    TEST_F(SkyrimLockpickWindow, aFrameWithNothingChangedMakesNoProtocolCalls)
    {
        open();
        picking();
        frame(0.0);
        const std::size_t calls = backend.calls().size();
        for (int i = 0; i < 20; ++i)
            frame(0.0);
        EXPECT_EQ(backend.calls().size(), calls);
    }
}
