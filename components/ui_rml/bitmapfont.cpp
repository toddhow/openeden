#include "bitmapfont.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

#include "codepage.hpp"

namespace UiRml
{
    namespace
    {
        // The sheets of Morrowind's fonts are 256 to 1024 pixels on a side; anything beyond this is not a font.
        constexpr int sMaxTextureSide = 16384;

        // A requested size within this of a whole multiple of the design size is drawn as that multiple.
        constexpr float sWholeScaleTolerance = 1e-3f;

        [[noreturn]] void fail(std::string_view path, std::string_view message)
        {
            throw std::runtime_error(std::format("Font loading error: {}\n  File: {}", message, path));
        }
    }

    FontFile parseFontFile(std::span<const char> bytes, std::string_view path)
    {
        constexpr std::size_t headerSize = 12;
        constexpr std::size_t nameSize = 284;
        constexpr std::size_t glyphsSize = 256 * sizeof(RawGlyph);
        if (bytes.size() < headerSize + nameSize + glyphsSize)
            fail(path, "File too small to be a valid font");

        FontFile file;
        int one1 = 0, one2 = 0;
        std::memcpy(&file.mSize, bytes.data(), 4);
        std::memcpy(&one1, bytes.data() + 4, 4);
        std::memcpy(&one2, bytes.data() + 8, 4);
        if (one1 != 1 || one2 != 1)
            fail(path, "Unexpected value");
        if (!std::isfinite(file.mSize) || file.mSize <= 0.f)
            fail(path, "Invalid font size");

        const char* name = bytes.data() + headerSize;
        std::size_t length = 0;
        while (length < nameSize && name[length] != '\0')
            ++length;
        file.mTextureName.assign(name, length);
        std::memcpy(file.mGlyphs.data(), bytes.data() + headerSize + nameSize, glyphsSize);
        return file;
    }

    FontTexture parseFontTexture(std::span<const char> bytes, std::string_view path)
    {
        if (bytes.size() < 8)
            fail(path, "File too small to be a valid bitmap");
        FontTexture texture;
        std::memcpy(&texture.mWidth, bytes.data(), 4);
        std::memcpy(&texture.mHeight, bytes.data() + 4, 4);
        if (texture.mWidth <= 0 || texture.mHeight <= 0)
            fail(path, "Width and height must be positive");
        if (texture.mWidth > sMaxTextureSide || texture.mHeight > sMaxTextureSide)
            fail(path, "Bitmap is too large to be a font sheet");
        const std::size_t wanted
            = static_cast<std::size_t>(texture.mWidth) * static_cast<std::size_t>(texture.mHeight) * 4;
        texture.mPixels.assign(wanted, 0);
        const std::size_t available = bytes.size() - 8;
        std::memcpy(texture.mPixels.data(), bytes.data() + 8, std::min(wanted, available));
        texture.mTruncated = available < wanted;
        return texture;
    }

    void premultiply(FontTexture& texture)
    {
        for (std::size_t i = 0; i + 3 < texture.mPixels.size(); i += 4)
        {
            const unsigned alpha = texture.mPixels[i + 3];
            for (std::size_t c = 0; c < 3; ++c)
                texture.mPixels[i + c]
                    = static_cast<std::uint8_t>((unsigned(texture.mPixels[i + c]) * alpha + 127u) / 255u);
        }
    }

    GlyphMap::GlyphMap(ToUTF8::FromType gameEncoding, DuplicatePolicy policy)
    {
        const ToUTF8::FromType encoding = fontEncoding(gameEncoding);
        ToUTF8::Utf8Encoder encoder(encoding);

        const auto claim = [&](char32_t codePoint, int glyph) {
            const auto [it, inserted] = mIndex.emplace(codePoint, glyph);
            if (inserted || it->second == glyph)
                return;
            if (policy == DuplicatePolicy::FirstWins)
            {
                mDuplicates.push_back({ unsigned(codePoint), unsigned(it->second), unsigned(glyph) });
            }
            else
            {
                mDuplicates.push_back({ unsigned(codePoint), unsigned(glyph), unsigned(it->second) });
                it->second = glyph;
            }
        };

        // The order of FontLoader::loadBitmapFont: for each glyph its own code point, then that glyph's substitutions.
        const std::span<const Substitution> table = substitutions();
        for (unsigned glyph = 0; glyph < 256; ++glyph)
        {
            try
            {
                claim(static_cast<char32_t>(legacyToUnicode(static_cast<unsigned char>(glyph), encoder, encoding)),
                    static_cast<int>(glyph));
            }
            catch (const std::logic_error&)
            {
                // A glyph byte the code page cannot express: the MyGUI loader throws; here the glyph stays unmapped.
            }
            for (const Substitution& substitution : table)
            {
                if (substitution.mGlyph == glyph)
                    claim(static_cast<char32_t>(substitution.mCodePoint), static_cast<int>(glyph));
            }
        }
        // The omitted code points come last in the loader's output, with an empty picture.
        for (const unsigned codePoint : omittedCodePoints())
            claim(static_cast<char32_t>(codePoint), kHidden);
    }

    int GlyphMap::lookup(char32_t codePoint) const
    {
        const auto it = mIndex.find(codePoint);
        return it == mIndex.end() ? kHidden : it->second;
    }

    std::u32string decodeUtf8(std::string_view utf8)
    {
        std::u32string out;
        out.reserve(utf8.size());
        std::size_t i = 0;
        while (i < utf8.size())
        {
            const unsigned char lead = static_cast<unsigned char>(utf8[i]);
            if (lead <= 0x7F)
            {
                out.push_back(lead);
                ++i;
                continue;
            }
            std::size_t extra = 0;
            char32_t value = 0;
            if (lead >= 0xC2 && lead <= 0xDF)
            {
                extra = 1;
                value = lead & 0x1F;
            }
            else if (lead >= 0xE0 && lead <= 0xEF)
            {
                extra = 2;
                value = lead & 0x0F;
            }
            else if (lead >= 0xF0 && lead <= 0xF4)
            {
                extra = 3;
                value = lead & 0x07;
            }
            else
            {
                out.push_back(U'�');
                ++i;
                continue;
            }
            bool ok = i + extra < utf8.size();
            for (std::size_t k = 1; ok && k <= extra; ++k)
            {
                const unsigned char next = static_cast<unsigned char>(utf8[i + k]);
                if ((next & 0xC0) != 0x80)
                    ok = false;
                else
                    value = (value << 6) | (next & 0x3F);
            }
            if (!ok || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) || (extra == 2 && value < 0x800)
                || (extra == 3 && value < 0x10000))
            {
                out.push_back(U'�');
                ++i; // resynchronise on the next byte
                continue;
            }
            out.push_back(value);
            i += extra + 1;
        }
        return out;
    }

    Metrics metricsFor(const FontFile& file, const GlyphMap& map, float scale)
    {
        Metrics m;
        m.mSize = static_cast<int>(std::lround(file.mSize * scale));
        m.mAscent = file.mSize * scale;
        m.mLineSpacing = file.mSize * scale;
        float maxDescender = 0.f;
        for (std::size_t i = 32; i < 256; ++i)
        {
            const RawGlyph& g = file.mGlyphs[i];
            if (g.mHeight > 0.f)
                maxDescender = std::max(maxDescender, g.mHeight - g.mAscent);
        }
        m.mDescent = maxDescender * scale;
        const int xGlyph = map.lookup(U'x');
        m.mXHeight = xGlyph >= 0 ? file.mGlyphs[static_cast<std::size_t>(xGlyph)].mAscent * scale : m.mAscent * 0.5f;
        m.mUnderlinePosition = 1.f * scale;
        m.mUnderlineThickness = std::max(1.f, scale);
        return m;
    }

    int stringWidth(const FontFile& file, const GlyphMap& map, Placement placement, std::string_view utf8)
    {
        float width = 0.f;
        for (const char32_t codePoint : decodeUtf8(utf8))
        {
            const int glyph = map.lookup(codePoint);
            if (glyph < 0)
                continue;
            const RawGlyph& g = file.mGlyphs[static_cast<std::size_t>(glyph)];
            width += (g.mWidth + g.mKerningRight) * placement.mScale;
        }
        return static_cast<int>(width + 0.5f);
    }

    int layoutString(const FontFile& file, const GlyphMap& map, Placement placement, std::string_view utf8, float x,
        float baselineY, QuadMesh& mesh)
    {
        const float scale = placement.mScale;
        float advance = 0.f;
        for (const char32_t codePoint : decodeUtf8(utf8))
        {
            const int glyph = map.lookup(codePoint);
            if (glyph < 0)
                continue;
            const RawGlyph& g = file.mGlyphs[static_cast<std::size_t>(glyph)];
            if (g.mWidth > 0.f && g.mHeight > 0.f)
            {
                float x0 = x + advance + g.mKerningLeft * scale;
                float y0 = baselineY - g.mAscent * scale;
                float x1, y1;
                if (placement.mRoundEdges)
                {
                    x0 = std::floor(x0 + 0.5f);
                    y0 = std::floor(y0 + 0.5f);
                    x1 = x0 + std::floor(g.mWidth * scale + 0.5f);
                    y1 = y0 + std::floor(g.mHeight * scale + 0.5f);
                }
                else
                {
                    x1 = x0 + g.mWidth * scale;
                    y1 = y0 + g.mHeight * scale;
                }
                const int base = static_cast<int>(mesh.mVertices.size());
                mesh.mVertices.push_back({ x0, y0, g.mTopLeftU, g.mTopLeftV });
                mesh.mVertices.push_back({ x1, y0, g.mTopRightU, g.mTopRightV });
                mesh.mVertices.push_back({ x1, y1, g.mBottomRightU, g.mBottomRightV });
                mesh.mVertices.push_back({ x0, y1, g.mBottomLeftU, g.mBottomLeftV });
                for (const int offset : { 0, 1, 2, 0, 2, 3 })
                    mesh.mIndices.push_back(base + offset);
            }
            advance += (g.mWidth + g.mKerningRight) * scale;
        }
        return static_cast<int>(advance + 0.5f);
    }

    SizeChoice chooseSize(float fontSize, int requestedSize)
    {
        SizeChoice choice;
        if (!(fontSize > 0.f) || requestedSize <= 0)
        {
            choice.mPlacement = Placement{ 1.f, true };
            choice.mTextureNearest = true;
            return choice;
        }
        const float ratio = static_cast<float>(requestedSize) / fontSize;
        const float whole = std::round(ratio);
        if (whole >= 1.f && std::abs(ratio - whole) < sWholeScaleTolerance)
        {
            choice.mPlacement = Placement{ whole, true };
            choice.mTextureNearest = true;
        }
        else
        {
            choice.mPlacement = Placement{ ratio, false };
            choice.mTextureNearest = false;
        }
        return choice;
    }
}
