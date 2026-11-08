#include "pexfile.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_map>
#include <unordered_set>

#include "papyruslimits.hpp"

namespace ScriptPapyrus::Pex
{
    namespace
    {
        using openeden::script::ErrorReport;
        using openeden::script::PexSizeConvention;
        using openeden::script::Severity;
        namespace Limits = ScriptPapyrus::Limits;

        // Thrown inside the reader only; parse() catches everything, so nothing crosses the contract.
        struct ParseError
        {
            std::string mMessage;
        };

        // How an operand must look. V: any value. D: an identifier (a destination). N: an identifier or a string (a
        // name). J: an integer (a jump offset).
        enum class Kind : char
        {
            Any = 'V',
            Dest = 'D',
            Name = 'N',
            Jump = 'J',
        };

        struct Shape
        {
            const char* mKinds = "";
            bool mCall = false;
        };

        // The operand layout of each opcode (pexfile.hpp). Opcodes whose operand order is not certain use V for every
        // operand: the layout (how many, which types) is what the reader needs, the order is the VM's business.
        constexpr Shape kShapes[] = {
            { "" }, // 0 nop
            { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, // 1-9
            { "DV" }, { "DV" }, { "DV" }, // 10-12 not, ineg, fneg
            { "DV" }, { "DV" }, // 13 assign, 14 cast
            { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, { "DVV" }, // 15-19 compares
            { "J" }, // 20 jmp
            { "VJ" }, { "VJ" }, // 21 jmpt, 22 jmpf
            { "NVD", true }, // 23 callmethod
            { "ND", true }, // 24 callparent
            { "NND", true }, // 25 callstatic
            { "V" }, // 26 return
            { "DVV" }, // 27 strcat
            { "NVD" }, // 28 propget
            { "NVV" }, // 29 propset
            { "VV" }, // 30 array_create
            { "VV" }, // 31 array_length
            { "VVV" }, { "VVV" }, // 32 getelement, 33 setelement
            { "VVVV" }, { "VVVV" }, // 34 find, 35 rfind
            { "DVN" }, // 36 is: destination, value, the type name (an identifier or a string in installed files)
            { "V" }, // 37 struct_create
            { "VVV" }, { "VVV" }, // 38 struct_get, 39 struct_set
            { "VVVVV" }, { "VVVVV" }, // 40, 41 array_findstruct, rfindstruct
            { "VVV" }, { "VVV" }, // 42 add, 43 insert
            { "V" }, // 44 removelast
            { "VVV" }, // 45 remove
            { "V" }, // 46 clear
            { "VVVV" }, // 47 getallmatchingstructs
        };
        constexpr std::size_t kShapeCount = sizeof(kShapes) / sizeof(kShapes[0]);
        constexpr std::uint8_t kLastSkyrimOpcode = 35;
        constexpr std::uint8_t kLastFallout4Opcode = 47;
        constexpr std::uint8_t kFirstGuardOpcode = 48;
        constexpr std::uint8_t kLastGuardOpcode = 50;

        constexpr std::uint32_t kNoId = 0xFFFFFFFFu;

        class Reader
        {
        public:
            Reader(std::span<const std::uint8_t> bytes, const ParseOptions& options)
                : mBytes(bytes)
                , mOptions(options)
            {
            }

            File read(std::vector<ErrorReport>& reports, std::string_view name);

        private:
            [[noreturn]] void fail(const std::string& text) const
            {
                std::string message;
                if (!mObject.empty())
                {
                    message += mObject;
                    if (!mFunction.empty())
                    {
                        message += "." + mFunction;
                        if (mInstruction >= 0)
                        {
                            message += "@" + std::to_string(mInstruction);
                        }
                    }
                    message += ": ";
                }
                message += text + " (offset " + std::to_string(mPosition) + ")";
                throw ParseError{ std::move(message) };
            }

            std::size_t remaining() const { return mBytes.size() - mPosition; }

            void need(std::size_t count, const char* what) const
            {
                if (remaining() < count)
                {
                    fail(std::string("unexpected end of file reading ") + what);
                }
            }

            std::uint8_t u8(const char* what)
            {
                need(1, what);
                return mBytes[mPosition++];
            }

            std::uint16_t u16(const char* what)
            {
                need(2, what);
                const std::uint8_t a = mBytes[mPosition];
                const std::uint8_t b = mBytes[mPosition + 1];
                mPosition += 2;
                return mBig ? static_cast<std::uint16_t>((a << 8) | b) : static_cast<std::uint16_t>((b << 8) | a);
            }

            std::uint32_t u32(const char* what)
            {
                need(4, what);
                std::uint32_t value = 0;
                for (std::size_t i = 0; i < 4; ++i)
                {
                    const std::uint32_t byte = mBytes[mPosition + i];
                    value |= mBig ? byte << (24 - 8 * i) : byte << (8 * i);
                }
                mPosition += 4;
                return value;
            }

            std::uint64_t u64(const char* what)
            {
                const std::uint64_t first = u32(what);
                const std::uint64_t second = u32(what);
                return mBig ? (first << 32) | second : (second << 32) | first;
            }

            std::string wstring(const char* what)
            {
                const std::size_t length = u16(what);
                if (length > Limits::kMaxStringBytes)
                {
                    fail(std::string(what) + " is " + std::to_string(length) + " bytes, over the cap of "
                        + std::to_string(Limits::kMaxStringBytes));
                }
                need(length, what);
                std::string text(reinterpret_cast<const char*>(mBytes.data() + mPosition), length);
                mPosition += length;
                return text;
            }

            // A count from the file, checked against its cap and against the bytes left (each element takes at least
            // \a minBytes), before anything is allocated for it.
            std::size_t count(std::size_t value, std::size_t cap, std::size_t minBytes, const char* what) const
            {
                if (value > cap)
                {
                    fail(std::string(what) + ": " + std::to_string(value) + ", over the cap of " + std::to_string(cap));
                }
                if (value * minBytes > remaining())
                {
                    fail(std::string(what) + ": " + std::to_string(value) + " does not fit in the rest of the file");
                }
                return value;
            }

            std::uint16_t index(const char* what)
            {
                const std::uint16_t value = u16(what);
                checkIndex(value, what);
                return value;
            }

            void checkIndex(std::uint32_t value, const char* what) const
            {
                if (value >= mStrings.size())
                {
                    fail("string index " + std::to_string(value) + " is outside the string table of "
                        + std::to_string(mStrings.size()) + " (" + what + ")");
                }
            }

            Value value(const char* what);
            Value operand(char kind, const char* what);
            void readFunction(Function& function, bool named, const std::string& label);
            void readInstructions(Function& function);
            void readObject(File& file, std::vector<ErrorReport>& reports, std::string_view name);
            void readDebug();

            bool resolves(std::uint32_t lowId) const;

            std::span<const std::uint8_t> mBytes;
            ParseOptions mOptions;
            std::size_t mPosition = 0;
            bool mBig = false;
            Game mGame = Game::SkyrimSE;
            std::vector<std::string> mStrings;
            std::size_t mFunctionsInFile = 0;
            std::size_t mInstructionsInFile = 0;

            // Where we are, for the messages.
            std::string mObject;
            std::string mFunction;
            std::int64_t mInstruction = -1;
            std::uint8_t mOpcode = 0;

            // Names an identifier operand may refer to: this function's, then the object's.
            // Lower-cased string table entries share one id each, worked out once per entry when the table is read
            // (hashing a name per operand would make the work the product of two caps instead of the file's size).
            std::vector<std::uint32_t> mLowId;
            std::uint32_t mSelfId = kNoId;
            std::uint32_t mStateId = kNoId;
            std::unordered_set<std::uint32_t> mFunctionNames;
            std::unordered_set<std::uint32_t> mObjectNames;
            bool mParentMayOwn = false;
        };

        Value Reader::value(const char* what)
        {
            Value result;
            const std::uint8_t type = u8(what);
            switch (type)
            {
                case 0:
                    result.mType = ValueType::None;
                    break;
                case 1:
                case 2:
                    result.mType = type == 1 ? ValueType::Identifier : ValueType::String;
                    result.mString = index(what);
                    break;
                case 3:
                    result.mType = ValueType::Integer;
                    result.mInteger = static_cast<std::int32_t>(u32(what));
                    break;
                case 4:
                    result.mType = ValueType::Float;
                    result.mFloat = std::bit_cast<float>(u32(what));
                    break;
                case 5:
                    result.mType = ValueType::Bool;
                    result.mInteger = u8(what) != 0 ? 1 : 0;
                    break;
                default:
                    fail("unknown value type " + std::to_string(type) + " (" + what + ")");
            }
            return result;
        }

        bool Reader::resolves(std::uint32_t lowId) const
        {
            return lowId == mSelfId || lowId == mStateId || mFunctionNames.contains(lowId)
                || mObjectNames.contains(lowId);
        }

        Value Reader::operand(char kind, const char* what)
        {
            const Value result = value(what);
            switch (static_cast<Kind>(kind))
            {
                case Kind::Dest:
                    if (result.mType != ValueType::Identifier)
                    {
                        fail(std::string("operand must be an identifier (") + what + ")");
                    }
                    break;
                case Kind::Name:
                    if (result.mType != ValueType::Identifier && result.mType != ValueType::String)
                    {
                        fail(std::string("operand must be a name (") + what + ")");
                    }
                    break;
                case Kind::Jump:
                    if (result.mType != ValueType::Integer)
                    {
                        fail(std::string("jump operand must be an integer (") + what + ")");
                    }
                    break;
                case Kind::Any:
                    break;
            }
            if (result.mType == ValueType::Identifier && kind != static_cast<char>(Kind::Name))
            {
                if (!mParentMayOwn && !resolves(mLowId[result.mString]))
                {
                    fail("identifier '" + mStrings[result.mString] + "' (opcode " + std::to_string(mOpcode)
                        + ") names no local, parameter or variable of the script");
                }
            }
            return result;
        }

        void Reader::readInstructions(Function& function)
        {
            const std::size_t instructionCount
                = count(u16("instruction count"), Limits::kMaxInstructions, 1, "function has instructions");
            // The cap message must say "instructions"; count() prefixes the text it is given.
            mInstructionsInFile += instructionCount;
            if (mInstructionsInFile > Limits::kMaxInstructionsPerFile)
            {
                fail("instructions in the file: " + std::to_string(mInstructionsInFile) + ", over the cap of "
                    + std::to_string(Limits::kMaxInstructionsPerFile));
            }
            function.mInstructions.reserve(instructionCount);
            for (std::size_t i = 0; i < instructionCount; ++i)
            {
                mInstruction = static_cast<std::int64_t>(i);
                const std::uint8_t opcode = u8("opcode");
                if (opcode >= kFirstGuardOpcode && opcode <= kLastGuardOpcode)
                {
                    fail("guard opcode " + std::to_string(opcode) + " is not supported");
                }
                if (opcode > kLastFallout4Opcode || opcode >= kShapeCount)
                {
                    fail("unknown opcode " + std::to_string(opcode));
                }
                if (opcode > kLastSkyrimOpcode && mGame == Game::SkyrimSE)
                {
                    fail("opcode " + std::to_string(opcode) + " is not valid in a Skyrim SE script");
                }
                mOpcode = opcode;
                const Shape& shape = kShapes[opcode];
                Instruction instruction;
                instruction.mOpcode = opcode;
                instruction.mFirstOperand = static_cast<std::uint32_t>(function.mOperands.size());
                instruction.mFixedCount = static_cast<std::uint16_t>(std::strlen(shape.mKinds));
                for (const char* kind = shape.mKinds; *kind != '\0'; ++kind)
                {
                    function.mOperands.push_back(operand(*kind, "operand"));
                }
                if (opcode == Op::Jmp || opcode == Op::JmpT || opcode == Op::JmpF)
                {
                    const std::int64_t offset = function.mOperands.back().mInteger;
                    const std::int64_t target = static_cast<std::int64_t>(i) + offset;
                    if (target < 0 || target > static_cast<std::int64_t>(instructionCount))
                    {
                        fail("jump to instruction " + std::to_string(target) + " is outside the function of "
                            + std::to_string(instructionCount) + " instructions (0 to "
                            + std::to_string(instructionCount) + " are allowed; the last is the end of the function)");
                    }
                }
                if (shape.mCall)
                {
                    const Value extra = value("call argument count");
                    if (extra.mType != ValueType::Integer)
                    {
                        fail("call argument count must be an integer");
                    }
                    if (extra.mInteger < 0 || static_cast<std::size_t>(extra.mInteger) > Limits::kMaxCallArguments)
                    {
                        fail("call has " + std::to_string(extra.mInteger) + " arguments, over the cap of "
                            + std::to_string(Limits::kMaxCallArguments));
                    }
                    if (static_cast<std::size_t>(extra.mInteger) > remaining())
                    {
                        fail("call arguments do not fit in the rest of the file");
                    }
                    function.mOperands.push_back(extra);
                    instruction.mExtraCount = static_cast<std::uint16_t>(extra.mInteger);
                    for (std::int32_t k = 0; k < extra.mInteger; ++k)
                    {
                        function.mOperands.push_back(operand('V', "call argument"));
                    }
                }
                function.mInstructions.push_back(instruction);
            }
            mInstruction = -1;
        }

        void Reader::readFunction(Function& function, bool named, const std::string& label)
        {
            if (++mFunctionsInFile > Limits::kMaxFunctionsPerFile)
            {
                fail("functions in the file: over the cap of " + std::to_string(Limits::kMaxFunctionsPerFile));
            }
            if (named)
            {
                function.mName = index("function name");
                mFunction = mStrings[function.mName];
            }
            else
            {
                mFunction = label;
            }
            function.mReturnType = index("return type");
            function.mDoc = index("function documentation");
            function.mUserFlags = u32("function user flags");
            function.mFlags = u8("function flags");
            if ((function.mFlags & ~(Function::kGlobal | Function::kNative)) != 0)
            {
                fail("unknown function flags " + std::to_string(function.mFlags));
            }
            mFunctionNames.clear();
            const std::size_t parameters
                = count(u16("parameter count"), Limits::kMaxParametersAndLocals, 4, "function has parameters");
            function.mParameters.reserve(parameters);
            for (std::size_t i = 0; i < parameters; ++i)
            {
                Parameter p;
                p.mName = index("parameter name");
                p.mType = index("parameter type");
                mFunctionNames.insert(mLowId[p.mName]);
                function.mParameters.push_back(p);
            }
            const std::size_t locals
                = count(u16("local count"), Limits::kMaxParametersAndLocals, 4, "function has locals");
            function.mLocals.reserve(locals);
            for (std::size_t i = 0; i < locals; ++i)
            {
                Parameter p;
                p.mName = index("local name");
                p.mType = index("local type");
                mFunctionNames.insert(mLowId[p.mName]);
                function.mLocals.push_back(p);
            }
            readInstructions(function);
            mFunction.clear();
            mFunctionNames.clear();
        }

        void Reader::readDebug()
        {
            u64("debug modification time");
            const std::size_t functions
                = count(u16("debug function count"), Limits::kMaxFunctionsPerFile, 9, "debug section has functions");
            for (std::size_t i = 0; i < functions; ++i)
            {
                need(6 + 1, "debug function");
                mPosition += 6 + 1;
                const std::size_t lines
                    = count(u16("debug line count"), Limits::kMaxInstructions, 2, "debug function has lines");
                mPosition += lines * 2;
            }
            if (mGame == Game::Fallout4)
            {
                const std::size_t groups = count(
                    u16("property group count"), Limits::kMaxPropertyGroups, 12, "debug section has property groups");
                for (std::size_t i = 0; i < groups; ++i)
                {
                    need(6 + 4, "property group");
                    mPosition += 6 + 4;
                    const std::size_t names = count(
                        u16("property group size"), Limits::kMaxMembersPerObject, 2, "property group has names");
                    mPosition += names * 2;
                }
                const std::size_t orders
                    = count(u16("struct order count"), Limits::kMaxStructOrders, 6, "debug section has struct orders");
                for (std::size_t i = 0; i < orders; ++i)
                {
                    need(4, "struct order");
                    mPosition += 4;
                    const std::size_t names
                        = count(u16("struct order size"), Limits::kMaxStructMembers, 2, "struct order has names");
                    mPosition += names * 2;
                }
            }
        }

        void Reader::readObject(File& file, std::vector<ErrorReport>& reports, std::string_view name)
        {
            Object object;
            object.mName = index("object name");
            mObject = mStrings[object.mName];
            const std::size_t sizeAt = mPosition;
            const std::uint32_t declared = u32("object size");
            const std::size_t bodyStart = mPosition;
            object.mParent = index("parent name");
            object.mDoc = index("object documentation");
            if (mGame == Game::Fallout4)
            {
                object.mIsConst = u8("object const flag") != 0;
            }
            object.mUserFlags = u32("object user flags");
            object.mAutoState = index("auto state");
            mParentMayOwn = !mStrings[object.mParent].empty();
            mObjectNames.clear();

            if (mGame == Game::Fallout4)
            {
                const std::size_t structs = count(u16("struct count"), Limits::kMaxStructs, 4, "object has structs");
                object.mStructs.reserve(structs);
                for (std::size_t i = 0; i < structs; ++i)
                {
                    StructDef def;
                    def.mName = index("struct name");
                    const std::size_t members
                        = count(u16("struct member count"), Limits::kMaxStructMembers, 12, "struct has members");
                    def.mMembers.reserve(members);
                    for (std::size_t m = 0; m < members; ++m)
                    {
                        StructMember member;
                        member.mName = index("member name");
                        member.mType = index("member type");
                        member.mUserFlags = u32("member flags");
                        member.mInitial = value("member value");
                        member.mIsConst = u8("member const flag") != 0;
                        member.mDoc = index("member documentation");
                        def.mMembers.push_back(member);
                    }
                    object.mStructs.push_back(std::move(def));
                }
            }

            const std::size_t variables
                = count(u16("variable count"), Limits::kMaxMembersPerObject, 9, "object has variables");
            object.mVariables.reserve(variables);
            for (std::size_t i = 0; i < variables; ++i)
            {
                Variable variable;
                variable.mName = index("variable name");
                variable.mType = index("variable type");
                variable.mUserFlags = u32("variable flags");
                variable.mInitial = value("variable value");
                if (mGame == Game::Fallout4)
                {
                    variable.mIsConst = u8("variable const flag") != 0;
                }
                mObjectNames.insert(mLowId[variable.mName]);
                object.mVariables.push_back(variable);
            }

            const std::size_t properties
                = count(u16("property count"), Limits::kMaxMembersPerObject, 11, "object has properties");
            object.mProperties.reserve(properties);
            for (std::size_t i = 0; i < properties; ++i)
            {
                Property property;
                property.mName = index("property name");
                property.mType = index("property type");
                property.mDoc = index("property documentation");
                property.mUserFlags = u32("property flags");
                property.mFlags = u8("property function flags");
                if ((property.mFlags & ~(Property::kRead | Property::kWrite | Property::kAuto)) != 0)
                {
                    fail("unknown property flags " + std::to_string(property.mFlags));
                }
                const std::string& propertyName = mStrings[property.mName];
                if ((property.mFlags & Property::kAuto) != 0)
                {
                    property.mAutoVariable = index("auto property variable");
                    if (!mObjectNames.contains(mLowId[property.mAutoVariable]))
                    {
                        fail("auto property '" + propertyName + "' names variable '" + mStrings[property.mAutoVariable]
                            + "' that the object does not have");
                    }
                }
                else
                {
                    if ((property.mFlags & Property::kRead) != 0)
                    {
                        readFunction(property.mGetter, false, propertyName + ".get");
                    }
                    if ((property.mFlags & Property::kWrite) != 0)
                    {
                        readFunction(property.mSetter, false, propertyName + ".set");
                    }
                }
                object.mProperties.push_back(std::move(property));
            }

            const std::size_t states = count(u16("state count"), Limits::kMaxStates, 4, "object has states");
            object.mStates.reserve(states);
            std::unordered_set<std::uint32_t> stateNames;
            for (std::size_t i = 0; i < states; ++i)
            {
                State state;
                state.mName = index("state name");
                if (!stateNames.insert(mLowId[state.mName]).second)
                {
                    fail("duplicate state '" + mStrings[state.mName] + "'");
                }
                const std::size_t functions
                    = count(u16("function count"), Limits::kMaxFunctionsPerState, 15, "state has functions");
                state.mFunctions.reserve(functions);
                std::unordered_set<std::uint32_t> functionNames;
                for (std::size_t f = 0; f < functions; ++f)
                {
                    Function function;
                    readFunction(function, true, std::string());
                    if (!functionNames.insert(mLowId[function.mName]).second)
                    {
                        fail("duplicate function '" + mStrings[function.mName] + "' in state '" + mStrings[state.mName]
                            + "'");
                    }
                    state.mFunctions.push_back(std::move(function));
                }
                object.mStates.push_back(std::move(state));
            }

            // The object's declared size is a consistency check: the object is parsed by its structure, and the size
            // must match one of the three conventions seen in the installed files.
            const std::size_t length = mPosition - bodyStart;
            const std::uint32_t swapped = ((declared & 0xFFu) << 24) | ((declared & 0xFF00u) << 8)
                | ((declared >> 8) & 0xFF00u) | (declared >> 24);
            if (declared == length + 4)
            {
                object.mSizeConvention = PexSizeConvention::IncludesSelf;
            }
            else if (declared == length)
            {
                object.mSizeConvention = PexSizeConvention::ExcludesSelf;
            }
            else if (swapped == length)
            {
                object.mSizeConvention = PexSizeConvention::ExcludesSelfOtherOrder;
            }
            else
            {
                mPosition = sizeAt;
                fail("object size " + std::to_string(declared)
                    + " matches none of the three conventions (the object is " + std::to_string(length)
                    + " bytes after the size field)");
            }
            if (object.mSizeConvention != PexSizeConvention::IncludesSelf)
            {
                const bool other = object.mSizeConvention == PexSizeConvention::ExcludesSelfOtherOrder;
                const std::string text = other
                    ? "object size counts the bytes after the field, written in the other byte order than the file's"
                    : "object size counts the bytes after the field";
                if (mOptions.mStrictSizeConvention)
                {
                    mPosition = sizeAt;
                    fail(text + "; strict mode accepts only the size that includes its own field");
                }
                ErrorReport report;
                report.severity = Severity::Warning;
                report.script = std::string(name);
                report.message = text + " (readable, not the usual convention)";
                reports.push_back(std::move(report));
            }
            mObject.clear();
            mObjectNames.clear();
            file.mObjects.push_back(std::move(object));
        }

        File Reader::read(std::vector<ErrorReport>& reports, std::string_view name)
        {
            if (mBytes.size() > Limits::kMaxFileBytes)
            {
                fail("file is " + std::to_string(mBytes.size()) + " bytes, over the cap of "
                    + std::to_string(Limits::kMaxFileBytes));
            }
            File file;
            need(4, "magic");
            if (mBytes[0] == 0xFA && mBytes[1] == 0x57 && mBytes[2] == 0xC0 && mBytes[3] == 0xDE)
            {
                mBig = true;
            }
            else if (mBytes[0] == 0xDE && mBytes[1] == 0xC0 && mBytes[2] == 0x57 && mBytes[3] == 0xFA)
            {
                mBig = false;
            }
            else
            {
                fail("bad magic: this is not a compiled Papyrus file");
            }
            mPosition = 4;
            file.mBigEndian = mBig;
            file.mMajor = u8("major version");
            file.mMinor = u8("minor version");
            const std::uint16_t gameId = u16("game id");
            if (file.mMajor == 3 && (file.mMinor == 1 || file.mMinor == 2) && gameId == 1)
            {
                mGame = Game::SkyrimSE;
            }
            else if (file.mMajor == 3 && file.mMinor == 9 && gameId == 2)
            {
                mGame = Game::Fallout4;
            }
            else
            {
                fail("unsupported .pex version " + std::to_string(file.mMajor) + "." + std::to_string(file.mMinor)
                    + " for game id " + std::to_string(gameId)
                    + " (accepted: 3.1 or 3.2 for game id 1, Skyrim SE; 3.9 for game id 2, Fallout 4; other games, "
                      "Starfield for one, are not supported yet)");
            }
            file.mGame = mGame;
            file.mCompileTime = u64("compile time");
            file.mSource = wstring("source file name");
            file.mUser = wstring("user name");
            file.mMachine = wstring("machine name");

            std::size_t stringCount = u16("string count");
            if (stringCount == 0xFFFF)
            {
                stringCount = u32("extended string count");
                if (stringCount > Limits::kMaxExtendedStrings)
                {
                    fail("string table has " + std::to_string(stringCount) + " strings, over the cap of "
                        + std::to_string(Limits::kMaxExtendedStrings));
                }
            }
            stringCount = count(stringCount, Limits::kMaxExtendedStrings, 2, "string table has strings");
            mStrings.reserve(stringCount);
            for (std::size_t i = 0; i < stringCount; ++i)
            {
                mStrings.push_back(wstring("string"));
            }
            file.mStrings = mStrings;
            {
                std::unordered_map<std::string, std::uint32_t> ids;
                mLowId.reserve(mStrings.size());
                for (const std::string& text : mStrings)
                {
                    const auto added = ids.emplace(lowered(text), static_cast<std::uint32_t>(ids.size()));
                    mLowId.push_back(added.first->second);
                }
                const auto self = ids.find("self");
                mSelfId = self == ids.end() ? kNoId : self->second;
                const auto state = ids.find("::state");
                mStateId = state == ids.end() ? kNoId : state->second;
            }

            const std::uint8_t hasDebug = u8("debug flag");
            if (hasDebug > 1)
            {
                fail("debug flag must be 0 or 1");
            }
            if (hasDebug == 1)
            {
                readDebug();
            }
            const std::size_t userFlags
                = count(u16("user flag count"), Limits::kMaxUserFlags, 3, "file has user flags");
            need(userFlags * 3, "user flags");
            mPosition += userFlags * 3;

            const std::size_t objects = count(u16("object count"), Limits::kMaxObjects, 6, "file has objects");
            if (objects == 0)
            {
                fail("file has no object");
            }
            file.mObjects.reserve(objects);
            for (std::size_t i = 0; i < objects; ++i)
            {
                readObject(file, reports, name);
            }
            if (remaining() != 0)
            {
                // 165 of the installed mod scripts end with 29 to 51 zero bytes after the last object. They are read
                // (with a warning) when every byte is zero and there are few of them; anything else is a damaged file.
                bool allZero = remaining() <= Limits::kMaxTrailingPadding && !mOptions.mStrictSizeConvention;
                for (std::size_t i = mPosition; allZero && i < mBytes.size(); ++i)
                {
                    allZero = mBytes[i] == 0;
                }
                if (!allZero)
                {
                    fail(std::to_string(remaining()) + " trailing bytes after the last object");
                }
                ErrorReport report;
                report.severity = Severity::Warning;
                report.script = std::string(name);
                report.message = std::to_string(remaining()) + " zero bytes of padding after the last object";
                reports.push_back(std::move(report));
            }
            return file;
        }
    }

    ParseResult parse(std::span<const std::uint8_t> bytes, std::string_view name, const ParseOptions& options) noexcept
    {
        ParseResult result;
        std::string failure;
        try
        {
            Reader reader(bytes, options);
            result.mFile = reader.read(result.mReports, name);
            result.mOk = true;
            return result;
        }
        catch (const ParseError& error)
        {
            failure = error.mMessage;
        }
        catch (const std::bad_alloc&)
        {
            failure = "out of memory reading the file";
        }
        catch (...)
        {
            failure = "unexpected error reading the file";
        }
        result.mOk = false;
        result.mFile = File();
        try
        {
            ErrorReport report;
            report.severity = Severity::Error;
            report.script = std::string(name);
            report.message = std::move(failure);
            result.mReports.push_back(std::move(report));
        }
        catch (...)
        {
            result.mReports.clear();
        }
        return result;
    }

    std::string lowered(std::string_view text)
    {
        std::string result(text);
        for (char& c : result)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return result;
    }
}
