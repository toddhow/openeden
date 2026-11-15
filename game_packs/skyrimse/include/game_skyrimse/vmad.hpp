#ifndef OPENEDEN_GAME_SKYRIMSE_VMAD_H
#define OPENEDEN_GAME_SKYRIMSE_VMAD_H

// The scripts a record carries (task 0176, milestone M-S6, Papyrus wiring). A VMAD subrecord lists the Papyrus scripts
// attached to a record, the values the editor stored for their properties, and, on a QUST, the stage fragments and the
// scripts of the quest's aliases. Pure parsing into plain structs; no engine, no Papyrus runtime.
//
// Layout (VERIFIED on Skyrim.esm: 974 QUST, 1,382 ACTI, 5,351 REFR, 822 ACHR, 805 NPC_, 5 DOOR and 6 CONT VMAD
// subrecords all end exactly at the end of the subrecord; the two object formats, 858 of 974 quests format 2):
//   i16 version, i16 object format (1 or 2), u16 script count
//   per script: wstring name, u8 status, u16 property count
//     per property: wstring name, u8 type, u8 status, the value
//       type 1 object: 8 bytes; format 2 = u16 unused, i16 alias (-1 none), u32 form id; format 1 = u32 form id, i16
//       alias, u16 unused (order of format 1 is RECALLED-VERIFY: both orders end exactly, only the ids and aliases of
//       format 2 were checked for plausibility: form id top byte 0, alias -1 or small); 2 wstring; 3 i32; 4 f32;
//       5 u8; 11..15 u32 count, then that many values of type - 10
//   a QUST then continues: u8 (always 2), u16 fragment count, wstring fragment script name, per fragment: u16 stage,
//     i16 (unknown), i32 log entry, u8 (unknown), wstring script name, wstring function name; then u16 alias count,
//     per alias: an object (8 bytes, the same format: the quest and the alias id), i16 version, i16 object format,
//     u16 script count and the scripts. Names of the fields are xEdit's wbVMAD, RECALLED-VERIFY; the walk ending
//     exactly at the end of every subrecord is the data's agreement.
// A wstring is a u16 length and that many bytes (Latin-1; names are ASCII in practice).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse::vmad
{
    enum class ValueKind : std::uint8_t
    {
        None,
        Object,
        String,
        Int,
        Float,
        Bool,
    };

    struct Value
    {
        ValueKind mKind = ValueKind::None;
        std::uint32_t mForm = 0; ///< Object: the form id (plugin-local until resolved)
        std::int16_t mAlias = -1; ///< Object: the alias id inside the quest, -1 for none
        std::string mText; ///< String
        std::int32_t mInteger = 0; ///< Int and Bool
        float mNumber = 0.f; ///< Float
    };

    struct Property
    {
        std::string mName;
        std::uint8_t mType = 0; ///< the VMAD type number (1 to 5, or 11 to 15 for an array)
        std::uint8_t mStatus = 0;
        bool mArray = false;
        Value mValue; ///< not an array
        std::vector<Value> mElements; ///< an array
    };

    struct Script
    {
        std::string mName;
        std::uint8_t mStatus = 0;
        std::vector<Property> mProperties;
    };

    struct Fragment
    {
        std::uint16_t mStage = 0;
        std::int32_t mLogEntry = 0;
        std::string mScript;
        std::string mFunction;
    };

    struct AliasScripts
    {
        std::uint32_t mQuest = 0;
        std::int16_t mAlias = -1;
        std::vector<Script> mScripts;
    };

    struct Vmad
    {
        std::int16_t mVersion = 0;
        std::int16_t mObjectFormat = 2;
        std::vector<Script> mScripts;
        bool mHasFragments = false;
        std::string mFragmentScript; ///< the quest's fragment script ("QF_..."), also in mScripts when it has one
        std::vector<Fragment> mFragments;
        std::vector<AliasScripts> mAliases;
    };

    struct Limits
    {
        std::uint32_t mMaxScripts = 256;
        std::uint32_t mMaxProperties = 4096;
        std::uint32_t mMaxArray = 65536;
        std::uint32_t mMaxFragments = 8192;
        std::uint32_t mMaxAliases = 4096;
        std::uint32_t mMaxNameBytes = 4096;
        std::uint32_t mMaxTextBytes = 65536;
    };

    struct ParseResult
    {
        bool mOk = false;
        std::string mError; ///< "offset N: what", empty when ok. `mVmad` holds what was read before the fault.
        Vmad mVmad;
    };

    /// \a withFragments: the subrecord belongs to a QUST (fragments and alias scripts follow the script list). The
    /// whole subrecord must be used; a trailing byte is an error.
    [[nodiscard]] ParseResult parse(std::span<const std::byte> bytes, bool withFragments, const Limits& limits = {});

    /// Applies \a resolve to every stored object form id (0 stays 0). A form id the function cannot resolve may return
    /// 0; the number of those is returned.
    std::size_t resolveForms(Vmad& vmad, const std::function<std::uint32_t(std::uint32_t)>& resolve);
    std::size_t resolveForms(std::vector<Script>& scripts, const std::function<std::uint32_t(std::uint32_t)>& resolve);

    /// The scripts of a placed reference (task 0181): its own, in order, then those of its base  base (null for none)
    /// in order, leaving out a base script whose name (compared without case) the reference already has: the
    /// reference's values win. The attach ordinal of a script is its place in this list, so the order is part of the
    /// save keys.
    [[nodiscard]] std::vector<Script> combineScripts(const std::vector<Script>& own, const std::vector<Script>* base);
}

#endif
