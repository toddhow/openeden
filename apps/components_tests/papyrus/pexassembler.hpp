#ifndef OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PEXASSEMBLER_H
#define OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PEXASSEMBLER_H

// Task 0093 stage a: the test-only ".pex" assembler. It writes valid compiled Papyrus files for Skyrim SE (version 3.2,
// big endian) and Fallout 4 (version 3.9, little endian) from a small C++ builder, so that no game asset is ever needed
// by a test. It is deterministic (the same builder and options give the same bytes), can write each of the three object
// size conventions found in the installed files, can shuffle the string table, and records where the interesting
// fields are (Layout) so a test can corrupt one deliberately.
//
//   PexAsm::ScriptBuilder script("MyQuest");
//   script.parent("Quest").autoProperty("Count", "Int", PexAsm::Int(3));
//   script.function("OnInit").local("::t", "Int").assign(PexAsm::Id("::t"), PexAsm::Int(1)).ret(PexAsm::Nil());
//   std::vector<std::uint8_t> bytes = script.build().mBytes;
//
// Operand order of each opcode follows the layout the reader validates (task 0093, pexfile.hpp); the order of the
// operands of the rarely used array and struct opcodes is RECALLED-VERIFY and is fixed in stage c, when the VM runs
// them.

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PexAsm
{
    enum class Target
    {
        SkyrimSE,
        Fallout4,
    };

    enum class SizeConv
    {
        IncludesSelf, ///< What Bethesda's compilers write.
        ExcludesSelf, ///< The bytes after the field, in the file's byte order.
        ExcludesSelfOtherOrder, ///< The bytes after the field, in the other byte order.
    };

    struct Operand
    {
        std::uint8_t mType = 0; ///< 0 none, 1 identifier, 2 string, 3 integer, 4 float, 5 bool
        std::string mText;
        std::int32_t mInteger = 0;
        float mFloat = 0.0f;
        std::uint8_t mBadType = 0; ///< Nonzero: written as the type byte instead of mType (corruption).
    };

    inline Operand Nil()
    {
        return Operand{};
    }
    inline Operand Id(std::string text)
    {
        return Operand{ 1, std::move(text), 0, 0.0f, 0 };
    }
    inline Operand Str(std::string text)
    {
        return Operand{ 2, std::move(text), 0, 0.0f, 0 };
    }
    inline Operand Int(std::int32_t value)
    {
        return Operand{ 3, {}, value, 0.0f, 0 };
    }
    inline Operand Flt(float value)
    {
        return Operand{ 4, {}, 0, value, 0 };
    }
    inline Operand Bool(bool value)
    {
        return Operand{ 5, {}, value ? 1 : 0, 0.0f, 0 };
    }

    namespace Op
    {
        inline constexpr std::uint8_t Nop
            = 0,
            IAdd = 1, FAdd = 2, ISub = 3, FSub = 4, IMul = 5, FMul = 6, IDiv = 7, FDiv = 8, IMod = 9, Not = 10,
            INeg = 11, FNeg = 12, Assign = 13, Cast = 14, CompareEq = 15, CompareLt = 16, CompareLe = 17,
            CompareGt = 18, CompareGe = 19, Jmp = 20, JmpT = 21, JmpF = 22, CallMethod = 23, CallParent = 24,
            CallStatic = 25, Return = 26, StrCat = 27, PropGet = 28, PropSet = 29, ArrayCreate = 30, ArrayLength = 31,
            ArrayGetElement = 32, ArraySetElement = 33, ArrayFindElement = 34, ArrayRFindElement = 35, Is = 36,
            StructCreate = 37, StructGet = 38, StructSet = 39, ArrayFindStruct = 40, ArrayRFindStruct = 41,
            ArrayAdd = 42, ArrayInsert = 43, ArrayRemoveLast = 44, ArrayRemove = 45, ArrayClear = 46,
            ArrayGetAllMatchingStructs = 47, LockGuards = 48, UnlockGuards = 49, TryLockGuards = 50;
    }

    struct Instr
    {
        std::uint8_t mOpcode = 0;
        std::vector<Operand> mFixed;
        std::vector<Operand> mExtra; ///< Extra call arguments (only for the three call opcodes).
        bool mWriteCount = true; ///< The call opcodes write the count value; false leaves it out (corruption).
    };

    struct FunctionDef
    {
        std::string mName;
        std::string mReturnType = "None";
        std::string mDoc;
        std::uint32_t mUserFlags = 0;
        std::uint8_t mFlags = 0; ///< 1 global, 2 native
        std::vector<std::pair<std::string, std::string>> mParameters;
        std::vector<std::pair<std::string, std::string>> mLocals;
        std::vector<Instr> mCode;
    };

    struct StateDef
    {
        std::string mName;
        std::vector<FunctionDef> mFunctions;
    };

    struct VariableDef
    {
        std::string mName;
        std::string mType;
        Operand mInitial;
        bool mConst = false;
        std::uint32_t mUserFlags = 0;
    };

    struct PropertyDef
    {
        std::string mName;
        std::string mType;
        std::string mDoc;
        std::uint8_t mFlags = 0; ///< 1 read, 2 write, 4 auto
        std::string mAutoVariable;
        FunctionDef mGetter;
        FunctionDef mSetter;
        std::uint32_t mUserFlags = 0;
    };

    struct StructMemberDef
    {
        std::string mName;
        std::string mType;
        Operand mInitial;
        bool mConst = false;
    };

    struct StructDefn
    {
        std::string mName;
        std::vector<StructMemberDef> mMembers;
    };

    /// Where the interesting fields of the LAST written file are (offsets from the start of the file). Zero means "not
    /// written" (for example the Fallout 4 only fields in a Skyrim file).
    struct Layout
    {
        std::size_t mMajor = 0;
        std::size_t mMinor = 0;
        std::size_t mGameId = 0;
        std::size_t mStringCount = 0;
        std::size_t mObjectCount = 0;
        std::size_t mObjectSize = 0; ///< The first object's size field.
        std::size_t mObjectParent = 0;
        std::size_t mVariableCount = 0;
        std::size_t mPropertyCount = 0;
        std::size_t mStateCount = 0;
        std::size_t mFirstStateFunctionCount = 0;
        std::size_t mFirstFunctionName = 0;
        std::size_t mFirstFunctionParamCount = 0;
        std::size_t mFirstFunctionLocalCount = 0;
        std::size_t mFirstFunctionInstrCount = 0;
        std::vector<std::size_t> mFirstFunctionInstr; ///< Offset of each instruction's opcode byte.
        std::size_t mStructCount = 0; ///< Fallout 4 only.
        std::size_t mFileSize = 0;
    };

    struct Options
    {
        Target mTarget = Target::SkyrimSE;
        SizeConv mSizeConv = SizeConv::IncludesSelf;
        std::uint32_t mShuffleSeed = 0; ///< Nonzero: a deterministic permutation of the string table.
        bool mDebugInfo = false; ///< Write a debug section.
        bool mExtendedStringCount = false; ///< Write the count as 0xFFFF followed by a u32.
        std::size_t mPadStrings = 0; ///< Unused strings appended to the table.
        std::size_t mTrailingBytes = 0; ///< Bytes after the last object.
        std::uint8_t mTrailingValue
            = 0xA5; ///< What those bytes are (zero is the padding seen in 165 installed mod files).
        int mMajorOverride = -1;
        int mMinorOverride = -1;
        int mGameIdOverride = -1;
        int mBigEndianOverride = -1; ///< -1: by target; 0 or 1 forces the byte order (and the magic follows it).
    };

    struct Built
    {
        std::vector<std::uint8_t> mBytes;
        Layout mLayout;
    };

    namespace Detail
    {
        class Writer
        {
        public:
            explicit Writer(bool bigEndian)
                : mBig(bigEndian)
            {
            }

            std::size_t size() const { return mBytes.size(); }
            std::vector<std::uint8_t>& bytes() { return mBytes; }

            void u8(std::uint8_t v) { mBytes.push_back(v); }
            void u16(std::uint16_t v)
            {
                if (mBig)
                {
                    u8(static_cast<std::uint8_t>(v >> 8));
                    u8(static_cast<std::uint8_t>(v));
                }
                else
                {
                    u8(static_cast<std::uint8_t>(v));
                    u8(static_cast<std::uint8_t>(v >> 8));
                }
            }
            void u32(std::uint32_t v)
            {
                if (mBig)
                {
                    u16(static_cast<std::uint16_t>(v >> 16));
                    u16(static_cast<std::uint16_t>(v));
                }
                else
                {
                    u16(static_cast<std::uint16_t>(v));
                    u16(static_cast<std::uint16_t>(v >> 16));
                }
            }
            void u64(std::uint64_t v)
            {
                if (mBig)
                {
                    u32(static_cast<std::uint32_t>(v >> 32));
                    u32(static_cast<std::uint32_t>(v));
                }
                else
                {
                    u32(static_cast<std::uint32_t>(v));
                    u32(static_cast<std::uint32_t>(v >> 32));
                }
            }
            void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
            /// A count that a two byte field must hold: more than 65,535 cannot be written.
            void count16(std::size_t n)
            {
                if (n > 0xFFFF)
                {
                    throw std::length_error("PexAsm: a count over 65,535 cannot be written in two bytes");
                }
                u16(static_cast<std::uint16_t>(n));
            }
            void wstr(const std::string& text)
            {
                u16(static_cast<std::uint16_t>(text.size()));
                for (char c : text)
                {
                    u8(static_cast<std::uint8_t>(c));
                }
            }
            void patch16(std::size_t at, std::uint16_t v)
            {
                Writer tmp(mBig);
                tmp.u16(v);
                mBytes[at] = tmp.mBytes[0];
                mBytes[at + 1] = tmp.mBytes[1];
            }
            void patch32(std::size_t at, std::uint32_t v)
            {
                Writer tmp(mBig);
                tmp.u32(v);
                for (std::size_t i = 0; i < 4; ++i)
                {
                    mBytes[at + i] = tmp.mBytes[i];
                }
            }

        private:
            bool mBig;
            std::vector<std::uint8_t> mBytes;
        };

        /// Strings in the order the table will hold them. With a frozen map the order is fixed and a lookup of an
        /// unknown string is a programming error of the assembler.
        class Interner
        {
        public:
            std::uint16_t get(const std::string& text)
            {
                auto found = mIndex.find(text);
                std::size_t original = 0;
                if (found == mIndex.end())
                {
                    original = mStrings.size();
                    mStrings.push_back(text);
                    mIndex.emplace(text, original);
                }
                else
                {
                    original = found->second;
                }
                return static_cast<std::uint16_t>(slotOf(original));
            }
            const std::vector<std::string>& strings() const { return mStrings; }
            void setPermutation(std::vector<std::size_t> permutation) { mPermutation = std::move(permutation); }
            std::size_t slotOf(std::size_t original) const
            {
                return mPermutation.empty() ? original : mPermutation[original];
            }

        private:
            std::vector<std::string> mStrings;
            std::unordered_map<std::string, std::size_t> mIndex;
            std::vector<std::size_t> mPermutation;
        };
    }

    class ScriptBuilder
    {
    public:
        explicit ScriptBuilder(std::string name)
            : mName(std::move(name))
        {
        }

        ScriptBuilder& parent(std::string name)
        {
            mParent = std::move(name);
            return *this;
        }
        ScriptBuilder& doc(std::string text)
        {
            mDoc = std::move(text);
            return *this;
        }
        ScriptBuilder& autoState(std::string name)
        {
            mAutoState = std::move(name);
            return *this;
        }
        ScriptBuilder& constObject(bool value = true)
        {
            mConst = value;
            return *this;
        }
        ScriptBuilder& userFlag(std::string name, std::uint8_t bit)
        {
            mUserFlags.emplace_back(std::move(name), bit);
            return *this;
        }
        ScriptBuilder& var(std::string name, std::string type, Operand initial = Nil())
        {
            mVariables.push_back(VariableDef{ std::move(name), std::move(type), std::move(initial), false, 0 });
            return *this;
        }
        /// An auto property and its backing variable "::<name>_var".
        ScriptBuilder& autoProperty(std::string name, std::string type, Operand initial = Nil(), bool readOnly = false)
        {
            const std::string backing = "::" + name + "_var";
            mVariables.push_back(VariableDef{ backing, type, std::move(initial), false, 0 });
            PropertyDef property;
            property.mName = std::move(name);
            property.mType = std::move(type);
            property.mFlags = readOnly ? 5 : 7;
            property.mAutoVariable = backing;
            mProperties.push_back(std::move(property));
            return *this;
        }
        /// A property with accessor functions; follow with getter() and/or setter() to fill them.
        ScriptBuilder& fullProperty(std::string name, std::string type, bool read = true, bool write = true)
        {
            PropertyDef property;
            property.mName = std::move(name);
            property.mType = type;
            property.mFlags = static_cast<std::uint8_t>((read ? 1 : 0) | (write ? 2 : 0));
            property.mGetter.mReturnType = type;
            property.mSetter.mParameters.emplace_back("aValue", type);
            mProperties.push_back(std::move(property));
            mCur = { Kind::Getter, mProperties.size() - 1, 0 };
            return *this;
        }
        ScriptBuilder& getter()
        {
            mCur = { Kind::Getter, mProperties.size() - 1, 0 };
            return *this;
        }
        ScriptBuilder& setter()
        {
            mCur = { Kind::Setter, mProperties.size() - 1, 0 };
            return *this;
        }
        ScriptBuilder& structDef(std::string name)
        {
            mStructs.push_back(StructDefn{ std::move(name), {} });
            return *this;
        }
        ScriptBuilder& member(std::string name, std::string type, Operand initial = Nil())
        {
            mStructs.back().mMembers.push_back(
                StructMemberDef{ std::move(name), std::move(type), std::move(initial), false });
            return *this;
        }
        /// Select (or create) a state; "" is the empty state that holds the default functions.
        ScriptBuilder& state(std::string name)
        {
            mStateName = std::move(name);
            mStateChosen = true;
            return *this;
        }
        ScriptBuilder& function(std::string name, std::string returnType = "None")
        {
            std::size_t stateIndex = findOrAddState(mStateChosen ? mStateName : std::string());
            FunctionDef def;
            def.mName = std::move(name);
            def.mReturnType = std::move(returnType);
            mStates[stateIndex].mFunctions.push_back(std::move(def));
            mCur = { Kind::Function, stateIndex, mStates[stateIndex].mFunctions.size() - 1 };
            return *this;
        }
        ScriptBuilder& global()
        {
            cur().mFlags |= 1;
            return *this;
        }
        ScriptBuilder& native()
        {
            cur().mFlags |= 2;
            return *this;
        }
        ScriptBuilder& flags(std::uint8_t value)
        {
            cur().mFlags = value;
            return *this;
        }
        ScriptBuilder& returns(std::string type)
        {
            cur().mReturnType = std::move(type);
            return *this;
        }
        ScriptBuilder& param(std::string name, std::string type)
        {
            cur().mParameters.emplace_back(std::move(name), std::move(type));
            return *this;
        }
        ScriptBuilder& local(std::string name, std::string type)
        {
            cur().mLocals.emplace_back(std::move(name), std::move(type));
            return *this;
        }
        ScriptBuilder& op(std::uint8_t opcode, std::vector<Operand> fixed, std::vector<Operand> extra = {})
        {
            Instr instr;
            instr.mOpcode = opcode;
            instr.mFixed = std::move(fixed);
            instr.mExtra = std::move(extra);
            cur().mCode.push_back(std::move(instr));
            return *this;
        }
        ScriptBuilder& rawInstr(Instr instr)
        {
            cur().mCode.push_back(std::move(instr));
            return *this;
        }
        ScriptBuilder& nop() { return op(Op::Nop, {}); }
        ScriptBuilder& assign(Operand dest, Operand value)
        {
            return op(Op::Assign, { std::move(dest), std::move(value) });
        }
        ScriptBuilder& binary(std::uint8_t opcode, Operand dest, Operand a, Operand b)
        {
            return op(opcode, { std::move(dest), std::move(a), std::move(b) });
        }
        ScriptBuilder& iadd(Operand dest, Operand a, Operand b)
        {
            return binary(Op::IAdd, std::move(dest), std::move(a), std::move(b));
        }
        ScriptBuilder& ret(Operand value) { return op(Op::Return, { std::move(value) }); }
        /// Jumps are relative to the jump instruction, counted in instructions.
        ScriptBuilder& jmp(std::int32_t offset) { return op(Op::Jmp, { Int(offset) }); }
        ScriptBuilder& jmpf(Operand condition, std::int32_t offset)
        {
            return op(Op::JmpF, { std::move(condition), Int(offset) });
        }
        ScriptBuilder& jmpt(Operand condition, std::int32_t offset)
        {
            return op(Op::JmpT, { std::move(condition), Int(offset) });
        }
        ScriptBuilder& callMethod(std::string function, Operand self, Operand dest, std::vector<Operand> args = {})
        {
            return op(Op::CallMethod, { Str(std::move(function)), std::move(self), std::move(dest) }, std::move(args));
        }
        ScriptBuilder& callStatic(
            std::string className, std::string function, Operand dest, std::vector<Operand> args = {})
        {
            return op(Op::CallStatic, { Str(std::move(className)), Str(std::move(function)), std::move(dest) },
                std::move(args));
        }
        ScriptBuilder& callParent(std::string function, Operand dest, std::vector<Operand> args = {})
        {
            return op(Op::CallParent, { Str(std::move(function)), std::move(dest) }, std::move(args));
        }

        /// The same script, written as a file.
        Built build(const Options& options = {}) const;

        const std::string& name() const { return mName; }
        std::size_t stateCount() const { return mStates.size(); }

    private:
        friend Built buildFile(const std::vector<ScriptBuilder>& scripts, const Options& options);

        enum class Kind
        {
            None,
            Function,
            Getter,
            Setter,
        };

        struct Cursor
        {
            Kind mKind = Kind::None;
            std::size_t mA = 0;
            std::size_t mB = 0;
        };

        FunctionDef& cur()
        {
            switch (mCur.mKind)
            {
                case Kind::Getter:
                    return mProperties[mCur.mA].mGetter;
                case Kind::Setter:
                    return mProperties[mCur.mA].mSetter;
                default:
                    return mStates[mCur.mA].mFunctions[mCur.mB];
            }
        }

        std::size_t findOrAddState(const std::string& name)
        {
            for (std::size_t i = 0; i < mStates.size(); ++i)
            {
                if (mStates[i].mName == name)
                {
                    return i;
                }
            }
            mStates.push_back(StateDef{ name, {} });
            return mStates.size() - 1;
        }

        std::string mName;
        std::string mParent;
        std::string mDoc;
        std::string mAutoState;
        bool mConst = false;
        std::vector<std::pair<std::string, std::uint8_t>> mUserFlags;
        std::vector<VariableDef> mVariables;
        std::vector<PropertyDef> mProperties;
        std::vector<StructDefn> mStructs;
        std::vector<StateDef> mStates;
        std::string mStateName;
        bool mStateChosen = false;
        Cursor mCur;
    };

    namespace Detail
    {
        inline void writeValue(Writer& w, Interner& strings, const Operand& value)
        {
            w.u8(value.mBadType != 0 ? value.mBadType : value.mType);
            switch (value.mType)
            {
                case 1:
                case 2:
                    w.u16(strings.get(value.mText));
                    break;
                case 3:
                    w.u32(static_cast<std::uint32_t>(value.mInteger));
                    break;
                case 4:
                    w.f32(value.mFloat);
                    break;
                case 5:
                    w.u8(value.mInteger != 0 ? 1 : 0);
                    break;
                default:
                    break;
            }
        }

        inline bool isCall(std::uint8_t opcode)
        {
            return opcode == Op::CallMethod || opcode == Op::CallParent || opcode == Op::CallStatic;
        }

        inline void writeFunction(Writer& w, Interner& strings, const FunctionDef& f, bool named, Layout* layout)
        {
            if (named)
            {
                if (layout != nullptr)
                {
                    layout->mFirstFunctionName = w.size();
                }
                w.u16(strings.get(f.mName));
            }
            w.u16(strings.get(f.mReturnType));
            w.u16(strings.get(f.mDoc));
            w.u32(f.mUserFlags);
            w.u8(f.mFlags);
            if (layout != nullptr)
            {
                layout->mFirstFunctionParamCount = w.size();
            }
            w.count16(f.mParameters.size());
            for (const auto& [name, type] : f.mParameters)
            {
                w.u16(strings.get(name));
                w.u16(strings.get(type));
            }
            if (layout != nullptr)
            {
                layout->mFirstFunctionLocalCount = w.size();
            }
            w.count16(f.mLocals.size());
            for (const auto& [name, type] : f.mLocals)
            {
                w.u16(strings.get(name));
                w.u16(strings.get(type));
            }
            if (layout != nullptr)
            {
                layout->mFirstFunctionInstrCount = w.size();
            }
            w.count16(f.mCode.size());
            for (const Instr& instr : f.mCode)
            {
                if (layout != nullptr)
                {
                    layout->mFirstFunctionInstr.push_back(w.size());
                }
                w.u8(instr.mOpcode);
                for (const Operand& operand : instr.mFixed)
                {
                    writeValue(w, strings, operand);
                }
                if (isCall(instr.mOpcode) && instr.mWriteCount)
                {
                    writeValue(w, strings, Int(static_cast<std::int32_t>(instr.mExtra.size())));
                    for (const Operand& operand : instr.mExtra)
                    {
                        writeValue(w, strings, operand);
                    }
                }
            }
        }

        inline std::vector<std::size_t> permutationFor(std::size_t count, std::uint32_t seed)
        {
            std::vector<std::size_t> permutation(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                permutation[i] = i;
            }
            std::uint32_t state = seed;
            for (std::size_t i = count; i > 1; --i)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                std::swap(permutation[i - 1], permutation[state % i]);
            }
            return permutation;
        }
    }

    inline Built buildFile(const std::vector<ScriptBuilder>& scripts, const Options& options)
    {
        using namespace Detail;
        const bool fo4 = options.mTarget == Target::Fallout4;
        const bool big = options.mBigEndianOverride >= 0 ? options.mBigEndianOverride == 1 : !fo4;

        Interner strings;
        Built built;
        // Two passes: the first collects the strings in order of first use, the second writes with the final indices
        // (a shuffle permutes them).
        std::vector<std::uint8_t> head;
        std::vector<std::uint8_t> body;
        Layout layout;
        std::size_t headSize = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                // Pad strings join the table after the used ones.
                for (std::size_t i = 0; i < options.mPadStrings; ++i)
                {
                    strings.get("pad" + std::to_string(i));
                }
                if (options.mShuffleSeed != 0)
                {
                    strings.setPermutation(permutationFor(strings.strings().size(), options.mShuffleSeed));
                }
            }
            layout = Layout{};
            Writer b(big);
            // Everything after the string table, as one block; offsets are relative to its start for now.
            b.u8(options.mDebugInfo ? 1 : 0);
            if (options.mDebugInfo)
            {
                b.u64(0x1122334455667788ull);
                std::size_t functionCount = 0;
                for (const ScriptBuilder& script : scripts)
                {
                    for (const StateDef& state : script.mStates)
                    {
                        functionCount += state.mFunctions.size();
                    }
                }
                b.u16(static_cast<std::uint16_t>(functionCount));
                for (const ScriptBuilder& script : scripts)
                {
                    for (const StateDef& state : script.mStates)
                    {
                        for (const FunctionDef& f : state.mFunctions)
                        {
                            b.u16(strings.get(script.mName));
                            b.u16(strings.get(state.mName));
                            b.u16(strings.get(f.mName));
                            b.u8(0);
                            b.count16(f.mCode.size());
                            for (std::size_t i = 0; i < f.mCode.size(); ++i)
                            {
                                b.u16(static_cast<std::uint16_t>(i + 1));
                            }
                        }
                    }
                }
                if (fo4)
                {
                    b.u16(0); // property groups
                    b.u16(0); // struct orders
                }
            }
            // User flags of the first script (a file has one; extra scripts add none).
            const std::size_t flagCount = scripts.empty() ? 0 : scripts[0].mUserFlags.size();
            b.u16(static_cast<std::uint16_t>(flagCount));
            if (!scripts.empty())
            {
                for (const auto& [name, bit] : scripts[0].mUserFlags)
                {
                    b.u16(strings.get(name));
                    b.u8(bit);
                }
            }
            layout.mObjectCount = b.size();
            b.count16(scripts.size());
            bool firstObject = true;
            for (const ScriptBuilder& script : scripts)
            {
                b.u16(strings.get(script.mName));
                const std::size_t sizeAt = b.size();
                if (firstObject)
                {
                    layout.mObjectSize = sizeAt;
                }
                b.u32(0);
                const std::size_t bodyStart = b.size();
                if (firstObject)
                {
                    layout.mObjectParent = b.size();
                }
                b.u16(strings.get(script.mParent));
                b.u16(strings.get(script.mDoc));
                if (fo4)
                {
                    b.u8(script.mConst ? 1 : 0);
                }
                b.u32(0);
                b.u16(strings.get(script.mAutoState));
                if (fo4)
                {
                    if (firstObject)
                    {
                        layout.mStructCount = b.size();
                    }
                    b.count16(script.mStructs.size());
                    for (const StructDefn& s : script.mStructs)
                    {
                        b.u16(strings.get(s.mName));
                        b.count16(s.mMembers.size());
                        for (const StructMemberDef& m : s.mMembers)
                        {
                            b.u16(strings.get(m.mName));
                            b.u16(strings.get(m.mType));
                            b.u32(0);
                            writeValue(b, strings, m.mInitial);
                            b.u8(m.mConst ? 1 : 0);
                            b.u16(strings.get(std::string()));
                        }
                    }
                }
                if (firstObject)
                {
                    layout.mVariableCount = b.size();
                }
                b.count16(script.mVariables.size());
                for (const VariableDef& v : script.mVariables)
                {
                    b.u16(strings.get(v.mName));
                    b.u16(strings.get(v.mType));
                    b.u32(v.mUserFlags);
                    writeValue(b, strings, v.mInitial);
                    if (fo4)
                    {
                        b.u8(v.mConst ? 1 : 0);
                    }
                }
                if (firstObject)
                {
                    layout.mPropertyCount = b.size();
                }
                b.count16(script.mProperties.size());
                for (const PropertyDef& p : script.mProperties)
                {
                    b.u16(strings.get(p.mName));
                    b.u16(strings.get(p.mType));
                    b.u16(strings.get(p.mDoc));
                    b.u32(p.mUserFlags);
                    b.u8(p.mFlags);
                    if ((p.mFlags & 4) != 0)
                    {
                        b.u16(strings.get(p.mAutoVariable));
                    }
                    else
                    {
                        if ((p.mFlags & 1) != 0)
                        {
                            writeFunction(b, strings, p.mGetter, false, nullptr);
                        }
                        if ((p.mFlags & 2) != 0)
                        {
                            writeFunction(b, strings, p.mSetter, false, nullptr);
                        }
                    }
                }
                if (firstObject)
                {
                    layout.mStateCount = b.size();
                }
                b.count16(script.mStates.size());
                bool firstFunction = true;
                bool firstState = true;
                for (const StateDef& state : script.mStates)
                {
                    b.u16(strings.get(state.mName));
                    if (firstObject && firstState)
                    {
                        layout.mFirstStateFunctionCount = b.size();
                    }
                    firstState = false;
                    b.count16(state.mFunctions.size());
                    for (const FunctionDef& f : state.mFunctions)
                    {
                        writeFunction(b, strings, f, true, firstObject && firstFunction ? &layout : nullptr);
                        firstFunction = false;
                    }
                }
                const std::size_t bodyLength = b.size() - bodyStart;
                if (options.mSizeConv == SizeConv::ExcludesSelfOtherOrder)
                {
                    // The bytes after the field counted, written in the other byte order than the file's.
                    Writer other(!big);
                    other.u32(static_cast<std::uint32_t>(bodyLength));
                    for (std::size_t i = 0; i < 4; ++i)
                    {
                        b.bytes()[sizeAt + i] = other.bytes()[i];
                    }
                }
                else
                {
                    const std::size_t extra = options.mSizeConv == SizeConv::IncludesSelf ? 4 : 0;
                    b.patch32(sizeAt, static_cast<std::uint32_t>(bodyLength + extra));
                }
                firstObject = false;
            }
            for (std::size_t i = 0; i < options.mTrailingBytes; ++i)
            {
                b.u8(options.mTrailingValue);
            }
            body = std::move(b.bytes());

            // The front part.
            Writer h(big);
            if (big)
            {
                h.u32(0xFA57C0DEu);
            }
            else
            {
                h.u8(0xDE);
                h.u8(0xC0);
                h.u8(0x57);
                h.u8(0xFA);
            }
            layout.mMajor = h.size();
            h.u8(static_cast<std::uint8_t>(options.mMajorOverride >= 0 ? options.mMajorOverride : 3));
            layout.mMinor = h.size();
            h.u8(static_cast<std::uint8_t>(options.mMinorOverride >= 0 ? options.mMinorOverride : (fo4 ? 9 : 2)));
            layout.mGameId = h.size();
            h.u16(static_cast<std::uint16_t>(options.mGameIdOverride >= 0 ? options.mGameIdOverride : (fo4 ? 2 : 1)));
            h.u64(0x0123456789ABCDEFull);
            h.wstr("generated.psc");
            h.wstr("gen");
            h.wstr("genhost");
            layout.mStringCount = h.size();
            const std::size_t count = strings.strings().size();
            if (options.mExtendedStringCount)
            {
                h.u16(0xFFFF);
                h.u32(static_cast<std::uint32_t>(count));
            }
            else
            {
                h.u16(static_cast<std::uint16_t>(count));
            }
            // Table in final slot order.
            std::vector<const std::string*> slots(count, nullptr);
            for (std::size_t i = 0; i < count; ++i)
            {
                slots[strings.slotOf(i)] = &strings.strings()[i];
            }
            for (const std::string* text : slots)
            {
                h.wstr(*text);
            }
            head = std::move(h.bytes());
            headSize = head.size();
        }

        built.mBytes = head;
        built.mBytes.insert(built.mBytes.end(), body.begin(), body.end());
        // Header offsets are absolute already; the rest were recorded relative to the body.
        std::size_t* bodyOffsets[] = { &layout.mObjectCount, &layout.mObjectSize, &layout.mObjectParent,
            &layout.mVariableCount, &layout.mPropertyCount, &layout.mStateCount, &layout.mFirstStateFunctionCount,
            &layout.mFirstFunctionName, &layout.mFirstFunctionParamCount, &layout.mFirstFunctionLocalCount,
            &layout.mFirstFunctionInstrCount, &layout.mStructCount };
        for (std::size_t* offset : bodyOffsets)
        {
            if (*offset != 0)
            {
                *offset += headSize;
            }
        }
        for (std::size_t& offset : layout.mFirstFunctionInstr)
        {
            offset += headSize;
        }
        layout.mFileSize = built.mBytes.size();
        built.mLayout = layout;
        return built;
    }

    inline Built ScriptBuilder::build(const Options& options) const
    {
        return buildFile({ *this }, options);
    }

    /// Overwrite a 16 or 32 bit field of a built file in the file's byte order.
    inline void patch16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value, bool bigEndian)
    {
        bytes[at] = static_cast<std::uint8_t>(bigEndian ? value >> 8 : value);
        bytes[at + 1] = static_cast<std::uint8_t>(bigEndian ? value : value >> 8);
    }
    inline void patch32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value, bool bigEndian)
    {
        patch16(bytes, at + (bigEndian ? 0 : 2), static_cast<std::uint16_t>(value >> 16), bigEndian);
        patch16(bytes, at + (bigEndian ? 2 : 0), static_cast<std::uint16_t>(value), bigEndian);
    }
    inline std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t at, bool bigEndian)
    {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const std::uint32_t byte = bytes[at + i];
            v |= bigEndian ? byte << (24 - 8 * i) : byte << (8 * i);
        }
        return v;
    }
}

#endif
