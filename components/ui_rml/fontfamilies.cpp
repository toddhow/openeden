#include "fontfamilies.hpp"

namespace UiRml
{
    std::string plainFontPath(const std::string& plainFont, const std::string& mainFont)
    {
        const std::string& chosen = plainFont.empty() ? mainFont : plainFont;
        return chosen.empty() ? std::string() : "fonts/" + chosen + ".fnt";
    }
}
