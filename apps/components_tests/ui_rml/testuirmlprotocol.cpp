// Task 0162: the element protocol over a real RmlUi context with a render interface that draws nothing. No window, no
// GPU, no fonts. Written after the implementation (components/ui_rml/rmlprotocol.cpp) and checked by mutation.
#include <components/ui_rml/clipboard.hpp>
#include <components/ui_rml/rmlprotocol.hpp>
#include <components/ui_rml/systeminterface.hpp>

#include <gtest/gtest.h>

#include <filesystem>

#include <RmlUi/Core.h>

namespace
{
    using namespace UiRml;
    using Ui::ElementId;
    using Ui::EventKind;

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

    struct UiRmlProtocolTest : testing::Test
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
        std::vector<Ui::Event> mEvents;
        std::unique_ptr<RmlProtocol> mProtocol;

        void SetUp() override
        {
            mContext = Rml::CreateContext("protocol-test", Rml::Vector2i(800, 600));
            ASSERT_NE(mContext, nullptr);
            RmlProtocol::Options options;
            options.mEventSink = [this](Ui::Event&& e) { mEvents.push_back(std::move(e)); };
            mProtocol = std::make_unique<RmlProtocol>(*mContext, std::move(options));
        }
        void TearDown() override
        {
            mProtocol.reset();
            Rml::RemoveContext("protocol-test");
        }

        static constexpr ElementId kDoc = Ui::makeElementId(1, 1);
        static constexpr ElementId kA = Ui::makeElementId(1, 2);
        static constexpr ElementId kB = Ui::makeElementId(1, 3);
        static constexpr ElementId kC = Ui::makeElementId(1, 4);

        Rml::ElementDocument* document() { return mContext->GetNumDocuments() ? mContext->GetDocument(0) : nullptr; }
    };

    TEST_F(UiRmlProtocolTest, ADocumentIsOneRmlDocumentAndClosesWithDestroy)
    {
        mProtocol->createDocument(kDoc, "main");
        ASSERT_EQ(mContext->GetNumDocuments(), 1);
        EXPECT_EQ(mProtocol->elementCount(), 1u);
        mProtocol->destroy(kDoc);
        mContext->Update();
        EXPECT_EQ(mContext->GetNumDocuments(), 0);
        EXPECT_EQ(mProtocol->elementCount(), 0u);
    }

    TEST_F(UiRmlProtocolTest, ADocumentIsLoadedFromTheTemplateFileWhenThereIsOne)
    {
        RmlProtocol::Options options;
        options.mDocumentFile = (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / "ui" / "document.rml")
                                    .generic_string();
        RmlProtocol protocol(*mContext, std::move(options));
        protocol.createDocument(kDoc, "from-file");
        ASSERT_EQ(mContext->GetNumDocuments(), 1);
        EXPECT_EQ(mContext->GetDocument(0)->GetTitle(), "from-file");
        EXPECT_EQ(mContext->GetDocument(0)->GetSourceURL().empty(), false);
        protocol.destroy(kDoc);
    }

    TEST_F(UiRmlProtocolTest, ATemplateFileThatCannotBeLoadedFallsBackToMemory)
    {
        RmlProtocol::Options options;
        options.mDocumentFile = "no/such/document.rml";
        RmlProtocol protocol(*mContext, std::move(options));
        protocol.createDocument(kDoc, "fallback");
        EXPECT_EQ(mContext->GetNumDocuments(), 1);
        EXPECT_EQ(mContext->GetDocument(0)->GetTitle(), "fallback");
        protocol.destroy(kDoc);
    }

    TEST_F(UiRmlProtocolTest, ElementsAreCreatedDetachedAndAttachedInOrder)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "span");
        mProtocol->create(kC, "div");
        EXPECT_EQ(document()->GetNumChildren(), 0);
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kDoc, kB, Ui::kNoElement);
        mProtocol->insertBefore(kDoc, kC, kB);
        ASSERT_EQ(document()->GetNumChildren(), 3);
        EXPECT_EQ(document()->GetChild(0)->GetTagName(), "div");
        EXPECT_EQ(document()->GetChild(1)->GetTagName(), "div"); // kC, before kB
        EXPECT_EQ(document()->GetChild(2)->GetTagName(), "span");
    }

    TEST_F(UiRmlProtocolTest, AnAttachedElementCanBeMovedAndDetached)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kDoc, kB, Ui::kNoElement);
        mProtocol->insertBefore(kA, kB, Ui::kNoElement); // move into A
        EXPECT_EQ(document()->GetNumChildren(), 1);
        EXPECT_EQ(document()->GetChild(0)->GetNumChildren(), 1);
        mProtocol->detach(kB);
        EXPECT_EQ(document()->GetChild(0)->GetNumChildren(), 0);
        mProtocol->insertBefore(kDoc, kB, Ui::kNoElement); // and it can be attached again
        EXPECT_EQ(document()->GetNumChildren(), 2);
    }

    TEST_F(UiRmlProtocolTest, AnElementCannotBecomeItsOwnAncestor)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kA, kB, Ui::kNoElement);
        mProtocol->insertBefore(kB, kA, Ui::kNoElement); // refused
        EXPECT_EQ(document()->GetNumChildren(), 1);
        EXPECT_EQ(document()->GetChild(0)->GetNumChildren(), 1);
        mProtocol->insertBefore(kA, kA, Ui::kNoElement); // refused
    }

    TEST_F(UiRmlProtocolTest, TextIsTextNotMarkup)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->setText(kA, "a <b>bold</b> & more");
        Rml::Element* a = document()->GetChild(0);
        EXPECT_EQ(a->GetNumChildren(), 1); // one text node, no <b> element
        EXPECT_EQ(a->GetInnerRML(), "a &lt;b&gt;bold&lt;/b&gt; &amp; more");
    }

    TEST_F(UiRmlProtocolTest, ClassPropertyAndAttributeReachTheElement)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->setClass(kA, "one two");
        mProtocol->setProperty(kA, "width", "12dp");
        mProtocol->setAttribute(kA, "data-x", "y");
        Rml::Element* a = document()->GetChild(0);
        EXPECT_TRUE(a->IsClassSet("one"));
        EXPECT_TRUE(a->IsClassSet("two"));
        ASSERT_NE(a->GetProperty("width"), nullptr);
        EXPECT_EQ(a->GetProperty("width")->ToString(), "12dp");
        EXPECT_EQ(a->GetAttribute<Rml::String>("data-x", ""), "y");
        mProtocol->setClass(kA, "three");
        EXPECT_FALSE(a->IsClassSet("one"));
        EXPECT_TRUE(a->IsClassSet("three"));
    }

    TEST_F(UiRmlProtocolTest, PropertiesWorkOnTheDocumentItself)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->setProperty(kDoc, "pointer-events", "none");
        ASSERT_NE(document()->GetProperty("pointer-events"), nullptr);
        EXPECT_EQ(document()->GetProperty("pointer-events")->ToString(), "none");
    }

    TEST_F(UiRmlProtocolTest, TheDefaultStyleMakesDivsBlockAndNamesTheFont)
    {
        const std::string css = RmlProtocol::documentDefaults("Morrowind");
        EXPECT_NE(css.find("display: block"), std::string::npos);
        EXPECT_NE(css.find("font-family: Morrowind"), std::string::npos);
        EXPECT_EQ(RmlProtocol::documentDefaults("").find("font-family"), std::string::npos);
    }

    TEST_F(UiRmlProtocolTest, ClickChangeAndScrollEventsReachTheSinkWithTheirIds)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "input");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kDoc, kB, Ui::kNoElement);
        mProtocol->listen(kA, EventKind::Click, true);
        mProtocol->listen(kB, EventKind::Change, true);
        Rml::Element* a = document()->GetChild(0);
        Rml::Element* b = document()->GetChild(1);
        a->DispatchEvent("click", Rml::Dictionary());
        Rml::Dictionary parameters;
        parameters["value"] = Rml::Variant(Rml::String("typed"));
        b->DispatchEvent("change", parameters);
        ASSERT_EQ(mEvents.size(), 2u);
        EXPECT_EQ(mEvents[0].mTarget, kA);
        EXPECT_EQ(mEvents[0].mKind, EventKind::Click);
        EXPECT_EQ(mEvents[1].mTarget, kB);
        EXPECT_EQ(mEvents[1].mKind, EventKind::Change);
        EXPECT_EQ(mEvents[1].mValue, "typed");
    }

    TEST_F(UiRmlProtocolTest, AClickOnAChildBubblesToTheListeningParent)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kA, kB, Ui::kNoElement);
        mProtocol->listen(kA, EventKind::Click, true);
        document()->GetChild(0)->GetChild(0)->DispatchEvent("click", Rml::Dictionary());
        ASSERT_EQ(mEvents.size(), 1u);
        EXPECT_EQ(mEvents[0].mTarget, kA);
    }

    TEST_F(UiRmlProtocolTest, StoppingToListenStopsTheEvents)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->listen(kA, EventKind::Click, true);
        mProtocol->listen(kA, EventKind::Click, true); // twice is one listener
        mProtocol->listen(kA, EventKind::Click, false);
        document()->GetChild(0)->DispatchEvent("click", Rml::Dictionary());
        EXPECT_TRUE(mEvents.empty());
    }

    TEST_F(UiRmlProtocolTest, DestroyingAnElementRemovesItAndItsDescendantsFromBothSides)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->create(kB, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->insertBefore(kA, kB, Ui::kNoElement);
        EXPECT_EQ(mProtocol->elementCount(), 3u);
        mProtocol->destroy(kA);
        EXPECT_EQ(document()->GetNumChildren(), 0);
        EXPECT_EQ(mProtocol->elementCount(), 1u);
        mProtocol->setText(kB, "ignored"); // gone: no crash
        mProtocol->destroy(kB);
    }

    TEST_F(UiRmlProtocolTest, DestroyingADetachedElementFreesIt)
    {
        mProtocol->create(kA, "div");
        EXPECT_EQ(mProtocol->elementCount(), 1u);
        mProtocol->destroy(kA);
        EXPECT_EQ(mProtocol->elementCount(), 0u);
    }

    TEST_F(UiRmlProtocolTest, UnknownIdsAndDuplicateCreatesAreIgnored)
    {
        const ElementId ghost = Ui::makeElementId(1, 99);
        mProtocol->setText(ghost, "x");
        mProtocol->setClass(ghost, "x");
        mProtocol->setProperty(ghost, "width", "1dp");
        mProtocol->setAttribute(ghost, "a", "b");
        mProtocol->listen(ghost, EventKind::Click, true);
        mProtocol->focus(ghost);
        mProtocol->detach(ghost);
        mProtocol->insertBefore(ghost, ghost, Ui::kNoElement);
        mProtocol->destroy(ghost);
        mProtocol->create(kA, "div");
        mProtocol->create(kA, "div");
        EXPECT_EQ(mProtocol->elementCount(), 1u);
    }

    TEST_F(UiRmlProtocolTest, ABadPropertyIsRefusedWithoutHarm)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->setProperty(kDoc, "no-such-property", "1");
        mProtocol->setProperty(kDoc, "width", "banana");
        EXPECT_EQ(mContext->GetNumDocuments(), 1);
    }

    TEST_F(UiRmlProtocolTest, DocumentsLeftOpenAreClosedWithTheProtocol)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol.reset();
        mContext->Update();
        EXPECT_EQ(mContext->GetNumDocuments(), 0);
    }

    TEST_F(UiRmlProtocolTest, FocusMovesTheContextFocus)
    {
        mProtocol->createDocument(kDoc, "main");
        mProtocol->create(kA, "div");
        mProtocol->insertBefore(kDoc, kA, Ui::kNoElement);
        mProtocol->setAttribute(kA, "tabindex", "auto");
        mContext->Update();
        mProtocol->focus(kA);
        EXPECT_EQ(mContext->GetFocusElement(), document()->GetChild(0));
    }
}
