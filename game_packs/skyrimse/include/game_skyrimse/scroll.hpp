#ifndef OPENEDEN_GAME_SKYRIMSE_SCROLL_H
#define OPENEDEN_GAME_SKYRIMSE_SCROLL_H

// Scrolls (task 0234, milestone M-S6): a SCRL record read as a SpellRecord the magic runtime of task 0186 can cast.
//
// What the data settles (the five masters, `testskyrimsescrollsreal.cpp`): a SCRL is DATA (value, weight), an SPIT of
// exactly 36 bytes laid out as a SPEL's (cost, flags, type, charge time, cast type, delivery, cast duration, range,
// half cost perk), and EFID + EFIT effects like a SPEL (EFIT 12 bytes). The cast type of a scroll is 3 ("Scroll") in
// all
// but two records, which are 2 (concentration: Detect Life and Telekinesis); the SPEL cast types 0, 1 and 2 are the
// only
// ones the runtime knows, so the copy that casts is normalised: 3 becomes fire and forget, and the cost is 0 (a
// scroll spends no magicka; its gold value is in DATA, not SPIT).
//
// Hostile input: everything decodeSpell does (sizes checked, non finite floats refused); nothing throws.

#include "game_skyrimse/perks.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace game_skyrimse
{
    /// SPIT +16 of a scroll (xEdit's "Scroll" cast type; RECALLED-VERIFY the name, the data shows the value 3).
    inline constexpr std::uint32_t kScrollCastType = 3;

    /// Decodes a SCRL as a SpellRecord with mScroll set, mCost 0 and the cast type normalised (see above).
    [[nodiscard]] bool decodeScroll(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SpellRecord& out, std::string& why);

    /// How much of a scroll the magic runtime applies, by its effect archetypes (the cases of
    /// SkyrimCombat::applyOneEffect when this was written: value modifier, peak value modifier, dual value modifier,
    /// absorb, stagger, demoralize and accumulate magnitude). Pure; used by the survey and the log.
    enum class ScrollSupport : std::uint8_t
    {
        None, ///< no effect the runtime applies (or no effects at all)
        Some, ///< at least one effect it applies and at least one it does not
        All, ///< every effect is one it applies
    };
    [[nodiscard]] bool scrollArchetypeApplied(std::uint32_t archetype);

    /// All, Some or None of the scroll's effects are ones the runtime applies; `effect` finds an MGEF by form id (null:
    /// unknown, which counts as not applied).
    [[nodiscard]] ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect);
}

#endif
