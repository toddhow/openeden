#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <MyGUI_TextBox.h>

#include "luauiharness.hpp"

// L0 of the Lua UI compatibility plan (docs/research/0045-ui-system.md section 16.4, task 0112).
//
// The real components/lua_ui code lays out Lua layout tables on MyGUI's dummy platform, and the computed rectangles
// are compared with golden files recorded from the code as it was before the leaf layer was changed.
// Never regenerate a golden to make a test pass; set OPENEDEN_RECORD_LUAUI_GOLDENS=1 only to record a new case or to
// record on purpose after toddhow has approved a change in behaviour.

namespace
{
    using namespace testing;
    namespace fs = std::filesystem;

    fs::path dataDir()
    {
        return fs::path{ OPENMW_PROJECT_SOURCE_DIR } / "apps" / "components_tests" / "lua_ui" / "data";
    }

    LuaUiTest::Rect rectAt(LuaUi::WidgetExtension* root, std::initializer_list<size_t> path)
    {
        LuaUi::WidgetExtension* ext = root;
        for (size_t index : path)
            ext = ext->children().at(index);
        const MyGUI::IntCoord c = ext->widget()->getCoord();
        return { c.left, c.top, c.width, c.height };
    }

    MATCHER_P4(IsRect, left, top, width, height, "")
    {
        return arg.left == left && arg.top == top && arg.width == width && arg.height == height;
    }

    struct LuaUiGeometryTest : Test
    {
        LuaUiTest::Harness mHarness;

        std::shared_ptr<LuaUi::Element> create(const std::string& layoutSource)
        {
            sol::object layout = mHarness.run("local ui, util = ...; return " + layoutSource, "layout");
            return mHarness.createElement(layout.as<sol::table>());
        }
    };

    // Rectangles worked out by hand from the arithmetic in widget.cpp / flex.cpp (integer maths, no rounding).

    TEST_F(LuaUiGeometryTest, absolute_position_and_size_are_used_as_given)
    {
        auto e = create(R"({ layer = 'Windows', props = { position = util.vector2(10, 20), size = util.vector2(300, 200) },
            content = ui.content{ { props = { position = util.vector2(5, 6), size = util.vector2(50, 60) } } } })");
        EXPECT_THAT(rectAt(e->mRoot, {}), IsRect(10, 20, 300, 200));
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(5, 6, 50, 60));
    }

    TEST_F(LuaUiGeometryTest, relative_size_and_position_are_a_part_of_the_parent_size)
    {
        auto e = create(R"({ layer = 'Windows', props = { size = util.vector2(400, 300) },
            content = ui.content{ { props = { relativeSize = util.vector2(0.5, 0.25),
                                              relativePosition = util.vector2(0.1, 0.2) } } } })");
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(40, 60, 200, 75));
    }

    TEST_F(LuaUiGeometryTest, anchor_moves_the_widget_back_by_a_part_of_its_own_size)
    {
        auto e = create(R"({ layer = 'Windows', props = { size = util.vector2(200, 100) },
            content = ui.content{ { props = { size = util.vector2(50, 20),
                relativePosition = util.vector2(0.5, 0.5), anchor = util.vector2(0.5, 0.5) } } } })");
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(75, 40, 50, 20));
    }

    TEST_F(LuaUiGeometryTest, a_root_with_relative_size_fills_the_layer)
    {
        auto e = create(R"({ layer = 'HUD', props = { relativeSize = util.vector2(1, 1) } })");
        EXPECT_THAT(rectAt(e->mRoot, {}), IsRect(0, 0, 1024, 768));
    }

    TEST_F(LuaUiGeometryTest, horizontal_flex_places_children_one_after_another_and_fits_them)
    {
        auto e = create(R"({ layer = 'Windows', type = ui.TYPE.Flex, props = { horizontal = true },
            content = ui.content{ { props = { size = util.vector2(10, 20) } },
                                  { props = { size = util.vector2(30, 40) } } } })");
        EXPECT_THAT(rectAt(e->mRoot, {}), IsRect(0, 0, 40, 40));
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(0, 0, 10, 20));
        EXPECT_THAT(rectAt(e->mRoot, { 1 }), IsRect(10, 0, 30, 40));
    }

    TEST_F(LuaUiGeometryTest, flex_grow_shares_the_free_space_by_weight)
    {
        auto e = create(R"({ layer = 'Windows', type = ui.TYPE.Flex,
            props = { horizontal = true, autoSize = false, size = util.vector2(100, 10) },
            content = ui.content{
                { props = { size = util.vector2(10, 10) }, external = { grow = 1 } },
                { props = { size = util.vector2(20, 10) } },
                { props = { size = util.vector2(5, 10) }, external = { grow = 2 } } } })");
        // free space = 100 - 35 = 65, per weight 21.67: the integer part of weight times factor is added
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(0, 0, 31, 10));
        EXPECT_THAT(rectAt(e->mRoot, { 1 }), IsRect(31, 0, 20, 10));
        EXPECT_THAT(rectAt(e->mRoot, { 2 }), IsRect(51, 0, 48, 10));
    }

    TEST_F(LuaUiGeometryTest, flex_center_alignment_uses_the_free_space_on_the_main_axis)
    {
        auto e = create(R"({ layer = 'Windows', type = ui.TYPE.Flex,
            props = { horizontal = true, autoSize = false, size = util.vector2(100, 50), align = ui.ALIGNMENT.Center,
                      arrange = ui.ALIGNMENT.End },
            content = ui.content{ { props = { size = util.vector2(20, 10) } } } })");
        EXPECT_THAT(rectAt(e->mRoot, { 0 }), IsRect(40, 40, 20, 10));
    }

    // Text metrics come from MyGUI's real TrueType font code on the stub textures. These tests only state the
    // relations that must hold for any fixed-advance font, the numbers are pinned by the golden files.

    LuaUiTest::Rect textRect(LuaUiGeometryTest& test, const std::string& props)
    {
        auto e = test.create("{ layer = 'Windows', type = ui.TYPE.Text, props = { " + props + " } }");
        return rectAt(e->mRoot, {});
    }

    TEST_F(LuaUiGeometryTest, text_extent_follows_the_number_of_characters_of_a_fixed_advance_font)
    {
        const auto two = textRect(*this, "text = 'ab', textSize = 16");
        const auto four = textRect(*this, "text = 'abab', textSize = 16");
        EXPECT_GT(two.width, 0);
        EXPECT_GT(two.height, 0);
        EXPECT_EQ(four.width, 2 * two.width);
        EXPECT_EQ(four.height, two.height);
    }

    TEST_F(LuaUiGeometryTest, a_bigger_text_size_gives_a_bigger_extent)
    {
        const auto small = textRect(*this, "text = 'abab', textSize = 10");
        const auto large = textRect(*this, "text = 'abab', textSize = 30");
        EXPECT_GT(large.width, small.width);
        EXPECT_GT(large.height, small.height);
    }

    TEST_F(LuaUiGeometryTest, word_wrap_in_a_narrow_box_makes_more_lines)
    {
        const auto line = textRect(*this, "text = 'aa', textSize = 16, multiline = true, wordWrap = true");
        auto e = create(R"({ layer = 'Windows', type = ui.TYPE.Text, props = { text = 'aa aa aa aa', textSize = 16,
            multiline = true, wordWrap = true, autoSize = false, size = util.vector2(1, 1) } })");
        auto* text = dynamic_cast<MyGUI::TextBox*>(e->mRoot->widget());
        ASSERT_NE(text, nullptr);
        // one word per line once the box is narrower than two words
        EXPECT_EQ(text->getTextSize().height, 4 * line.height);
    }

    // The corpus: every data/*.lua file returns a list of { name, layout, steps }. The golden file next to it holds
    // the rectangles after creation and after each step.

    std::vector<std::string> corpusFiles()
    {
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(dataDir()))
            if (entry.path().extension() == ".lua")
                names.push_back(entry.path().stem().string());
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string readAll(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream out;
        out << stream.rdbuf();
        std::string text = out.str();
        // Windows checkouts convert line endings, the content is the same
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    std::string recordCorpusFile(const std::string& file)
    {
        // one harness per file: the earlier file's elements and textures do not leak into the next
        LuaUiTest::Harness harness;
        const std::string path = (dataDir() / (file + ".lua")).string();
        sol::object cases = harness.run(readAll(path), file);
        sol::table list = cases.as<sol::table>();

        std::ostringstream out;
        for (size_t i = 1; i <= list.size(); ++i)
        {
            sol::table item = list[i];
            const std::string name = item.get<std::string>("name");
            // debugging aid: record a single case
            if (const char* only = std::getenv("OPENEDEN_LUAUI_ONLY_CASE"); only != nullptr && name != only)
                continue;
            out << "## " << name << '\n';
            const bool sortSiblings = item.get_or("unorderedChildren", false);
            std::shared_ptr<LuaUi::Element> element = harness.createElement(item.get<sol::table>("layout"));
            out << LuaUiTest::recordGeometry(element->mRoot, sortSiblings);
            sol::optional<sol::table> steps = item.get<sol::optional<sol::table>>("steps");
            if (steps)
            {
                for (size_t step = 1; step <= steps->size(); ++step)
                {
                    sol::protected_function fn = (*steps)[step];
                    sol::protected_function_result result = fn();
                    if (!result.valid())
                        throw std::runtime_error("step failed: " + std::string(result.get<sol::error>().what()));
                    harness.updateElement(*element);
                    out << "# step " << step << '\n' << LuaUiTest::recordGeometry(element->mRoot, sortSiblings);
                }
            }
            LuaUi::Element::erase(element.get());
        }
        return out.str();
    }

    struct LuaUiCorpusTest : TestWithParam<std::string>
    {
    };

    TEST_P(LuaUiCorpusTest, rectangles_equal_the_recorded_golden)
    {
        const std::string file = GetParam();
        const std::string actual = recordCorpusFile(file);
        const fs::path golden = dataDir() / (file + ".golden.txt");
        if (std::getenv("OPENEDEN_RECORD_LUAUI_GOLDENS") != nullptr)
        {
            std::ofstream out(golden, std::ios::binary);
            out << actual;
            GTEST_SKIP() << "recorded " << golden.string();
        }
        ASSERT_TRUE(fs::exists(golden)) << "no golden for " << file;
        EXPECT_EQ(actual, readAll(golden));
    }

    TEST_P(LuaUiCorpusTest, recording_twice_gives_the_same_text)
    {
        const std::string file = GetParam();
        EXPECT_EQ(recordCorpusFile(file), recordCorpusFile(file));
    }

    INSTANTIATE_TEST_SUITE_P(LuaUiGeometryCorpus, LuaUiCorpusTest, ValuesIn(corpusFiles()),
        [](const TestParamInfo<std::string>& param) { return param.param; });
}
