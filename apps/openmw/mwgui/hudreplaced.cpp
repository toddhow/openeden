#include "hudreplaced.hpp"

namespace MWGui
{
    ReplacedHudParts replacedHudParts(bool playerInNeutralCell, bool neutralCombat, bool rmlui)
    {
        ReplacedHudParts parts;
        if (!playerInNeutralCell || !neutralCombat || !rmlui)
            return parts;
        parts.mStats = true;
        parts.mWeaponSpell = true;
        parts.mEnemy = true;
        return parts;
    }
}
