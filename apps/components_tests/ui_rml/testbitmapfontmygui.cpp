// Task 0104 (decision 0015, note 0045 15.4 and 15.10): characterisation of the bitmap font glyph map against the
// MyGUI font loader it replaces. Two checks:
//  1. the substitution and omitted tables and the Polish hack of components/ui_rml/codepage.cpp equal the ones in the
//     source of components/fontloader/fontloader.cpp (read as text; they are in an anonymous namespace there);
//  2. a MyGUI::ResourceManualFont filled the way FontLoader::loadBitmapFont fills it (the same <Code> entries in the
//  same
//     order, with the glyph byte in the `advance` attribute to tell glyphs apart) answers getGlyphInfo exactly as
//     UiRml::GlyphMap::lookup does, for every code point of the three code pages, so that which glyph wins when two
//     claim one code point (MyGUI's rule) is the rule the new engine uses. Czech and the other pages that do not
//     fit the font are inherited behaviour, not corrected here.
#include <components/ui_rml/bitmapfont.hpp>
#include <components/ui_rml/codepage.hpp>

#include <MyGUI_FontData.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManualFont.h>
#include <MyGUI_Version.h>
#include <MyGUI_XmlDocument.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace
{
    using namespace UiRml;

    std::string fontLoaderSource()
    {
        const std::filesystem::path path = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / ".." / ".."
            / "components" / "fontloader" / "fontloader.cpp";
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    TEST(UiRmlMyGuiParity, the_substitution_table_equals_the_one_in_the_font_loader_source)
    {
        const std::string source = fontLoaderSource();
        ASSERT_FALSE(source.empty()) << "fontloader.cpp was not found next to the shader sources";
        const std::regex re(R"(additional\.emplace\((\d+), (0x[0-9A-Fa-f]+)\);)");
        std::vector<std::pair<unsigned, unsigned>> fromSource;
        for (std::sregex_iterator it(source.begin(), source.end(), re), end; it != end; ++it)
            fromSource.emplace_back(unsigned(std::stoul((*it)[1])), unsigned(std::stoul((*it)[2], nullptr, 16)));
        ASSERT_EQ(fromSource.size(), substitutions().size());
        for (std::size_t i = 0; i < fromSource.size(); ++i)
        {
            EXPECT_EQ(fromSource[i].first, substitutions()[i].mGlyph) << "entry " << i;
            EXPECT_EQ(fromSource[i].second, substitutions()[i].mCodePoint) << "entry " << i;
        }
    }

    TEST(UiRmlMyGuiParity, the_omitted_code_points_equal_the_ones_in_the_font_loader_source)
    {
        const std::string source = fontLoaderSource();
        ASSERT_FALSE(source.empty());
        const std::regex re(R"(omitted\.push_back\((0x[0-9A-Fa-f]+)\);)");
        std::vector<unsigned> fromSource;
        for (std::sregex_iterator it(source.begin(), source.end(), re), end; it != end; ++it)
            fromSource.push_back(unsigned(std::stoul((*it)[1], nullptr, 16)));
        ASSERT_EQ(fromSource.size(), omittedCodePoints().size());
        for (std::size_t i = 0; i < fromSource.size(); ++i)
            EXPECT_EQ(fromSource[i], omittedCodePoints()[i]) << "entry " << i;
    }

    TEST(UiRmlMyGuiParity, the_polish_hack_equals_the_one_in_the_font_loader_source)
    {
        const std::string source = fontLoaderSource();
        ASSERT_FALSE(source.empty());
        // case 0x80: return 0xc6; ... default: return c;  (inside mapUtf8Char)
        const std::size_t start = source.find("unsigned char mapUtf8Char");
        ASSERT_NE(start, std::string::npos);
        const std::size_t end = source.find("default:", start);
        ASSERT_NE(end, std::string::npos);
        const std::string body = source.substr(start, end - start);
        const std::regex re(R"(case (0x[0-9a-fA-F]+):\s*return (0x[0-9a-fA-F]+);)");
        std::set<unsigned> swapped;
        for (std::sregex_iterator it(body.begin(), body.end(), re), last; it != last; ++it)
        {
            const unsigned from = unsigned(std::stoul((*it)[1], nullptr, 16));
            const unsigned to = unsigned(std::stoul((*it)[2], nullptr, 16));
            EXPECT_EQ(to, unsigned(polishSwap(static_cast<unsigned char>(from)))) << std::hex << from;
            swapped.insert(from);
        }
        EXPECT_EQ(swapped.size(), 33u); // the cases the source lists
        for (unsigned c = 0; c < 256; ++c)
        {
            if (!swapped.contains(c))
            {
                EXPECT_EQ(unsigned(polishSwap(static_cast<unsigned char>(c))), c) << std::hex << c;
            }
        }
    }

    // MyGUI's font resource asks the render manager singleton for its texture. A render manager with one fake texture
    // is enough: only the glyph tables are looked at here.
    class FakeTexture final : public MyGUI::ITexture
    {
    public:
        const std::string& getName() const override { return mName; }
        void createManual(int, int, MyGUI::TextureUsage, MyGUI::PixelFormat) override {}
        void loadFromFile(const std::string&) override {}
        void saveToFile(const std::string&) override {}
        void setShader(const std::string&) override {}
        void destroy() override {}
        void* lock(MyGUI::TextureUsage) override { return nullptr; }
        void unlock() override {}
        bool isLocked() const override { return false; }
        int getWidth() const override { return 256; }
        int getHeight() const override { return 256; }
        MyGUI::PixelFormat getFormat() const override { return MyGUI::PixelFormat::R8G8B8A8; }
        MyGUI::TextureUsage getUsage() const override { return MyGUI::TextureUsage::Static; }
        size_t getNumElemBytes() const override { return 4; }

    private:
        std::string mName = "fake";
    };

    class FakeRenderManager final : public MyGUI::RenderManager
    {
    public:
        MyGUI::IVertexBuffer* createVertexBuffer() override { return nullptr; }
        void destroyVertexBuffer(MyGUI::IVertexBuffer*) override {}
        MyGUI::ITexture* createTexture(const std::string&) override { return &mTexture; }
        void destroyTexture(MyGUI::ITexture*) override {}
        MyGUI::ITexture* getTexture(const std::string&) override { return &mTexture; }
        const MyGUI::IntSize& getViewSize() const override { return mSize; }
        MyGUI::VertexColourType getVertexFormat() const override { return MyGUI::VertexColourType::ColourARGB; }
        void setViewSize(int, int) override {}
        void registerShader(const std::string&, const std::string&, const std::string&) override {}

    private:
        FakeTexture mTexture;
        MyGUI::IntSize mSize{ 1280, 720 };
    };

    // Fills a ResourceManualFont as FontLoader::loadBitmapFont does (without the texture): code order, attributes.
    // The glyph byte is stored as the advance (+1, so that no glyph has advance 0, which the omitted entries have).
    std::unique_ptr<MyGUI::ResourceManualFont> mygui(ToUTF8::FromType gameEncoding)
    {
        const ToUTF8::FromType encoding = fontEncoding(gameEncoding);
        ToUTF8::Utf8Encoder encoder(encoding);
        MyGUI::xml::Document document;
        MyGUI::xml::ElementPtr root = document.createRoot("ResourceManualFont");
        root->addAttribute("name", "TestFont");
        MyGUI::xml::ElementPtr height = root->createChild("Property");
        height->addAttribute("key", "DefaultHeight");
        height->addAttribute("value", 16);
        MyGUI::xml::ElementPtr codes = root->createChild("Codes");

        const auto add = [&](unsigned index, unsigned glyph, bool empty) {
            MyGUI::xml::ElementPtr code = codes->createChild("Code");
            code->addAttribute("index", index);
            code->addAttribute("coord", "0 0 8 8");
            code->addAttribute("advance", empty ? 0.f : float(glyph + 1));
            code->addAttribute("bearing", "0 0");
            code->addAttribute("size", empty ? "0 0" : "8 8");
        };
        for (unsigned glyph = 0; glyph < 256; ++glyph)
        {
            add(unsigned(legacyToUnicode(static_cast<unsigned char>(glyph), encoder, encoding)), glyph, false);
            for (const Substitution& substitution : substitutions())
                if (substitution.mGlyph == glyph)
                    add(substitution.mCodePoint, glyph, false);
        }
        for (const unsigned codePoint : omittedCodePoints())
            add(codePoint, 0, true);

        static MyGUI::LogManager sLog; // MyGUI's assertions write to it (without one they recurse)
        static FakeRenderManager sRender;
        auto font = std::make_unique<MyGUI::ResourceManualFont>();
        font->deserialization(root, MyGUI::Version(3, 2, 0));
        return font;
    }

    class UiRmlMyGuiGlyphs : public testing::TestWithParam<ToUTF8::FromType>
    {
    };

    TEST_P(UiRmlMyGuiGlyphs, getGlyphInfo_answers_as_the_glyph_map_for_every_code_point_of_the_page)
    {
        const ToUTF8::FromType encoding = GetParam();
        const auto font = mygui(encoding);
        const GlyphMap map(encoding, DuplicatePolicy::FirstWins);
        std::set<char32_t> checked;
        const auto check = [&](char32_t codePoint) {
            if (!checked.insert(codePoint).second)
                return;
            const MyGUI::GlyphInfo* info = font->getGlyphInfo(MyGUI::Char(codePoint));
            const int expected = map.lookup(codePoint);
            if (expected == GlyphMap::kHidden)
            {
                // an omitted code point has an empty glyph, an unmapped one none at all: both draw nothing
                if (info != nullptr)
                {
                    EXPECT_EQ(info->advance, 0.f) << "hidden code point " << std::hex << unsigned(codePoint);
                    EXPECT_EQ(info->width, 0.f) << std::hex << unsigned(codePoint);
                }
            }
            else
            {
                ASSERT_NE(info, nullptr) << std::hex << unsigned(codePoint);
                EXPECT_EQ(info->advance, float(expected + 1))
                    << "code point " << std::hex << unsigned(codePoint) << " glyph " << std::dec << expected;
            }
        };
        for (char32_t cp = 0; cp < 0x2200; ++cp) // the Latin, Greek, Cyrillic and punctuation blocks the pages reach
            check(cp);
        for (const Substitution& substitution : substitutions())
            check(substitution.mCodePoint);
        check(U'中'); // not in any page: MyGUI has no substitute glyph
        check(U'\U0001F600');
    }

    INSTANTIATE_TEST_SUITE_P(
        Pages, UiRmlMyGuiGlyphs, testing::Values(ToUTF8::WINDOWS_1252, ToUTF8::WINDOWS_1250, ToUTF8::WINDOWS_1251));
}
