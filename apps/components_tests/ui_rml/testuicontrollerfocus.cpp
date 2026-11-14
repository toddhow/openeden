// Task 0111 follow-ups (decision 0015, UI system U2): the focus entry (a configurable key or controller button moves
// the controller focus into the first `nav-capture` document and out of it again) and the clipboard, tested against a
// real RmlUi context with a render interface that draws nothing. No window, no GPU, no fonts. Written before the code
// (red against stubs first).
#include <components/ui_rml/clipboard.hpp>
#include <components/ui_rml/controllerfocus.hpp>
#include <components/ui_rml/systeminterface.hpp>

#include <gtest/gtest.h>

#include <RmlUi/Core.h>

#include <string>

namespace
{
    using namespace UiRml;

    // Draws nothing; textures are never loaded by the documents below.
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

    struct FakeClipboard final : ClipboardBackend
    {
        std::string mText;
        std::string get() override { return mText; }
        void set(const std::string& text) override { mText = text; }
    };

    // RmlUi keeps global state: one initialisation for the whole suite.
    struct UiRmlContextTest : testing::Test
    {
        static inline FakeClipboard sBackend;
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

        void SetUp() override
        {
            mContext = Rml::CreateContext("test", Rml::Vector2i(800, 600));
            ASSERT_NE(mContext, nullptr);
        }
        void TearDown() override { Rml::RemoveContext("test"); }

        Rml::ElementDocument* load(const char* markup)
        {
            Rml::ElementDocument* document = mContext->LoadDocumentFromMemory(markup);
            EXPECT_NE(document, nullptr);
            if (document != nullptr)
                document->Show();
            return document;
        }

        std::string idOfFocus() const
        {
            Rml::Element* focus = mContext->GetFocusElement();
            return focus != nullptr ? focus->GetId() : std::string();
        }
    };

    const char* const kNavPanel
        = "<rml><head><style>body { display: block; } div { display: block; }"
          " button { display: block; tab-index: auto; width: 100px; height: 20px; }</style></head>"
          "<body nav-capture=\"true\"><div>text</div><button id=\"first\">one</button>"
          "<button id=\"second\">two</button></body></rml>";

    TEST_F(UiRmlContextTest, a_nav_capture_document_is_found)
    {
        load(kNavPanel);
        mContext->Update();
        EXPECT_TRUE(hasNavCaptureDocument(*mContext));
    }

    TEST_F(UiRmlContextTest, a_context_without_documents_or_without_nav_capture_has_none)
    {
        EXPECT_FALSE(hasNavCaptureDocument(*mContext));
        load("<rml><body><button id=\"b\">x</button></body></rml>");
        mContext->Update();
        EXPECT_FALSE(hasNavCaptureDocument(*mContext));
    }

    TEST_F(UiRmlContextTest, a_hidden_nav_capture_document_does_not_count)
    {
        Rml::ElementDocument* document = load(kNavPanel);
        mContext->Update();
        document->Hide();
        mContext->Update();
        EXPECT_FALSE(hasNavCaptureDocument(*mContext));
    }

    TEST_F(UiRmlContextTest, the_entry_focuses_the_first_control_in_document_order)
    {
        load(kNavPanel);
        mContext->Update();
        EXPECT_EQ(idOfFocus(), "");
        EXPECT_TRUE(toggleControllerFocus(*mContext));
        EXPECT_EQ(idOfFocus(), "first");
    }

    TEST_F(UiRmlContextTest, the_second_entry_leaves_the_document_again)
    {
        load(kNavPanel);
        mContext->Update();
        toggleControllerFocus(*mContext);
        ASSERT_EQ(idOfFocus(), "first");
        EXPECT_TRUE(toggleControllerFocus(*mContext));
        EXPECT_EQ(idOfFocus(), "");
    }

    TEST_F(UiRmlContextTest, the_entry_from_a_control_that_was_moved_by_the_controller_still_leaves)
    {
        load(kNavPanel);
        mContext->Update();
        toggleControllerFocus(*mContext);
        mContext->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        mContext->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        mContext->Update();
        toggleControllerFocus(*mContext);
        EXPECT_EQ(idOfFocus(), "");
    }

    TEST_F(UiRmlContextTest, without_a_nav_capture_document_the_entry_does_nothing)
    {
        load("<rml><body><button id=\"b\">x</button></body></rml>");
        mContext->Update();
        EXPECT_FALSE(toggleControllerFocus(*mContext));
        EXPECT_EQ(idOfFocus(), "");
    }

    TEST_F(UiRmlContextTest, a_focused_control_of_a_document_without_nav_capture_is_not_left_by_the_entry)
    {
        load(
            "<rml><head><style>button { display: block; tab-index: auto; }</style></head><body><button "
            "id=\"plain\">x</button></body></rml>");
        load(kNavPanel);
        mContext->Update();
        mContext->GetDocument(0)->GetElementById("plain")->Focus();
        // the focus is in a document that did not ask for the controller: the entry goes to the panel that did
        EXPECT_TRUE(toggleControllerFocus(*mContext));
        EXPECT_EQ(idOfFocus(), "first");
    }

    TEST_F(UiRmlContextTest, the_entry_skips_controls_that_cannot_take_focus)
    {
        load(
            "<rml><head><style>body { display: block; } button { display: block; tab-index: auto; }</style></head>"
            "<body nav-capture=\"true\"><button id=\"off\" style=\"tab-index: none\">x</button>"
            "<button id=\"on\">y</button></body></rml>");
        mContext->Update();
        toggleControllerFocus(*mContext);
        EXPECT_EQ(idOfFocus(), "on");
    }

    // ---- the clipboard through RmlUi's own text input ----

    TEST_F(UiRmlContextTest, the_system_interface_copies_through_the_bridge)
    {
        sSystem.SetClipboardText("from rml");
        EXPECT_EQ(sBridge.textForUi(), "from rml");
        Rml::String back;
        sSystem.GetClipboardText(back);
        EXPECT_EQ(back, "from rml");
        sBridge.pump();
        EXPECT_EQ(sBackend.mText, "from rml");
    }

    TEST_F(UiRmlContextTest, control_v_pastes_what_the_bridge_holds_into_a_text_input)
    {
        load(
            "<rml><head><style>body { display: block; } input { display: block; width: 200px; height: 20px; }"
            "</style></head><body><input id=\"field\" type=\"text\" value=\"\"/></body></rml>");
        mContext->Update();
        Rml::Element* field = mContext->GetDocument(0)->GetElementById("field");
        ASSERT_NE(field, nullptr);
        field->Focus();
        sBackend.mText = "pasted";
        sBridge.refresh();
        mContext->ProcessKeyDown(Rml::Input::KI_V, Rml::Input::KM_CTRL);
        mContext->ProcessKeyUp(Rml::Input::KI_V, Rml::Input::KM_CTRL);
        mContext->Update();
        EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "pasted");
    }

    TEST_F(UiRmlContextTest, control_c_after_select_all_copies_the_text_to_the_bridge)
    {
        load(
            "<rml><head><style>body { display: block; } input { display: block; width: 200px; height: 20px; }"
            "</style></head><body><input id=\"field\" type=\"text\" value=\"hello\"/></body></rml>");
        mContext->Update();
        Rml::Element* field = mContext->GetDocument(0)->GetElementById("field");
        ASSERT_NE(field, nullptr);
        field->Focus();
        mContext->ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
        mContext->ProcessKeyUp(Rml::Input::KI_A, Rml::Input::KM_CTRL);
        mContext->ProcessKeyDown(Rml::Input::KI_C, Rml::Input::KM_CTRL);
        mContext->ProcessKeyUp(Rml::Input::KI_C, Rml::Input::KM_CTRL);
        mContext->Update();
        sBridge.pump();
        EXPECT_EQ(sBackend.mText, "hello");
    }
}
