// Task 0188: a text element that wraps asks the font engine for one string per line and expects the meshes of all lines
// to end up in ONE mesh list (RmlUi's own engine appends to it). The bitmap font engine replaced the indices on every
// call and so drew only the glyphs of the first line. These tests run the engine inside a real RmlUi context.
#include <components/ui_rml/fontengine.hpp>
#include <components/ui_rml/systeminterface.hpp>

#include <gtest/gtest.h>

#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace UiRml;

    std::vector<char> makeFnt(const std::string& texName)
    {
        std::array<RawGlyph, 256> glyphs{};
        RawGlyph g{};
        g.mTopRightU = 0.5f;
        g.mBottomLeftV = 0.5f;
        g.mBottomRightU = 0.5f;
        g.mBottomRightV = 0.5f;
        g.mWidth = 8;
        g.mHeight = 10;
        g.mKerningLeft = 1;
        g.mKerningRight = 1;
        g.mAscent = 9;
        glyphs['A'] = g; // advance 10
        const float size = 16.f;
        const int one = 1;
        std::vector<char> out(12 + 284 + 256 * sizeof(RawGlyph), 0);
        std::memcpy(out.data(), &size, 4);
        std::memcpy(out.data() + 4, &one, 4);
        std::memcpy(out.data() + 8, &one, 4);
        std::memcpy(out.data() + 12, texName.data(), texName.size());
        std::memcpy(out.data() + 12 + 284, glyphs.data(), sizeof(RawGlyph) * 256);
        return out;
    }

    std::vector<char> makeTex()
    {
        const int w = 4, h = 4;
        std::vector<char> out(8 + std::size_t(w) * h * 4, char(-128));
        std::memcpy(out.data(), &w, 4);
        std::memcpy(out.data() + 4, &h, 4);
        return out;
    }

    struct Compiled
    {
        std::vector<Rml::Vertex> mVertices;
        std::vector<int> mIndices;
    };

    class RecordingRender final : public Rml::RenderInterface
    {
    public:
        std::vector<Compiled> mGeometry;
        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
        {
            mGeometry.push_back({ { vertices.begin(), vertices.end() }, { indices.begin(), indices.end() } });
            return Rml::CompiledGeometryHandle(mGeometry.size());
        }
        void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
        void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
        Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
        void ReleaseTexture(Rml::TextureHandle) override {}
        void EnableScissorRegion(bool) override {}
        void SetScissorRegion(Rml::Rectanglei) override {}
    };

    struct UiRmlWrappedText : testing::Test
    {
        static inline SystemInterface sSystem{ nullptr };
        static inline RecordingRender sRender;
        static inline std::map<std::string, std::vector<char>> sFiles;
        static inline std::unique_ptr<BitmapFontEngine> sEngine;

        static void SetUpTestSuite()
        {
            sFiles["fonts/sample.fnt"] = makeFnt("Sample_Tex");
            sFiles["fonts/Sample_Tex.tex"] = makeTex();
            Rml::SetSystemInterface(&sSystem);
            Rml::SetRenderInterface(&sRender);
            ASSERT_TRUE(Rml::Initialise());
            sEngine = std::make_unique<BitmapFontEngine>(
                Rml::GetFontEngineInterface(),
                [](const std::string& path, std::vector<char>& out) {
                    const auto it = sFiles.find(path);
                    if (it == sFiles.end())
                        return false;
                    out = it->second;
                    return true;
                },
                ToUTF8::WINDOWS_1252, nullptr);
            Rml::SetFontEngineInterface(sEngine.get());
            ASSERT_TRUE(sEngine->addBitmapFont("Bitmap", "fonts/sample.fnt"));
        }
        static void TearDownTestSuite()
        {
            Rml::Shutdown();
            sEngine.reset();
        }

        // Every quad must be referenced by an index, and no index may leave its mesh.
        static void expectEveryQuadIsDrawn(const Compiled& c, std::size_t quads)
        {
            ASSERT_EQ(c.mVertices.size(), quads * 4);
            std::set<int> used;
            for (const int i : c.mIndices)
            {
                ASSERT_GE(i, 0);
                ASSERT_LT(std::size_t(i), c.mVertices.size());
                used.insert(i);
            }
            EXPECT_EQ(used.size(), c.mVertices.size()) << "some glyph quads are never referenced by an index";
            EXPECT_EQ(c.mIndices.size(), quads * 6);
        }
    };

    TEST_F(UiRmlWrappedText, two_strings_into_one_mesh_list_are_appended_with_their_indices_offset)
    {
        Rml::Context* context = Rml::CreateContext("wrapped-direct", Rml::Vector2i(200, 200));
        ASSERT_NE(context, nullptr);
        const Rml::FontFaceHandle face
            = sEngine->GetFontFaceHandle("Bitmap", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 16);
        const Rml::TextShapingContext shaping{ "en" };
        Rml::TexturedMeshList meshes;
        const Rml::ColourbPremultiplied white(255, 255, 255, 255);
        sEngine->GenerateString(context->GetRenderManager(), face, 0, "AA", { 0.f, 10.f }, white, 1.f, shaping, meshes);
        sEngine->GenerateString(
            context->GetRenderManager(), face, 0, "AAA", { 0.f, 30.f }, white, 1.f, shaping, meshes);
        ASSERT_EQ(meshes.size(), 1u);
        Compiled c;
        for (const auto& v : meshes[0].mesh.vertices)
            c.mVertices.push_back(v);
        c.mIndices.assign(meshes[0].mesh.indices.begin(), meshes[0].mesh.indices.end());
        expectEveryQuadIsDrawn(c, 5);
        // the second string's quads come after the first's
        EXPECT_EQ(*std::max_element(c.mIndices.begin(), c.mIndices.end()), 19);
        Rml::RemoveContext("wrapped-direct");
    }

    TEST_F(UiRmlWrappedText, a_wrapped_text_element_draws_every_glyph_of_every_line)
    {
        sRender.mGeometry.clear();
        Rml::Context* context = Rml::CreateContext("wrapped-doc", Rml::Vector2i(400, 300));
        ASSERT_NE(context, nullptr);
        // 32 px wide: one word of three glyphs (30 px) fits, two do not, so the nine glyphs become three lines.
        Rml::ElementDocument* doc = context->LoadDocumentFromMemory(
            "<rml><body><div style=\"display: block; width: 32px; font-family: Bitmap; font-size: 16px;\">"
            "AAA AAA AAA</div></body></rml>");
        ASSERT_NE(doc, nullptr);
        doc->Show();
        context->Update();
        context->Render();
        ASSERT_FALSE(sRender.mGeometry.empty());
        const Compiled& text = *std::max_element(sRender.mGeometry.begin(), sRender.mGeometry.end(),
            [](const Compiled& a, const Compiled& b) { return a.mVertices.size() < b.mVertices.size(); });
        expectEveryQuadIsDrawn(text, 9);
        Rml::RemoveContext("wrapped-doc");
    }
}
