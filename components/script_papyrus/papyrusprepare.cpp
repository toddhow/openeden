#include "papyrusvm.hpp"

// Linking a loaded script to its parents and compiling its functions for the interpreter (task 0093 stage c). It runs
// the first time a script is used (an instance is attached, a class is called), not at load, because a parent may be
// loaded after its child. What it makes is plain data: names are ids, a variable is a slot, a jump is an offset.

namespace ScriptPapyrus::Vm
{
    namespace
    {
        using namespace openeden::script;

        enum class Role : std::uint8_t
        {
            Value,
            Dest,
            Name,
            Jump,
        };

        // The role of operand \a index of \a opcode (the operand order is the one read from the installed scripts, see
        // the stage c log of task 0093).
        Role roleOf(std::uint8_t opcode, std::size_t index)
        {
            using namespace Pex::Op;
            switch (opcode)
            {
                case Jmp:
                    return Role::Jump;
                case JmpT:
                case JmpF:
                    return index == 1 ? Role::Jump : Role::Value;
                case CallMethod:
                    return index == 0 ? Role::Name : index == 1 ? Role::Value : Role::Dest;
                case CallParent:
                    return index == 0 ? Role::Name : Role::Dest;
                case CallStatic:
                    return index <= 1 ? Role::Name : Role::Dest;
                case PropGet:
                    return index == 0 ? Role::Name : index == 1 ? Role::Value : Role::Dest;
                case PropSet:
                    return index == 0 ? Role::Name : Role::Value;
                case Is:
                    return index == 0 ? Role::Dest : index == 1 ? Role::Value : Role::Name;
                case StructGet:
                    return index == 0 ? Role::Dest : index == 1 ? Role::Value : Role::Name;
                case StructSet:
                    return index == 1 ? Role::Name : Role::Value;
                case ArrayFindStruct:
                case ArrayRFindStruct:
                    return index == 1 ? Role::Dest : index == 2 ? Role::Name : Role::Value;
                case ArrayGetAllMatchingStructs:
                    return index == 1 ? Role::Dest : index == 2 ? Role::Name : Role::Value;
                case ArrayFindElement:
                case ArrayRFindElement:
                    return index == 1 ? Role::Dest : Role::Value;
                case ArrayCreate:
                case ArrayLength:
                case ArrayGetElement:
                case StructCreate:
                    return index == 0 ? Role::Dest : Role::Value;
                case Return:
                case ArrayRemoveLast:
                case ArrayClear:
                case ArraySetElement:
                case ArrayAdd:
                case ArrayInsert:
                case ArrayRemove:
                    return Role::Value;
                default:
                    // 1 to 19 and 27: a destination, then values.
                    return index == 0 ? Role::Dest : Role::Value;
            }
        }

        // The operand index of the destination whose declared type the instruction needs (casts, array and struct
        // creation, and the result of a call); -1 when the opcode has none.
        int destIndexOf(std::uint8_t opcode)
        {
            using namespace Pex::Op;
            switch (opcode)
            {
                case CallMethod:
                case CallStatic:
                case PropGet:
                    return 2;
                case CallParent:
                    return 1;
                case ArrayFindElement:
                case ArrayRFindElement:
                case ArrayFindStruct:
                case ArrayRFindStruct:
                case ArrayGetAllMatchingStructs:
                    return 1;
                case Jmp:
                case JmpT:
                case JmpF:
                case Return:
                case PropSet:
                case StructSet:
                case ArraySetElement:
                case ArrayAdd:
                case ArrayInsert:
                case ArrayRemoveLast:
                case ArrayRemove:
                case ArrayClear:
                case Nop:
                    return -1;
                default:
                    return 0;
            }
        }
    }

    TypeRef Runtime::parseType(std::string_view text)
    {
        TypeRef type;
        std::string lower(text);
        for (char& c : lower)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (lower.empty() || lower == "none")
        {
            return type;
        }
        if (lower.size() > 2 && lower.compare(lower.size() - 2, 2, "[]") == 0)
        {
            const TypeRef element = parseType(text.substr(0, text.size() - 2));
            type.kind = TypeKind::Array;
            type.elem = element.kind;
            type.name = element.name;
            type.name2 = element.name2;
            return type;
        }
        if (lower == "int")
        {
            type.kind = TypeKind::Int;
        }
        else if (lower == "float")
        {
            type.kind = TypeKind::Float;
        }
        else if (lower == "bool")
        {
            type.kind = TypeKind::Bool;
        }
        else if (lower == "string")
        {
            type.kind = TypeKind::String;
        }
        else if (lower == "var")
        {
            type.kind = TypeKind::Var;
        }
        else if (const std::size_t hash = lower.find('#'); hash != std::string::npos)
        {
            type.kind = TypeKind::Struct;
            type.name = nameId(text.substr(0, hash));
            type.name2 = nameId(text.substr(hash + 1));
        }
        else
        {
            type.kind = TypeKind::Object;
            type.name = nameId(text);
        }
        return type;
    }

    Val Runtime::valueOfPex(const Pex::File& file, const Pex::Value& value)
    {
        switch (value.mType)
        {
            case Pex::ValueType::Integer:
                return Val::makeInt(value.mInteger);
            case Pex::ValueType::Float:
                return Val::makeFloat(value.mFloat);
            case Pex::ValueType::Bool:
                return Val::makeBool(value.mInteger != 0);
            case Pex::ValueType::String:
                return Val::makeString(internString(file.str(value.mString)));
            default:
                return Val{};
        }
    }

    Val Runtime::zeroOf(const TypeRef& type) const
    {
        switch (type.kind)
        {
            case TypeKind::Int:
                return Val::makeInt(0);
            case TypeKind::Float:
                return Val::makeFloat(0.0f);
            case TypeKind::Bool:
                return Val::makeBool(false);
            case TypeKind::String:
                return Val::makeString(0);
            default:
                return Val{};
        }
    }

    std::uint32_t Runtime::scriptByClassId(std::uint32_t classId) const
    {
        const auto found = mScriptByClassId.find(classId);
        return found == mScriptByClassId.end() ? kNoScript : found->second;
    }

    void Runtime::prepareFail(Script& script, const std::string& why)
    {
        script.mPrepareFailed = true;
        script.mPrepareError = why;
        mWorld.log(Severity::Error, script.mFileName + ": the script cannot be run: " + why);
    }

    bool Runtime::prepare(std::uint32_t scriptIndex)
    {
        Script& script = mScripts[scriptIndex];
        if (script.mPrepared)
        {
            return !script.mPrepareFailed;
        }
        script.mPrepared = true;
        if (++mPrepareDepth > kMaxParentDepth)
        {
            --mPrepareDepth;
            prepareFail(script, "the chain of parents is deeper than " + std::to_string(kMaxParentDepth));
            return false;
        }
        struct DepthGuard
        {
            std::size_t& mDepth;
            ~DepthGuard() { --mDepth; }
        } guard{ mPrepareDepth };

        const Pex::File& file = script.mFile;
        const Pex::Object& object = file.mObjects[0];
        script.mClassId = nameId(script.mClass);
        script.mParentId = script.mParent.empty() ? 0 : nameId(script.mParent);

        // The parent first: this script's variables follow the parent's.
        script.mChain.assign(1, scriptIndex);
        script.mVarBase = 0;
        if (!script.mParent.empty())
        {
            const std::uint32_t parentIndex = scriptByClassId(script.mParentId);
            if (parentIndex == kNoScript)
            {
                script.mMissingParentId = script.mParentId;
                if (mPolicy == UnknownFunctionPolicy::Strict)
                {
                    prepareFail(script, "its parent '" + script.mParent + "' is not loaded");
                    return false;
                }
                logOnce("parent:" + script.mFileName,
                    script.mFileName + ": the parent '" + script.mParent
                        + "' is not loaded; the script runs without what it inherits");
            }
            else
            {
                if (!prepare(parentIndex))
                {
                    prepareFail(script, "its parent '" + script.mParent + "' cannot be run");
                    return false;
                }
                const Script& parent = mScripts[parentIndex];
                if (parent.mChain.empty())
                {
                    prepareFail(script, "its parent chain loops back to it");
                    return false;
                }
                script.mChain.insert(script.mChain.end(), parent.mChain.begin(), parent.mChain.end());
                script.mVarBase = parent.totalVars();
            }
        }

        // Own variables.
        for (const Pex::Variable& variable : object.mVariables)
        {
            VarInfo info;
            info.name = nameId(file.str(variable.mName));
            info.type = parseType(file.str(variable.mType));
            info.initial = valueOfPex(file, variable.mInitial);
            if (variable.mInitial.mType == Pex::ValueType::None)
            {
                info.initial = zeroOf(info.type);
            }
            script.mVarIndex.emplace(info.name, static_cast<std::uint32_t>(script.mVars.size()));
            script.mVars.push_back(info);
        }
        script.mAutoStateName = internString(file.str(object.mAutoState));
        script.mAutoStateId = nameId(file.str(object.mAutoState));

        for (const Pex::StructDef& definition : object.mStructs)
        {
            StructInfo info;
            for (const Pex::StructMember& member : definition.mMembers)
            {
                info.memberNames.push_back(nameId(file.str(member.mName)));
                info.memberTypes.push_back(parseType(file.str(member.mType)));
                Val initial = valueOfPex(file, member.mInitial);
                if (member.mInitial.mType == Pex::ValueType::None)
                {
                    initial = zeroOf(info.memberTypes.back());
                }
                info.memberDefaults.push_back(initial);
            }
            script.mStructs.emplace(nameId(file.str(definition.mName)), std::move(info));
        }

        // Functions of every state, then the accessors of the full properties.
        for (const Pex::State& state : object.mStates)
        {
            StateInfo info;
            info.name = nameId(file.str(state.mName));
            for (const Pex::Function& source : state.mFunctions)
            {
                CFunc compiled;
                compileFunction(script, source, info.name, compiled);
                const std::uint32_t index = static_cast<std::uint32_t>(script.mFuncs.size());
                info.functions.emplace(compiled.name, index);
                script.mFuncs.push_back(std::move(compiled));
            }
            script.mStateIndex.emplace(info.name, static_cast<std::uint32_t>(script.mStates.size()));
            script.mStates.push_back(std::move(info));
        }
        for (const Pex::Property& property : object.mProperties)
        {
            PropInfo info;
            info.type = parseType(file.str(property.mType));
            const std::string propertyName(file.str(property.mName));
            if ((property.mFlags & Pex::Property::kAuto) != 0)
            {
                info.autoProperty = true;
                const auto backing = script.mVarIndex.find(nameId(file.str(property.mAutoVariable)));
                if (backing == script.mVarIndex.end())
                {
                    continue;
                }
                info.backing = backing->second;
            }
            else
            {
                auto add = [&](const Pex::Function& source, const char* suffix) {
                    CFunc compiled;
                    compileFunction(script, source, 0, compiled);
                    compiled.name = nameId(propertyName + suffix);
                    script.mFuncs.push_back(std::move(compiled));
                    return static_cast<std::int32_t>(script.mFuncs.size() - 1);
                };
                if ((property.mFlags & Pex::Property::kRead) != 0)
                {
                    info.getter = add(property.mGetter, ".get");
                }
                if ((property.mFlags & Pex::Property::kWrite) != 0)
                {
                    info.setter = add(property.mSetter, ".set");
                }
            }
            script.mProps.emplace(nameId(propertyName), info);
        }
        return true;
    }

    void Runtime::compileFunction(Script& script, const Pex::Function& source, std::uint32_t stateId, CFunc& out)
    {
        const Pex::File& file = script.mFile;
        out.name = source.mName == Pex::Function::kNoName ? 0 : nameId(file.str(source.mName));
        out.state = stateId;
        out.global = source.isGlobal();
        out.native = source.isNative();
        out.ret = parseType(file.str(source.mReturnType));
        std::unordered_map<std::uint32_t, std::uint32_t> slots;
        for (const Pex::Parameter& parameter : source.mParameters)
        {
            slots.emplace(nameId(file.str(parameter.mName)), static_cast<std::uint32_t>(out.slotTypes.size()));
            out.slotTypes.push_back(parseType(file.str(parameter.mType)));
        }
        out.paramCount = static_cast<std::uint16_t>(source.mParameters.size());
        for (const Pex::Parameter& local : source.mLocals)
        {
            slots.emplace(nameId(file.str(local.mName)), static_cast<std::uint32_t>(out.slotTypes.size()));
            out.slotTypes.push_back(parseType(file.str(local.mType)));
        }
        out.frameSize = static_cast<std::uint16_t>(out.slotTypes.size());

        const std::uint32_t selfId = nameId("self");
        const std::uint32_t stateVarId = nameId("::state");
        const std::uint32_t noneVarId = nameId("::nonevar");

        // Compiles one operand. \a type receives the declared type of a variable or a local (None otherwise).
        auto compileOperand = [&](const Pex::Value& value, Role role, TypeRef& type) {
            COp op;
            type = TypeRef{};
            if (role == Role::Name)
            {
                if (value.mType == Pex::ValueType::Identifier || value.mType == Pex::ValueType::String)
                {
                    op.kind = OpKind::Name;
                    op.n = nameId(file.str(value.mString));
                }
                else
                {
                    op.kind = OpKind::Bad;
                }
                return op;
            }
            if (role == Role::Jump)
            {
                op.kind = OpKind::Const;
                op.v = Val::makeInt(value.mInteger);
                return op;
            }
            if (value.mType != Pex::ValueType::Identifier)
            {
                if (role == Role::Dest)
                {
                    op.kind = OpKind::Bad;
                    return op;
                }
                op.kind = OpKind::Const;
                op.v = valueOfPex(file, value);
                return op;
            }
            const std::uint32_t id = nameId(file.str(value.mString));
            op.n = id;
            if (const auto local = slots.find(id); local != slots.end())
            {
                op.kind = OpKind::Local;
                op.n = local->second;
                type = out.slotTypes[local->second];
                return op;
            }
            if (id == selfId)
            {
                op.kind = OpKind::Self;
                return op;
            }
            if (id == stateVarId)
            {
                op.kind = OpKind::StateVar;
                type.kind = TypeKind::String;
                return op;
            }
            if (id == noneVarId)
            {
                op.kind = OpKind::Discard;
                return op;
            }
            for (const std::uint32_t scriptIndex : script.mChain)
            {
                const Script& owner = mScripts[scriptIndex];
                const auto variable = owner.mVarIndex.find(id);
                if (variable != owner.mVarIndex.end())
                {
                    op.kind = OpKind::Var;
                    op.n = owner.mVarBase + variable->second;
                    type = owner.mVars[variable->second].type;
                    return op;
                }
            }
            op.kind = OpKind::Bad;
            return op;
        };

        out.code.reserve(source.mInstructions.size());
        out.ops.reserve(source.mOperands.size());
        for (const Pex::Instruction& instruction : source.mInstructions)
        {
            CInstr compiled;
            compiled.op = instruction.mOpcode;
            compiled.fixed = instruction.mFixedCount;
            compiled.extra = instruction.mExtraCount;
            compiled.first = static_cast<std::uint32_t>(out.ops.size());
            const int destIndex = destIndexOf(instruction.mOpcode);
            const std::size_t total = static_cast<std::size_t>(instruction.mFixedCount) + instruction.mExtraCount;
            for (std::size_t i = 0; i < total; ++i)
            {
                // The argument count of a call sits in the reader's pool between the fixed operands and the arguments;
                // the compiled operands hold the fixed operands and then the arguments only.
                const std::size_t sourceIndex = i < instruction.mFixedCount ? i : i + 1;
                const Pex::Value& value = source.mOperands[instruction.mFirstOperand + sourceIndex];
                const Role role = i < instruction.mFixedCount ? roleOf(instruction.mOpcode, i) : Role::Value;
                TypeRef type;
                out.ops.push_back(compileOperand(value, role, type));
                if (static_cast<int>(i) == destIndex)
                {
                    compiled.dest = type;
                }
            }
            out.code.push_back(compiled);
        }
    }

    const VarInfo* Runtime::findVar(const Instance& instance, std::uint32_t nameId, std::uint32_t& slot) const
    {
        for (const std::uint32_t scriptIndex : mScripts[instance.script].mChain)
        {
            const Script& owner = mScripts[scriptIndex];
            const auto variable = owner.mVarIndex.find(nameId);
            if (variable != owner.mVarIndex.end())
            {
                slot = owner.mVarBase + variable->second;
                return &owner.mVars[variable->second];
            }
        }
        return nullptr;
    }
}
