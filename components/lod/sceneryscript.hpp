#ifndef OPENMW_COMPONENTS_LOD_SCENERYSCRIPT_H
#define OPENMW_COMPONENTS_LOD_SCENERYSCRIPT_H

// OpenEden world LOD (task 0096, stage b). Whether the script of an activator may change the activator itself in a
// way a far cluster cannot follow. The bake takes unnamed activators (lava planes, waterfalls, fences) into the
// clusters; vanilla gives nearly all of them a script (a sound loop, the lava's damage, the Ghostgate fence that
// disables itself when the Heart is destroyed), so "no script" alone would take none. A script that never enables,
// disables, moves, turns, scales or removes its object, and starts no other script, leaves the object as the cell
// stores it. The Ghostgate fence's script disables the fence, so it stays live.
//
// Pure text analysis of a Morrowind script (case-insensitive, `;` starts a comment, words are runs of letters, digits
// and underscores). It is a conservative heuristic, not a proof: another script can still act on the reference by its
// id; the cluster's override rule (LodClusters::overrideReference) covers that when the cell is loaded.

#include <cctype>
#include <string_view>

namespace Lod
{
    /// True when the text contains any of the commands that change the object the script runs on (or start other
    /// scripts) as a word outside a comment or string: enable, disable, setpos, setangle, setscale, move, moveworld,
    /// rotate, rotateworld, position, positioncell, removeme, setdelete, startscript, stopscript.
    inline bool scriptChangesItsObject(std::string_view text) noexcept
    {
        static constexpr std::string_view commands[]
            = { "enable", "disable", "setpos", "setangle", "setscale", "move", "moveworld", "rotate", "rotateworld",
                  "position", "positioncell", "removeme", "setdelete", "startscript", "stopscript" };
        std::size_t i = 0;
        const auto isWordChar = [](char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte < 0x80 && (std::isalnum(byte) || c == '_');
        };
        while (i < text.size())
        {
            const char c = text[i];
            if (c == ';') // a comment runs to the end of the line
            {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '"') // a string (a sound name) runs to the closing quote or the end of the line
            {
                ++i;
                while (i < text.size() && text[i] != '"' && text[i] != '\n')
                    ++i;
                if (i < text.size() && text[i] == '"')
                    ++i;
                continue;
            }
            if (!isWordChar(c))
            {
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < text.size() && isWordChar(text[i]))
                ++i;
            const std::string_view word = text.substr(start, i - start);
            for (const std::string_view command : commands)
            {
                if (word.size() != command.size())
                    continue;
                bool equal = true;
                for (std::size_t k = 0; k < word.size() && equal; ++k)
                    equal = std::tolower(static_cast<unsigned char>(word[k])) == command[k];
                if (equal)
                    return true;
            }
        }
        return false;
    }
}

#endif
