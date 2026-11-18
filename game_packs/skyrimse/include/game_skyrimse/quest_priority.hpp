// Task 0298: the order of quests when their alias packages are tried (Civil War: CW00A's forced greeting before CW's sandbox).
#ifndef OPENEDEN_GAME_SKYRIMSE_QUEST_PRIORITY_HPP
#define OPENEDEN_GAME_SKYRIMSE_QUEST_PRIORITY_HPP

#include <cstdint>

namespace game_skyrimse
{
    /// The order in which the packages that quest aliases give one actor are tried: the quest with the higher priority
    /// first (QUST DATA: CW00A has 71 and CW 70, so the forced greeting of CW00A is ahead of CW's map table sandbox),
    /// equal priorities by quest id so the order never depends on a hash. RECALLED-VERIFY: the data's numbers fit.
    [[nodiscard]] inline bool questPriorityBefore(
        std::uint8_t priorityA, std::uint32_t idA, std::uint8_t priorityB, std::uint32_t idB)
    {
        if (priorityA != priorityB)
            return priorityA > priorityB;
        return idA < idB;
    }
}

#endif
