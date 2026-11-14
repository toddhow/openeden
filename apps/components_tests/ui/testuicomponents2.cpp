// Task 0166: the second slice of `openeden.ui` components (Checkbox, Toggle, Slider, ProgressBar, ScrollView, Tabs) over
// the headless backend. Written from the spec (docs/tasks/0166-ui-followups-and-components.md) before the components
// existed; they ran red against a library without them.
#include "uiluafixture.hpp"

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;
    using UiComponents2 = UiLuaFixture;

    // ---- Checkbox and Toggle ----------------------------------------------------------------------------------

    TEST_F(UiComponents2, ACheckboxShowsItsLabelAndStateAndTogglesTheValueOnClick)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            on = ui.Value(false)
            changes = {}
            doc = ui.Document{ name = 'd', build = function()
                return ui.Checkbox{ value = on, label = 'Sound', onChange = function(v) changes[#changes + 1] = v end }
            end }
            ui.update()
        )"));
        const auto boxes = withClass("checkbox");
        ASSERT_EQ(boxes.size(), 1u);
        const ElementId box = boxes[0];
        EXPECT_EQ(backend.node(box)->mClass, "checkbox");
        EXPECT_TRUE(backend.node(box)->mListens.count(EventKind::Click));
        EXPECT_EQ(backend.node(box)->mAttributes.at("tabindex"), "auto");
        ASSERT_EQ(withClass("check-box").size(), 1u);
        const auto labels = withClass("check-label");
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(backend.node(labels[0])->mText, "Sound");

        backend.clearCalls();
        backend.emit(box, EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_TRUE(get<bool>("on:get()"));
        EXPECT_EQ(backend.node(box)->mClass, "checkbox checked");
        EXPECT_EQ(backend.callCount(), 1u) << "one setClass, nothing else";
        EXPECT_EQ(get<int>("#changes"), 1);
        EXPECT_TRUE(get<bool>("changes[1]"));

        backend.emit(box, EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_FALSE(get<bool>("on:get()"));
        EXPECT_EQ(backend.node(box)->mClass, "checkbox");
        EXPECT_FALSE(get<bool>("changes[2]"));
    }

    TEST_F(UiComponents2, ACheckboxFollowsTheValueWhenAScriptChangesIt)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            on = ui.Value(true)
            doc = ui.Document{ name = 'd', build = function() return ui.Checkbox{ value = on, label = 'x', class = 'wide' } end }
            ui.update()
        )"));
        const auto boxes = withClass("checkbox");
        ASSERT_EQ(boxes.size(), 1u);
        EXPECT_EQ(backend.node(boxes[0])->mClass, "checkbox checked wide");
        backend.clearCalls();
        ASSERT_TRUE(run("on:set(false); ui.update()"));
        EXPECT_EQ(backend.node(boxes[0])->mClass, "checkbox wide");
        EXPECT_EQ(backend.callCount(), 1u);
        backend.clearCalls();
        ASSERT_TRUE(run("on:set(false); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
    }

    TEST_F(UiComponents2, ACheckboxWithAPlainValueKeepsItsOwnState)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function() return ui.Checkbox{ value = true, label = 'x' } end }
            ui.update()
        )"));
        const auto boxes = withClass("checkbox");
        ASSERT_EQ(boxes.size(), 1u);
        EXPECT_EQ(backend.node(boxes[0])->mClass, "checkbox checked");
        backend.emit(boxes[0], EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(backend.node(boxes[0])->mClass, "checkbox");
    }

    TEST_F(UiComponents2, AToggleIsACheckboxWithItsOwnClassesAndAKnob)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            on = ui.Value(false)
            doc = ui.Document{ name = 'd', build = function() return ui.Toggle{ value = on, label = 'Fog' } end }
            ui.update()
        )"));
        const auto toggles = withClass("toggle");
        ASSERT_EQ(toggles.size(), 1u);
        EXPECT_EQ(backend.node(toggles[0])->mClass, "toggle");
        EXPECT_EQ(withClass("toggle-track").size(), 1u);
        EXPECT_EQ(withClass("toggle-knob").size(), 1u);
        EXPECT_EQ(backend.node(withClass("toggle-label")[0])->mText, "Fog");
        backend.emit(toggles[0], EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_TRUE(get<bool>("on:get()"));
        EXPECT_EQ(backend.node(toggles[0])->mClass, "toggle on");
    }

    TEST_F(UiComponents2, ATogglesLabelCanBeReactiveAndUnknownPropertiesAreErrors)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            name = ui.Value('a')
            doc = ui.Document{ name = 'd', build = function() return ui.Toggle{ value = false, label = name } end }
            ui.update()
            name:set('b')
            ui.update()
            local ok, err = pcall(ui.Checkbox, { value = false, bogus = 1 })
            failed, message = not ok, tostring(err)
        )"));
        EXPECT_EQ(backend.node(withClass("toggle-label")[0])->mText, "b");
        EXPECT_TRUE(get<bool>("failed"));
    }

    // ---- Slider -----------------------------------------------------------------------------------------------

    TEST_F(UiComponents2, ASliderIsARangeInputThatIsTwoWayWithoutEchoing)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            level = ui.Value(0.5)
            seen = {}
            doc = ui.Document{ name = 'd', build = function()
                return ui.Slider{ value = level, min = 0, max = 2, step = 0.25, onChange = function(v) seen[#seen + 1] = v end }
            end }
            ui.update()
        )"));
        const auto sliders = withClass("slider");
        ASSERT_EQ(sliders.size(), 1u);
        const ElementId slider = sliders[0];
        const auto* node = backend.node(slider);
        EXPECT_EQ(node->mTag, "input");
        EXPECT_EQ(node->mAttributes.at("type"), "range");
        EXPECT_EQ(node->mAttributes.at("min"), "0");
        EXPECT_EQ(node->mAttributes.at("max"), "2");
        EXPECT_EQ(node->mAttributes.at("step"), "0.25");
        EXPECT_EQ(node->mAttributes.at("value"), "0.5");
        EXPECT_TRUE(node->mListens.count(EventKind::Change));

        backend.clearCalls();
        backend.emit(slider, EventKind::Change, "1.25");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_DOUBLE_EQ(get<double>("level:get()"), 1.25);
        EXPECT_DOUBLE_EQ(get<double>("seen[1]"), 1.25);
        EXPECT_EQ(backend.callCount(), 0u) << "the element already shows it";

        ASSERT_TRUE(run("level:set(0); ui.update()"));
        EXPECT_EQ(backend.node(slider)->mAttributes.at("value"), "0");
        EXPECT_EQ(backend.callCount(), 1u);
    }

    TEST_F(UiComponents2, ASliderIgnoresAChangeThatIsNotANumber)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            level = ui.Value(1)
            doc = ui.Document{ name = 'd', build = function() return ui.Slider{ value = level } end }
            ui.update()
        )"));
        const auto sliders = withClass("slider");
        ASSERT_EQ(sliders.size(), 1u);
        backend.emit(sliders[0], EventKind::Change, "abc");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_DOUBLE_EQ(get<double>("level:get()"), 1.0);
        EXPECT_EQ(backend.node(sliders[0])->mAttributes.at("min"), "0");
        EXPECT_EQ(backend.node(sliders[0])->mAttributes.at("max"), "1");
    }

    TEST_F(UiComponents2, ASliderNeedsMaxAboveMin)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            ui.withScope(ui.Scope(), function()
                local ok = pcall(ui.Slider, { value = 0, min = 1, max = 1 })
                bad = not ok
            end)
        )"));
        EXPECT_TRUE(get<bool>("bad"));
    }

    // ---- ProgressBar ------------------------------------------------------------------------------------------

    TEST_F(UiComponents2, AProgressBarSizesItsFillByPercentAndClamps)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            p = ui.Value(0.5)
            doc = ui.Hud{ name = 'h', build = function() return ui.ProgressBar{ value = p } end }
            ui.update()
        )"));
        const auto bars = withClass("progress");
        ASSERT_EQ(bars.size(), 1u);
        const auto fills = withClass("progress-fill");
        ASSERT_EQ(fills.size(), 1u);
        EXPECT_EQ(backend.node(fills[0])->mProperties.at("width"), "50%");
        backend.clearCalls();
        ASSERT_TRUE(run("p:set(0.25); ui.update()"));
        EXPECT_EQ(backend.node(fills[0])->mProperties.at("width"), "25%");
        EXPECT_EQ(backend.callCount(), 1u);
        ASSERT_TRUE(run("p:set(7); ui.update()"));
        EXPECT_EQ(backend.node(fills[0])->mProperties.at("width"), "100%");
        ASSERT_TRUE(run("p:set(-3); ui.update()"));
        EXPECT_EQ(backend.node(fills[0])->mProperties.at("width"), "0%");
    }

    TEST_F(UiComponents2, AProgressBarUsesMaxAndItsFillIsAChildOfTheBar)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Hud{ name = 'h', build = function() return ui.ProgressBar{ value = 30, max = 120 } end }
        )"));
        const auto bars = withClass("progress");
        const auto fills = withClass("progress-fill");
        ASSERT_EQ(bars.size(), 1u);
        ASSERT_EQ(fills.size(), 1u);
        EXPECT_EQ(backend.node(fills[0])->mParent, bars[0]);
        EXPECT_EQ(backend.node(fills[0])->mProperties.at("width"), "25%");
    }

    // ---- ScrollView -------------------------------------------------------------------------------------------

    TEST_F(UiComponents2, AScrollViewIsAFixedHeightScrollingContainerForItsChildren)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                return ui.ScrollView{ height = 80, ui.Text{ text = 'one' }, ui.Text{ text = 'two' } }
            end }
        )"));
        const auto views = withClass("scrollview");
        ASSERT_EQ(views.size(), 1u);
        const auto* node = backend.node(views[0]);
        EXPECT_EQ(node->mProperties.at("height"), "80dp");
        EXPECT_EQ(node->mProperties.at("overflow-y"), "auto");
        EXPECT_NE(node->mClass.find("scroll"), std::string::npos);
        ASSERT_EQ(node->mChildren.size(), 2u);
        EXPECT_EQ(backend.node(node->mChildren[1])->mText, "two");
    }

    TEST_F(UiComponents2, AScrollViewWithoutAHeightIsAnError)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            ui.withScope(ui.Scope(), function()
                local ok = pcall(ui.ScrollView, {})
                bad = not ok
            end)
        )"));
        EXPECT_TRUE(get<bool>("bad"));
    }

    // ---- Tabs -------------------------------------------------------------------------------------------------

    constexpr const char* kTabsScript = R"(
        ui = require('openeden.ui')
        sel = ui.Value('b')
        built = {}
        doc = ui.Document{ name = 'd', build = function()
            return ui.Tabs{ value = sel, tabs = {
                { id = 'a', label = 'Alpha', content = function() built[#built + 1] = 'a'; return ui.Text{ text = 'in a' } end },
                { id = 'b', label = 'Beta', content = function() built[#built + 1] = 'b'; return ui.Text{ text = 'in b' } end },
            } }
        end }
        ui.update()
    )";

    TEST_F(UiComponents2, TabsShowOnlyTheSelectedPanelAndMarkTheActiveTab)
    {
        ASSERT_TRUE(run(kTabsScript));
        const auto tabs = withClass("tab");
        ASSERT_EQ(tabs.size(), 2u);
        const auto panels = withClass("tab-panel");
        ASSERT_EQ(panels.size(), 2u);
        EXPECT_EQ(backend.node(tabs[0])->mText, "Alpha");
        EXPECT_EQ(backend.node(tabs[0])->mClass, "tab");
        EXPECT_EQ(backend.node(tabs[1])->mClass, "tab active");
        EXPECT_EQ(backend.node(panels[0])->mProperties.at("display"), "none");
        EXPECT_EQ(backend.node(panels[1])->mProperties.at("display"), "block");
        EXPECT_EQ(get<int>("#built"), 2);
        EXPECT_EQ(withClass("tab-bar").size(), 1u);
    }

    TEST_F(UiComponents2, AClickOnATabSelectsItAndAScriptChangeFollowsWithOneCallPerChange)
    {
        ASSERT_TRUE(run(kTabsScript));
        const auto tabs = withClass("tab");
        const auto panels = withClass("tab-panel");
        ASSERT_EQ(tabs.size(), 2u);
        EXPECT_TRUE(backend.node(tabs[0])->mListens.count(EventKind::Click));
        backend.emit(tabs[0], EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<std::string>("sel:get()"), "a");
        EXPECT_EQ(backend.node(tabs[0])->mClass, "tab active");
        EXPECT_EQ(backend.node(tabs[1])->mClass, "tab");
        EXPECT_EQ(backend.node(panels[0])->mProperties.at("display"), "block");
        EXPECT_EQ(backend.node(panels[1])->mProperties.at("display"), "none");

        backend.clearCalls();
        ASSERT_TRUE(run("sel:set('b'); ui.update()"));
        EXPECT_EQ(backend.node(panels[1])->mProperties.at("display"), "block");
        EXPECT_EQ(backend.callCount(), 4u) << "two classes and two displays, nothing more";
    }

    TEST_F(UiComponents2, TabsWithoutAValueSelectTheFirstAndKeepTheirOwnState)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                return ui.Tabs{ tabs = {
                    { id = 'x', label = 'X', content = function() return ui.Text{ text = 'x' } end },
                    { id = 'y', label = 'Y', content = function() return ui.Text{ text = 'y' } end } } }
            end }
            ui.update()
        )"));
        const auto tabs = withClass("tab");
        ASSERT_EQ(tabs.size(), 2u);
        EXPECT_EQ(backend.node(tabs[0])->mClass, "tab active");
        backend.emit(tabs[1], EventKind::Click);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(backend.node(tabs[1])->mClass, "tab active");
        EXPECT_EQ(backend.node(tabs[0])->mClass, "tab");
    }

    TEST_F(UiComponents2, BadTabListsAreErrors)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local content = function() return ui.Text{ text = 't' } end
            ui.withScope(ui.Scope(), function()
                emptyFails = not pcall(ui.Tabs, { tabs = {} })
                dupFails = not pcall(ui.Tabs, { tabs = { { id = 'a', label = 'A', content = content }, { id = 'a', label = 'B', content = content } } })
                noContentFails = not pcall(ui.Tabs, { tabs = { { id = 'a', label = 'A' } } })
                badValueFails = not pcall(ui.Tabs, { value = 'zzz', tabs = { { id = 'a', label = 'A', content = content } } })
            end)
        )"));
        EXPECT_TRUE(get<bool>("emptyFails"));
        EXPECT_TRUE(get<bool>("dupFails"));
        EXPECT_TRUE(get<bool>("noContentFails"));
        EXPECT_TRUE(get<bool>("badValueFails"));
    }

    TEST_F(UiComponents2, ADestroyedDocumentTakesTheNewComponentsWithIt)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            on = ui.Value(false)
            doc = ui.Document{ name = 'd', build = function()
                return ui.Column{ ui.Checkbox{ value = on, label = 'a' }, ui.Toggle{ value = on, label = 'b' },
                    ui.Slider{ value = ui.Value(0) }, ui.ProgressBar{ value = 0.5 }, ui.ScrollView{ height = 10 } }
            end }
            ui.update()
            doc:destroy()
            ui.update()
        )"));
        EXPECT_EQ(backend.nodes().size(), 0u);
    }

    TEST_F(UiComponents2, AFrameWithNothingChangedMakesNoCallsWithTheNewComponents)
    {
        ASSERT_TRUE(run(kTabsScript));
        ASSERT_TRUE(run(R"(
            on = ui.Value(true)
            doc2 = ui.Document{ name = 'e', build = function()
                return ui.Column{ ui.Checkbox{ value = on, label = 'a' }, ui.Slider{ value = ui.Value(0.5) },
                    ui.ProgressBar{ value = 0.5 } }
            end }
            ui.update()
        )"));
        backend.clearCalls();
        ASSERT_TRUE(run("ui.update(); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
    }
}
