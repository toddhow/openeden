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

    /// ENIT of an ENCH is 36 bytes (VERIFIED on the five masters: 0 other sizes).
    inline constexpr std::size_t kEnchantEnitBytes = 36;
    /// task 0309: four ENCH records of Skyrim.esm have an ENIT of 32 bytes; every field read lies in the first 24
    inline constexpr std::size_t kEnchantEnitShortBytes = 32;

    /// Task 0295: decodes an ENCH as a SpellRecord with mEnchantment set. ENIT (36 bytes, VERIFIED on the masters):
    /// cost +0, flags +4, cast type +8 (0 constant, 1 fire and forget, 2 concentration), enchant amount +12, delivery
    /// +16 (0 self, 1 touch, 2 aimed, 3 target actor, 4 target location), enchant type +20 (6 gear, 12 staff). The
    /// effects are EFID and EFIT as a SPEL's. The copy that casts has cost 0 (a staff and a weapon spend charge, not
    /// magicka; charge is not tracked).
    [[nodiscard]] bool decodeEnchantment(std::span<const std::byte> bytes, const Content::RawRecord& record,
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
    /// Task 0250: the same with the summoning archetypes (Summon Creature and Reanimate) counted when `summons` is on.
    [[nodiscard]] bool scrollArchetypeApplied(std::uint32_t archetype, bool summons);

    /// All, Some or None of the scroll's effects are ones the runtime applies; `effect` finds an MGEF by form id (null:
    /// unknown, which counts as not applied).
    [[nodiscard]] ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect);
    [[nodiscard]] ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect, bool summons);
}

#endif
