#ifndef OPENEDEN_ANIMATION_TEXTKEYS_H
#define OPENEDEN_ANIMATION_TEXTKEYS_H

#include <string_view>

#include "tags.hpp"

namespace Anim
{
    /// The one parser of text keys (task 0124, stage C1). Every animation source gives its keys in the same form
    /// to the same table: a lower-case `"group: key"` at a time in seconds. The readers differ only in how the
    /// text arrives: a Morrowind .kf holds it in a record (addTextKeys), a COLLADA model's `.txt` file holds one
    /// line per key (addTextKeyLine).

    /// Adds the keys of one text key record: `text` may hold several keys separated by CR or LF; each is trimmed
    /// and lower-cased, and empty ones are dropped. All get `time`. As OpenMW's `NiTextKeyExtraData` handling.
    void addTextKeys(Tags& tags, float time, std::string_view text);

    /// Adds the key of one line of a `.txt` key file: `<key> <time>`, the time after the last space (a time that is
    /// not a number is 0), the key trimmed and lower-cased. A line with no space, or whose key is empty, adds
    /// nothing and returns false. As OpenMW 0.51 read them (RetrieveAnimationsVisitor).
    bool addTextKeyLine(Tags& tags, std::string_view line);
}

#endif
