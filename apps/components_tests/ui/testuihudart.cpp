// Task 0252: the frame art of the Skyrim-style combat HUD (files/data/openeden/hud/skyrimstyle.lua, the pictures made
// by tools/hudart/generate.py). The module over the headless UI backend: with `env.hasArt()` true the frames, fills and
// the emblem carry RCSS decorators, otherwise the plain boxes of task 0244 are drawn unchanged. Written from the task
// spec first and run red against the module of task 0244; the real RmlUi parse of the decorator strings is checked on a
// real context, the picture files by reading their PNG headers.
#include "uiluafixture.hpp"

#include <RmlUi/Core.h>

#include <components/ui_rml/clipboard.hpp>
#include <components/ui_rml/systeminterface.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace
{
    using UiTest::UiLuaFixture;

    struct UiHudArt : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                mod = require('openeden.hud.skyrimstyle')
                style = { isActive = true, p = { 80, 100, 50, 100, 25, 100, 12 }, b = { 0, 0, 0, 1, 0, 0 },
                    bossName = '', barData = {}, nameData = {} }
                function style.active() return style.isActive end
                function style.player() return style.p end
                function style.boss() return style.b end
                function style.boss_name() return style.bossName end
                function style.bars() return style.barData end
                function style.bar_names() return style.nameData end
                clock = 0
                artOn = true
                env = {
                  now = function() return clock end,
                  screen = function() return 1280, 720 end,
                  project = function(x, y, z) if x < 0 then return nil end return x / 1280, y / 720 end,
                  hasArt = function() return artOn end,
                }
                function bar(id, x, y, hp, hpmax, flags, ally)
                  return { id, x, y, 150, hp, hpmax, 40, 40, 50, 50, 7, ally and 1 or 0, flags or 0 }
                end
                function setBars(list)
                  local flat, names = {}, {}
                  for _, b in ipairs(list) do
                    for _, v in ipairs(b) do flat[#flat + 1] = v end
                    names[#names + 1] = b.name or 'Actor'
                  end
                  style.barData = flat
                  style.nameData = names
                end
                function build() hud = mod.create(ui, style, env); hud:update(); ui.update() end
                function frame() hud:update(); ui.update() end
            )"));
        }

        const Ui::HeadlessBackend::Node* first(const std::string& cls) const
        {
            const auto ids = withClass(cls);
            if (ids.empty())
                return nullptr;
            auto it = backend.nodes().find(ids.front());
            return it == backend.nodes().end() ? nullptr : &it->second;
        }

        std::string property(const std::string& cls, const std::string& name) const
        {
            const auto* node = first(cls);
            if (node == nullptr)
                return "<no such element>";
            auto it = node->mProperties.find(name);
            return it == node->mProperties.end() ? "<unset>" : it->second;
        }

        double pixels(const std::string& text) const { return std::atof(text.c_str()); }
    };

    TEST_F(UiHudArt, withoutHasArtThePlainBoxesAreDrawnAsBefore)
    {
        ASSERT_TRUE(run("artOn = nil; env.hasArt = nil; build()"));
        EXPECT_EQ(withClass("style-art-frame").size(), 0u);
        EXPECT_GT(withClass("style-backing").size(), 0u);
        EXPECT_GT(withClass("style-cap").size(), 0u);
        EXPECT_EQ(property("style-fill-health", "decorator"), "<unset>");
        EXPECT_NE(property("style-fill-health", "background-color"), "<unset>");
        EXPECT_EQ(property("style-emblem", "decorator"), "<unset>");
        EXPECT_EQ(pixels(property("style-emblem", "width")), 44.0);
    }

    TEST_F(UiHudArt, aMissingPictureSetFallsBackToThePlainBoxes)
    {
        ASSERT_TRUE(run("artOn = false; build()"));
        EXPECT_EQ(withClass("style-art-frame").size(), 0u);
        EXPECT_GT(withClass("style-backing").size(), 0u);
        EXPECT_EQ(property("style-fill-health", "decorator"), "<unset>");
    }

    TEST_F(UiHudArt, withArtEveryBarFrameIsOneThreeSliceDecoratorAndTheBoxesAreGone)
    {
        ASSERT_TRUE(run("build()"));
        // the player's three bars, the boss bar, and per actor slot a health bar and two small ones
        EXPECT_EQ(withClass("style-art-frame").size(), 3u + 1u + 8u * 3u);
        EXPECT_EQ(withClass("style-backing").size(), 0u);
        EXPECT_EQ(withClass("style-edge").size(), 0u);
        EXPECT_EQ(withClass("style-cap").size(), 0u);
        for (const auto id : withClass("style-art-frame"))
        {
            const std::string d = backend.nodes().at(id).mProperties.at("decorator");
            EXPECT_EQ(d.rfind("tiled-horizontal(", 0), 0u) << d;
            EXPECT_NE(d.find("/textures/openeden/hud/frame_h"), std::string::npos) << d;
        }
    }

    TEST_F(UiHudArt, theFrameHeightIsTheBarHeightPlusTheInsetAndHasPictures)
    {
        ASSERT_TRUE(run("build()"));
        EXPECT_EQ(property("style-art-frame", "height"), "24dp");
        const std::string d = get<std::string>("mod.frameDecorator(24)");
        EXPECT_EQ(d,
            "tiled-horizontal(/textures/openeden/hud/frame_h24_l.png, /textures/openeden/hud/frame_h24_c.png, "
            "/textures/openeden/hud/frame_h24_r.png)");
        // every height a bar can have is one the art ships
        for (const char* expr : { "mod.PLAYER_HEIGHT", "mod.PLAYER_SMALL_HEIGHT", "mod.BOSS_HEIGHT", "mod.ACTOR_HEIGHT",
                 "mod.ACTOR_SMALL_HEIGHT" })
        {
            const int h = get<int>(std::string(expr) + " + 2 * mod.ART_INSET");
            const bool listed = get<bool>(
                "(function(n) for _, v in ipairs(mod.ART_FRAME_HEIGHTS) do if v == n then "
                "return true end end return false end)("
                + std::to_string(h) + ")");
            EXPECT_TRUE(listed) << expr << " + inset = " << h;
        }
    }

    TEST_F(UiHudArt, fillsAreGradientPicturesNotColours)
    {
        ASSERT_TRUE(run("build()"));
        EXPECT_EQ(property("style-fill-health", "decorator"), "image(/textures/openeden/hud/fill_red.png)");
        EXPECT_EQ(property("style-fill-magicka", "decorator"), "image(/textures/openeden/hud/fill_blue.png)");
        EXPECT_EQ(property("style-fill-stamina", "decorator"), "image(/textures/openeden/hud/fill_green.png)");
        EXPECT_EQ(property("style-trail-health", "decorator"), "image(/textures/openeden/hud/fill_trail.png)");
        EXPECT_EQ(property("style-fill-health", "background-color"), "<unset>");
    }

    TEST_F(UiHudArt, theEmblemIsOnePictureAndTheFillWidthsAreUnchanged)
    {
        ASSERT_TRUE(run("build()"));
        EXPECT_EQ(property("style-emblem", "decorator"), "image(/textures/openeden/hud/emblem.png)");
        EXPECT_EQ(pixels(property("style-emblem", "width")), get<double>("mod.ART_EMBLEM"));
        EXPECT_DOUBLE_EQ(pixels(property("style-fill-health", "width")), get<double>("mod.PLAYER_WIDTH") * 0.8);
    }

    TEST_F(UiHudArt, theEndCapsOfTheArtMoveTheBarsInsideTheFrame)
    {
        ASSERT_TRUE(run("build()"));
        EXPECT_EQ(pixels(property("style-fill-health", "left")), get<double>("mod.ART_CAP"));
        EXPECT_EQ(pixels(property("style-fill-health", "top")), get<double>("mod.ART_INSET"));
        // the boss document hangs centred, with the caps on both sides
        ASSERT_TRUE(run("style.b = { 1, 50, 100, 5, 1, 0 }; style.bossName = 'X'; frame()"));
        bool seen = false;
        for (const auto& [id, node] : backend.nodes())
            if (node.mDocument && node.mName == "style-boss")
            {
                seen = true;
                EXPECT_EQ(pixels(node.mProperties.at("width")), get<double>("mod.BOSS_WIDTH + 2 * mod.ART_CAP"));
                EXPECT_EQ(pixels(node.mProperties.at("margin-left")),
                    get<double>("-math.floor(mod.BOSS_WIDTH / 2) - mod.ART_CAP"));
            }
        EXPECT_TRUE(seen);
    }

    TEST_F(UiHudArt, anActorBarIsRedForAFoeAndGreenForAnAllyAndIsCentredWithTheArtCaps)
    {
        ASSERT_TRUE(run("setBars({ bar(1, 640, 360, 50, 100, 0, false) }); build()"));
        EXPECT_EQ(property("style-actor-fill-1", "decorator"), "image(/textures/openeden/hud/fill_red.png)");
        bool seen = false;
        for (const auto& [id, node] : backend.nodes())
            if (node.mDocument && node.mName == "style-actor-1")
            {
                seen = true;
                EXPECT_EQ(pixels(node.mProperties.at("left")),
                    640.0 - get<double>("mod.ACTOR_WIDTH") / 2 - get<double>("mod.ART_CAP"));
            }
        EXPECT_TRUE(seen);
        ASSERT_TRUE(run("setBars({ bar(1, 640, 360, 50, 100, 0, true) }); frame()"));
        EXPECT_EQ(property("style-actor-fill-1", "decorator"), "image(/textures/openeden/hud/fill_green.png)");
    }

    TEST_F(UiHudArt, aStillFrameWithArtMakesNoProtocolCall)
    {
        ASSERT_TRUE(run("setBars({ bar(1, 640, 360, 50, 100, 3, false) }); build(); frame()"));
        const auto before = backend.callCount();
        ASSERT_TRUE(run("frame(); frame(); frame()"));
        EXPECT_EQ(backend.callCount(), before);
    }

    TEST_F(UiHudArt, everyPictureTheModuleNamesExistsAndHasTheSizeTheLayoutExpects)
    {
        const std::filesystem::path data = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data";
        ASSERT_TRUE(run("files = mod.artFiles()"));
        const int count = get<int>("#files");
        EXPECT_EQ(count, 1 + 5 * 3 + 4);
        auto size = [&](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            unsigned char head[24] = {};
            in.read(reinterpret_cast<char*>(head), 24);
            EXPECT_EQ(std::string(reinterpret_cast<char*>(head) + 1, 3), "PNG") << path;
            auto be = [&](int o) { return (head[o] << 24) | (head[o + 1] << 16) | (head[o + 2] << 8) | head[o + 3]; };
            return std::pair<int, int>(be(16), be(20));
        };
        for (int i = 1; i <= count; ++i)
        {
            const std::string name = get<std::string>("files[" + std::to_string(i) + "]");
            const auto path = data / name;
            ASSERT_TRUE(std::filesystem::exists(path)) << name;
            const auto [w, h] = size(path);
            if (name.find("frame_h") != std::string::npos)
            {
                const int height = std::atoi(name.c_str() + name.find("frame_h") + 7);
                EXPECT_EQ(h, height) << name;
                if (name[name.size() - 5] != 'c')
                {
                    EXPECT_EQ(w, get<int>("mod.ART_CAP")) << name;
                }
            }
            if (name.find("emblem") != std::string::npos)
            {
                EXPECT_EQ(w, get<int>("mod.ART_EMBLEM")) << name;
                EXPECT_EQ(h, get<int>("mod.ART_EMBLEM")) << name;
            }
        }
    }

    // The strings must be ones RmlUi's own property parser takes (a real context, no window).
    class NullRender final : public Rml::RenderInterface
    {
    public:
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
        {
            return 1;
        }
        void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
        void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
        Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String&) override
        {
            size = { 4, 4 };
            return 1;
        }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
        void ReleaseTexture(Rml::TextureHandle) override {}
        void EnableScissorRegion(bool) override {}
        void SetScissorRegion(Rml::Rectanglei) override {}
    };

    struct NoClipboard final : UiRml::ClipboardBackend
    {
        std::string get() override { return {}; }
        void set(const std::string&) override {}
    };

    TEST_F(UiHudArt, rmlUiAcceptsTheDecoratorStrings)
    {
        static NoClipboard backendClip;
        static UiRml::ClipboardBridge bridge{ backendClip };
        static UiRml::SystemInterface system{ &bridge };
        static NullRender render;
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&render);
        ASSERT_TRUE(Rml::Initialise());
        Rml::Context* context = Rml::CreateContext("hudart-test", Rml::Vector2i(800, 600));
        ASSERT_NE(context, nullptr);
        Rml::ElementDocument* doc = context->CreateDocument();
        ASSERT_NE(doc, nullptr);
        Rml::Element* e = doc->AppendChild(doc->CreateElement("div"));
        for (const std::string expr :
            { "mod.frameDecorator(24)", "mod.frameDecorator(11)", "mod.fillDecorator('red')", "mod.emblemDecorator()" })
        {
            const std::string text = get<std::string>(expr);
            EXPECT_TRUE(e->SetProperty("decorator", text)) << text;
            EXPECT_NE(e->GetProperty("decorator"), nullptr) << text;
        }
        Rml::RemoveContext("hudart-test");
        Rml::Shutdown();
    }
}
