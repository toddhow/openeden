#ifndef OPENEDEN_COMPONENTS_UI_RML_CODEPAGE_H
#define OPENEDEN_COMPONENTS_UI_RML_CODEPAGE_H

#include <span>
#include <string_view>

#include <components/toutf8/toutf8.hpp>

namespace UiRml
{
    /// One substitution of the font loader: the glyph at index `mGlyph` of a Morrowind .fnt file is also drawn for the
    /// unicode code point `mCodePoint` (a character the game font lacks, shown as a similar one).
    struct Substitution
    {
        unsigned mGlyph;
        unsigned mCodePoint;
    };

    /// Copy of fontloader.cpp's helper: decodes the first UTF-8 sequence of `utf8` (0 for an empty string); throws
    /// std::logic_error("not a UTF-8 string") for a malformed one.
    unsigned long utf8ToUnicode(std::string_view utf8);

    /// Copy of fontloader.cpp's Polish font hack (Windows-1250 data files): a glyph byte that Morrowind's Polish font
    /// stores in a swapped place.
    unsigned char polishSwap(unsigned char c);

    /// The unicode code point a glyph byte of a Morrowind font stands for (fontloader.cpp: getUnicode). `encoding` is
    /// the FONT encoding (fontEncoding of the data files' encoding).
    unsigned long legacyToUnicode(unsigned char c, ToUTF8::Utf8Encoder& encoder, ToUTF8::FromType encoding);

    /// The encoding the .fnt glyph bytes use for a data files encoding: CP437 where the data files are Windows-1252.
    ToUTF8::FromType fontEncoding(ToUTF8::FromType gameEncoding);

    /// The 63 substitutions, in the order fontloader.cpp lists them (grouped by glyph index when emitted).
    std::span<const Substitution> substitutions();

    /// The code points the loader hides altogether (bullet, currency sign, section sign, pilcrow).
    std::span<const unsigned> omittedCodePoints();
}

#endif
