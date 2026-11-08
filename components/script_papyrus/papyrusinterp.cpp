#include "papyrusvm.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

// The interpreter of the Papyrus runtime (task 0093 stage c): the instruction loop, calls (script, native, parent and
// static), properties, states, arrays, structs, casts and the policy for what is not there.
//
// Operand order of every opcode is the one read from the installed scripts (the stage c log of task 0093); the
// opcodes that never occur in them (35, 41, 44 and 47) follow the same pattern and are marked recalled there.
//
// A stack here is plain data (frames of ids and indexes, a vector of values): no frame holds a pointer. A latent call
// is not run in this stage: it returns the zero of its type (tolerant) or fails (strict) and is counted.

namespace ScriptPapyrus::Vm
{
    using namespace openeden::script;

    namespace
    {
        constexpr std::size_t kMaxStackValues = 1u << 20;

        std::int32_t floatToInt(float value)
        {
            if (std::isnan(value))
            {
                return 0;
            }
            if (value >= 2147483648.0f)
            {
                return std::numeric_limits<std::int32_t>::max();
            }
            if (value <= -2147483648.0f)
            {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<std::int32_t>(value);
        }

        std::int32_t toInt(const Val& value)
        {
            switch (value.kind)
            {
                case Kind::Int:
                case Kind::Bool:
                    return value.asInt();
                case Kind::Float:
                    return floatToInt(value.asFloat());
                default:
                    return 0;
            }
        }

        float toFloat(const Val& value)
        {
            switch (value.kind)
            {
                case Kind::Int:
                case Kind::Bool:
                    return static_cast<float>(value.asInt());
                case Kind::Float:
                    return value.asFloat();
                default:
                    return 0.0f;
            }
        }

        bool isNumber(const Val& value)
        {
            return value.kind == Kind::Int || value.kind == Kind::Float || value.kind == Kind::Bool;
        }

        char lowerChar(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        std::int32_t parseInteger(std::string_view text)
        {
            std::size_t i = 0;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            {
                ++i;
            }
            if (i < text.size() && text[i] == '+')
            {
                ++i;
            }
            std::int32_t value = 0;
            const auto result = std::from_chars(text.data() + i, text.data() + text.size(), value);
            return result.ec == std::errc() ? value : 0;
        }
    }

    // ---- errors
    // ------------------------------------------------------------------------------------------------------

    bool Runtime::fail(ErrorKind kind, const std::string& message)
    {
        if (mErrKind == ErrorKind::None)
        {
            mErrKind = kind;
            mErrMsg = message;
        }
        return false;
    }

    void Runtime::logOnce(const std::string& key, const std::string& message)
    {
        if (mLoggedOnce.insert(key).second)
        {
            mWorld.log(Severity::Warning, message);
        }
    }

    // ---- values
    // ------------------------------------------------------------------------------------------------------

    Instance* Runtime::instanceOf(const Val& self)
    {
        return self.kind == Kind::Instance && self.a >= 1 && self.a <= mInstances.size() ? &mInstances[self.a - 1]
                                                                                         : nullptr;
    }

    const Instance* Runtime::instanceOf(const Val& self) const
    {
        return self.kind == Kind::Instance && self.a >= 1 && self.a <= mInstances.size() ? &mInstances[self.a - 1]
                                                                                         : nullptr;
    }

    Val Runtime::readOp(const Stack& stack, const Frame& frame, const COp& op)
    {
        switch (op.kind)
        {
            case OpKind::Local:
                return stack.values[frame.base + op.n];
            case OpKind::Var:
            {
                const Instance* instance = instanceOf(frame.self);
                if (instance == nullptr || op.n >= instance->vars.size())
                {
                    fail(ErrorKind::BadScript, "a variable of the script is read where there is no instance");
                    return Val{};
                }
                return instance->vars[op.n];
            }
            case OpKind::Self:
                return frame.self;
            case OpKind::StateVar:
            {
                const Instance* instance = instanceOf(frame.self);
                return instance != nullptr ? Val::makeString(instance->stateName) : Val::makeString(0);
            }
            case OpKind::Const:
                return op.v;
            case OpKind::Bad:
                fail(ErrorKind::BadScript, "the identifier '" + std::string(nameText(op.n)) + "' names nothing");
                return Val{};
            default:
                return Val{};
        }
    }

    void Runtime::writeOp(Stack& stack, const Frame& frame, const COp& op, const Val& value)
    {
        switch (op.kind)
        {
            case OpKind::Local:
                stack.values[frame.base + op.n] = value;
                break;
            case OpKind::Var:
            {
                Instance* instance = instanceOf(frame.self);
                if (instance == nullptr || op.n >= instance->vars.size())
                {
                    fail(ErrorKind::BadScript, "a variable of the script is written where there is no instance");
                    return;
                }
                instance->vars[op.n] = value;
                break;
            }
            case OpKind::StateVar:
            {
                Instance* instance = instanceOf(frame.self);
                if (instance == nullptr || value.kind != Kind::String)
                {
                    fail(ErrorKind::TypeMismatch,
                        "the state is set where there is no script instance, or to something that is not a string");
                    return;
                }
                // A permanent copy: the string the script holds may be collected.
                const std::string_view name = textOf(value);
                instance->stateName = internString(name);
                instance->stateId = nameId(name);
                break;
            }
            case OpKind::Discard:
            case OpKind::None:
                break;
            default:
                fail(ErrorKind::BadScript,
                    op.kind == OpKind::Bad ? "the identifier '" + std::string(nameText(op.n)) + "' names nothing"
                                           : std::string("a value is written to something that is not a variable"));
                break;
        }
    }

    bool Runtime::truthy(const Val& value) const
    {
        switch (value.kind)
        {
            case Kind::None:
                return false;
            case Kind::Int:
            case Kind::Bool:
                return value.a != 0;
            case Kind::Float:
                return value.asFloat() != 0.0f;
            case Kind::String:
                return !textOf(value).empty();
            case Kind::Form:
                return value.a != 0;
            default:
                return true;
        }
    }

    bool Runtime::stringsEqual(std::uint32_t a, std::uint32_t b) const
    {
        if (a == b)
        {
            return true;
        }
        const std::string& x = mStrings[a].text;
        const std::string& y = mStrings[b].text;
        if (x.size() != y.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            if (lowerChar(x[i]) != lowerChar(y[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool Runtime::valuesEqual(const Val& a, const Val& b) const
    {
        if (isNumber(a) && isNumber(b))
        {
            if (a.kind == Kind::Float || b.kind == Kind::Float)
            {
                return toFloat(a) == toFloat(b);
            }
            return a.asInt() == b.asInt();
        }
        if (a.kind == Kind::String && b.kind == Kind::String)
        {
            return stringsEqual(a.a, b.a);
        }
        // Objects: None, native forms and attached scripts. An attached script is the form it is attached to.
        auto identity = [this](const Val& v, std::uint32_t& kind, std::uint32_t& id) {
            kind = 0;
            id = 0;
            if (v.kind == Kind::Form && v.a != 0)
            {
                kind = 1;
                id = v.a;
            }
            else if (const Instance* instance = instanceOf(v))
            {
                if (instance->key.kind == AttachKind::Form && instance->key.formId != 0)
                {
                    kind = 1;
                    id = instance->key.formId;
                }
                else
                {
                    kind = 2;
                    id = v.a;
                }
            }
            else if (v.kind == Kind::Array || v.kind == Kind::Struct)
            {
                kind = v.kind == Kind::Array ? 3 : 4;
                id = v.a;
            }
            else
            {
                return v.kind == Kind::None || v.kind == Kind::Form;
            }
            return true;
        };
        std::uint32_t kindA;
        std::uint32_t idA;
        std::uint32_t kindB;
        std::uint32_t idB;
        if (!identity(a, kindA, idA) || !identity(b, kindB, idB))
        {
            return false;
        }
        return kindA == kindB && idA == idB;
    }

    void Runtime::appendText(std::string& out, const Val& value)
    {
        char buffer[64];
        switch (value.kind)
        {
            case Kind::None:
                out += "None";
                break;
            case Kind::Int:
            {
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.asInt());
                out.append(buffer, result.ptr);
                break;
            }
            case Kind::Float:
            {
                const int length = std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value.asFloat()));
                out.append(buffer, static_cast<std::size_t>(std::max(length, 0)));
                break;
            }
            case Kind::Bool:
                out += value.a != 0 ? "True" : "False";
                break;
            case Kind::String:
                out += textOf(value);
                break;
            case Kind::Form:
            {
                const int length = std::snprintf(buffer, sizeof(buffer), "(%08X)", value.a);
                out += "[";
                out += value.b != 0 ? nameText(value.b) : std::string_view("Form");
                out += " <";
                out.append(buffer, static_cast<std::size_t>(std::max(length, 0)));
                out += ">]";
                break;
            }
            case Kind::Instance:
            {
                const Instance* instance = instanceOf(value);
                const int length = std::snprintf(buffer, sizeof(buffer), "(%08X)", value.a);
                out += "[";
                out += instance != nullptr ? std::string_view(mScripts[instance->script].mClass)
                                           : std::string_view("None");
                out += " <";
                out.append(buffer, static_cast<std::size_t>(std::max(length, 0)));
                out += ">]";
                break;
            }
            case Kind::Array:
                out += "[Array]";
                break;
            case Kind::Struct:
                out += "[Struct]";
                break;
        }
    }

    Val Runtime::coerce(const Val& value, const TypeRef& type)
    {
        switch (type.kind)
        {
            case TypeKind::Int:
                if (value.kind == Kind::Int)
                {
                    return value;
                }
                return isNumber(value) ? Val::makeInt(toInt(value))
                                       : (value.kind == Kind::None ? Val::makeInt(0) : value);
            case TypeKind::Float:
                if (value.kind == Kind::Float)
                {
                    return value;
                }
                return isNumber(value) ? Val::makeFloat(toFloat(value))
                                       : (value.kind == Kind::None ? Val::makeFloat(0.0f) : value);
            case TypeKind::Bool:
                if (value.kind == Kind::Bool)
                {
                    return value;
                }
                return isNumber(value) ? Val::makeBool(truthy(value))
                                       : (value.kind == Kind::None ? Val::makeBool(false) : value);
            case TypeKind::String:
                return value.kind == Kind::None ? Val::makeString(0) : value;
            default:
                return value;
        }
    }

    // ---- classes
    // -----------------------------------------------------------------------------------------------------

    bool Runtime::classDerives(std::uint32_t startScript, std::uint32_t targetId) const
    {
        const Script& start = mScripts[startScript];
        for (const std::uint32_t index : start.mChain)
        {
            if (mScripts[index].mClassId == targetId)
            {
                return true;
            }
        }
        const Script& last = mScripts[start.mChain.back()];
        return last.mMissingParentId != 0 && last.mMissingParentId == targetId;
    }

    std::uint32_t Runtime::classOfForm(const Val& value)
    {
        if (value.b != 0)
        {
            return value.b;
        }
        if (!mWorld.formClass(value.a, mScratchClass) || mScratchClass.empty())
        {
            return kNoScript;
        }
        return nameId(mScratchClass);
    }

    bool Runtime::valueIsClass(const Val& value, std::uint32_t targetId)
    {
        if (value.kind == Kind::Instance)
        {
            const Instance* instance = instanceOf(value);
            return instance != nullptr && classDerives(instance->script, targetId);
        }
        if (value.kind != Kind::Form || value.a == 0)
        {
            return false;
        }
        const std::uint32_t classId = classOfForm(value);
        if (classId == kNoScript)
        {
            return false;
        }
        const std::uint32_t start = scriptByClassId(classId);
        if (start == kNoScript || !prepare(start))
        {
            return classId == targetId;
        }
        return classDerives(start, targetId);
    }

    bool Runtime::castTo(const Val& source, const TypeRef& target, Val& out)
    {
        switch (target.kind)
        {
            case TypeKind::Int:
                if (source.kind == Kind::String)
                {
                    out = Val::makeInt(parseInteger(textOf(source)));
                }
                else if (source.kind == Kind::Form)
                {
                    out = Val::makeInt(static_cast<std::int32_t>(source.a));
                }
                else
                {
                    out = Val::makeInt(toInt(source));
                }
                return true;
            case TypeKind::Float:
                if (source.kind == Kind::String)
                {
                    mScratch.assign(textOf(source));
                    out = Val::makeFloat(std::strtof(mScratch.c_str(), nullptr));
                }
                else
                {
                    out = Val::makeFloat(toFloat(source));
                }
                return true;
            case TypeKind::Bool:
                out = Val::makeBool(truthy(source));
                return true;
            case TypeKind::String:
                if (source.kind == Kind::String)
                {
                    out = source;
                    return true;
                }
                mScratch.clear();
                appendText(mScratch, source);
                return newString(mScratch, out);
            case TypeKind::Object:
                out = Val{};
                if ((source.kind == Kind::Instance || source.kind == Kind::Form) && valueIsClass(source, target.name))
                {
                    out = source;
                }
                return true;
            case TypeKind::Array:
                out = source.kind == Kind::Array ? source : Val{};
                return true;
            case TypeKind::Struct:
                out = source.kind == Kind::Struct ? source : Val{};
                return true;
            case TypeKind::Var:
                out = source;
                return true;
            default:
                out = Val{};
                return true;
        }
    }

    // ---- lookup
    // ------------------------------------------------------------------------------------------------------

    Resolved Runtime::findFunction(std::uint32_t startScript, std::uint32_t stateId, std::uint32_t name)
    {
        const bool cacheable = startScript < (1u << 16) && stateId < (1u << 24) && name < (1u << 24);
        const std::uint64_t key
            = (static_cast<std::uint64_t>(startScript) << 48) | (static_cast<std::uint64_t>(stateId) << 24) | name;
        if (cacheable)
        {
            const auto cached = mFunctionCache.find(key);
            if (cached != mFunctionCache.end())
            {
                return cached->second;
            }
        }
        Resolved result;
        for (const std::uint32_t index : mScripts[startScript].mChain)
        {
            const Script& script = mScripts[index];
            for (int pass = 0; pass < 2 && !result.found; ++pass)
            {
                const std::uint32_t wanted = pass == 0 ? stateId : 0;
                if (pass == 1 && stateId == 0)
                {
                    break;
                }
                const auto state = script.mStateIndex.find(wanted);
                if (state == script.mStateIndex.end())
                {
                    continue;
                }
                const auto function = script.mStates[state->second].functions.find(name);
                if (function != script.mStates[state->second].functions.end())
                {
                    result = { index, function->second, true };
                }
            }
            if (result.found)
            {
                break;
            }
        }
        if (result.found && cacheable)
        {
            mFunctionCache.emplace(key, result);
        }
        return result;
    }

    bool Runtime::findProperty(
        std::uint32_t startScript, std::uint32_t name, std::uint32_t& scriptIndex, const PropInfo*& info) const
    {
        for (const std::uint32_t index : mScripts[startScript].mChain)
        {
            const auto found = mScripts[index].mProps.find(name);
            if (found != mScripts[index].mProps.end())
            {
                scriptIndex = index;
                info = &found->second;
                return true;
            }
        }
        return false;
    }

    // ---- native boundary
    // ---------------------------------------------------------------------------------------------

    bool Runtime::toPapyrus(const Val& value, PapyrusValue& out) const
    {
        out = PapyrusValue{};
        switch (value.kind)
        {
            case Kind::Int:
                out = PapyrusValue::makeInt(value.asInt());
                break;
            case Kind::Float:
                out = PapyrusValue::makeFloat(value.asFloat());
                break;
            case Kind::Bool:
                out = PapyrusValue::makeBool(value.a != 0);
                break;
            case Kind::String:
                out = PapyrusValue::makeString(textOf(value));
                break;
            case Kind::Form:
            {
                std::string_view display;
                if (value.b != 0)
                {
                    display = nameText(value.b);
                    const auto owner = mScriptByClassId.find(value.b);
                    if (owner != mScriptByClassId.end())
                    {
                        display = mScripts[owner->second].mClass; // the class as its own file spells it
                    }
                }
                out = PapyrusValue::makeForm(display, value.a);
            }
            break;
            case Kind::Instance:
                out = PapyrusValue::makeInstance(value.a);
                break;
            case Kind::Array:
                out = PapyrusValue::makeArray(value.a);
                break;
            case Kind::Struct:
                out = PapyrusValue::makeStruct(value.a);
                break;
            default:
                break;
        }
        return true;
    }

    bool Runtime::fromPapyrus(const PapyrusValue& value, Val& out)
    {
        out = Val{};
        switch (value.kind)
        {
            case PapyrusKind::None:
                return true;
            case PapyrusKind::Int:
                out = Val::makeInt(value.integer);
                return true;
            case PapyrusKind::Float:
                out = Val::makeFloat(value.number);
                return true;
            case PapyrusKind::Bool:
                out = Val::makeBool(value.integer != 0);
                return true;
            case PapyrusKind::String:
                return newString(value.text, out);
            case PapyrusKind::Object:
                if (value.attached)
                {
                    out = value.handle >= 1 && value.handle <= mInstances.size() ? Val::makeInstance(value.handle)
                                                                                 : Val{};
                    return true;
                }
                out = Val::makeForm(value.handle, value.text.empty() ? 0 : nameId(value.text));
                return true;
            case PapyrusKind::Array:
                if (value.handle < mHeap.size() && mHeap[value.handle].type == HeapObj::Type::Array)
                {
                    out = Val::makeArray(value.handle);
                }
                return true;
            case PapyrusKind::Struct:
                if (value.handle < mHeap.size() && mHeap[value.handle].type == HeapObj::Type::Struct)
                {
                    out = Val::makeStruct(value.handle);
                }
                return true;
        }
        return true;
    }

    // ---- calls
    // -------------------------------------------------------------------------------------------------------

    bool Runtime::pushFrame(Stack& stack, std::uint32_t script, std::uint32_t func, const Val& self, const Val* args,
        std::size_t argCount, const COp& dest, std::uint32_t maxDepth)
    {
        const Script& owner = mScripts[script];
        const CFunc& function = owner.mFuncs[func];
        if (stack.frames.size() >= maxDepth)
        {
            return fail(ErrorKind::DepthExceeded,
                "the call depth cap of " + std::to_string(maxDepth) + " was reached calling " + owner.mClass + "."
                    + std::string(nameText(function.name)));
        }
        const std::size_t base = stack.values.size();
        if (base + function.frameSize > kMaxStackValues)
        {
            return fail(
                ErrorKind::LimitExceeded, "the stack holds more than " + std::to_string(kMaxStackValues) + " values");
        }
        stack.values.resize(base + function.frameSize);
        for (std::size_t i = 0; i < function.frameSize; ++i)
        {
            stack.values[base + i]
                = i < argCount ? coerce(args[i], function.slotTypes[i]) : zeroOf(function.slotTypes[i]);
        }
        Frame frame;
        frame.script = script;
        frame.func = func;
        frame.pc = 0;
        frame.base = static_cast<std::uint32_t>(base);
        frame.self = self;
        frame.retDest = dest;
        stack.frames.push_back(frame);
        ++mStats.scriptCalls;
        return true;
    }

    bool Runtime::unresolvedCall(Stack& stack, ErrorKind kind, std::uint32_t classId, std::uint32_t name,
        const COp& dest, const TypeRef& destType, const char* what)
    {
        const std::string description
            = "class '" + std::string(nameText(classId)) + "' " + what + " '" + std::string(nameText(name)) + "'";
        if (mPolicy == UnknownFunctionPolicy::Strict)
        {
            return fail(kind, description);
        }
        if (kind == ErrorKind::MissingClass)
        {
            ++mStats.missingClassCalls;
        }
        logOnce("unresolved:" + std::to_string(classId) + ":" + std::to_string(name),
            description + "; the call returns the zero of its type");
        writeOp(stack, stack.frames.back(), dest, zeroOf(destType));
        return true;
    }

    bool Runtime::callNative(
        Stack& stack, const Resolved& resolved, const Val& self, const Val* args, std::size_t argCount, const COp& dest)
    {
        const Script& owner = mScripts[resolved.script];
        const CFunc& function = owner.mFuncs[resolved.func];
        auto label = [&]() { return owner.mClass + "." + std::string(nameText(function.name)); };
        const std::uint64_t key = (static_cast<std::uint64_t>(owner.mClassId) << 32) | function.name;
        Val result = zeroOf(function.ret);
        const auto bound = mNativeByKey.find(key);
        if (bound == mNativeByKey.end())
        {
            if (mPolicy == UnknownFunctionPolicy::Strict)
            {
                return fail(ErrorKind::UnregisteredNative, "native '" + label() + "' is declared and not registered");
            }
            ++mStats.unregisteredNativeCalls;
            logOnce("native:" + label(),
                "native '" + label() + "' is declared and not registered; it returns the zero of its type");
        }
        else if (mNatives[bound->second].desc.latent)
        {
            if (mPolicy == UnknownFunctionPolicy::Strict)
            {
                return fail(ErrorKind::LatentUnsupported,
                    "latent native '" + label() + "' is not supported yet (task 0093 stage d)");
            }
            ++mStats.latentSkipped;
            logOnce("latent:" + label(),
                "latent native '" + label() + "' is not supported yet; it returns the zero of its type");
        }
        else
        {
            mNativeArgs.clear();
            for (std::size_t i = 0; i < argCount; ++i)
            {
                PapyrusValue converted;
                toPapyrus(coerce(args[i], function.slotTypes[i]), converted);
                mNativeArgs.push_back(converted);
            }
            NativeCall call;
            toPapyrus(self, call.self);
            call.arguments = std::span<const PapyrusValue>(mNativeArgs.data(), mNativeArgs.size());
            NativeResult nativeResult;
            mInNative = true;
            bool threw = false;
            std::string what;
            try
            {
                nativeResult = mNatives[bound->second].fn(call);
            }
            catch (const std::exception& error)
            {
                threw = true;
                what = error.what();
            }
            catch (...)
            {
                threw = true;
                what = "an exception that is not a std::exception";
            }
            mInNative = false;
            if (threw)
            {
                return fail(ErrorKind::NativeFailed, "native '" + label() + "' threw: " + what);
            }
            ++mStats.nativeCalls;
            Val converted;
            if (!fromPapyrus(nativeResult.value, converted))
            {
                return false;
            }
            result = coerce(converted, function.ret);
        }
        writeOp(stack, stack.frames.back(), dest, result);
        return mErrKind == ErrorKind::None;
    }

    bool Runtime::callResolved(Stack& stack, const Resolved& resolved, const Val& self, const Val* args,
        std::size_t argCount, const COp& dest, const TypeRef&, std::uint32_t maxDepth)
    {
        const Script& owner = mScripts[resolved.script];
        const CFunc& function = owner.mFuncs[resolved.func];
        if (argCount != function.paramCount)
        {
            return fail(ErrorKind::ArgumentCount,
                owner.mClass + "." + std::string(nameText(function.name)) + " takes "
                    + std::to_string(function.paramCount) + " arguments but " + std::to_string(argCount)
                    + " were given");
        }
        if (function.native)
        {
            return callNative(stack, resolved, self, args, argCount, dest);
        }
        return pushFrame(
            stack, resolved.script, resolved.func, function.global ? Val{} : self, args, argCount, dest, maxDepth);
    }

    bool Runtime::doCall(Stack& stack, const CInstr& instr, const COp* ops, std::uint32_t, std::uint32_t maxDepth)
    {
        const Frame frame = stack.frames.back();
        const std::size_t destIndex = instr.op == Pex::Op::CallParent ? 1 : 2;
        const COp& destOp = ops[destIndex];
        for (std::size_t i = 0; i < instr.extra; ++i)
        {
            mArgScratch[i] = readOp(stack, frame, ops[instr.fixed + i]);
        }
        if (mErrKind != ErrorKind::None)
        {
            return false;
        }

        std::uint32_t start = kNoScript;
        std::uint32_t state = 0;
        std::uint32_t name = 0;
        std::uint32_t classId = 0;
        Val self;

        if (instr.op == Pex::Op::CallStatic)
        {
            classId = ops[0].n;
            name = ops[1].n;
            start = scriptByClassId(classId);
            if (start == kNoScript)
            {
                return unresolvedCall(
                    stack, ErrorKind::MissingClass, classId, name, destOp, instr.dest, "is not loaded; no call to");
            }
        }
        else if (instr.op == Pex::Op::CallParent)
        {
            name = ops[0].n;
            const Script& declaring = mScripts[frame.script];
            classId = declaring.mParentId;
            if (declaring.mChain.size() < 2)
            {
                if (declaring.mMissingParentId != 0 || declaring.mParentId != 0)
                {
                    return unresolvedCall(
                        stack, ErrorKind::MissingClass, classId, name, destOp, instr.dest, "is not loaded; no call to");
                }
                return unresolvedCall(stack, ErrorKind::NoSuchFunction, declaring.mClassId, name, destOp, instr.dest,
                    "has no parent to call");
            }
            start = declaring.mChain[1];
            self = frame.self;
            if (const Instance* instance = instanceOf(self))
            {
                state = instance->stateId;
            }
        }
        else
        {
            name = ops[0].n;
            const Val receiver = readOp(stack, frame, ops[1]);
            if (mErrKind != ErrorKind::None)
            {
                return false;
            }
            self = receiver;
            switch (receiver.kind)
            {
                case Kind::Instance:
                {
                    const Instance* instance = instanceOf(receiver);
                    if (instance == nullptr)
                    {
                        return fail(ErrorKind::NoSuchInstance, "call on an instance that does not exist");
                    }
                    start = instance->script;
                    state = instance->stateId;
                    classId = mScripts[start].mClassId;
                    break;
                }
                case Kind::Form:
                {
                    classId = classOfForm(receiver);
                    start = classId == kNoScript ? kNoScript : scriptByClassId(classId);
                    if (start == kNoScript)
                    {
                        return unresolvedCall(stack, ErrorKind::MissingClass, classId == kNoScript ? 0 : classId, name,
                            destOp, instr.dest, "is not loaded; no call to");
                    }
                    break;
                }
                case Kind::None:
                    if (mPolicy == UnknownFunctionPolicy::Strict)
                    {
                        return fail(ErrorKind::NoneReceiver, "call of '" + std::string(nameText(name)) + "' on None");
                    }
                    ++mStats.noneReceiverCalls;
                    logOnce("none:" + std::to_string(name),
                        "call of '" + std::string(nameText(name)) + "' on None; it returns the zero of its type");
                    writeOp(stack, frame, destOp, zeroOf(instr.dest));
                    return mErrKind == ErrorKind::None;
                default:
                    return fail(ErrorKind::TypeMismatch,
                        "call of '" + std::string(nameText(name)) + "' on a value that is not an object");
            }
        }

        if (!prepare(start))
        {
            return fail(ErrorKind::BadScript, mScripts[start].mFileName + ": " + mScripts[start].mPrepareError);
        }
        const Resolved resolved = findFunction(start, state, name);
        if (!resolved.found)
        {
            if (mNames.mIsEvent[name] != 0)
            {
                ++mStats.ignoredEvents;
                writeOp(stack, frame, destOp, zeroOf(instr.dest));
                return mErrKind == ErrorKind::None;
            }
            return unresolvedCall(
                stack, ErrorKind::NoSuchFunction, classId, name, destOp, instr.dest, "has no function");
        }
        return callResolved(stack, resolved, self, mArgScratch.data(), instr.extra, destOp, instr.dest, maxDepth);
    }

    bool Runtime::doProperty(Stack& stack, const CInstr& instr, const COp* ops, bool set, std::uint32_t maxDepth)
    {
        const Frame frame = stack.frames.back();
        const std::uint32_t name = ops[0].n;
        const Val receiver = readOp(stack, frame, ops[1]);
        Val value;
        if (set)
        {
            value = readOp(stack, frame, ops[2]);
        }
        if (mErrKind != ErrorKind::None)
        {
            return false;
        }
        const COp discard;
        const COp& destOp = set ? discard : ops[2];

        std::uint32_t start = kNoScript;
        std::uint32_t classId = 0;
        if (receiver.kind == Kind::Instance)
        {
            const Instance* instance = instanceOf(receiver);
            if (instance == nullptr)
            {
                return fail(ErrorKind::NoSuchInstance, "property of an instance that does not exist");
            }
            start = instance->script;
            classId = mScripts[start].mClassId;
        }
        else if (receiver.kind == Kind::Form)
        {
            classId = classOfForm(receiver);
            start = classId == kNoScript ? kNoScript : scriptByClassId(classId);
            if (start == kNoScript)
            {
                return unresolvedCall(stack, ErrorKind::MissingClass, classId == kNoScript ? 0 : classId, name, destOp,
                    instr.dest, "is not loaded; no property");
            }
        }
        else if (receiver.kind == Kind::None)
        {
            if (mPolicy == UnknownFunctionPolicy::Strict)
            {
                return fail(ErrorKind::NoneReceiver, "property '" + std::string(nameText(name)) + "' of None");
            }
            ++mStats.noneReceiverCalls;
            logOnce("noneprop:" + std::to_string(name),
                "property '" + std::string(nameText(name)) + "' of None; it reads as the zero of its type");
            if (!set)
            {
                writeOp(stack, frame, destOp, zeroOf(instr.dest));
            }
            return mErrKind == ErrorKind::None;
        }
        else
        {
            return fail(ErrorKind::TypeMismatch,
                "property '" + std::string(nameText(name)) + "' of a value that is not an object");
        }

        if (!prepare(start))
        {
            return fail(ErrorKind::BadScript, mScripts[start].mFileName + ": " + mScripts[start].mPrepareError);
        }
        std::uint32_t owner = 0;
        const PropInfo* info = nullptr;
        if (!findProperty(start, name, owner, info))
        {
            return unresolvedCall(
                stack, ErrorKind::NoSuchFunction, classId, name, destOp, instr.dest, "has no property");
        }
        if (info->autoProperty)
        {
            Instance* instance = instanceOf(receiver);
            const std::uint32_t slot = mScripts[owner].mVarBase + info->backing;
            if (instance == nullptr || slot >= instance->vars.size())
            {
                return unresolvedCall(
                    stack, ErrorKind::NoSuchFunction, classId, name, destOp, instr.dest, "has no stored property");
            }
            if (set)
            {
                instance->vars[slot] = coerce(value, info->type);
            }
            else
            {
                writeOp(stack, frame, destOp, instance->vars[slot]);
            }
            return mErrKind == ErrorKind::None;
        }
        const std::int32_t accessor = set ? info->setter : info->getter;
        if (accessor < 0)
        {
            return unresolvedCall(stack, ErrorKind::NoSuchFunction, classId, name, destOp, instr.dest,
                set ? "has no setter for property" : "has no getter for property");
        }
        const Resolved resolved{ owner, static_cast<std::uint32_t>(accessor), true };
        return callResolved(
            stack, resolved, receiver, set ? &value : nullptr, set ? 1 : 0, destOp, instr.dest, maxDepth);
    }

    // ---- arrays and structs
    // ------------------------------------------------------------------------------------------

    bool Runtime::doArrayOp(Stack& stack, const CInstr& instr, const COp* ops)
    {
        const Frame frame = stack.frames.back();
        auto read = [&](std::size_t i) { return readOp(stack, frame, ops[i]); };
        auto write = [&](std::size_t i, const Val& value) { writeOp(stack, frame, ops[i], value); };

        // The array operand: Array, or None (only some opcodes accept it).
        auto arrayAt = [&](const Val& value, HeapObj*& object, bool noneIsEmpty) {
            object = nullptr;
            if (value.kind == Kind::Array && value.a < mHeap.size() && mHeap[value.a].type == HeapObj::Type::Array)
            {
                object = &mHeap[value.a];
                return true;
            }
            if (value.kind == Kind::None)
            {
                return noneIsEmpty ? true : fail(ErrorKind::NoneReceiver, "the array is None");
            }
            return fail(ErrorKind::TypeMismatch, "a value that is not an array is used as one");
        };
        auto indexError = [&](std::int32_t index, std::size_t length) {
            return fail(ErrorKind::IndexOutOfRange,
                "index " + std::to_string(index) + " of an array of " + std::to_string(length) + " elements");
        };

        HeapObj* array = nullptr;
        switch (instr.op)
        {
            case Pex::Op::ArrayCreate:
            {
                const std::int32_t size = toInt(read(1));
                if (size < 0)
                {
                    return fail(ErrorKind::LimitExceeded, "an array of " + std::to_string(size) + " elements");
                }
                Val created;
                if (!newArray(instr.dest.kind == TypeKind::Array ? instr.dest.elem : TypeKind::None,
                        static_cast<std::size_t>(size), created))
                {
                    return false;
                }
                write(0, created);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArrayLength:
            {
                if (!arrayAt(read(1), array, true))
                {
                    return false;
                }
                write(0, Val::makeInt(array == nullptr ? 0 : static_cast<std::int32_t>(array->items.size())));
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArrayGetElement:
            {
                if (!arrayAt(read(1), array, false))
                {
                    return false;
                }
                const std::int32_t index = toInt(read(2));
                if (index < 0 || static_cast<std::size_t>(index) >= array->items.size())
                {
                    return indexError(index, array->items.size());
                }
                write(0, array->items[static_cast<std::size_t>(index)]);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArraySetElement:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                const std::int32_t index = toInt(read(1));
                const Val value = read(2);
                if (index < 0 || static_cast<std::size_t>(index) >= array->items.size())
                {
                    return indexError(index, array->items.size());
                }
                TypeRef element;
                element.kind = array->elem;
                array->items[static_cast<std::size_t>(index)] = coerce(value, element);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArrayFindElement:
            case Pex::Op::ArrayRFindElement:
            case Pex::Op::ArrayFindStruct:
            case Pex::Op::ArrayRFindStruct:
            {
                const bool structs = instr.op == Pex::Op::ArrayFindStruct || instr.op == Pex::Op::ArrayRFindStruct;
                const bool reverse = instr.op == Pex::Op::ArrayRFindElement || instr.op == Pex::Op::ArrayRFindStruct;
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                const std::uint32_t member = structs ? ops[2].n : 0;
                const Val wanted = read(structs ? 3 : 2);
                const std::int32_t start = toInt(read(structs ? 4 : 3));
                const std::int64_t size = static_cast<std::int64_t>(array->items.size());
                std::int32_t found = -1;
                if (size > 0)
                {
                    std::int64_t index;
                    if (reverse)
                    {
                        index = start < 0 || start >= size ? size - 1 : start;
                    }
                    else
                    {
                        index = start < 0 ? 0 : start;
                    }
                    for (; index >= 0 && index < size; index += reverse ? -1 : 1)
                    {
                        const Val& item = array->items[static_cast<std::size_t>(index)];
                        bool equal;
                        if (structs)
                        {
                            equal = false;
                            if (item.kind == Kind::Struct && item.a < mHeap.size())
                            {
                                const HeapObj& object = mHeap[item.a];
                                const auto definition = mScripts[object.script].mStructs.find(object.structName);
                                if (definition != mScripts[object.script].mStructs.end())
                                {
                                    const auto& names = definition->second.memberNames;
                                    const auto at = std::find(names.begin(), names.end(), member);
                                    equal = at != names.end()
                                        && valuesEqual(
                                            object.items[static_cast<std::size_t>(at - names.begin())], wanted);
                                }
                            }
                        }
                        else
                        {
                            equal = valuesEqual(item, wanted);
                        }
                        if (equal)
                        {
                            found = static_cast<std::int32_t>(index);
                            break;
                        }
                    }
                }
                write(1, Val::makeInt(found));
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArrayGetAllMatchingStructs:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                const std::uint32_t member = ops[2].n;
                const Val wanted = read(3);
                std::vector<Val>& matches = mMatchScratch;
                matches.clear();
                for (const Val& item : array->items)
                {
                    if (item.kind != Kind::Struct || item.a >= mHeap.size())
                    {
                        continue;
                    }
                    const HeapObj& object = mHeap[item.a];
                    const auto definition = mScripts[object.script].mStructs.find(object.structName);
                    if (definition == mScripts[object.script].mStructs.end())
                    {
                        continue;
                    }
                    const auto& names = definition->second.memberNames;
                    const auto at = std::find(names.begin(), names.end(), member);
                    if (at != names.end()
                        && valuesEqual(object.items[static_cast<std::size_t>(at - names.begin())], wanted))
                    {
                        matches.push_back(item);
                    }
                }
                Val created;
                if (!newArray(instr.dest.kind == TypeKind::Array ? instr.dest.elem : TypeKind::Struct, matches.size(),
                        created))
                {
                    return false;
                }
                mHeap[created.a].items = matches;
                write(1, created);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::ArrayAdd:
            case Pex::Op::ArrayInsert:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                const Val value = read(1);
                const std::int32_t third = toInt(read(2));
                const bool add = instr.op == Pex::Op::ArrayAdd;
                const std::int64_t count = add ? std::max<std::int32_t>(third, 1) : 1;
                if (!add && (third < 0 || static_cast<std::size_t>(third) > array->items.size()))
                {
                    return indexError(third, array->items.size());
                }
                if (array->items.size() + static_cast<std::size_t>(count) > mOptions.maxArrayLength)
                {
                    return fail(ErrorKind::LimitExceeded,
                        "an array of " + std::to_string(array->items.size() + static_cast<std::size_t>(count))
                            + " elements is over the cap of " + std::to_string(mOptions.maxArrayLength));
                }
                const std::uint32_t arrayId = read(0).a;
                if (!chargeHeap(static_cast<std::uint64_t>(count) * sizeof(Val)))
                {
                    return false;
                }
                array = &mHeap[arrayId]; // a collection may have run; the array is a root, but be exact
                TypeRef element;
                element.kind = array->elem;
                const Val stored = coerce(value, element);
                if (add)
                {
                    array->items.insert(array->items.end(), static_cast<std::size_t>(count), stored);
                }
                else
                {
                    array->items.insert(array->items.begin() + third, stored);
                }
                return true;
            }
            case Pex::Op::ArrayRemoveLast:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                if (array->items.empty())
                {
                    return indexError(-1, 0);
                }
                array->items.pop_back();
                return true;
            }
            case Pex::Op::ArrayRemove:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                std::int32_t index = toInt(read(1));
                const std::int32_t count = std::max<std::int32_t>(toInt(read(2)), 1);
                const std::int64_t size = static_cast<std::int64_t>(array->items.size());
                if (index < 0)
                {
                    index = static_cast<std::int32_t>(index + size);
                }
                if (index < 0 || index >= size)
                {
                    return indexError(toInt(read(1)), array->items.size());
                }
                const std::int64_t last = std::min<std::int64_t>(size, static_cast<std::int64_t>(index) + count);
                array->items.erase(array->items.begin() + index, array->items.begin() + last);
                return true;
            }
            case Pex::Op::ArrayClear:
            {
                if (!arrayAt(read(0), array, false))
                {
                    return false;
                }
                array->items.clear();
                return true;
            }
            default:
                return fail(ErrorKind::UnsupportedOpcode, "opcode " + std::to_string(instr.op));
        }
    }

    bool Runtime::doStructOp(Stack& stack, const CInstr& instr, const COp* ops)
    {
        const Frame frame = stack.frames.back();
        auto structAt = [&](const Val& value, HeapObj*& object) {
            object = nullptr;
            if (value.kind == Kind::None)
            {
                return fail(ErrorKind::NoneReceiver, "the struct is None");
            }
            if (value.kind != Kind::Struct || value.a >= mHeap.size() || mHeap[value.a].type != HeapObj::Type::Struct)
            {
                return fail(ErrorKind::TypeMismatch, "a value that is not a struct is used as one");
            }
            object = &mHeap[value.a];
            return true;
        };
        auto memberIndex = [&](const HeapObj& object, std::uint32_t member, std::size_t& index) {
            const auto definition = mScripts[object.script].mStructs.find(object.structName);
            if (definition != mScripts[object.script].mStructs.end())
            {
                const auto& names = definition->second.memberNames;
                const auto at = std::find(names.begin(), names.end(), member);
                if (at != names.end())
                {
                    index = static_cast<std::size_t>(at - names.begin());
                    return true;
                }
            }
            return fail(ErrorKind::TypeMismatch,
                "the struct '" + std::string(nameText(object.structName)) + "' has no member '"
                    + std::string(nameText(member)) + "'");
        };

        HeapObj* object = nullptr;
        std::size_t member = 0;
        switch (instr.op)
        {
            case Pex::Op::StructCreate:
            {
                if (instr.dest.kind != TypeKind::Struct)
                {
                    return fail(
                        ErrorKind::TypeMismatch, "a struct is created into something that is not of a struct type");
                }
                const std::uint32_t owner = scriptByClassId(instr.dest.name);
                if (owner == kNoScript)
                {
                    return fail(ErrorKind::MissingClass,
                        "the script '" + std::string(nameText(instr.dest.name))
                            + "' that declares the struct is not loaded");
                }
                if (!prepare(owner))
                {
                    return fail(ErrorKind::BadScript, mScripts[owner].mPrepareError);
                }
                Val created;
                if (!newStruct(owner, instr.dest.name2, created))
                {
                    return false;
                }
                writeOp(stack, frame, ops[0], created);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::StructGet:
            {
                if (!structAt(readOp(stack, frame, ops[1]), object) || !memberIndex(*object, ops[2].n, member))
                {
                    return false;
                }
                writeOp(stack, frame, ops[0], object->items[member]);
                return mErrKind == ErrorKind::None;
            }
            case Pex::Op::StructSet:
            {
                const Val value = readOp(stack, frame, ops[2]);
                if (!structAt(readOp(stack, frame, ops[0]), object) || !memberIndex(*object, ops[1].n, member))
                {
                    return false;
                }
                const auto definition = mScripts[object->script].mStructs.find(object->structName);
                object->items[member] = coerce(value, definition->second.memberTypes[member]);
                return mErrKind == ErrorKind::None;
            }
            default:
                return fail(ErrorKind::UnsupportedOpcode, "opcode " + std::to_string(instr.op));
        }
    }

    // ---- the instruction loop
    // ----------------------------------------------------------------------------------------

    bool Runtime::execute(Stack& stack, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed)
    {
        using namespace Pex::Op;
        // Where an error happened: the script file and "Class.function@instruction".
        auto locate = [this](const Frame& at, std::uint32_t instruction) {
            const Script& owner = mScripts[at.script];
            mErrFile = owner.mFileName;
            mErrLoc = owner.mClass + "." + std::string(nameText(owner.mFuncs[at.func].name)) + "@"
                + std::to_string(instruction);
        };
        while (!stack.frames.empty())
        {
            Frame& frame = stack.frames.back();
            const CFunc& function = mScripts[frame.script].mFuncs[frame.func];
            const std::uint32_t pc = frame.pc;
            Val returned;
            bool returning = false;
            if (pc >= function.code.size())
            {
                // Running off the end is a return of the zero of the declared type.
                returned = zeroOf(function.ret);
                returning = true;
            }
            else
            {
                if (executed >= maxInstructions)
                {
                    fail(ErrorKind::BudgetExceeded,
                        "the budget of " + std::to_string(maxInstructions) + " instructions was used up");
                    locate(frame, pc);
                    return false;
                }
                ++executed;
                ++mStats.instructions;
                const CInstr& instr = function.code[pc];
                const COp* ops = function.ops.data() + instr.first;
                frame.pc = pc + 1;
                auto R = [&](std::size_t i) { return readOp(stack, frame, ops[i]); };
                auto W = [&](std::size_t i, const Val& value) { writeOp(stack, frame, ops[i], value); };

                switch (instr.op)
                {
                    case Nop:
                        break;
                    case IAdd:
                    case ISub:
                    case IMul:
                    {
                        const std::uint32_t x = static_cast<std::uint32_t>(toInt(R(1)));
                        const std::uint32_t y = static_cast<std::uint32_t>(toInt(R(2)));
                        const std::uint32_t r = instr.op == IAdd ? x + y : instr.op == ISub ? x - y : x * y;
                        W(0, Val::makeInt(static_cast<std::int32_t>(r)));
                        break;
                    }
                    case IDiv:
                    case IMod:
                    {
                        const std::int32_t x = toInt(R(1));
                        const std::int32_t y = toInt(R(2));
                        if (y == 0)
                        {
                            fail(ErrorKind::DivideByZero,
                                instr.op == IDiv ? "integer division by zero" : "integer remainder of zero");
                            break;
                        }
                        const bool extreme = x == std::numeric_limits<std::int32_t>::min() && y == -1;
                        W(0, Val::makeInt(extreme ? (instr.op == IDiv ? x : 0) : (instr.op == IDiv ? x / y : x % y)));
                        break;
                    }
                    case FAdd:
                    case FSub:
                    case FMul:
                    {
                        const float x = toFloat(R(1));
                        const float y = toFloat(R(2));
                        W(0, Val::makeFloat(instr.op == FAdd ? x + y : instr.op == FSub ? x - y : x * y));
                        break;
                    }
                    case FDiv:
                    {
                        const float x = toFloat(R(1));
                        const float y = toFloat(R(2));
                        if (y == 0.0f)
                        {
                            fail(ErrorKind::DivideByZero, "float division by zero");
                            break;
                        }
                        W(0, Val::makeFloat(x / y));
                        break;
                    }
                    case Not:
                        W(0, Val::makeBool(!truthy(R(1))));
                        break;
                    case INeg:
                        W(0, Val::makeInt(static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(toInt(R(1))))));
                        break;
                    case FNeg:
                        W(0, Val::makeFloat(-toFloat(R(1))));
                        break;
                    case Assign:
                        W(0, R(1));
                        break;
                    case Cast:
                    {
                        Val out;
                        if (castTo(R(1), instr.dest, out))
                        {
                            W(0, out);
                        }
                        break;
                    }
                    case CompareEq:
                        W(0, Val::makeBool(valuesEqual(R(1), R(2))));
                        break;
                    case CompareLt:
                    case CompareLe:
                    case CompareGt:
                    case CompareGe:
                    {
                        const Val a = R(1);
                        const Val b = R(2);
                        if (!isNumber(a) || !isNumber(b))
                        {
                            fail(ErrorKind::TypeMismatch, "values that are not numbers are ordered");
                            break;
                        }
                        int order;
                        if (a.kind == Kind::Float || b.kind == Kind::Float)
                        {
                            const float x = toFloat(a);
                            const float y = toFloat(b);
                            order = x < y ? -1 : x > y ? 1 : 0;
                            if (std::isnan(x) || std::isnan(y))
                            {
                                W(0, Val::makeBool(false));
                                break;
                            }
                        }
                        else
                        {
                            order = a.asInt() < b.asInt() ? -1 : a.asInt() > b.asInt() ? 1 : 0;
                        }
                        const bool result = instr.op == CompareLt ? order < 0
                            : instr.op == CompareLe               ? order <= 0
                            : instr.op == CompareGt               ? order > 0
                                                                  : order >= 0;
                        W(0, Val::makeBool(result));
                        break;
                    }
                    case Jmp:
                        frame.pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + ops[0].v.asInt());
                        break;
                    case JmpT:
                    case JmpF:
                    {
                        const bool condition = truthy(R(0));
                        if (condition == (instr.op == JmpT))
                        {
                            frame.pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + ops[1].v.asInt());
                        }
                        break;
                    }
                    case CallMethod:
                    case CallParent:
                    case CallStatic:
                        doCall(stack, instr, ops, pc, maxDepth);
                        break;
                    case Return:
                        returned = coerce(R(0), function.ret);
                        returning = true;
                        break;
                    case StrCat:
                    {
                        mScratch.clear();
                        appendText(mScratch, R(1));
                        appendText(mScratch, R(2));
                        Val out;
                        if (newString(mScratch, out))
                        {
                            W(0, out);
                        }
                        break;
                    }
                    case PropGet:
                        doProperty(stack, instr, ops, false, maxDepth);
                        break;
                    case PropSet:
                        doProperty(stack, instr, ops, true, maxDepth);
                        break;
                    case ArrayCreate:
                    case ArrayLength:
                    case ArrayGetElement:
                    case ArraySetElement:
                    case ArrayFindElement:
                    case ArrayRFindElement:
                    case ArrayFindStruct:
                    case ArrayRFindStruct:
                    case ArrayAdd:
                    case ArrayInsert:
                    case ArrayRemoveLast:
                    case ArrayRemove:
                    case ArrayClear:
                    case ArrayGetAllMatchingStructs:
                        doArrayOp(stack, instr, ops);
                        break;
                    case StructCreate:
                    case StructGet:
                    case StructSet:
                        doStructOp(stack, instr, ops);
                        break;
                    case Is:
                    {
                        const Val value = R(1);
                        W(0, Val::makeBool(valueIsClass(value, ops[2].n)));
                        break;
                    }
                    default:
                        fail(ErrorKind::UnsupportedOpcode, "opcode " + std::to_string(instr.op) + " is not supported");
                        break;
                }
                if (mErrKind != ErrorKind::None)
                {
                    // The error's place: the innermost frame, at the instruction that failed.
                    const Frame& at = stack.frames.back();
                    locate(at, at.pc == 0 ? 0 : at.pc - 1);
                    return false;
                }
                if (!returning)
                {
                    continue;
                }
            }

            // A return: hand the value to the caller, or finish the stack.
            if (stack.frames.size() == 1)
            {
                stack.result = returned;
                stack.values.clear();
                stack.frames.clear();
                return true;
            }
            const COp dest = frame.retDest;
            const std::uint32_t base = frame.base;
            stack.frames.pop_back();
            stack.values.resize(base);
            writeOp(stack, stack.frames.back(), dest, returned);
            if (mErrKind != ErrorKind::None)
            {
                const Frame& at = stack.frames.back();
                locate(at, at.pc == 0 ? 0 : at.pc - 1);
                return false;
            }
        }
        return true;
    }
}
