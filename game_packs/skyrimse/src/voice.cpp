#include "game_skyrimse/voice.hpp"

#include <cstdio>

namespace game_skyrimse
{
    namespace
    {
        // SETTLED BY DATA (task 0143, stage log): 26 characters is the length of "<quest>_<topic>" in every file name
        // of Skyrim.esm's voice folders that is cut at all, and the quest is cut to 10 only when the whole is cut.
        constexpr std::size_t kMaxStemLength = 26;
        constexpr std::size_t kCutQuestLength = 10;

        char lowerAscii(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        std::string lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                c = lowerAscii(c);
            return out;
        }

        /// A name that goes into a path: not empty, no separator, no "..", no control character.
        bool safeComponent(std::string_view s)
        {
            if (s.empty() || s.find("..") != std::string_view::npos)
                return false;
            for (const char c : s)
            {
                if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
                    return false;
            }
            return true;
        }
    }

    std::string voiceStem(std::string_view questEditorId, std::string_view topicEditorId)
    {
        const std::string quest = lower(questEditorId);
        const std::string topic = lower(topicEditorId);
        std::string stem = quest + "_" + topic;
        if (stem.size() > kMaxStemLength)
        {
            stem = quest.substr(0, kCutQuestLength) + "_" + topic;
            if (stem.size() > kMaxStemLength)
                stem.resize(kMaxStemLength);
        }
        return stem;
    }

    std::string voicePath(std::string_view plugin, std::string_view voiceType, std::string_view stem,
        std::uint32_t storedInfoId, unsigned responseNumber)
    {
        if (!safeComponent(plugin) || !safeComponent(voiceType) || !safeComponent(stem))
            return {};
        char tail[40];
        std::snprintf(tail, sizeof(tail), "_%08x_%u.fuz", static_cast<unsigned>(storedInfoId), responseNumber);
        return "voice/" + lower(plugin) + "/" + lower(voiceType) + "/" + lower(stem) + tail;
    }
}
