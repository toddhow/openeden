// Task 0104 (decision 0015, UI system U1): the font engine that draws Morrowind's bitmap fonts itself and hands every
// other face to the engine RmlUi brought. A fake fallback records what it is asked; the font files are generated here.
#include <components/ui_rml/fontengine.hpp>

#include <RmlUi/Core.h>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace UiRml;

    std::vector<char> makeFnt(float size, const std::string& texName)
    {
        std::array<RawGlyph, 256> glyphs{};
        auto set = [&](int i, float w, float h, float kl, float kr, float asc) {
            RawGlyph g{};
            g.mTopLeftU = 0.f;
            g.mTopLeftV = 0.f;
            g.mTopRightU = 0.5f;
            g.mTopRightV = 0.f;
            g.mBottomLeftU = 0.f;
            g.mBottomLeftV = 0.5f;
            g.mBottomRightU = 0.5f;
            g.mBottomRightV = 0.5f;
            g.mWidth = w;
            g.mHeight = h;
            g.mKerningLeft = kl;
            g.mKerningRight = kr;
            g.mAscent = asc;
            glyphs[std::size_t(i)] = g;
        };
        set('A', 8, 10, 1, 1, 9); // advance 9
        set('B', 6, 10, 0, 2, 9); // advance 8
        set('x', 5, 6, 0, 1, 6);
        std::vector<char> out(12 + 284 + 256 * sizeof(RawGlyph), 0);
        const int one = 1;
        std::memcpy(out.data(), &size, 4);
        std::memcpy(out.data() + 4, &one, 4);
        std::memcpy(out.data() + 8, &one, 4);
        std::memcpy(out.data() + 12, texName.data(), texName.size());
        std::memcpy(out.data() + 12 + 284, glyphs.data(), sizeof(RawGlyph) * 256);
        return out;
    }

    std::vector<char> makeTex(int w, int h)
    {
        std::vector<char> out(8 + std::size_t(w) * h * 4, char(0x80));
        std::memcpy(out.data(), &w, 4);
        std::memcpy(out.data() + 4, &h, 4);
        return out;
    }

    // The engine RmlUi would bring: records every call; hands out a recognisable handle.
    class FakeFallback final : public Rml::FontEngineInterface
    {
    public:
        static constexpr Rml::FontFaceHandle kHandle = 0x1000;
        int mShutdowns = 0;
        int mReleases = 0;
        std::vector<std::string> mLoadedFiles;
        std::vector<std::string> mLoadedFamilies;
        std::vector<std::pair<const Rml::byte*, std::size_t>> mLoadedMemory;
        std::vector<std::string> mHandlesAsked;
        Rml::FontMetrics mMetrics{};

        void Shutdown() override { ++mShutdowns; }
        void ReleaseFontResources() override { ++mReleases; }
        bool LoadFontFace(const Rml::String& file, int, bool, Rml::Style::FontWeight) override
        {
            mLoadedFiles.push_back(file);
            return true;
        }
        bool LoadFontFace(const Rml::String& file, int, const Rml::String& family, Rml::Style::FontStyle,
            Rml::Style::FontWeight, bool) override
        {
            mLoadedFiles.push_back(file);
            mLoadedFamilies.push_back(family);
            return true;
        }
        bool LoadFontFace(Rml::Span<const Rml::byte> data, int, const Rml::String& family, Rml::Style::FontStyle,
            Rml::Style::FontWeight, bool) override
        {
            mLoadedMemory.emplace_back(data.data(), data.size());
            mLoadedFamilies.push_back(family);
            return true;
        }
        Rml::FontFaceHandle GetFontFaceHandle(
            const Rml::String& family, Rml::Style::FontStyle, Rml::Style::FontWeight, int size) override
        {
            mHandlesAsked.push_back(family + ":" + std::to_string(size));
            return kHandle;
        }
        const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle) override
        {
            mMetrics.size = 777;
            return mMetrics;
        }
        int GetStringWidth(
            Rml::FontFaceHandle, Rml::StringView, const Rml::TextShapingContext&, Rml::Character) override
        {
            return 777;
        }
        int GetVersion(Rml::FontFaceHandle) override { return 5; }
    };

    struct Fixture
    {
        std::map<std::string, std::vector<char>> mFiles;
        FakeFallback mFallback;
        std::vector<std::string> mReads;
        BitmapFontEngine mEngine;

        Fixture()
            : mEngine(
                &mFallback,
                [this](const std::string& path, std::vector<char>& out) {
                    mReads.push_back(path);
                    const auto it = mFiles.find(path);
                    if (it == mFiles.end())
                        return false;
                    out = it->second;
                    return true;
                },
                ToUTF8::WINDOWS_1252, nullptr)
        {
            mFiles["fonts/sample.fnt"] = makeFnt(16.f, "Sample_Tex");
            mFiles["fonts/Sample_Tex.tex"] = makeTex(4, 4);
        }
    };

    Rml::TextShapingContext shaping()
    {
        static const Rml::String language = "en";
        return Rml::TextShapingContext{ language };
    }

    TEST(UiRmlFontEngine, a_bitmap_font_is_loaded_through_the_read_function_and_its_texture_by_the_name_in_the_file)
    {
        Fixture f;
        EXPECT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        EXPECT_TRUE(f.mEngine.hasBitmapFamily("Morrowind"));
        EXPECT_TRUE(f.mEngine.hasBitmapFamily("MORROWIND")); // family names are case insensitive
        EXPECT_FALSE(f.mEngine.hasBitmapFamily("other"));
        ASSERT_EQ(f.mReads.size(), 2u);
        EXPECT_EQ(f.mReads[0], "fonts/sample.fnt");
        EXPECT_EQ(f.mReads[1], "fonts/Sample_Tex.tex");
    }

    TEST(UiRmlFontEngine, a_missing_font_file_or_texture_or_a_bad_file_is_refused_and_leaves_no_family)
    {
        Fixture f;
        EXPECT_FALSE(f.mEngine.addBitmapFont("a", "fonts/missing.fnt"));
        f.mFiles.erase("fonts/Sample_Tex.tex");
        EXPECT_FALSE(f.mEngine.addBitmapFont("b", "fonts/sample.fnt"));
        f.mFiles["fonts/Sample_Tex.tex"] = makeTex(4, 4);
        f.mFiles["fonts/bad.fnt"] = std::vector<char>(50, 0);
        EXPECT_FALSE(f.mEngine.addBitmapFont("c", "fonts/bad.fnt"));
        for (const char* family : { "a", "b", "c" })
            EXPECT_FALSE(f.mEngine.hasBitmapFamily(family)) << family;
    }

    TEST(UiRmlFontEngine, a_face_of_our_family_is_ours_and_any_other_family_goes_to_the_fallback)
    {
        Fixture f;
        ASSERT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        const Rml::FontFaceHandle ours = f.mEngine.GetFontFaceHandle(
            "morrowind", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 16);
        EXPECT_NE(ours, 0u);
        EXPECT_NE(ours, FakeFallback::kHandle);
        EXPECT_TRUE(f.mFallback.mHandlesAsked.empty());
        const Rml::FontFaceHandle theirs = f.mEngine.GetFontFaceHandle(
            "Liberation", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 18);
        EXPECT_EQ(theirs, FakeFallback::kHandle);
        ASSERT_EQ(f.mFallback.mHandlesAsked.size(), 1u);
        EXPECT_EQ(f.mFallback.mHandlesAsked[0], "Liberation:18");
    }

    TEST(UiRmlFontEngine, one_family_at_one_size_is_one_face_and_another_size_is_another)
    {
        Fixture f;
        ASSERT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        const auto get = [&](int size) {
            return f.mEngine.GetFontFaceHandle(
                "Morrowind", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, size);
        };
        EXPECT_EQ(get(16), get(16));
        EXPECT_NE(get(16), get(32));
    }

    TEST(UiRmlFontEngine, metrics_and_string_width_come_from_the_bitmap_data_at_the_chosen_scale)
    {
        Fixture f;
        ASSERT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        const auto face = [&](int size) {
            return f.mEngine.GetFontFaceHandle(
                "Morrowind", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, size);
        };
        const Rml::FontFaceHandle one = face(16);
        const Rml::FontFaceHandle two = face(32);
        EXPECT_EQ(f.mEngine.GetFontMetrics(one).size, 16);
        EXPECT_FLOAT_EQ(f.mEngine.GetFontMetrics(one).ascent, 16.f);
        EXPECT_FLOAT_EQ(f.mEngine.GetFontMetrics(one).line_spacing, 16.f);
        EXPECT_EQ(f.mEngine.GetFontMetrics(two).size, 32);
        EXPECT_FLOAT_EQ(f.mEngine.GetFontMetrics(two).line_spacing, 32.f);
        EXPECT_FALSE(f.mEngine.GetFontMetrics(one).has_ellipsis);
        EXPECT_EQ(f.mEngine.GetStringWidth(one, "AB", shaping(), Rml::Character::Null), 17);
        EXPECT_EQ(f.mEngine.GetStringWidth(two, "AB", shaping(), Rml::Character::Null), 34);
        // the fallback's metrics and widths are not ours
        const Rml::FontFaceHandle other
            = f.mEngine.GetFontFaceHandle("Other", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 16);
        EXPECT_EQ(f.mEngine.GetFontMetrics(other).size, 777);
        EXPECT_EQ(f.mEngine.GetStringWidth(other, "AB", shaping(), Rml::Character::Null), 777);
    }

    TEST(UiRmlFontEngine, version_and_effects_of_our_faces_do_not_touch_the_fallback)
    {
        Fixture f;
        ASSERT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        const Rml::FontFaceHandle ours = f.mEngine.GetFontFaceHandle(
            "Morrowind", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 16);
        EXPECT_EQ(f.mEngine.GetVersion(ours), 0);
        EXPECT_EQ(f.mEngine.PrepareFontEffects(ours, Rml::FontEffectList{}), 0u);
        EXPECT_EQ(f.mEngine.GetVersion(FakeFallback::kHandle), 5); // not ours: forwarded
    }

    TEST(UiRmlFontEngine, loading_a_fnt_file_through_rmlui_adds_a_bitmap_family)
    {
        Fixture f;
        EXPECT_TRUE(f.mEngine.LoadFontFace(
            "fonts/sample.fnt", 0, "Morrowind", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, false));
        EXPECT_TRUE(f.mEngine.hasBitmapFamily("Morrowind"));
        EXPECT_TRUE(f.mFallback.mLoadedFiles.empty());
        EXPECT_TRUE(f.mFallback.mLoadedMemory.empty());
    }

    TEST(UiRmlFontEngine, a_truetype_file_is_read_through_the_vfs_and_kept_alive_for_the_fallback)
    {
        Fixture f;
        f.mFiles["fonts/Face.ttf"] = std::vector<char>{ 'T', 'T', 'F', '!' };
        EXPECT_TRUE(f.mEngine.LoadFontFace(
            "fonts/Face.ttf", 0, "Mono", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, false));
        ASSERT_EQ(f.mFallback.mLoadedMemory.size(), 1u);
        EXPECT_TRUE(f.mFallback.mLoadedFiles.empty());
        EXPECT_EQ(f.mFallback.mLoadedMemory[0].second, 4u);
        // RmlUi's rule: the memory must stay readable until after Shutdown
        EXPECT_EQ(std::memcmp(f.mFallback.mLoadedMemory[0].first, "TTF!", 4), 0);
        EXPECT_FALSE(f.mEngine.hasBitmapFamily("Mono"));
    }

    TEST(UiRmlFontEngine, a_truetype_file_the_vfs_does_not_have_is_left_to_the_fallback_by_name)
    {
        Fixture f;
        EXPECT_TRUE(f.mEngine.LoadFontFace(
            "C:/fonts/disk.ttf", 0, "Mono", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, false));
        EXPECT_EQ(f.mFallback.mLoadedFiles, (std::vector<std::string>{ "C:/fonts/disk.ttf" }));
        EXPECT_TRUE(f.mFallback.mLoadedMemory.empty());
    }

    TEST(UiRmlFontEngine, memory_fonts_and_fallback_faces_are_forwarded)
    {
        Fixture f;
        const std::vector<Rml::byte> data{ 1, 2, 3 };
        EXPECT_TRUE(f.mEngine.LoadFontFace(Rml::Span<const Rml::byte>(data.data(), data.size()), 0, "Mem",
            Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true));
        EXPECT_EQ(f.mFallback.mLoadedMemory.size(), 1u);
        EXPECT_TRUE(f.mEngine.LoadFontFace("x.ttf", 0, true, Rml::Style::FontWeight::Normal));
        EXPECT_EQ(f.mFallback.mLoadedFiles.back(), "x.ttf");
    }

    TEST(UiRmlFontEngine, shutdown_forgets_the_families_and_shuts_the_fallback_down_once)
    {
        Fixture f;
        ASSERT_TRUE(f.mEngine.addBitmapFont("Morrowind", "fonts/sample.fnt"));
        f.mEngine.Shutdown();
        EXPECT_EQ(f.mFallback.mShutdowns, 1);
        EXPECT_FALSE(f.mEngine.hasBitmapFamily("Morrowind"));
    }

    TEST(UiRmlFontEngine, releasing_font_resources_is_passed_on)
    {
        Fixture f;
        f.mEngine.ReleaseFontResources();
        EXPECT_EQ(f.mFallback.mReleases, 1);
    }
}
