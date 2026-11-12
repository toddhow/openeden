#ifndef OPENEDEN_GAME_SKYRIMSE_VOICE_H
#define OPENEDEN_GAME_SKYRIMSE_VOICE_H

// The names of Skyrim SE voice files (task 0143, milestone M-S4). Pure string rules, no file access.
//
// A spoken line of an INFO response is the file
//
//   Sound\Voice\<plugin file>\<voice type editor id>\<stem>_<INFO form id>_<response number>.fuz
//
// all lower case in this code (the VFS compares case-insensitively). The stem is made from the editor ids of the
// topic's quest and of the topic, and its rule was SETTLED BY DATA (task 0143 stage log): over the 34,290 voice files
// of Skyrim.esm that belong to an INFO response of Skyrim.esm, 34,252 (99.89 percent) follow it; the other 38 carry
// names of records that were renamed after the line was recorded and cannot be derived. The INFO form id is the id AS
// STORED in the plugin whose folder holds the file (its own file-local form id, master index included), 8 hex digits.
// The response number is the TRDT number of the response (1 for the first line).
//
// Used with the plugin of the INFO record that wins the load order (Skyrim.esm's INFO overridden by Update.esm is
// looked for under update.esm, the folder of the record that is used).

#include <cstdint>
#include <string>
#include <string_view>

namespace game_skyrimse
{
    /// Quest editor id + "_" + topic editor id, lower case. When that is longer than 26 characters the quest part is
    /// cut to its first 10 characters and the whole is cut to 26 characters. A topic with no editor id gives
    /// "<quest>_". Editor ids with non-ASCII bytes are lower-cased ASCII only.
    [[nodiscard]] std::string voiceStem(std::string_view questEditorId, std::string_view topicEditorId);

    /// "voice/<plugin>/<voice type>/<stem>_<storedId as 8 hex digits>_<number>.fuz", lower case, relative to the
    /// sound folder. Empty when the plugin, the voice type or the stem is empty, or when one of them contains a path
    /// separator, a ".." or a control character (the names come from data files).
    [[nodiscard]] std::string voicePath(std::string_view plugin, std::string_view voiceType, std::string_view stem,
        std::uint32_t storedInfoId, unsigned responseNumber);
}

#endif
