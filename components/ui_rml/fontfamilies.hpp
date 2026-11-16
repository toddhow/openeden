#ifndef OPENMW_COMPONENTS_UI_RML_FONTFAMILIES_H
#define OPENMW_COMPONENTS_UI_RML_FONTFAMILIES_H

#include <string>
#include <string_view>

namespace UiRml
{
    /// The bitmap font family of the whole overlay (the game's `Fonts_Font_0`, Magic Cards in Morrowind).
    inline constexpr std::string_view kMainFontFamily = "Morrowind";
    /// A second family for text that must be legible (task 0208): the game's `Fonts_Font_1` (Century Gothic). Magic
    /// Cards draws the capital P as a looped letter that reads as D at 16 px, in MyGUI as well. A document asks for it
    /// with the class `plain-font` (files/data/ui/base.rcss).
    inline constexpr std::string_view kPlainFontFamily = "MorrowindPlain";

    /// The VFS path of the plain family's font file: its own when the game names one, else the main font's (so the
    /// family name still resolves), else empty.
    [[nodiscard]] std::string plainFontPath(const std::string& plainFont, const std::string& mainFont);
}

#endif
