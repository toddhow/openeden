#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PEXFILE_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PEXFILE_H

// The parsed form of a compiled Papyrus file (".pex"), task 0093 stage b. Plain data: a function is a vector of
// instructions whose operands live in a pool, so nothing here holds a pointer (a stack built from it can be saved).
//
// File layout (measured on every installed Skyrim SE and Fallout 4 script, research note 0046 sections 3 and 12). All
// numbers use the file's byte order; a "wstring" is a two byte length and that many bytes.
//   magic u32 (0xFA57C0DE; the bytes FA 57 C0 DE mean big endian, DE C0 57 FA little endian)
//   major u8, minor u8, game id u16, compile time u64, source wstring, user wstring, machine wstring
//   string table: count u16 (0xFFFF: a u32 count follows), then that many wstrings
//   debug: has u8; if set: modification time u64, function count u16, per function (object, state, function: u16 each,
//     type u8, instruction count u16, that many u16 line numbers); Fallout 4 adds property groups (object, group, doc
//     u16, flags u32, count u16, count u16 names) and struct orders (object, order u16, count u16, count u16 names)
//   user flags: count u16, per flag (name u16, bit u8)
//   objects: count u16, per object: name u16, size u32, parent u16, doc u16, [Fallout 4: const u8], flags u32,
//     auto state u16, [Fallout 4: structs: count u16, per struct name u16, member count u16, per member name u16,
//     type u16, flags u32, value, const u8, doc u16], variables: count u16, per variable name u16, type u16,
//     flags u32, value, [Fallout 4: const u8]; properties: count u16, per property name u16, type u16, doc u16,
//     flags u32, property flags u8 (1 read, 2 write, 4 auto), then for an auto property the backing variable u16,
//     otherwise a function without a name for each of read and write; states: count u16, per state name u16, count u16
//     of functions (with a name)
//   function: [name u16], return type u16, doc u16, flags u32, function flags u8 (1 global, 2 native), parameters
//     (count u16, per name u16 and type u16), locals (same), instructions (count u16, per instruction an opcode u8 and
//     its operands; callmethod, callparent and callstatic end with an integer value counting extra arguments)
//   value: type u8 (0 none, 1 identifier u16, 2 string u16, 3 integer i32, 4 float, 5 bool u8)

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/script/host.hpp>
#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus::Pex
{
    enum class Game : std::uint8_t
    {
        SkyrimSE, ///< Version 3.1 or 3.2, game id 1, big endian.
        Fallout4, ///< Version 3.9, game id 2, little endian.
    };

    enum class ValueType : std::uint8_t
    {
        None = 0,
        Identifier = 1,
        String = 2,
        Integer = 3,
        Float = 4,
        Bool = 5,
    };

    struct Value
    {
        ValueType mType = ValueType::None;
        std::uint16_t mString = 0; ///< Identifier and String: index into the string table.
        std::int32_t mInteger = 0; ///< Integer, and Bool as 0 or 1.
        float mFloat = 0.0f;
    };

    /// Opcode numbers. 0 to 35 exist in both games; 36 to 47 only in Fallout 4; 48 to 50 are the guard opcodes, which
    /// are refused (no installed script uses them).
    namespace Op
    {
        inline constexpr std::uint8_t Nop = 0;
        inline constexpr std::uint8_t IAdd = 1;
        inline constexpr std::uint8_t FAdd = 2;
        inline constexpr std::uint8_t ISub = 3;
        inline constexpr std::uint8_t FSub = 4;
        inline constexpr std::uint8_t IMul = 5;
        inline constexpr std::uint8_t FMul = 6;
        inline constexpr std::uint8_t IDiv = 7;
        inline constexpr std::uint8_t FDiv = 8;
        inline constexpr std::uint8_t IMod = 9;
        inline constexpr std::uint8_t Not = 10;
        inline constexpr std::uint8_t INeg = 11;
        inline constexpr std::uint8_t FNeg = 12;
        inline constexpr std::uint8_t Assign = 13;
        inline constexpr std::uint8_t Cast = 14;
        inline constexpr std::uint8_t CompareEq = 15;
        inline constexpr std::uint8_t CompareLt = 16;
        inline constexpr std::uint8_t CompareLe = 17;
        inline constexpr std::uint8_t CompareGt = 18;
        inline constexpr std::uint8_t CompareGe = 19;
        inline constexpr std::uint8_t Jmp = 20;
        inline constexpr std::uint8_t JmpT = 21;
        inline constexpr std::uint8_t JmpF = 22;
        inline constexpr std::uint8_t CallMethod = 23;
        inline constexpr std::uint8_t CallParent = 24;
        inline constexpr std::uint8_t CallStatic = 25;
        inline constexpr std::uint8_t Return = 26;
        inline constexpr std::uint8_t StrCat = 27;
        inline constexpr std::uint8_t PropGet = 28;
        inline constexpr std::uint8_t PropSet = 29;
        inline constexpr std::uint8_t ArrayCreate = 30;
        inline constexpr std::uint8_t ArrayLength = 31;
        inline constexpr std::uint8_t ArrayGetElement = 32;
        inline constexpr std::uint8_t ArraySetElement = 33;
        inline constexpr std::uint8_t ArrayFindElement = 34;
        inline constexpr std::uint8_t ArrayRFindElement = 35;
        inline constexpr std::uint8_t Is = 36;
        inline constexpr std::uint8_t StructCreate = 37;
        inline constexpr std::uint8_t StructGet = 38;
        inline constexpr std::uint8_t StructSet = 39;
        inline constexpr std::uint8_t ArrayFindStruct = 40;
        inline constexpr std::uint8_t ArrayRFindStruct = 41;
        inline constexpr std::uint8_t ArrayAdd = 42;
        inline constexpr std::uint8_t ArrayInsert = 43;
        inline constexpr std::uint8_t ArrayRemoveLast = 44;
        inline constexpr std::uint8_t ArrayRemove = 45;
        inline constexpr std::uint8_t ArrayClear = 46;
        inline constexpr std::uint8_t ArrayGetAllMatchingStructs = 47;
        inline constexpr std::uint8_t LockGuards = 48;
        inline constexpr std::uint8_t UnlockGuards = 49;
        inline constexpr std::uint8_t TryLockGuards = 50;
    }

    struct Instruction
    {
        std::uint8_t mOpcode = 0;
        std::uint32_t mFirstOperand = 0; ///< Index into Function::mOperands.
        std::uint16_t mFixedCount = 0;
        std::uint16_t mExtraCount = 0; ///< Extra call arguments (after the fixed operands and the count value).
    };

    struct Parameter
    {
        std::uint16_t mName = 0;
        std::uint16_t mType = 0;
    };

    struct Function
    {
        static constexpr std::uint8_t kGlobal = 1;
        static constexpr std::uint8_t kNative = 2;
        static constexpr std::uint16_t kNoName = 0xFFFF;

        std::uint16_t mName = kNoName; ///< String index; kNoName for a property accessor (it has no name in the file).
        std::uint16_t mReturnType = 0;
        std::uint16_t mDoc = 0;
        std::uint32_t mUserFlags = 0;
        std::uint8_t mFlags = 0;
        std::vector<Parameter> mParameters;
        std::vector<Parameter> mLocals;
        std::vector<Instruction> mInstructions;
        std::vector<Value> mOperands; ///< Fixed operands, the count value and the extra arguments, per instruction.

        bool isGlobal() const { return (mFlags & kGlobal) != 0; }
        bool isNative() const { return (mFlags & kNative) != 0; }
    };

    struct Variable
    {
        std::uint16_t mName = 0;
        std::uint16_t mType = 0;
        std::uint32_t mUserFlags = 0;
        Value mInitial;
        bool mIsConst = false;
    };

    struct Property
    {
        static constexpr std::uint8_t kRead = 1;
        static constexpr std::uint8_t kWrite = 2;
        static constexpr std::uint8_t kAuto = 4;

        std::uint16_t mName = 0;
        std::uint16_t mType = 0;
        std::uint16_t mDoc = 0;
        std::uint32_t mUserFlags = 0;
        std::uint8_t mFlags = 0;
        std::uint16_t mAutoVariable = 0; ///< String index of the backing variable, for an auto property.
        Function mGetter; ///< Valid when mFlags has kRead and not kAuto.
        Function mSetter; ///< Valid when mFlags has kWrite and not kAuto.
    };

    struct State
    {
        std::uint16_t mName = 0;
        std::vector<Function> mFunctions;
    };

    struct StructMember
    {
        std::uint16_t mName = 0;
        std::uint16_t mType = 0;
        std::uint32_t mUserFlags = 0;
        Value mInitial;
        bool mIsConst = false;
        std::uint16_t mDoc = 0;
    };

    struct StructDef
    {
        std::uint16_t mName = 0;
        std::vector<StructMember> mMembers;
    };

    struct Object
    {
        std::uint16_t mName = 0;
        std::uint16_t mParent = 0;
        std::uint16_t mDoc = 0;
        bool mIsConst = false;
        std::uint32_t mUserFlags = 0;
        std::uint16_t mAutoState = 0;
        openeden::script::PexSizeConvention mSizeConvention = openeden::script::PexSizeConvention::IncludesSelf;
        std::vector<StructDef> mStructs;
        std::vector<Variable> mVariables;
        std::vector<Property> mProperties;
        std::vector<State> mStates;
    };

    struct File
    {
        Game mGame = Game::SkyrimSE;
        std::uint8_t mMajor = 0;
        std::uint8_t mMinor = 0;
        bool mBigEndian = false;
        std::uint64_t mCompileTime = 0;
        std::string mSource;
        std::string mUser;
        std::string mMachine;
        std::vector<std::string> mStrings;
        std::vector<Object> mObjects;

        /// The string at \a index, or an empty view for an index past the table (a validated file has none).
        std::string_view str(std::uint32_t index) const
        {
            return index < mStrings.size() ? std::string_view(mStrings[index]) : std::string_view();
        }
    };

    struct ParseOptions
    {
        /// Refuse a file whose object size is not written the way Bethesda's compilers write it (tests, and the strict
        /// policy of the runtime). Off: the three observed conventions are accepted and counted.
        bool mStrictSizeConvention = false;
    };

    struct ParseResult
    {
        bool mOk = false;
        File mFile; ///< Valid only when mOk.
        std::vector<openeden::script::ErrorReport> mReports; ///< Errors and warnings, in file order.
    };

    /// Read and validate a file. Never throws, never executes. \a name is used in the reports.
    ParseResult parse(
        std::span<const std::uint8_t> bytes, std::string_view name, const ParseOptions& options = {}) noexcept;

    /// ASCII lower-casing; Papyrus names are compared without regard to case.
    std::string lowered(std::string_view text);
}

#endif
