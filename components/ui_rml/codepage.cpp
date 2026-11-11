#include "codepage.hpp"

#include <array>
#include <stdexcept>

// utf8ToUnicode, polishSwap and the two tables are copies of what components/fontloader/fontloader.cpp keeps in an
// anonymous namespace (MyGUI's font loader). apps/openmw_tests/ui_rml/testbitmapfontmygui.cpp compares what they
// produce with what the real loader gives MyGUI, so a change made on one side only is caught (task 0104).
namespace UiRml
{
    unsigned long utf8ToUnicode(std::string_view utf8)
    {
        if (utf8.empty())
            return 0;
        size_t i = 0;
        unsigned long unicode;
        size_t numbytes;
        unsigned char ch = utf8[i++];
        if (ch <= 0x7F)
        {
            unicode = ch;
            numbytes = 0;
        }
        else if (ch <= 0xBF)
        {
            throw std::logic_error("not a UTF-8 string");
        }
        else if (ch <= 0xDF)
        {
            unicode = ch & 0x1F;
            numbytes = 1;
        }
        else if (ch <= 0xEF)
        {
            unicode = ch & 0x0F;
            numbytes = 2;
        }
        else if (ch <= 0xF7)
        {
            unicode = ch & 0x07;
            numbytes = 3;
        }
        else
        {
            throw std::logic_error("not a UTF-8 string");
        }
        for (size_t j = 0; j < numbytes; ++j)
        {
            ch = utf8[i++];
            if (ch < 0x80 || ch > 0xBF)
                throw std::logic_error("not a UTF-8 string");
            unicode <<= 6;
            unicode += ch & 0x3F;
        }
        if (unicode >= 0xD800 && unicode <= 0xDFFF)
            throw std::logic_error("not a UTF-8 string");
        if (unicode > 0x10FFFF)
            throw std::logic_error("not a UTF-8 string");

        return unicode;
    }

    /// This is a hack for Polish font
    unsigned char polishSwap(unsigned char c)
    {
        switch (c)
        {
            case 0x80:
                return 0xc6;
            case 0x81:
                return 0x9c;
            case 0x82:
                return 0xe6;
            case 0x83:
                return 0xb3;
            case 0x84:
                return 0xf1;
            case 0x85:
                return 0xb9;
            case 0x86:
                return 0xbf;
            case 0x87:
                return 0x9f;
            case 0x88:
                return 0xea;
            case 0x89:
                return 0xea;
            case 0x8a:
                return 0x00; // not contained in win1250
            case 0x8b:
                return 0x00; // not contained in win1250
            case 0x8c:
                return 0x8f;
            case 0x8d:
                return 0xaf;
            case 0x8e:
                return 0xa5;
            case 0x8f:
                return 0x8c;
            case 0x90:
                return 0xca;
            case 0x93:
                return 0xa3;
            case 0x94:
                return 0xf6;
            case 0x95:
                return 0xf3;
            case 0x96:
                return 0xaf;
            case 0x97:
                return 0x8f;
            case 0x99:
                return 0xd3;
            case 0x9a:
                return 0xd1;
            case 0x9c:
                return 0x00; // not contained in win1250
            case 0xa0:
                return 0xb9;
            case 0xa1:
                return 0xaf;
            case 0xa2:
                return 0xf3;
            case 0xa3:
                return 0xbf;
            case 0xa4:
                return 0x00; // not contained in win1250
            case 0xe1:
                return 0x8c;
            case 0xe3:
                return 0x00; // not contained in win1250
            case 0xf5:
                return 0x00; // not contained in win1250
            default:
                return c;
        }
    }

    namespace
    {
        // glyph index (the byte in the .fnt file), unicode code point; in the order of fontloader.cpp
        constexpr std::array<Substitution, 63> sSubstitutions{ {
            { 44, 0x201A }, // ‚ (Single Low-9 Quotation Mark, 0x82) => , (comma)
            { 44, 0x201E }, // „ (Double Low-9 Quotation Mark, 0x84) => , (comma)
            { 46, 0x2026 }, // … (Horizontal Ellipsis, 0x85) => . (period)
            { 43, 0x2020 }, // † (Dagger, 0x86) => + (plus sign)
            { 216, 0x2021 }, // ‡ (Double Dagger, 0x87) => ╫
            { 94, 0x02C6 }, // ˆ (Modifier Letter Circumflex Accent, 0x88) => ^ (caret)
            { 37, 0x2030 }, // ‰ (Per Mille Sign, 0x89) => % (percent sign)
            { 83, 0x0160 }, // Š (Latin Capital Letter S with Caron, 0x8A) => S (latin capital S)
            { 60, 0x2039 }, // ‹ (Single Left-Pointing Angle Quotation Mark, 0x8B) => < (less-than)
            { 79, 0x0152 }, // Œ (Latin Capital Ligature Oe, 0x8C) => O (latin capital O)
            { 90, 0x017D }, // Ž (Latin Capital Letter Z with Caron, 0x8E) => Z (latin capital Z)
            { 39, 0x2018 }, // ‘ (Left Single Quotation Mark, 0x91) => ' (apostrophe) (MW: backtick)
            { 39, 0x2019 }, // ’ (Right Single Quotation Mark, 0x92) => ' (apostrophe)
            { 34, 0x201C }, // “ (Left Double Quotation Mark, 0x93) => " (double quote mark)
            { 34, 0x201D }, // ” (Right Double Quotation Mark, 0x94) => " (double quote mark)
            { 45, 0x2013 }, // – (En Dash, 0x96) => - (hyphen)
            { 45, 0x2014 }, // — (Em Dash, 0x97) => - (hyphen)
            { 126, 0x02DC }, // ˜ (Small Tilde, 0x98) => ~ (tilde)
            { 84, 0x2122 }, // ™ (Trade Mark Sign, 0x99) => T (latin capital T)
            { 115, 0x0161 }, // š (Latin Small Letter S with Caron, 0x9A) => s (latin small S)
            { 62, 0x203A }, // › (Single Right-Pointing Angle Quotation Mark, 0x9B) => > (greater-than)
            { 111, 0x0153 }, // œ (Latin Small Ligature Oe, 0x9C) => o (latin small O)
            { 122, 0x017E }, // ž (Latin Small Letter Z with Caron, 0x9E) => z (latin small Z)
            { 89, 0x0178 }, // Ÿ (Latin Capital Letter Y with Diaeresis, 0x9F) => Y (latin capital Y)
            { 221, 0x00A6 }, // ¦ (Broken Bar, 0xA6) => ▌
            { 34, 0x00A8 }, // ¨ (Diaeresis) => " (double quote mark)
            { 99, 0x00A9 }, // © (Copyright Sign) => c (latin small C)
            { 45, 0x00AD }, // Soft Hyphen => - (hyphen)
            { 114, 0x00AE }, // ® (Registered Sign) => r (latin small R)
            { 95, 0x00AF }, // ¯ (Macron) => _ (underscore)
            { 51, 0x00B3 }, // ³ (Superscript Three) => 3 (three digit)
            { 39, 0x00B4 }, // ´ (Acute Accent) => ' (apostrophe)
            { 44, 0x00B8 }, // ¸ (Cedilla) => , (comma)
            { 49, 0x00B9 }, // ¹ (Superscript One) => 1 (one digit)
            { 65, 0x00C0 }, // À (Latin Capital Letter A with Grave) => A (latin capital A)
            { 65, 0x00C1 }, // Á (Latin Capital Letter A with Acute) => A (latin capital A)
            { 65, 0x00C2 }, // Â (Latin Capital Letter A with Circumflex) => A (latin capital A)
            { 65, 0x00C3 }, // Ã (Latin Capital Letter A with Tilde) => A (latin capital A)
            { 69, 0x00C8 }, // È (Latin Capital Letter E with Grave) => E (latin capital E)
            { 69, 0x00CA }, // Ê (Latin Capital Letter E with Circumflex) => E (latin capital E)
            { 69, 0x00CB }, // Ë (Latin Capital Letter E with Diaeresis) => E (latin capital E)
            { 73, 0x00CC }, // Ì (Latin Capital Letter I with Grave) => I (latin capital I)
            { 73, 0x00CD }, // Í (Latin Capital Letter I with Acute) => I (latin capital I)
            { 73, 0x00CE }, // Î (Latin Capital Letter I with Circumflex) => I (latin capital I)
            { 73, 0x00CF }, // Ï (Latin Capital Letter I with Diaeresis) => I (latin capital I)
            { 68, 0x00D0 }, // Ð (Latin Capital Letter Eth) => D (latin capital D)
            { 79, 0x00D2 }, // Ò (Latin Capital Letter O with Grave) => O (latin capital O)
            { 79, 0x00D3 }, // Ó (Latin Capital Letter O with Acute) => O (latin capital O)
            { 79, 0x00D4 }, // Ô (Latin Capital Letter O with Circumflex) => O (latin capital O)
            { 79, 0x00D5 }, // Õ (Latin Capital Letter O with Tilde) => O (latin capital O)
            { 120, 0x00D7 }, // × (Multiplication Sign) => x (latin small X)
            { 79, 0x00D8 }, // Ø (Latin Capital Letter O with Stroke) => O (latin capital O)
            { 85, 0x00D9 }, // Ù (Latin Capital Letter U with Grave) => U (latin capital U)
            { 85, 0x00DA }, // Ú (Latin Capital Letter U with Acute) => U (latin capital U)
            { 85, 0x00DB }, // Û (Latin Capital Letter U with Circumflex) => U (latin capital U)
            { 89, 0x00DD }, // Ý (Latin Capital Letter Y with Acute) => Y (latin capital Y)
            { 97, 0x00E3 }, // ã (Latin Small Letter A with Tilde) => a (latin small A)
            { 100, 0x00F0 }, // ð (Latin Small Letter Eth) => d (latin small D)
            { 111, 0x00F5 }, // õ (Latin Small Letter O with Tilde) => o (latin small O)
            { 111, 0x00F8 }, // ø (Latin Small Letter O with Stroke) => o (latin small O)
            { 121, 0x00FD }, // ý (Latin Small Letter Y with Acute) => y (latin small Y)
            { 69, 0x0401 }, // Ё (Cyrillic Capital Letter Io) => E (latin capital E)
            { 137, 0x0451 }, // ё (Cyrillic Small Letter Io) => ë (latin small E-diaeresis)
        } };

        // Some symbols must not be shown altogether (code points).
        constexpr std::array<unsigned, 4> sOmitted{ {
            0x2022, // • (Bullet, 0x95)
            0x00A4, // ¤ (Currency Sign)
            0x00A7, // § (Section Sign)
            0x00B6, // ¶ (Pilcrow Sign)
        } };
    }

    std::span<const Substitution> substitutions()
    {
        return sSubstitutions;
    }

    std::span<const unsigned> omittedCodePoints()
    {
        return sOmitted;
    }

    ToUTF8::FromType fontEncoding(ToUTF8::FromType gameEncoding)
    {
        // Morrowind's game fonts are CP437 where the data files are Windows-1252 (fontloader.cpp,
        // FontLoader::FontLoader).
        return gameEncoding == ToUTF8::WINDOWS_1252 ? ToUTF8::CP437 : gameEncoding;
    }

    unsigned long legacyToUnicode(unsigned char c, ToUTF8::Utf8Encoder& encoder, ToUTF8::FromType encoding)
    {
        // getUnicode of fontloader.cpp.
        if (encoding == ToUTF8::WINDOWS_1250) // Hack for polish font
        {
            const std::array<char, 2> str{ static_cast<char>(polishSwap(c)), '\0' };
            return utf8ToUnicode(encoder.getUtf8(std::string_view(str.data(), 1)));
        }
        const std::array<char, 2> str{ static_cast<char>(c), '\0' };
        return utf8ToUnicode(encoder.getUtf8(std::string_view(str.data(), 1)));
    }
}
