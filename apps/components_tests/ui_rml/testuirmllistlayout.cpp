// Task 0166: the layout of a `ui.List` (files/data/openeden/ui.lua) on a real RmlUi context: its rows must take the
// width of the list wherever the list sits (in a block, a flex column, a flex row, a shrink-to-fit parent), without the
// script giving the list an explicit width. The protocol calls below are the ones the library makes for a List.
#include <components/ui_rml/clipboard.hpp>
#include <components/ui_rml/rmlprotocol.hpp>
#include <components/ui_rml/systeminterface.hpp>

#include <gtest/gtest.h>

#include <RmlUi/Core.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    using namespace UiRml;
    using Ui::ElementId;

    class NullRender final : public Rml::RenderInterface
    {
    public:
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
        {
            return 1;
        }
        void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
        void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
        Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
        void ReleaseTexture(Rml::TextureHandle) override {}
        void EnableScissorRegion(bool) override {}
        void SetScissorRegion(Rml::Rectanglei) override {}
    };

    struct NoClipboard final : ClipboardBackend
    {
        std::string get() override { return {}; }
        void set(const std::string&) override {}
    };

    struct UiRmlListLayoutTest : testing::Test
    {
        static inline NoClipboard sBackend;
        static inline ClipboardBridge sBridge{ sBackend };
        static inline SystemInterface sSystem{ &sBridge };
        static inline NullRender sRender;

        static void SetUpTestSuite()
        {
            Rml::SetSystemInterface(&sSystem);
            Rml::SetRenderInterface(&sRender);
            ASSERT_TRUE(Rml::Initialise());
        }
        static void TearDownTestSuite() { Rml::Shutdown(); }

        Rml::Context* mContext = nullptr;
        std::unique_ptr<RmlProtocol> mProtocol;
        unsigned mNext = 1;

        void SetUp() override
        {
            mContext = Rml::CreateContext("list-layout-test", Rml::Vector2i(800, 600));
            ASSERT_NE(mContext, nullptr);
            RmlProtocol::Options options;
            options.mLoadBaseSheet = false; // the in-memory defaults make divs blocks; no theme tokens are needed here
            mProtocol = std::make_unique<RmlProtocol>(*mContext, std::move(options));
        }
        void TearDown() override
        {
            mProtocol.reset();
            Rml::RemoveContext("list-layout-test");
        }

        ElementId id() { return Ui::makeElementId(1, mNext++); }

        ElementId element(const char* tag, ElementId parent, const char* cls = "")
        {
            const ElementId e = id();
            mProtocol->create(e, tag);
            if (*cls != 0)
                mProtocol->setClass(e, cls);
            mProtocol->insertBefore(parent, e, Ui::kNoElement);
            return e;
        }

        // What `ui.List` writes (files/data/openeden/ui.lua, ui.List): outer, body, and one row with a child.
        // Returns the row; `width` is the list's explicit width or empty.
        ElementId list(ElementId parent, const std::string& width = {})
        {
            const ElementId outer = element("div", parent, "list");
            mProtocol->setProperty(outer, "height", "52dp");
            mProtocol->setProperty(outer, "overflow-y", "auto");
            mProtocol->setProperty(outer, "width", width.empty() ? "100%" : width);
            const ElementId body = element("div", outer, "list-body");
            mProtocol->setProperty(body, "position", "relative");
            mProtocol->setProperty(body, "height", "192dp");
            const ElementId row = element("div", body, "list-row");
            mProtocol->setProperty(row, "position", "absolute");
            mProtocol->setProperty(row, "left", "0dp");
            mProtocol->setProperty(row, "width", width.empty() ? "100%" : width);
            mProtocol->setProperty(row, "height", "16dp");
            mProtocol->setProperty(row, "top", "0dp");
            mRowChild = element("div", row, "text");
            mBody = body;
            mOuter = outer;
            return row;
        }

        ElementId mRowChild = 0, mBody = 0, mOuter = 0;
        std::map<ElementId, Rml::Element*> mElements;

        Rml::Element* find(ElementId target)
        {
            // Elements are found by walking the document: ids are not exposed, so tag them with an id attribute.
            return mContext->GetDocument(0)->GetElementById("e" + std::to_string(target));
        }

        float widthOf(ElementId target)
        {
            Rml::Element* e = find(target);
            return e ? e->GetBox().GetSize(Rml::BoxArea::Border).x : -1.f;
        }

        // The list's content is taller than the list, so its scrollbar is shown: the rows take what is left beside it.
        // Before task 0166 an unstyled scrollbar took the whole width and every row was 0 wide.
        void expectRowsFillTheListBesideItsScrollbar(ElementId row)
        {
            const float scrollbar = documentScrollbarWidth();
            EXPECT_GT(widthOf(mBody), 1.f);
            EXPECT_FLOAT_EQ(widthOf(mBody), widthOf(mOuter) - scrollbar);
            EXPECT_FLOAT_EQ(widthOf(row), widthOf(mBody));
            EXPECT_FLOAT_EQ(widthOf(mRowChild), widthOf(mBody));
        }

        static float documentScrollbarWidth() { return 8.f; } // RmlProtocol::scrollbarDefaults

        void tagAndUpdate(std::initializer_list<ElementId> ids)
        {
            for (ElementId e : ids)
                mProtocol->setAttribute(e, "id", "e" + std::to_string(e));
            mContext->Update();
        }

        ElementId documentWith(const char* docWidth)
        {
            const ElementId doc = id();
            mProtocol->createDocument(doc, "d");
            mProtocol->setProperty(doc, "position", "absolute");
            mProtocol->setProperty(doc, "left", "10dp");
            mProtocol->setProperty(doc, "top", "10dp");
            if (*docWidth != 0)
                mProtocol->setProperty(doc, "width", docWidth);
            return doc;
        }
    };

    TEST_F(UiRmlListLayoutTest, ARowTakesTheListsWidthInAColumnOfAFixedWidthDocument)
    {
        const ElementId doc = documentWith("260dp");
        const ElementId column = element("div", doc, "column");
        mProtocol->setProperty(column, "display", "flex");
        mProtocol->setProperty(column, "flex-direction", "column");
        const ElementId row = list(column);
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_FLOAT_EQ(widthOf(mOuter), 260.f);
        expectRowsFillTheListBesideItsScrollbar(row);
    }

    TEST_F(UiRmlListLayoutTest, ARowTakesTheListsWidthInAColumnOfAShrinkToFitDocument)
    {
        const ElementId doc = documentWith("");
        const ElementId column = element("div", doc, "column");
        mProtocol->setProperty(column, "display", "flex");
        mProtocol->setProperty(column, "flex-direction", "column");
        const ElementId row = list(column);
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_GT(widthOf(mOuter), 1.f);
        expectRowsFillTheListBesideItsScrollbar(row);
    }

    TEST_F(UiRmlListLayoutTest, ARowTakesTheListsWidthInAFlexRowAndBesideAFixedSibling)
    {
        const ElementId doc = documentWith("300dp");
        const ElementId rowBox = element("div", doc, "row");
        mProtocol->setProperty(rowBox, "display", "flex");
        mProtocol->setProperty(rowBox, "flex-direction", "row");
        const ElementId side = element("div", rowBox, "side");
        mProtocol->setProperty(side, "width", "50dp");
        mProtocol->setProperty(side, "height", "20dp");
        const ElementId row = list(rowBox);
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_GT(widthOf(mOuter), 1.f);
        expectRowsFillTheListBesideItsScrollbar(row);
    }

    TEST_F(UiRmlListLayoutTest, ARowTakesTheListsWidthInAColumnThatAlignsItsItemsToTheStart)
    {
        const ElementId doc = documentWith("260dp");
        const ElementId column = element("div", doc, "column");
        mProtocol->setProperty(column, "display", "flex");
        mProtocol->setProperty(column, "flex-direction", "column");
        mProtocol->setProperty(column, "align-items", "flex-start");
        const ElementId row = list(column);
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_GT(widthOf(mOuter), 1.f);
        expectRowsFillTheListBesideItsScrollbar(row);
    }

    TEST_F(UiRmlListLayoutTest, AListThatDoesNotOverflowHasNoScrollbarAndFillsItsWidth)
    {
        const ElementId doc = documentWith("260dp");
        const ElementId row = list(doc);
        mProtocol->setProperty(mBody, "height", "10dp");
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_FLOAT_EQ(widthOf(mBody), 260.f);
        EXPECT_FLOAT_EQ(widthOf(row), 260.f);
    }

    TEST_F(UiRmlListLayoutTest, TheTemplateFileAndTheInMemoryDefaultsStyleTheScrollbarsTheSame)
    {
        std::ifstream file(
            std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / "ui" / "document.rml");
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        ASSERT_FALSE(text.empty());
        EXPECT_NE(text.find(RmlProtocol::scrollbarDefaults()), std::string::npos);
        EXPECT_NE(RmlProtocol::documentDefaults("").find(RmlProtocol::scrollbarDefaults()), std::string::npos);
        // the template's style block comes before the link to the base sheet, so the theme's own sizes win
        EXPECT_LT(text.find("<style>"), text.find("<link"));
    }

    TEST_F(UiRmlListLayoutTest, AnExplicitWidthStillWins)
    {
        const ElementId doc = documentWith("260dp");
        const ElementId column = element("div", doc, "column");
        mProtocol->setProperty(column, "display", "flex");
        mProtocol->setProperty(column, "flex-direction", "column");
        const ElementId row = list(column, "200dp");
        tagAndUpdate({ mOuter, mBody, row, mRowChild });
        EXPECT_FLOAT_EQ(widthOf(mOuter), 200.f);
        EXPECT_FLOAT_EQ(widthOf(row), 200.f); // an explicit width is taken as it is: the row does not look at the body
    }

}
