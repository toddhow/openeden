#ifndef OPENMW_GAME_MWGUI_HUDREPLACED_H
#define OPENMW_GAME_MWGUI_HUDREPLACED_H

namespace MWGui
{
    /// Which parts of Morrowind's MyGUI HUD a game pack's own RmlUi HUD replaces (task 0188, task 0180 decision 3).
    /// Only a neutral (Skyrim) session replaces anything, so a Morrowind session is never touched.
    struct ReplacedHudParts
    {
        bool mStats = false; ///< the health, magicka and stamina bars
        bool mWeaponSpell = false; ///< the selected weapon and spell icons (they name Morrowind items and spells)
        bool mEnemy = false; ///< the enemy health bar

        bool any() const { return mStats || mWeaponSpell || mEnemy; }
    };

    /// `playerInNeutralCell`: the player stands in a cell of a game pack's neutral world (a Morrowind session never
    /// does, whatever its settings say). `neutralCombat` is `[Game] neutral combat` and `rmlui` is `[GUI] rmlui`: the
    /// combat HUD is drawn by the RmlUi overlay for the player script of that combat, so it needs both.
    /// Everything else stays: the minimap, cell name, active effects, sneak indicator and drowning bar have no
    /// replacement, and the crosshair stays because the combat HUD's own crosshair draws nothing yet (task 0188 found
    /// no pixel of it in any frame; the MyGUI one is the only crosshair there is).
    ReplacedHudParts replacedHudParts(bool playerInNeutralCell, bool neutralCombat, bool rmlui);
}

#endif
