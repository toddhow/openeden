#ifndef OPENEDEN_COMPONENTS_UI_RML_BITMAPFONT_H
#define OPENEDEN_COMPONENTS_UI_RML_BITMAPFONT_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <components/toutf8/toutf8.hpp>

// Morrowind's bitmap fonts (.fnt + .tex) as plain data, with no UI library in sight (task 0104, decision 0015):
// the file readers, the code point to glyph map that the MyGUI font loader builds, and the layout of a string into
// textured quads. fontengine.* adapts this to RmlUi.
namespace UiRml
{
    /// One of the 256 glyph records of a .fnt file, in the file's layout (14 floats). The texture coordinates are
    /// normalised (0 to 1) and the other values are in pixels of the font's own size.
    struct RawGlyph
    {
        float mUnused;
        float mTopLeftU, mTopLeftV;
        float mTopRightU, mTopRightV;
        float mBottomLeftU, mBottomLeftV;
        float mBottomRightU, mBottomRightV;
        float mWidth, mHeight;
        float mKerningLeft, mKerningRight;
        float mAscent;
    };
    static_assert(sizeof(RawGlyph) == 14 * sizeof(float));

    /// The parsed .fnt file.
    struct FontFile
    {
        float mSize = 0; ///< the font's design height in pixels
        std::string mTextureName; ///< the .tex file's name without extension and folder (a `fonts/<name>.tex`)
        std::array<RawGlyph, 256> mGlyphs{};
    };

    /// The parsed .tex file: width, height and straight (not premultiplied) RGBA8 pixels, row by row.
    struct FontTexture
    {
        int mWidth = 0;
        int mHeight = 0;
        std::vector<std::uint8_t> mPixels;
        bool mTruncated = false; ///< the file ended early; the rest of the pixels are zero (the MyGUI loader warns)
    };

    /// Parses a .fnt file; throws std::runtime_error ("Font loading error: <reason>") for a file the MyGUI font loader
    /// also refuses. `path` is for the message only.
    FontFile parseFontFile(std::span<const char> bytes, std::string_view path);

    /// Parses a .tex file; throws std::runtime_error for a file too small or with a non positive size.
    FontTexture parseFontTexture(std::span<const char> bytes, std::string_view path);

    /// In place: colour channels times alpha (RmlUi draws with premultiplied alpha).
    void premultiply(FontTexture& texture);

    /// When two glyphs claim one code point (a real mapping and a substitution, or two substitutions).
    enum class DuplicatePolicy
    {
        FirstWins,
        LastWins,
    };

    /// The code point to glyph map the MyGUI font loader gives MyGUI, in the order it emits it: for each glyph byte 0
    /// to 255 its real code point (through the font encoding), then the substitutions of that glyph. Omitted code
    /// points draw nothing.
    class GlyphMap
    {
    public:
        static constexpr int kHidden = -1;

        GlyphMap(ToUTF8::FromType gameEncoding, DuplicatePolicy policy);

        /// The glyph byte to draw for `codePoint`; `kHidden` for a code point that draws nothing: one the loader omits
        /// and one nothing maps (MyGUI's font has no substitute glyph either: its getGlyphInfo returns null and the
        /// text view skips the character; pinned by apps/components_tests/ui_rml/testbitmapfontmygui.cpp).
        int lookup(char32_t codePoint) const;
        /// The code points mapped to some glyph (not the omitted ones).
        std::size_t size() const { return mIndex.size(); }
        /// Code points two glyphs claimed, each as (code point, glyph kept, glyph dropped).
        const std::vector<std::array<unsigned, 3>>& duplicates() const { return mDuplicates; }

    private:
        std::unordered_map<char32_t, int> mIndex;
        std::vector<std::array<unsigned, 3>> mDuplicates;
    };

    /// Decodes UTF-8 into code points. A malformed byte becomes U+FFFD and decoding continues with the next byte.
    std::u32string decodeUtf8(std::string_view utf8);

    struct Vertex2
    {
        float mX, mY, mU, mV;
    };

    struct QuadMesh
    {
        std::vector<Vertex2> mVertices;
        std::vector<int> mIndices;
    };

    /// How a font is drawn at one size.
    struct Placement
    {
        float mScale = 1.f; ///< pixel size asked for divided by the font's design size (or its rounded integer)
        bool mRoundEdges = true; ///< glyph quads on whole pixels (the crisp look at whole scales)
    };

    /// Vertical metrics of the font at a scale (RmlUi's FontMetrics fields, all in pixels).
    struct Metrics
    {
        int mSize = 0;
        float mAscent = 0, mDescent = 0, mLineSpacing = 0, mXHeight = 0, mUnderlinePosition = 0,
              mUnderlineThickness = 0;
    };

    Metrics metricsFor(const FontFile& file, const GlyphMap& map, float scale);

    /// Width of `text` in whole pixels (the sum of advances, rounded half up).
    int stringWidth(const FontFile& file, const GlyphMap& map, Placement placement, std::string_view utf8);

    /// Appends the quads of `text` to `mesh` with the pen at (x, baselineY); returns the advance in whole pixels.
    int layoutString(const FontFile& file, const GlyphMap& map, Placement placement, std::string_view utf8, float x,
        float baselineY, QuadMesh& mesh);

    /// The scale and edge rounding for a requested pixel size. `integerScaling`: at a whole-number ratio the glyphs are
    /// drawn on whole pixels (and the texture is sampled with nearest filtering: textureNearest); in between they are
    /// drawn at the exact ratio with linear filtering (the MyGUI look). Decision 0015 / note 0045 15.3.
    struct SizeChoice
    {
        Placement mPlacement;
        bool mTextureNearest = false;
    };
    SizeChoice chooseSize(float fontSize, int requestedSize);
}

#endif
