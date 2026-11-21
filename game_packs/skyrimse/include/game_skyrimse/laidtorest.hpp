#pragma once

// Task 0342: "Laid to Rest" (MS14, Morthal) through the game's own dialogue, scenes and Papyrus (decompiled with
// houseCARL; section 1 of docs/tasks/0342-skyrim-laid-to-rest.md).

#include <cstdint>
#include <string_view>

namespace game_skyrimse::laidtorest
{
    /// `[extension.skyrimse] laid to rest` (off; needs `papyrus`, `actor packages`, `quest alias packages`): the
    /// Innkeeper alias of MS14 (GetIsEditorLocation + faction + GetIsID, run on an actor) is answered, JobJarlFaction
    /// at rank -1 counts as a membership (Idgrod and the other jarls hold it that way: the Jarl lines test GetInFaction
    /// == 1).
    inline constexpr std::string_view kKey = "laid to rest";

    inline constexpr std::uint32_t kMs14Quest = 0x25F3E; ///< MS14 "Laid to Rest"
    inline constexpr std::uint32_t kJobJarlFaction
        = 0x00050920; ///< FACT JobJarlFaction (VERIFIED: the fn_71 parameter 330016)
}
