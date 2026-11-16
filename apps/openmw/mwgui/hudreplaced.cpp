#include "hudreplaced.hpp"

namespace MWGui
{
    ReplacedHudParts replacedHudParts(
        bool playerInNeutralCell, bool neutralCombat, bool rmlui, bool hudStyle, bool sneakEye, bool compass)
    {
        ReplacedHudParts parts;
        if (!playerInNeutralCell || !neutralCombat || !rmlui)
            return parts;
        parts.mStats = true;
        parts.mWeaponSpell = true;
        parts.mEnemy = true;
        parts.mCrosshair = true;
        parts.mSneak = hudStyle && sneakEye;
        parts.mMinimap = hudStyle && compass;
        return parts;
    }
}
