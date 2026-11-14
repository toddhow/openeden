// Task 0162: the protocol calls the base components of `openeden.ui` produce, over the headless backend. Written from the
// spec (docs/tasks/0162-ui-element-protocol-and-lua-library.md) and run red against a stub library first.
#include "uiluafixture.hpp"

namespace
{
    using UiTest::UiLuaFixture;
    using Ui::ElementId;
    using Ui::EventKind;
    using UiComponents = UiLuaFixture;

    TEST_F(UiComponents, AHudWithOneTextMakesExactlyTheseCalls)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Hud{ name = 'h', build = function() return ui.Text{ text = 'hi' } end }
            ui.update()
        )"));
        EXPECT_EQ(backend.calls(),
            (std::vector<std::string>{ "createDocument #1 h", "setClass #1 hud", "setProperty #1 pointer-events none", "create #2 div",
                "setClass #2 text", "setText #2 hi", "insertBefore #1 #2 #0" }));
        EXPECT_EQ(backend.commitCount(), 1u);
    }

    TEST_F(UiComponents, ADocumentTakesPointerEventsAndAHudDoesNot)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            d = ui.Document{ name = 'win', class = 'window', build = function() end }
            h = ui.Hud{ name = 'hud', build = function() end }
        )"));
        const auto docs = withClass("window");
        ASSERT_EQ(docs.size(), 1u);
        EXPECT_EQ(backend.node(docs[0])->mProperties.count("pointer-events"), 0u);
        bool sawHud = false;
        for (const auto& [id, n] : backend.nodes())
            if (n.mDocument && n.mName == "hud")
            {
                sawHud = true;
                EXPECT_EQ(n.mProperties.at("pointer-events"), "none");
            }
        EXPECT_TRUE(sawHud);
    }

    TEST_F(UiComponents, AReactiveTextCostsOneSetTextPerChange)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            v = ui.Value('a')
            doc = ui.Hud{ name = 'h', build = function() return ui.Text{ text = v } end }
            ui.update()
        )"));
        backend.clearCalls();
        ASSERT_TRUE(run("v:set('b'); v:set('c'); ui.update()"));
        EXPECT_EQ(backend.calls(), (std::vector<std::string>{ "setText #2 c" }));
        backend.clearCalls();
        ASSERT_TRUE(run("v:set('c'); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
    }

    TEST_F(UiComponents, ANumberOrComputedTextIsShownAsText)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            n = ui.Value(1)
            doc = ui.Hud{ name = 'h', build = function()
                return ui.Column{ ui.Text{ text = n }, ui.Text{ text = ui.Computed(function() return 'n=' .. n:get() end) } }
            end }
            ui.update()
        )"));
        std::vector<std::string> texts;
        for (const auto& [id, node] : backend.nodes())
            if (!node.mText.empty())
                texts.push_back(node.mText);
        std::sort(texts.begin(), texts.end());
        EXPECT_EQ(texts, (std::vector<std::string>{ "1", "n=1" }));
    }

    TEST_F(UiComponents, ChildrenAreAttachedInOrder)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                return ui.Column{ ui.Text{ text = 'one' }, ui.Text{ text = 'two' }, false, ui.Text{ text = 'three' } }
            end }
            ui.update()
        )"));
        const auto columns = withClass("column");
        ASSERT_EQ(columns.size(), 1u);
        std::vector<std::string> texts;
        for (ElementId child : backend.node(columns[0])->mChildren)
            texts.push_back(backend.node(child)->mText);
        EXPECT_EQ(texts, (std::vector<std::string>{ "one", "two", "three" }));
    }

    TEST_F(UiComponents, RowAndColumnAreFlexContainersAndStackIsPositioned)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                return ui.Frame{ ui.Row{ gap = 4 }, ui.Column{}, ui.Stack{} }
            end }
        )"));
        const auto row = withClass("row"), column = withClass("column"), stack = withClass("stack");
        ASSERT_EQ(row.size(), 1u);
        ASSERT_EQ(column.size(), 1u);
        ASSERT_EQ(stack.size(), 1u);
        EXPECT_EQ(backend.node(row[0])->mProperties.at("display"), "flex");
        EXPECT_EQ(backend.node(row[0])->mProperties.at("flex-direction"), "row");
        EXPECT_EQ(backend.node(row[0])->mProperties.at("gap"), "4dp");
        EXPECT_EQ(backend.node(column[0])->mProperties.at("flex-direction"), "column");
        EXPECT_EQ(backend.node(stack[0])->mProperties.at("position"), "relative");
    }

    TEST_F(UiComponents, ClassAndStyleCanBeReactiveAndOnlyChangesReachTheProtocol)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            sel = ui.Value(false)
            w = ui.Value(10)
            doc = ui.Document{ name = 'd', build = function()
                return ui.Frame{
                    class = ui.Computed(function() return sel:get() and 'item selected' or 'item' end),
                    style = { width = w, color = '#fff' } }
            end }
            ui.update()
        )"));
        const auto frames = withClass("frame");
        ASSERT_EQ(frames.size(), 1u);
        EXPECT_EQ(backend.node(frames[0])->mClass, "frame item");
        EXPECT_EQ(backend.node(frames[0])->mProperties.at("width"), "10dp");
        EXPECT_EQ(backend.node(frames[0])->mProperties.at("color"), "#fff");
        backend.clearCalls();
        ASSERT_TRUE(run("sel:set(true); ui.update()"));
        EXPECT_EQ(backend.calls().size(), 1u);
        EXPECT_EQ(backend.node(frames[0])->mClass, "frame item selected");
        backend.clearCalls();
        ASSERT_TRUE(run("w:set(12); ui.update()"));
        EXPECT_EQ(backend.calls(), (std::vector<std::string>{ "setProperty #" + std::to_string(frames[0] & 0xffffff)
                                       + " width 12dp" }));
    }

    TEST_F(UiComponents, VisibleTogglesDisplay)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            show = ui.Value(true)
            doc = ui.Document{ name = 'd', build = function() return ui.Frame{ visible = show } end }
            ui.update()
        )"));
        const auto frames = withClass("frame");
        ASSERT_EQ(frames.size(), 1u);
        EXPECT_NE(backend.node(frames[0])->mProperties.at("display"), "none");
        ASSERT_TRUE(run("show:set(false); ui.update()"));
        EXPECT_EQ(backend.node(frames[0])->mProperties.at("display"), "none");
        ASSERT_TRUE(run("show:set(true); ui.update()"));
        EXPECT_NE(backend.node(frames[0])->mProperties.at("display"), "none");
    }

    TEST_F(UiComponents, AButtonListensToClicksAndCallsTheHandler)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            clicks = 0
            doc = ui.Document{ name = 'd', build = function()
                return ui.Button{ text = 'Go', onClick = function() clicks = clicks + 1 end }
            end }
            ui.update()
        )"));
        const auto buttons = withClass("button");
        ASSERT_EQ(buttons.size(), 1u);
        const auto* node = backend.node(buttons[0]);
        EXPECT_EQ(node->mText, "Go");
        EXPECT_TRUE(node->mListens.count(EventKind::Click));
        EXPECT_EQ(node->mAttributes.at("tabindex"), "auto");
        EXPECT_TRUE(backend.emit(buttons[0], EventKind::Click));
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<int>("clicks"), 1);
    }

    TEST_F(UiComponents, AButtonWithoutAHandlerListensToNothing)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function() return ui.Button{ text = 'x' } end }
        )"));
        const auto buttons = withClass("button");
        ASSERT_EQ(buttons.size(), 1u);
        EXPECT_TRUE(backend.node(buttons[0])->mListens.empty());
    }

    TEST_F(UiComponents, AHandlerThatRaisesDoesNotStopOtherHandlersAndTheErrorIsReported)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            second = 0
            doc = ui.Document{ name = 'd', build = function()
                return ui.Column{
                    ui.Button{ text = 'bad', onClick = function() error('handler failed') end },
                    ui.Button{ text = 'good', onClick = function() second = second + 1 end } }
            end }
            ui.update()
        )"));
        const auto buttons = withClass("button");
        ASSERT_EQ(buttons.size(), 2u);
        for (ElementId id : buttons)
            backend.emit(id, EventKind::Click);
        ASSERT_TRUE(run("local ok, err = pcall(ui.update); assert(not ok and tostring(err):find('handler failed'), tostring(err))"));
        EXPECT_EQ(get<int>("second"), 1);
    }

    TEST_F(UiComponents, FocusableAutofocusAndFocusEvents)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            log = {}
            doc = ui.Document{ name = 'd', build = function()
                first = ui.Frame{ focusable = true, autofocus = true,
                    onFocus = function() log[#log + 1] = 'focus' end, onBlur = function() log[#log + 1] = 'blur' end }
                second = ui.Frame{}
                return ui.Column{ first, second }
            end }
            ui.update()
        )"));
        const auto frames = withClass("frame");
        ASSERT_EQ(frames.size(), 2u);
        ElementId focusable = Ui::kNoElement, plain = Ui::kNoElement;
        for (ElementId id : frames)
            (backend.node(id)->mAttributes.count("tabindex") ? focusable : plain) = id;
        ASSERT_NE(focusable, Ui::kNoElement);
        EXPECT_EQ(backend.node(plain)->mAttributes.count("tabindex"), 0u);
        EXPECT_EQ(backend.focused(), focusable);
        backend.emit(focusable, EventKind::Focus);
        backend.emit(focusable, EventKind::Blur);
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<std::string>("table.concat(log, ',')"), "focus,blur");
    }

    TEST_F(UiComponents, UiFocusAsksTheBackendToFocusTheNode)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function() node = ui.Button{ text = 'a' }; return node end }
            ui.focus(node)
            ui.update()
        )"));
        const auto buttons = withClass("button");
        EXPECT_EQ(backend.focused(), buttons[0]);
    }

    TEST_F(UiComponents, NavPropertiesNameTheTargetElementsById)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            doc = ui.Document{ name = 'd', build = function()
                local a = ui.Button{ text = 'a' }
                local b = ui.Button{ text = 'b' }
                a.nav = nil
                return ui.Column{ ui.Button{ text = 'c', nav = { down = b, up = a } }, a, b }
            end }
            ui.update()
        )"));
        ElementId c = Ui::kNoElement;
        for (const auto& [id, node] : backend.nodes())
            if (node.mText == "c")
                c = id;
        ASSERT_NE(c, Ui::kNoElement);
        const auto& props = backend.node(c)->mProperties;
        ASSERT_EQ(props.count("nav-down"), 1u);
        ASSERT_EQ(props.count("nav-up"), 1u);
        EXPECT_EQ(props.at("nav-down").front(), '#');
        // The named elements carry that id as their `id` attribute.
        std::size_t found = 0;
        for (const auto& [id, node] : backend.nodes())
            if (node.mAttributes.count("id")
                && (("#" + node.mAttributes.at("id")) == props.at("nav-down")
                    || ("#" + node.mAttributes.at("id")) == props.at("nav-up")))
                ++found;
        EXPECT_EQ(found, 2u);
    }

    TEST_F(UiComponents, ImageTakesASourceThatCanChange)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            src = ui.Value('a.png')
            doc = ui.Document{ name = 'd', build = function() return ui.Image{ src = src } end }
            ui.update()
        )"));
        const auto images = withClass("image");
        ASSERT_EQ(images.size(), 1u);
        EXPECT_EQ(backend.node(images[0])->mTag, "img");
        EXPECT_EQ(backend.node(images[0])->mAttributes.at("src"), "a.png");
        ASSERT_TRUE(run("src:set('b.png'); ui.update()"));
        EXPECT_EQ(backend.node(images[0])->mAttributes.at("src"), "b.png");
    }

    TEST_F(UiComponents, TextInputIsTwoWayWithoutEchoing)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            value = ui.Value('start')
            changes = {}
            doc = ui.Document{ name = 'd', build = function()
                return ui.TextInput{ value = value, onChange = function(v) changes[#changes + 1] = v end }
            end }
            ui.update()
        )"));
        const auto inputs = withClass("input");
        ASSERT_EQ(inputs.size(), 1u);
        const ElementId input = inputs[0];
        EXPECT_EQ(backend.node(input)->mTag, "input");
        EXPECT_EQ(backend.node(input)->mAttributes.at("type"), "text");
        EXPECT_EQ(backend.node(input)->mAttributes.at("value"), "start");
        EXPECT_TRUE(backend.node(input)->mListens.count(EventKind::Change));

        // The user types: the Value follows, the element is not told to write the text it already shows.
        backend.clearCalls();
        backend.emit(input, EventKind::Change, "typed");
        ASSERT_TRUE(run("ui.update()"));
        EXPECT_EQ(get<std::string>("value:get()"), "typed");
        EXPECT_EQ(get<std::string>("changes[1]"), "typed");
        EXPECT_EQ(backend.callCount(), 0u);

        // The script changes the Value: the element is told once.
        ASSERT_TRUE(run("value:set('script'); ui.update()"));
        EXPECT_EQ(backend.node(input)->mAttributes.at("value"), "script");
        EXPECT_EQ(backend.callCount(), 1u);
    }

    TEST_F(UiComponents, DestroyingADocumentIsOneCallAndStopsEverythingInIt)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            v = ui.Value('a')
            doc = ui.Document{ name = 'd', build = function()
                return ui.Column{ ui.Text{ text = v }, ui.Button{ text = 'b', onClick = function() end } }
            end }
            ui.update()
        )"));
        EXPECT_GT(backend.liveElements(), 3u);
        backend.clearCalls();
        ASSERT_TRUE(run("doc:destroy(); ui.update()"));
        EXPECT_EQ(backend.calls().size(), 1u);
        EXPECT_EQ(backend.liveElements(), 0u);
        backend.clearCalls();
        ASSERT_TRUE(run("v:set('zzz'); ui.update()"));
        EXPECT_EQ(backend.callCount(), 0u);
        ASSERT_TRUE(run("doc:destroy()")); // twice is harmless
        EXPECT_EQ(backend.callCount(), 0u);
    }

    TEST_F(UiComponents, ABuildFunctionThatRaisesLeavesNothingBehind)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local ok, err = pcall(ui.Document, { name = 'd', build = function()
                ui.Text{ text = 'x' }
                error('build failed')
            end })
            assert(not ok and tostring(err):find('build failed'), tostring(err))
            ui.update()
        )"));
        EXPECT_EQ(backend.liveElements(), 0u);
    }

    TEST_F(UiComponents, ComponentsOutsideABuildAreRefused)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local ok, err = pcall(ui.Text, { text = 'x' })
            assert(not ok and tostring(err):find('scope'), tostring(err))
        )"));
    }

    TEST_F(UiComponents, TwoDocumentsAreTwoDocuments)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            a = ui.Hud{ name = 'slow', build = function() return ui.Text{ text = 'fixed' } end }
            fps = ui.Value('0')
            b = ui.Hud{ name = 'fps', build = function() return ui.Text{ text = fps } end }
            ui.update()
        )"));
        std::size_t documents = 0;
        for (const auto& [id, node] : backend.nodes())
            documents += node.mDocument ? 1 : 0;
        EXPECT_EQ(documents, 2u);
    }

    TEST_F(UiComponents, AnUnknownPropertyIsALocatedError)
    {
        ASSERT_TRUE(run(R"(
            ui = require('openeden.ui')
            local ok, err = pcall(ui.Document, { name = 'd', build = function() return ui.Text{ texxt = 'typo' } end })
            assert(not ok and tostring(err):find('texxt'), tostring(err))
        )"));
    }
}
