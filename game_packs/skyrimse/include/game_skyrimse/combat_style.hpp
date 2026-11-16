#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_STYLE_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_STYLE_H

// CSTY, a combat style (task 0190, stage S5-9; docs/tasks/0190-skyrim-combat-s5-9-ai.md section 2).
//
// VERIFIED on Skyrim.esm (145 records): CSGD is 40 bytes (136 records; 32 for 7, 8 for 2), CSME 32 (92) or 28 (52),
// CSCR 16 (136; 8 for 8), DATA 4 bytes, every float finite; the value ranges agree with the field names.
// RECALLED-VERIFY:
// the names and order of the fields (xEdit's), the meaning of the DATA bits (1 duelling, 2 flanking, 4 allow dual
// wielding). What a multiplier DOES in Skyrim is not in the data: the pack's AI uses them as weights (combat_ai.hpp).
//
// The defaults are the values of the record `DefaultCombatstyle` in Skyrim.esm: an NPC with no style, a block that is
// missing and a field the record is too short for all take them.

#include <game_skyrimse/land.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace game_skyrimse
{
    namespace CombatStyleFlag
    {
        inline constexpr std::uint32_t Dueling = 1;
        inline constexpr std::uint32_t Flanking = 2;
        inline constexpr std::uint32_t AllowDualWielding = 4;
    }

    struct CombatStyleRecord
    {
        RecordCommon mCommon;
        std::uint32_t mFlags = CombatStyleFlag::Dueling; ///< DATA
        // CSGD
        float mOffensive = 0.24f; ///< +0
        float mDefensive = 0.24f; ///< +4
        float mGroupOffensive = 0.51f; ///< +8
        float mMeleeScore = 1.f; ///< +12
        float mMagicScore = 1.f; ///< +16
        float mRangedScore = 1.f; ///< +20
        float mShoutScore = 1.f; ///< +24
        float mUnarmedScore = 1.f; ///< +28
        float mStaffScore = 1.f; ///< +32
        float mAvoidThreatChance = 0.2f; ///< +36
        // CSME
        float mAttackStaggered = 0.38f; ///< +0
        float mPowerAttackStaggered = 0.38f; ///< +4
        float mPowerAttackBlocking = 0.72f; ///< +8
        float mBash = 0.76f; ///< +12
        float mBashRecoil = 0.76f; ///< +16
        float mBashAttack = 0.38f; ///< +20
        float mBashPowerAttack = 0.38f; ///< +24
        float mSpecialAttack = 0.1f; ///< +28 (absent in a 28 byte block)
        // CSCR
        float mCircle = 0.22f; ///< +0
        float mFallback = 0.11f; ///< +4
        float mFlankDistance = 0.2f; ///< +8
        float mStalkTime = 0.2f; ///< +12
        bool mHasData = false; ///< a CSGD, CSME or CSCR was read
    };

    /// Decodes one CSTY record. A block of an unexpected size is counted (`context.mNotes.odd`) and left at its
    /// defaults; a non-finite float is odd too. False only for a hostile string.
    [[nodiscard]] bool decodeCombatStyle(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, CombatStyleRecord& out, std::string& why);
}

#endif
