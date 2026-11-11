// Task 0104 (decision 0015, UI system U1): Morrowind's bitmap fonts as plain data: the file readers, the code point
// map, the layout of a string and the size choice. Written from the spec before the implementation; the files are
// generated here (no game data).
#include <components/ui_rml/bitmapfont.hpp>
#include <components/ui_rml/codepage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace UiRml;

    // A .fnt file: float size, int 1, int 1, 284 name bytes, 256 glyph records of 14 floats.
    std::vector<char> makeFnt(
        float size, const std::string& texName, const std::array<RawGlyph, 256>& glyphs, int one1 = 1, int one2 = 1)
    {
        std::vector<char> out(12 + 284 + 256 * sizeof(RawGlyph), 0);
        std::memcpy(out.data(), &size, 4);
        std::memcpy(out.data() + 4, &one1, 4);
        std::memcpy(out.data() + 8, &one2, 4);
        std::memcpy(out.data() + 12, texName.data(), texName.size());
        std::memcpy(out.data() + 12 + 284, glyphs.data(), sizeof(RawGlyph) * 256);
        return out;
    }

    RawGlyph glyph(float width, float height, float kerningLeft, float kerningRight, float ascent)
    {
        RawGlyph g{};
        g.mTopLeftU = 0.25f;
        g.mTopLeftV = 0.5f;
        g.mTopRightU = 0.5f;
        g.mTopRightV = 0.5f;
        g.mBottomLeftU = 0.25f;
        g.mBottomLeftV = 0.75f;
        g.mBottomRightU = 0.5f;
        g.mBottomRightV = 0.75f;
        g.mWidth = width;
        g.mHeight = height;
        g.mKerningLeft = kerningLeft;
        g.mKerningRight = kerningRight;
        g.mAscent = ascent;
        return g;
    }

    FontFile sampleFile()
    {
        FontFile f;
        f.mSize = 16.f;
        f.mTextureName = "Sample";
        for (RawGlyph& g : f.mGlyphs)
            g = glyph(0, 0, 0, 0, 0);
        f.mGlyphs['A'] = glyph(8, 10, 1, 1, 9); // advance 9
        f.mGlyphs['B'] = glyph(6, 10, 0, 2, 9); // advance 8
        f.mGlyphs[' '] = glyph(0, 0, 0, 5, 0); // advance 5
        f.mGlyphs['x'] = glyph(5, 6, 0, 1, 6);
        f.mGlyphs['_'] = glyph(7, 2, 0, 1, -1); // the not-defined glyph, advance 8
        return f;
    }

    std::vector<char> makeTex(int w, int h, std::size_t bytes)
    {
        std::vector<char> out(8 + bytes, 0);
        std::memcpy(out.data(), &w, 4);
        std::memcpy(out.data() + 4, &h, 4);
        for (std::size_t i = 0; i < bytes; ++i)
            out[8 + i] = static_cast<char>(i * 7 + 3);
        return out;
    }

    TEST(UiRmlFontFile, reads_size_texture_name_and_all_glyph_records)
    {
        std::array<RawGlyph, 256> glyphs{};
        glyphs[65] = glyph(8, 10, 1, 2, 9);
        glyphs[255] = glyph(3, 4, 5, 6, 7);
        const std::vector<char> bytes = makeFnt(16.f, "Magic_Cards_Regular", glyphs);
        const FontFile f = parseFontFile(bytes, "fonts/test.fnt");
        EXPECT_EQ(f.mSize, 16.f);
        EXPECT_EQ(f.mTextureName, "Magic_Cards_Regular");
        EXPECT_EQ(f.mGlyphs[65].mWidth, 8.f);
        EXPECT_EQ(f.mGlyphs[65].mKerningRight, 2.f);
        EXPECT_EQ(f.mGlyphs[65].mTopLeftU, 0.25f);
        EXPECT_EQ(f.mGlyphs[255].mAscent, 7.f);
        EXPECT_EQ(f.mGlyphs[255].mHeight, 4.f);
        EXPECT_EQ(f.mGlyphs[0].mWidth, 0.f);
    }

    TEST(UiRmlFontFile, a_file_too_small_is_refused_with_the_loaders_wording)
    {
        std::vector<char> bytes(100, 0);
        try
        {
            parseFontFile(bytes, "fonts/small.fnt");
            FAIL() << "no exception";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(
                std::string(e.what()).find("Font loading error: File too small to be a valid font"), std::string::npos)
                << e.what();
            EXPECT_NE(std::string(e.what()).find("fonts/small.fnt"), std::string::npos) << e.what();
        }
    }

    TEST(UiRmlFontFile, the_two_header_integers_must_be_one)
    {
        std::array<RawGlyph, 256> glyphs{};
        EXPECT_THROW(parseFontFile(makeFnt(16.f, "A", glyphs, 2, 1), "x"), std::runtime_error);
        EXPECT_THROW(parseFontFile(makeFnt(16.f, "A", glyphs, 1, 0), "x"), std::runtime_error);
        EXPECT_NO_THROW(parseFontFile(makeFnt(16.f, "A", glyphs, 1, 1), "x"));
    }

    TEST(UiRmlFontFile, a_texture_name_without_a_terminating_zero_is_cut_at_284_bytes)
    {
        std::array<RawGlyph, 256> glyphs{};
        const std::string longName(284, 'q');
        const std::vector<char> bytes = makeFnt(16.f, longName, glyphs);
        // The glyph table follows at once: the name is not zero terminated and must not run into it.
        const FontFile f = parseFontFile(bytes, "x");
        EXPECT_EQ(f.mTextureName, longName);
    }

    TEST(UiRmlFontFile, a_non_finite_design_size_is_refused)
    {
        std::array<RawGlyph, 256> glyphs{};
        EXPECT_THROW(parseFontFile(makeFnt(std::nanf(""), "A", glyphs), "x"), std::runtime_error);
        EXPECT_THROW(parseFontFile(makeFnt(0.f, "A", glyphs), "x"), std::runtime_error);
        EXPECT_THROW(parseFontFile(makeFnt(-4.f, "A", glyphs), "x"), std::runtime_error);
    }

    TEST(UiRmlFontTexture, reads_width_height_and_pixels)
    {
        const FontTexture t = parseFontTexture(makeTex(2, 3, 2 * 3 * 4), "fonts/a.tex");
        EXPECT_EQ(t.mWidth, 2);
        EXPECT_EQ(t.mHeight, 3);
        ASSERT_EQ(t.mPixels.size(), 24u);
        EXPECT_EQ(t.mPixels[0], 3);
        EXPECT_EQ(t.mPixels[1], 10);
        EXPECT_FALSE(t.mTruncated);
    }

    TEST(UiRmlFontTexture, a_short_pixel_block_is_padded_with_zero_and_flagged)
    {
        const FontTexture t = parseFontTexture(makeTex(2, 2, 6), "fonts/a.tex");
        ASSERT_EQ(t.mPixels.size(), 16u);
        EXPECT_TRUE(t.mTruncated);
        EXPECT_EQ(t.mPixels[5], static_cast<std::uint8_t>(5 * 7 + 3));
        EXPECT_EQ(t.mPixels[6], 0);
        EXPECT_EQ(t.mPixels[15], 0);
    }

    TEST(UiRmlFontTexture, bad_sizes_are_refused)
    {
        EXPECT_THROW(parseFontTexture(makeTex(0, 4, 16), "x"), std::runtime_error);
        EXPECT_THROW(parseFontTexture(makeTex(4, -1, 16), "x"), std::runtime_error);
        EXPECT_THROW(parseFontTexture(std::vector<char>(5, 0), "x"), std::runtime_error);
        // far beyond any font sheet: refused before anything is allocated
        EXPECT_THROW(parseFontTexture(makeTex(100000, 100000, 16), "x"), std::runtime_error);
    }

    TEST(UiRmlFontTexture, premultiply_scales_colour_by_alpha_and_keeps_alpha)
    {
        FontTexture t;
        t.mWidth = 4;
        t.mHeight = 1;
        t.mPixels = { 255, 255, 255, 255, 200, 100, 50, 128, 255, 0, 10, 0, 90, 90, 90, 1 };
        premultiply(t);
        EXPECT_EQ(t.mPixels[0], 255);
        EXPECT_EQ(t.mPixels[3], 255);
        EXPECT_EQ(t.mPixels[4], 100); // (200 * 128 + 127) / 255 = 100
        EXPECT_EQ(t.mPixels[5], 50);
        EXPECT_EQ(t.mPixels[6], 25);
        EXPECT_EQ(t.mPixels[7], 128);
        EXPECT_EQ(t.mPixels[8], 0); // alpha 0 clears the colour
        EXPECT_EQ(t.mPixels[10], 0);
        EXPECT_EQ(t.mPixels[12], 0); // 90 * 1 / 255 rounds to 0
        EXPECT_EQ(t.mPixels[15], 1);
    }

    TEST(UiRmlUtf8, decodes_ascii_two_three_and_four_byte_sequences)
    {
        EXPECT_EQ(decodeUtf8("A"), std::u32string(U"A"));
        EXPECT_EQ(decodeUtf8("\xC3\xA9"), std::u32string(U"é"));
        EXPECT_EQ(decodeUtf8("\xE2\x82\xAC"), std::u32string(U"€"));
        EXPECT_EQ(decodeUtf8("\xF0\x9F\x98\x80"), std::u32string(U"\U0001F600"));
        EXPECT_EQ(decodeUtf8(""), std::u32string());
    }

    TEST(UiRmlUtf8, a_malformed_byte_becomes_the_replacement_character_and_decoding_goes_on)
    {
        EXPECT_EQ(decodeUtf8(std::string_view("a\xFF"
                                              "b")),
            std::u32string(U"a�"
                           "b"));
        EXPECT_EQ(decodeUtf8("\x80"), std::u32string(U"�")); // a lone continuation byte
        EXPECT_EQ(decodeUtf8("x\xC3"), std::u32string(U"x�")); // a cut-off sequence
        EXPECT_EQ(decodeUtf8("\xC3("), std::u32string(U"�(")); // a lead byte followed by a plain byte
    }

    TEST(UiRmlGlyphMap, ascii_letters_map_to_their_own_glyph_in_cp437_and_windows_1250)
    {
        for (const auto encoding : { ToUTF8::WINDOWS_1252, ToUTF8::WINDOWS_1250, ToUTF8::WINDOWS_1251 })
        {
            const GlyphMap map(encoding, DuplicatePolicy::FirstWins);
            EXPECT_EQ(map.lookup(U'A'), 65);
            EXPECT_EQ(map.lookup(U'z'), 122);
            EXPECT_EQ(map.lookup(U'0'), 48);
        }
    }

    TEST(UiRmlGlyphMap, a_substitution_draws_a_similar_glyph_for_a_character_the_font_lacks)
    {
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        EXPECT_EQ(map.lookup(U'À'), 65); // A with grave => A
        EXPECT_EQ(map.lookup(U'–'), 45); // en dash => hyphen
        EXPECT_EQ(map.lookup(U'…'), 46); // ellipsis => period
        EXPECT_EQ(map.lookup(U'š'), 115); // s with caron => s
    }

    TEST(UiRmlGlyphMap, omitted_code_points_draw_nothing)
    {
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        for (const char32_t cp : { U'•', U'¤', U'§', U'¶' })
            EXPECT_EQ(map.lookup(cp), GlyphMap::kHidden) << std::hex << unsigned(cp);
    }

    TEST(UiRmlGlyphMap, an_unmapped_code_point_draws_nothing_as_in_mygui)
    {
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        EXPECT_EQ(map.lookup(U'中'), GlyphMap::kHidden);
        EXPECT_EQ(map.lookup(U'\U0001F600'), GlyphMap::kHidden);
    }

    TEST(UiRmlGlyphMap, the_polish_hack_swaps_glyph_bytes_in_windows_1250_only)
    {
        EXPECT_EQ(polishSwap(0x85), 0xb9);
        EXPECT_EQ(polishSwap(0x41), 0x41);
        const GlyphMap polish(ToUTF8::WINDOWS_1250, DuplicatePolicy::FirstWins);
        EXPECT_EQ(polish.lookup(U'ą'), 0x85); // a with ogonek: glyph 0x85 stands for it (and 0xa0, a duplicate)
        const GlyphMap latin(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        EXPECT_NE(latin.lookup(U'ą'), 0x85);
    }

    TEST(UiRmlGlyphMap, the_duplicate_policy_chooses_between_the_first_and_the_last_glyph_and_lists_them)
    {
        const GlyphMap first(ToUTF8::WINDOWS_1250, DuplicatePolicy::FirstWins);
        const GlyphMap last(ToUTF8::WINDOWS_1250, DuplicatePolicy::LastWins);
        EXPECT_EQ(first.lookup(U'ą'), 0x85);
        EXPECT_EQ(last.lookup(U'ą'), 0xb9); // three glyphs claim it: 0x85, 0xa0 and 0xb9 itself
        const std::array<unsigned, 3> expected{ 0x0105, 0x85, 0xa0 };
        EXPECT_NE(std::find(first.duplicates().begin(), first.duplicates().end(), expected), first.duplicates().end());
        const std::array<unsigned, 3> expectedLast{ 0x0105, 0xa0, 0x85 };
        EXPECT_NE(std::find(last.duplicates().begin(), last.duplicates().end(), expectedLast), last.duplicates().end());
    }

    TEST(UiRmlGlyphMap, the_windows_1252_data_files_use_the_cp437_font_code_page)
    {
        EXPECT_EQ(fontEncoding(ToUTF8::WINDOWS_1252), ToUTF8::CP437);
        EXPECT_EQ(fontEncoding(ToUTF8::WINDOWS_1250), ToUTF8::WINDOWS_1250);
        EXPECT_EQ(fontEncoding(ToUTF8::WINDOWS_1251), ToUTF8::WINDOWS_1251);
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        EXPECT_EQ(map.lookup(U'é'), 130); // CP437 0x82 is e with acute
        EXPECT_EQ(map.lookup(U'ü'), 129); // CP437 0x81 is u with diaeresis
    }

    TEST(UiRmlGlyphMap, the_substitution_and_omitted_tables_have_the_sizes_of_the_loader)
    {
        EXPECT_EQ(substitutions().size(), 63u);
        EXPECT_EQ(omittedCodePoints().size(), 4u);
        EXPECT_EQ(substitutions().front().mGlyph, 44u);
        EXPECT_EQ(substitutions().front().mCodePoint, 0x201Au);
    }

    TEST(UiRmlLayout, a_glyph_is_placed_at_the_pen_plus_kerning_left_and_above_the_baseline_by_its_ascent)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        const int advance = layoutString(f, map, Placement{ 1.f, true }, "A", 10.f, 100.f, mesh);
        EXPECT_EQ(advance, 9);
        ASSERT_EQ(mesh.mVertices.size(), 4u);
        ASSERT_EQ(mesh.mIndices.size(), 6u);
        // x0 = 10 + 1, y0 = 100 - 9; x1 = x0 + 8, y1 = y0 + 10
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mX, 11.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mY, 91.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mX, 19.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mY, 101.f);
        // texture coordinates from the file: top left, top right, bottom right, bottom left
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mU, 0.25f);
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mV, 0.5f);
        EXPECT_FLOAT_EQ(mesh.mVertices[1].mU, 0.5f);
        EXPECT_FLOAT_EQ(mesh.mVertices[1].mV, 0.5f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mU, 0.5f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mV, 0.75f);
        EXPECT_FLOAT_EQ(mesh.mVertices[3].mU, 0.25f);
        EXPECT_FLOAT_EQ(mesh.mVertices[3].mV, 0.75f);
        EXPECT_EQ(mesh.mIndices, (std::vector<int>{ 0, 1, 2, 0, 2, 3 }));
    }

    TEST(UiRmlLayout, the_pen_advances_by_width_plus_kerning_right_and_a_second_glyph_follows)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 1.f, true }, "AB", 0.f, 20.f, mesh), 17);
        ASSERT_EQ(mesh.mVertices.size(), 8u);
        EXPECT_FLOAT_EQ(mesh.mVertices[4].mX, 9.f); // B: pen 9 + kerning left 0
        EXPECT_FLOAT_EQ(mesh.mVertices[6].mX, 15.f);
        ASSERT_EQ(mesh.mIndices.size(), 12u);
        EXPECT_EQ(mesh.mIndices[6], 4); // the second quad's indices start at its first vertex
    }

    TEST(UiRmlLayout, glyphs_without_a_picture_advance_the_pen_and_add_no_quad)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 1.f, true }, " A", 0.f, 20.f, mesh), 14);
        ASSERT_EQ(mesh.mVertices.size(), 4u);
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mX, 6.f); // pen 5 + kerning 1
    }

    TEST(UiRmlLayout, hidden_code_points_and_unmapped_ones_add_nothing_and_do_not_advance)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 1.f, true }, "\xE2\x80\xA2", 0.f, 20.f, mesh), 0); // bullet
        EXPECT_TRUE(mesh.mVertices.empty());
        EXPECT_EQ(layoutString(f, map, Placement{ 1.f, true }, "\xE4\xB8\xAD", 0.f, 20.f, mesh), 0); // a CJK character
        EXPECT_TRUE(mesh.mVertices.empty());
    }

    TEST(UiRmlLayout, scale_two_doubles_every_distance)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 2.f, true }, "A", 0.f, 100.f, mesh), 18);
        ASSERT_EQ(mesh.mVertices.size(), 4u);
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mX, 2.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[0].mY, 82.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mX, 18.f);
        EXPECT_FLOAT_EQ(mesh.mVertices[2].mY, 102.f);
    }

    TEST(UiRmlLayout, rounded_edges_put_quads_on_whole_pixels_and_exact_edges_do_not)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh rounded, exact;
        layoutString(f, map, Placement{ 1.5f, true }, "A", 0.f, 100.f, rounded);
        layoutString(f, map, Placement{ 1.5f, false }, "A", 0.f, 100.f, exact);
        ASSERT_EQ(rounded.mVertices.size(), 4u);
        ASSERT_EQ(exact.mVertices.size(), 4u);
        // x0 = 1.5, y0 = 100 - 13.5 = 86.5, width 12, height 15
        EXPECT_FLOAT_EQ(exact.mVertices[0].mX, 1.5f);
        EXPECT_FLOAT_EQ(exact.mVertices[0].mY, 86.5f);
        EXPECT_FLOAT_EQ(exact.mVertices[2].mX, 13.5f);
        EXPECT_FLOAT_EQ(exact.mVertices[2].mY, 101.5f);
        EXPECT_FLOAT_EQ(rounded.mVertices[0].mX, 2.f);
        EXPECT_FLOAT_EQ(rounded.mVertices[0].mY, 87.f);
        EXPECT_FLOAT_EQ(rounded.mVertices[2].mX, 14.f); // 2 + round(12)
        EXPECT_FLOAT_EQ(rounded.mVertices[2].mY, 102.f); // 87 + round(15)
    }

    TEST(UiRmlLayout, string_width_is_the_advance_of_layout_and_rounds_half_up)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        EXPECT_EQ(stringWidth(f, map, Placement{ 1.f, true }, "AB"), 17);
        EXPECT_EQ(stringWidth(f, map, Placement{ 1.f, true }, ""), 0);
        EXPECT_EQ(stringWidth(f, map, Placement{ 0.5f, true }, "A"), 5); // 4.5 rounds up
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 0.5f, true }, "A", 0.f, 0.f, mesh), 5);
        EXPECT_EQ(stringWidth(f, map, Placement{ 1.f, true },
                      "A\xE2\x80\xA2"
                      "B"),
            17);
    }

    TEST(UiRmlLayout, an_empty_string_adds_nothing)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        QuadMesh mesh;
        EXPECT_EQ(layoutString(f, map, Placement{ 1.f, true }, "", 5.f, 5.f, mesh), 0);
        EXPECT_TRUE(mesh.mVertices.empty());
        EXPECT_TRUE(mesh.mIndices.empty());
    }

    TEST(UiRmlMetrics, vertical_metrics_follow_the_scale)
    {
        const FontFile f = sampleFile();
        const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
        const Metrics m1 = metricsFor(f, map, 1.f);
        EXPECT_EQ(m1.mSize, 16);
        EXPECT_FLOAT_EQ(m1.mAscent, 16.f);
        EXPECT_FLOAT_EQ(m1.mLineSpacing, 16.f);
        EXPECT_FLOAT_EQ(m1.mXHeight, 6.f); // the glyph of x: ascent 6
        EXPECT_FLOAT_EQ(
            m1.mDescent, 3.f); // the largest (height - ascent) of a glyph with a picture: the underscore's 2 + 1
        const Metrics m2 = metricsFor(f, map, 2.f);
        EXPECT_EQ(m2.mSize, 32);
        EXPECT_FLOAT_EQ(m2.mAscent, 32.f);
        EXPECT_FLOAT_EQ(m2.mDescent, 6.f);
        EXPECT_FLOAT_EQ(m2.mUnderlineThickness, 2.f);
        EXPECT_FLOAT_EQ(m1.mUnderlineThickness, 1.f);
    }

    TEST(UiRmlSize, the_requested_size_picks_scale_rounding_and_sampling)
    {
        // Design size 16.
        const SizeChoice one = chooseSize(16.f, 16);
        EXPECT_FLOAT_EQ(one.mPlacement.mScale, 1.f);
        EXPECT_TRUE(one.mPlacement.mRoundEdges);
        EXPECT_TRUE(one.mTextureNearest);
        const SizeChoice two = chooseSize(16.f, 32);
        EXPECT_FLOAT_EQ(two.mPlacement.mScale, 2.f);
        EXPECT_TRUE(two.mTextureNearest);
        const SizeChoice oneAndHalf = chooseSize(16.f, 24);
        EXPECT_FLOAT_EQ(oneAndHalf.mPlacement.mScale, 1.5f);
        EXPECT_FALSE(oneAndHalf.mPlacement.mRoundEdges);
        EXPECT_FALSE(oneAndHalf.mTextureNearest);
        const SizeChoice small = chooseSize(16.f, 8);
        EXPECT_FLOAT_EQ(small.mPlacement.mScale, 0.5f);
        EXPECT_FALSE(small.mTextureNearest); // below one the glyphs are shrunk: smooth
        const SizeChoice off = chooseSize(16.f, 17);
        EXPECT_FALSE(off.mTextureNearest);
        EXPECT_FALSE(off.mPlacement.mRoundEdges);
        EXPECT_FLOAT_EQ(off.mPlacement.mScale, 17.f / 16.f);
        // Garbage in: a zero design size or a zero request draws at scale one.
        EXPECT_FLOAT_EQ(chooseSize(0.f, 16).mPlacement.mScale, 1.f);
        EXPECT_FLOAT_EQ(chooseSize(16.f, 0).mPlacement.mScale, 1.f);
    }
}

// Opt-in, on the installed game: OPENEDEN_MORROWIND_DATA names the Data Files folder (no game file is read otherwise).
// Every .fnt of the install must parse, name an existing .tex of the right size, and give sane glyph metrics.
namespace
{
    TEST(UiRmlInstalledFonts, every_installed_bitmap_font_parses_with_its_texture)
    {
        const char* data = std::getenv("OPENEDEN_MORROWIND_DATA");
        if (data == nullptr)
            GTEST_SKIP() << "OPENEDEN_MORROWIND_DATA is not set";
        const std::filesystem::path fonts = std::filesystem::path(data) / "Fonts";
        if (!std::filesystem::exists(fonts))
            GTEST_SKIP() << fonts << " does not exist";
        int checked = 0;
        for (const auto& entry : std::filesystem::directory_iterator(fonts))
        {
            if (entry.path().extension() != ".fnt")
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const FontFile file = parseFontFile(bytes, entry.path().string());
            EXPECT_GT(file.mSize, 4.f) << entry.path();
            EXPECT_LT(file.mSize, 128.f) << entry.path();
            const std::filesystem::path texture = fonts / (file.mTextureName + ".tex");
            ASSERT_TRUE(std::filesystem::exists(texture)) << texture;
            std::ifstream texIn(texture, std::ios::binary);
            const std::vector<char> texBytes((std::istreambuf_iterator<char>(texIn)), std::istreambuf_iterator<char>());
            const FontTexture tex = parseFontTexture(texBytes, texture.string());
            EXPECT_FALSE(tex.mTruncated) << texture;
            // the capital A of a Latin font has a picture and an advance of a few pixels
            const RawGlyph& a = file.mGlyphs['A'];
            EXPECT_GT(a.mWidth, 1.f) << entry.path();
            EXPECT_GT(a.mWidth + a.mKerningRight, 2.f) << entry.path();
            EXPECT_LT(a.mWidth + a.mKerningRight, file.mSize * 2.f) << entry.path();
            // the texture coordinates are normalised
            EXPECT_GE(a.mTopLeftU, 0.f);
            EXPECT_LE(a.mBottomRightU, 1.f);
            const GlyphMap map(ToUTF8::WINDOWS_1252, DuplicatePolicy::FirstWins);
            EXPECT_GT(stringWidth(file, map, Placement{ 1.f, true }, "OpenEden UI"), 20) << entry.path();
            ++checked;
        }
        EXPECT_GE(checked, 1);
    }
}
