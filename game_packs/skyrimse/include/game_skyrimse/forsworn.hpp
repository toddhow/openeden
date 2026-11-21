#pragma once

// Task 0337: "The Forsworn Conspiracy" (MS01, Markarth) through the game's own trigger box, scene, dialogue and Papyrus
// (decompiled with houseCARL; section 1 of docs/tasks/0337-skyrim-forsworn-conspiracy.md).
//
// The chain: the box `MarkarthWorldIntroSceneTrigger` (defaultsetStageTrigSCRIPT) starts the quest MS01IntroWorldScene
// at stage 10; its scene is the market argument, stage 12's fragment has Weylin play a paired kill move on Margret
// (`Actor.PlayIdleWithTarget(SneakKillMove, Margret)`), Weylin's death sets stage 200 and the end scene stage 300.

#include <cstdint>
#include <string>
#include <string_view>

namespace game_skyrimse::forsworn
{
    /// `[extension.skyrimse] forsworn conspiracy` (off; needs `papyrus`, `actor packages`, `quest alias packages`): box
    /// triggers fire for the player and a quest may move actors near him (the same world as the Daedric quests), and a
    /// paired kill move a script plays kills its target.
    inline constexpr std::string_view kKey = "forsworn conspiracy";

    inline constexpr std::uint32_t kIntroQuest = 0xAF75F; ///< MS01IntroWorldScene
    inline constexpr std::uint32_t kMs01Quest = 0x18B4B; ///< MS01 "The Forsworn Conspiracy"

    /// True when an idle of this editor id is a paired kill move (the IDLE records of the data: `KillMove...`,
    /// `SneakKillMove`, `pa_...KillMove...`; compared without case). The data gives its animation event as
    /// `pa_KillMove...` for all of them, but the pack does not read IDLE records, so the editor id is the rule.
    [[nodiscard]] inline bool isKillMoveName(std::string_view editorId)
    {
        std::string lower(editorId);
        for (char& ch : lower)
            ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        return lower.find("killmove") != std::string::npos;
    }
}
