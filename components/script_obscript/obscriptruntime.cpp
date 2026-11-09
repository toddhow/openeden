#include "obscriptruntime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/interpreter/context.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/opcodes.hpp>
#include <components/interpreter/runtime.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

#include "obscriptcompile.hpp"
#include "obscriptlimits.hpp"

namespace ScriptObscript
{
    using namespace openeden::script;

    namespace
    {
        constexpr int kFirstFunctionCode = 0x20000; // segment 3 (components/compiler/extensions.cpp)
        constexpr int kLastFunctionCode = 0x2ffff;

        std::string lowerText(std::string_view text)
        {
            std::string result(text);
            Misc::StringUtils::lowerCaseInPlace(result);
            return result;
        }

        // Something a script did wrong while it ran.
        class ScriptError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        struct Instance
        {
            ScriptId mScript = 0;
            FormId mSelf = 0;
            std::vector<std::int16_t> mShorts;
            std::vector<std::int32_t> mLongs;
            std::vector<float> mFloats;
        };

        class RuntimeImpl;

        // The interpreter's view of one running instance. Lives on the stack of RuntimeImpl::run; owns nothing.
        class InstanceContext final : public Interpreter::Context
        {
        public:
            InstanceContext(ObScriptWorld& world, const CompiledScript& script, Instance& instance)
                : mWorld(world)
                , mScript(script)
                , mInstance(instance)
            {
            }

            ObScriptWorld& world() { return mWorld; }
            FormId self() const { return mInstance.mSelf; }

            /// A name written in a script ("ref.Function", a form argument): a local ref variable, else a form. Runs
            /// for every such call, so it lower-cases into a buffer on the stack (compiled names are at most
            /// Limits::kMaxIdentifierLength characters) and allocates nothing.
            FormId resolveReference(std::string_view name) const
            {
                char buffer[Limits::kMaxIdentifierLength];
                std::string_view lowered;
                std::string heap; // only for a name the compiler would have refused
                if (name.size() <= sizeof(buffer))
                {
                    for (std::size_t i = 0; i < name.size(); ++i)
                    {
                        buffer[i] = Misc::StringUtils::toLower(name[i]);
                    }
                    lowered = std::string_view(buffer, name.size());
                }
                else
                {
                    heap = lowerText(name);
                    lowered = heap;
                }
                if (mScript.isReference(lowered))
                {
                    const int index = mScript.mLocals.searchIndex('l', lowered);
                    if (index >= 0 && static_cast<std::size_t>(index) < mInstance.mLongs.size())
                    {
                        return static_cast<FormId>(mInstance.mLongs[static_cast<std::size_t>(index)]);
                    }
                    return 0;
                }
                FormId form = 0;
                if (mWorld.resolveForm(lowered, form))
                {
                    return form;
                }
                return 0;
            }

            ESM::RefId getTarget() const override { return ESM::RefId(); }

            /// A form used as a value (Compiler::ParserOptions::mFormValues). The compiler gives the name in lower
            /// case and locals never come here, so the world is asked directly (no allocation).
            int getFormId(std::string_view name) const override
            {
                FormId form = 0;
                return mWorld.resolveForm(name, form) ? static_cast<int>(form) : 0;
            }

            int getLocalShort(int index) const override { return at(mInstance.mShorts, index); }
            int getLocalLong(int index) const override { return at(mInstance.mLongs, index); }
            float getLocalFloat(int index) const override { return at(mInstance.mFloats, index); }

            void setLocalShort(int index, int value) override
            {
                put(mInstance.mShorts, index, static_cast<std::int16_t>(value));
            }
            void setLocalLong(int index, int value) override { put(mInstance.mLongs, index, value); }
            void setLocalFloat(int index, float value) override { put(mInstance.mFloats, index, value); }

            void messageBox(std::string_view message, const std::vector<std::string>&) override
            {
                mWorld.report(message);
            }
            void report(const std::string& message) override { mWorld.report(message); }

            int getGlobalShort(std::string_view name) const override { return globalInteger(name); }
            int getGlobalLong(std::string_view name) const override { return globalInteger(name); }
            float getGlobalFloat(std::string_view name) const override
            {
                const Value value = mWorld.getGlobal(name);
                return value.kind == ValueKind::Float ? value.number : static_cast<float>(value.integer);
            }
            void setGlobalShort(std::string_view name, int value) override
            {
                mWorld.setGlobal(name, Value::makeInteger(value));
            }
            void setGlobalLong(std::string_view name, int value) override
            {
                mWorld.setGlobal(name, Value::makeInteger(value));
            }
            void setGlobalFloat(std::string_view name, float value) override
            {
                mWorld.setGlobal(name, Value::makeFloat(value));
            }
            std::vector<std::string> getGlobals() const override { return {}; }
            char getGlobalType(std::string_view name) const override
            {
                switch (mWorld.globalKind(name))
                {
                    case ValueKind::Integer:
                    case ValueKind::Form:
                        return 'l';
                    case ValueKind::Float:
                        return 'f';
                    case ValueKind::String:
                        break;
                }
                return ' ';
            }

            // Morrowind specific questions: an ObScript script never asks them.
            std::string getActionBinding(std::string_view) const override { return {}; }
            std::string_view getActorName() const override { return {}; }
            std::string_view getNPCRace() const override { return {}; }
            std::string_view getNPCClass() const override { return {}; }
            std::string_view getNPCFaction() const override { return {}; }
            std::string_view getNPCRank() const override { return {}; }
            std::string_view getPCName() const override { return {}; }
            std::string_view getPCRace() const override { return {}; }
            std::string_view getPCClass() const override { return {}; }
            std::string_view getPCRank() const override { return {}; }
            std::string_view getPCNextRank() const override { return {}; }
            int getPCBounty() const override { return 0; }
            std::string_view getCurrentCellName() const override { return {}; }

            int getMemberShort(ESM::RefId id, std::string_view name, bool) const override
            {
                return memberInteger(id, name);
            }
            int getMemberLong(ESM::RefId id, std::string_view name, bool) const override
            {
                return memberInteger(id, name);
            }
            float getMemberFloat(ESM::RefId id, std::string_view name, bool) const override
            {
                const Value value = mWorld.getVariable(resolveReference(id.getRefIdString()), name);
                return value.kind == ValueKind::Float ? value.number : static_cast<float>(value.integer);
            }
            void setMemberShort(ESM::RefId id, std::string_view name, int value, bool) override
            {
                mWorld.setVariable(resolveReference(id.getRefIdString()), name, Value::makeInteger(value));
            }
            void setMemberLong(ESM::RefId id, std::string_view name, int value, bool) override
            {
                mWorld.setVariable(resolveReference(id.getRefIdString()), name, Value::makeInteger(value));
            }
            void setMemberFloat(ESM::RefId id, std::string_view name, float value, bool) override
            {
                mWorld.setVariable(resolveReference(id.getRefIdString()), name, Value::makeFloat(value));
            }

        private:
            template <class T>
            static T at(const std::vector<T>& values, int index)
            {
                if (index < 0 || static_cast<std::size_t>(index) >= values.size())
                {
                    return T{};
                }
                return values[static_cast<std::size_t>(index)];
            }

            template <class T>
            static void put(std::vector<T>& values, int index, T value)
            {
                if (index < 0 || static_cast<std::size_t>(index) >= values.size())
                {
                    throw ScriptError("a local variable index is out of range");
                }
                values[static_cast<std::size_t>(index)] = value;
            }

            int globalInteger(std::string_view name) const
            {
                const Value value = mWorld.getGlobal(name);
                return value.kind == ValueKind::Float ? static_cast<int>(value.number) : value.integer;
            }

            int memberInteger(ESM::RefId id, std::string_view name) const
            {
                const Value value = mWorld.getVariable(resolveReference(id.getRefIdString()), name);
                return value.kind == ValueKind::Float ? static_cast<int>(value.number) : value.integer;
            }

            ObScriptWorld& mWorld;
            const CompiledScript& mScript;
            Instance& mInstance;
        };

        // A call of a registered function, or of an unknown name in tolerant mode. Segment 3: the argument is the
        // number of optional arguments given. The explicit form first takes the reference's name off the stack.
        class OpFunction final : public Interpreter::Opcode1
        {
        public:
            OpFunction(FunctionEntry& entry, bool explicitReference, bool skipVariant = false)
                : mEntry(entry)
                , mExplicit(explicitReference)
                , mSkipVariant(skipVariant)
            {
            }

            void execute(Interpreter::Runtime& runtime, unsigned int optionalCount) override
            {
                InstanceContext& context = static_cast<InstanceContext&>(runtime.getContext());

                FormId reference = 0;
                if (mExplicit)
                {
                    const std::string_view name = runtime.getStringLiteral(runtime[0].mInteger);
                    runtime.pop();
                    reference = context.resolveReference(name);
                }

                if (mEntry.mUnknown || mEntry.mSkipArguments || mSkipVariant)
                {
                    if (!mEntry.mNoticeGiven)
                    {
                        mEntry.mNoticeGiven = true;
                        context.world().report(mEntry.mUnknown
                                ? std::format("unknown function '{}' is ignored", mEntry.mName)
                                : std::format(
                                    "function '{}' is declared without a shape and is ignored", mEntry.mName));
                    }
                    pushZero(runtime);
                    return;
                }

                if (optionalCount > mEntry.mTotal - mEntry.mRequired)
                {
                    throw ScriptError(std::format("function '{}' was given too many arguments", mEntry.mName));
                }
                const std::size_t count = mEntry.mRequired + optionalCount;

                if (!mEntry.mHandler)
                {
                    // Declared from a table and not implemented: take the arguments off the stack, say so once.
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        runtime.pop();
                    }
                    if (!mEntry.mNoticeGiven)
                    {
                        mEntry.mNoticeGiven = true;
                        context.world().report(std::format(
                            "function '{}' is declared but has no implementation and is ignored", mEntry.mName));
                    }
                    pushZero(runtime);
                    return;
                }
                std::array<Value, Limits::kMaxFunctionArguments> arguments;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const char kind = mEntry.mArgKinds[i];
                    const Interpreter::Data data = runtime[0];
                    switch (kind)
                    {
                        case 'f':
                            arguments[i] = Value::makeFloat(data.mFloat);
                            break;
                        case 'l':
                        case 's':
                            arguments[i] = Value::makeInteger(data.mInteger);
                            break;
                        case 'r':
                            arguments[i]
                                = Value::makeForm(context.resolveReference(runtime.getStringLiteral(data.mInteger)));
                            break;
                        default:
                            arguments[i] = Value::makeString(runtime.getStringLiteral(data.mInteger));
                            break;
                    }
                    runtime.pop();
                }

                if (mExplicit && reference == 0)
                {
                    // "Thing.Function" where Thing is none (a command that found nothing, a reference variable never
                    // set, a form that does not resolve): a handler reads a zero reference as "the script's own
                    // object", so the call is not made.
                    if (mEntry.mNullReferenceSkips++ == 0)
                    {
                        context.world().report(std::format(
                            "function '{}' was not called: the object it is called on is none", mEntry.mSpelling));
                    }
                    pushZero(runtime);
                    return;
                }

                CallArgs call;
                call.self = context.self();
                call.reference = reference;
                call.arguments = std::span<const Value>(arguments.data(), count);
                Value result;
                try
                {
                    result = mEntry.mHandler(call);
                }
                catch (const ScriptError&)
                {
                    throw;
                }
                catch (const std::exception& e)
                {
                    throw ScriptError(std::format("function '{}' failed: {}", mEntry.mName, e.what()));
                }
                catch (...)
                {
                    throw ScriptError(std::format("function '{}' failed", mEntry.mName));
                }

                if (mEntry.mReturns == ValueKind::Float)
                {
                    runtime.push(result.kind == ValueKind::Float ? result.number : static_cast<float>(result.integer));
                }
                else
                {
                    runtime.push(result.kind == ValueKind::Float
                            ? static_cast<Interpreter::Type_Integer>(result.number)
                            : static_cast<Interpreter::Type_Integer>(result.integer));
                }
            }

        private:
            void pushZero(Interpreter::Runtime& runtime) const
            {
                if (mEntry.mReturns == ValueKind::Float)
                {
                    runtime.push(0.0f);
                }
                else
                {
                    runtime.push(Interpreter::Type_Integer{ 0 });
                }
            }

            FunctionEntry& mEntry;
            bool mExplicit;
            bool mSkipVariant; // compiled with the arguments skipped: nothing of them is on the stack
        };

        // ---- save blob helpers -------------------------------------------------------------------------------

        void putU16(std::vector<std::uint8_t>& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void putU32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                out.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }

        // Reads from a byte range with every access checked; a failed read sets mOk to false and returns 0.
        class ByteReader
        {
        public:
            explicit ByteReader(const std::vector<std::uint8_t>& bytes)
                : mBytes(bytes)
            {
            }

            std::size_t remaining() const { return mBytes.size() - mPosition; }
            bool ok() const { return mOk; }

            std::uint16_t u16()
            {
                if (remaining() < 2)
                {
                    mOk = false;
                    return 0;
                }
                const std::uint16_t value
                    = static_cast<std::uint16_t>(mBytes[mPosition] | (mBytes[mPosition + 1] << 8));
                mPosition += 2;
                return value;
            }

            std::uint32_t u32()
            {
                if (remaining() < 4)
                {
                    mOk = false;
                    return 0;
                }
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<std::uint32_t>(mBytes[mPosition + static_cast<std::size_t>(i)]) << (8 * i);
                }
                mPosition += 4;
                return value;
            }

            std::string text(std::size_t length)
            {
                if (remaining() < length)
                {
                    mOk = false;
                    return {};
                }
                std::string value(reinterpret_cast<const char*>(mBytes.data() + mPosition), length);
                mPosition += length;
                return value;
            }

        private:
            const std::vector<std::uint8_t>& mBytes;
            std::size_t mPosition = 0;
            bool mOk = true;
        };

        /// The variables a save stores by name: all but the compiler's hidden locals.
        std::size_t visibleCount(const CompiledScript& script, char type)
        {
            const std::vector<std::string>& names = script.mLocals.get(type);
            if (type != 'l')
            {
                return names.size();
            }
            return static_cast<std::size_t>(std::count_if(
                names.begin(), names.end(), [](const std::string& name) { return !isHiddenReferenceName(name); }));
        }

        /// Position of each stored variable of a script by name (hidden locals are not in it).
        struct LocalIndices
        {
            explicit LocalIndices(const CompiledScript& script)
            {
                fill(mShorts, script.mLocals.get('s'));
                fill(mLongs, script.mLocals.get('l'));
                fill(mFloats, script.mLocals.get('f'));
            }

            std::unordered_map<std::string, std::size_t> mShorts;
            std::unordered_map<std::string, std::size_t> mLongs;
            std::unordered_map<std::string, std::size_t> mFloats;

        private:
            static void fill(std::unordered_map<std::string, std::size_t>& out, const std::vector<std::string>& names)
            {
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (!isHiddenReferenceName(names[i]))
                    {
                        out.emplace(names[i], i);
                    }
                }
            }
        };

        constexpr std::size_t kMaxScriptNameBytes = 256;
        // id, self, name length, three counts
        constexpr std::size_t kMinInstanceRecordBytes = 4 + 4 + 2 + 2 + 2 + 2;

        ErrorReport saveError(std::string message)
        {
            ErrorReport report;
            report.script = "save";
            report.message = std::move(message);
            return report;
        }

        // ---- the runtime -------------------------------------------------------------------------------------

        class RuntimeImpl final : public ObScriptContract
        {
        public:
            RuntimeImpl(ObScriptWorld& world, UnknownFunctionPolicy policy)
                : mWorld(world)
                , mPolicy(policy)
            {
                Interpreter::installOpcodes(mInterpreter);
            }

            bool registerFunction(
                const FunctionDescriptor& descriptor, FunctionHandler handler, ErrorReport& error) noexcept override
            {
                try
                {
                    error = ErrorReport();
                    error.script = descriptor.name;
                    if (!isValidFunctionName(descriptor.name))
                    {
                        error.message = "not a valid function name";
                        return false;
                    }
                    const std::string name = lowerText(descriptor.name);
                    const auto existing = mByName.find(name);
                    if (existing != mByName.end() && !existing->second->mDeclared)
                    {
                        error.message = "a function with this name is already registered";
                        return false;
                    }
                    if (descriptor.returns != ValueKind::Integer && descriptor.returns != ValueKind::Float)
                    {
                        error.message = "a function returns an Integer or a Float";
                        return false;
                    }
                    if (!handler)
                    {
                        error.message = "a function needs a handler";
                        return false;
                    }
                    if (mRegisteredCount >= Limits::kMaxFunctions || !hasCodes())
                    {
                        error.message = "the function table is full";
                        return false;
                    }
                    std::size_t required = 0;
                    std::size_t total = 0;
                    std::string problem;
                    const std::string translated = translateSignature(descriptor.signature, required, total, problem);
                    if (translated.empty())
                    {
                        error.message = problem;
                        return false;
                    }

                    if (existing != mByName.end())
                    {
                        // A declaration gets its handler, if the host's shape is the declared one (scripts compiled
                        // against the declaration depend on it).
                        FunctionEntry& declared = *existing->second;
                        const std::string normalised = descriptor.signature.find('/') == std::string::npos
                            ? descriptor.signature + "/"
                            : descriptor.signature;
                        if (declared.mSkipArguments || declared.mSignature != normalised
                            || declared.mReturns != descriptor.returns)
                        {
                            error.message = "the shape differs from the function's declaration";
                            return false;
                        }
                        declared.mAllowsExplicit = descriptor.allowsExplicitReference;
                        declared.mHandler = std::move(handler);
                        declared.mDeclared = false;
                        return true;
                    }

                    FunctionEntry& entry = addEntry(name, descriptor.name);
                    entry.mSignature = descriptor.signature.find('/') == std::string::npos ? descriptor.signature + "/"
                                                                                           : descriptor.signature;
                    entry.mCompilerSignature = translated;
                    entry.mRequired = required;
                    entry.mTotal = total;
                    entry.mReturns = descriptor.returns;
                    entry.mAllowsExplicit = descriptor.allowsExplicitReference;
                    entry.mHandler = std::move(handler);
                    for (const char c : descriptor.signature)
                    {
                        if (c != '/')
                        {
                            entry.mArgKinds += c;
                        }
                    }
                    ++mRegisteredCount;
                    return true;
                }
                catch (const std::exception& e)
                {
                    error.message = std::format("the function could not be registered ({})", e.what());
                    return false;
                }
                catch (...)
                {
                    error.message = "the function could not be registered";
                    return false;
                }
            }

            bool declareFunction(const FunctionDescriptor& descriptor, ErrorReport& error) noexcept override
            {
                try
                {
                    error = ErrorReport();
                    error.script = descriptor.name;
                    if (!isValidFunctionName(descriptor.name))
                    {
                        error.message = "not a valid function name";
                        return false;
                    }
                    const std::string name = lowerText(descriptor.name);
                    if (mByName.contains(name))
                    {
                        error.message = "a function with this name is already registered or declared";
                        return false;
                    }
                    if (descriptor.returns != ValueKind::Integer && descriptor.returns != ValueKind::Float)
                    {
                        error.message = "a function returns an Integer or a Float";
                        return false;
                    }
                    if (mRegisteredCount >= Limits::kMaxFunctions || !hasCodes(4))
                    {
                        error.message = "the function table is full";
                        return false;
                    }
                    const bool skip = descriptor.signature == "*";
                    std::size_t required = 0;
                    std::size_t total = 0;
                    std::string translated = "/zzzzzzzzzzzz"; // skip up to twelve arguments
                    if (!skip)
                    {
                        std::string problem;
                        translated = translateSignature(descriptor.signature, required, total, problem);
                        if (translated.empty())
                        {
                            error.message = problem;
                            return false;
                        }
                    }

                    FunctionEntry& entry = addEntry(name, descriptor.name);
                    entry.mSkipCode = mNextCode;
                    mNextCode += 2;
                    mInterpreter.installSegment3<OpFunction>(entry.mSkipCode, entry, false, true);
                    mInterpreter.installSegment3<OpFunction>(entry.mSkipCode + 1, entry, true, true);
                    ++mDeclaredCount;
                    entry.mSignature = skip                                   ? "*"
                        : descriptor.signature.find('/') == std::string::npos ? descriptor.signature + "/"
                                                                              : descriptor.signature;
                    entry.mCompilerSignature = translated;
                    entry.mRequired = required;
                    entry.mTotal = total;
                    entry.mReturns = descriptor.returns;
                    entry.mAllowsExplicit = descriptor.allowsExplicitReference;
                    entry.mDeclared = true;
                    entry.mSkipArguments = skip;
                    for (const char c : descriptor.signature)
                    {
                        if (c != '/' && c != '*')
                        {
                            entry.mArgKinds += c;
                        }
                    }
                    ++mRegisteredCount;
                    return true;
                }
                catch (const std::exception& e)
                {
                    error.message = std::format("the function could not be declared ({})", e.what());
                    return false;
                }
                catch (...)
                {
                    error.message = "the function could not be declared";
                    return false;
                }
            }

            CompileResult compile(const ScriptSource& source) noexcept override
            {
                CompileResult result;
                try
                {
                    CompileServices services;
                    services.mWorld = &mWorld;
                    services.mPolicy = mPolicy;
                    services.mHasDeclared = mDeclaredCount > 0;
                    services.mFind = [this](const std::string& name) -> const FunctionEntry* {
                        const auto it = mByName.find(name);
                        return it == mByName.end() ? nullptr : it->second;
                    };
                    // Unknown names a script introduces are only a proposal until the script compiles: they take
                    // their codes now (the bytecode needs them) but get their opcodes, and keep their place in the
                    // table, only when the script is stored. A script that fails leaves nothing behind.
                    UnknownTransaction transaction(*this);
                    services.mAddUnknown = [this, &transaction](const std::string& name, const std::string& spelling,
                                               bool beforeDot) -> const FunctionEntry* {
                        if (beforeDot)
                        {
                            if (mDotUnknownCount >= Limits::kMaxUnknownBeforeDot
                                || mDotUnknownCount - transaction.mDotBefore >= Limits::kMaxUnknownBeforeDotPerScript
                                || !hasCodes())
                            {
                                return nullptr;
                            }
                            FunctionEntry& entry = addEntry(name, spelling, false);
                            entry.mUnknown = true;
                            entry.mSignature = "/";
                            entry.mCompilerSignature = "/zzzzzzzzzzzz";
                            ++mDotUnknownCount;
                            return &entry;
                        }
                        if (mUnknownCount >= Limits::kMaxUnknownFunctions
                            || mUnknownCount - transaction.mUnknownBefore >= Limits::kMaxUnknownFunctionsPerScript
                            || !hasCodes())
                        {
                            return nullptr;
                        }
                        FunctionEntry& entry = addEntry(name, spelling, false);
                        entry.mUnknown = true;
                        entry.mSignature = "/";
                        entry.mCompilerSignature = "/zzzzzzzzzzzz"; // skip up to twelve arguments
                        ++mUnknownCount;
                        return &entry;
                    };
                    services.mForEachFunction = [this](const std::function<void(const FunctionEntry&)>& visit) {
                        for (const FunctionEntry& entry : mFunctions)
                        {
                            visit(entry);
                        }
                    };

                    CompileOutput output = compileScript(source, services);
                    result.reports = std::move(output.mReports);
                    if (output.mOk)
                    {
                        if (mScripts.size() >= Limits::kMaxScripts)
                        {
                            ErrorReport report;
                            report.script = source.name;
                            report.message = "too many compiled scripts";
                            result.reports.push_back(std::move(report));
                            return result;
                        }
                        mScripts.push_back(std::make_unique<CompiledScript>(std::move(output.mScript)));
                        transaction.commit();
                        result.script = static_cast<ScriptId>(mScripts.size());
                        mScriptByName[lowerText(source.name)] = result.script;
                        result.ok = true;
                    }
                }
                catch (...)
                {
                    result.ok = false;
                    result.script = 0;
                    ErrorReport report;
                    report.script = source.name;
                    report.message = "the script could not be compiled";
                    result.reports.push_back(std::move(report));
                }
                return result;
            }

            InstanceId createInstance(ScriptId script, FormId self) noexcept override
            {
                try
                {
                    if (script == 0 || script > mScripts.size() || mInstances.size() >= Limits::kMaxInstances
                        || mNextInstance == 0)
                    {
                        return 0;
                    }
                    const CompiledScript& compiled = *mScripts[script - 1];
                    Instance instance;
                    instance.mScript = script;
                    instance.mSelf = self;
                    instance.mShorts.assign(compiled.mLocals.get('s').size(), 0);
                    instance.mLongs.assign(compiled.mLocals.get('l').size(), 0);
                    instance.mFloats.assign(compiled.mLocals.get('f').size(), 0.0f);
                    const InstanceId id = mNextInstance++;
                    mInstances.emplace(id, std::move(instance));
                    return id;
                }
                catch (...)
                {
                    return 0;
                }
            }

            RunResult run(InstanceId instance, std::string_view event, const Budget& budget) noexcept override
            {
                RunResult result;
                const CompiledScript* script = nullptr;
                try
                {
                    const auto found = mInstances.find(instance);
                    if (found == mInstances.end())
                    {
                        result.error.severity = Severity::Error;
                        result.error.message = std::format("there is no script instance {}", instance);
                        return result;
                    }
                    Instance& state = found->second;
                    script = mScripts[state.mScript - 1].get();

                    const CompiledBlock* block = nullptr;
                    for (const CompiledBlock& candidate : script->mBlocks)
                    {
                        if (Misc::StringUtils::ciEqual(candidate.mEvent, event))
                        {
                            block = &candidate;
                            break;
                        }
                    }
                    if (block == nullptr)
                    {
                        result.ok = true;
                        return result;
                    }
                    result.blockFound = true;

                    Interpreter::RunLimits limits;
                    limits.mMaxInstructions
                        = budget.maxInstructions != 0 ? budget.maxInstructions : Limits::kDefaultMaxInstructions;
                    limits.mMaxDepth = budget.maxCallDepth != 0 ? budget.maxCallDepth : Limits::kDefaultMaxCallDepth;

                    InstanceContext context(mWorld, *script, state);
                    const bool outermost = mRunDepth == 0;
                    const std::uint64_t before = outermost ? 0 : mInterpreter.getInstructionsExecuted();
                    ++mRunDepth;
                    try
                    {
                        mInterpreter.run(block->mProgram, context, limits);
                        result.ok = true;
                    }
                    catch (...)
                    {
                        --mRunDepth;
                        result.instructionsExecuted = mInterpreter.getInstructionsExecuted() - before;
                        throw;
                    }
                    --mRunDepth;
                    result.instructionsExecuted = mInterpreter.getInstructionsExecuted() - before;
                }
                catch (const std::exception& e)
                {
                    result.ok = false;
                    result.error = ErrorReport();
                    result.error.script = script != nullptr ? script->mName : std::string();
                    result.error.message = std::string(e.what());
                }
                catch (...)
                {
                    result.ok = false;
                    result.error = ErrorReport();
                    result.error.script = script != nullptr ? script->mName : std::string();
                    result.error.message = "the script stopped for an unknown reason";
                }
                return result;
            }

            Value getLocal(InstanceId instance, std::string_view name) const noexcept override
            {
                try
                {
                    const auto found = mInstances.find(instance);
                    if (found == mInstances.end())
                    {
                        return {};
                    }
                    const Instance& state = found->second;
                    const CompiledScript& script = *mScripts[state.mScript - 1];
                    const std::string lowered = lowerText(name);
                    int index = script.mLocals.searchIndex('s', lowered);
                    if (index >= 0)
                    {
                        return Value::makeInteger(state.mShorts[static_cast<std::size_t>(index)]);
                    }
                    index = script.mLocals.searchIndex('l', lowered);
                    if (index >= 0)
                    {
                        const std::int32_t value = state.mLongs[static_cast<std::size_t>(index)];
                        return script.isReference(lowered) ? Value::makeForm(static_cast<FormId>(value))
                                                           : Value::makeInteger(value);
                    }
                    index = script.mLocals.searchIndex('f', lowered);
                    if (index >= 0)
                    {
                        return Value::makeFloat(state.mFloats[static_cast<std::size_t>(index)]);
                    }
                }
                catch (...)
                {
                }
                return {};
            }

            bool setLocal(InstanceId instance, std::string_view name, Value value) noexcept override
            {
                try
                {
                    const auto found = mInstances.find(instance);
                    if (found == mInstances.end())
                    {
                        return false;
                    }
                    Instance& state = found->second;
                    const CompiledScript& script = *mScripts[state.mScript - 1];
                    const std::string lowered = lowerText(name);
                    const std::int32_t integer
                        = value.kind == ValueKind::Float ? static_cast<std::int32_t>(value.number) : value.integer;
                    const float number
                        = value.kind == ValueKind::Float ? value.number : static_cast<float>(value.integer);
                    int index = script.mLocals.searchIndex('s', lowered);
                    if (index >= 0)
                    {
                        state.mShorts[static_cast<std::size_t>(index)] = static_cast<std::int16_t>(integer);
                        return true;
                    }
                    index = script.mLocals.searchIndex('l', lowered);
                    if (index >= 0)
                    {
                        state.mLongs[static_cast<std::size_t>(index)] = integer;
                        return true;
                    }
                    index = script.mLocals.searchIndex('f', lowered);
                    if (index >= 0)
                    {
                        state.mFloats[static_cast<std::size_t>(index)] = number;
                        return true;
                    }
                }
                catch (...)
                {
                }
                return false;
            }

            SaveBlob serialise() const noexcept override
            {
                SaveBlob blob;
                try
                {
                    blob.formatVersion = Limits::kSaveFormatVersion;
                    putU32(blob.bytes, static_cast<std::uint32_t>(mInstances.size()));
                    putU32(blob.bytes, mNextInstance);
                    for (const auto& [id, instance] : mInstances)
                    {
                        const CompiledScript& script = *mScripts[instance.mScript - 1];
                        putU32(blob.bytes, id);
                        putU32(blob.bytes, instance.mSelf);
                        putU16(blob.bytes, static_cast<std::uint16_t>(script.mName.size()));
                        blob.bytes.insert(blob.bytes.end(), script.mName.begin(), script.mName.end());
                        // Version 2: the variables by name, without the compiler's hidden locals (they are written
                        // before every use), so an edited script cannot read a value into another variable.
                        const auto putName = [&blob](const std::string& name) {
                            putU16(blob.bytes, static_cast<std::uint16_t>(name.size()));
                            blob.bytes.insert(blob.bytes.end(), name.begin(), name.end());
                        };
                        const std::vector<std::string>& shortNames = script.mLocals.get('s');
                        const std::vector<std::string>& longNames = script.mLocals.get('l');
                        const std::vector<std::string>& floatNames = script.mLocals.get('f');
                        putU16(blob.bytes, static_cast<std::uint16_t>(visibleCount(script, 's')));
                        putU16(blob.bytes, static_cast<std::uint16_t>(visibleCount(script, 'l')));
                        putU16(blob.bytes, static_cast<std::uint16_t>(visibleCount(script, 'f')));
                        for (std::size_t i = 0; i < shortNames.size(); ++i)
                        {
                            putName(shortNames[i]);
                            putU16(blob.bytes, static_cast<std::uint16_t>(instance.mShorts[i]));
                        }
                        for (std::size_t i = 0; i < longNames.size(); ++i)
                        {
                            if (isHiddenReferenceName(longNames[i]))
                            {
                                continue;
                            }
                            putName(longNames[i]);
                            putU32(blob.bytes, static_cast<std::uint32_t>(instance.mLongs[i]));
                        }
                        for (std::size_t i = 0; i < floatNames.size(); ++i)
                        {
                            putName(floatNames[i]);
                            std::uint32_t bits = 0;
                            std::memcpy(&bits, &instance.mFloats[i], sizeof(bits));
                            putU32(blob.bytes, bits);
                        }
                    }
                }
                catch (...)
                {
                    blob.bytes.clear();
                }
                return blob;
            }

            RestoreResult restore(const SaveBlob& blob) noexcept override
            {
                RestoreResult result;
                try
                {
                    if (mRunDepth > 0)
                    {
                        // A running script holds references into mInstances.
                        result.error = saveError("restore cannot be used from inside a running script function");
                        return result;
                    }
                    if (blob.formatVersion != Limits::kSaveFormatVersion
                        && blob.formatVersion != Limits::kPositionalSaveFormatVersion)
                    {
                        result.error = saveError(
                            std::format("unsupported script save version {} (this runtime reads version {})",
                                blob.formatVersion, Limits::kSaveFormatVersion));
                        return result;
                    }
                    ByteReader reader(blob.bytes);
                    const std::uint32_t count = reader.u32();
                    const std::uint32_t nextId = reader.u32();
                    if (!reader.ok())
                    {
                        result.error = saveError("the script save is truncated");
                        return result;
                    }
                    if (nextId == 0)
                    {
                        result.error = saveError("the script save has no next instance number");
                        return result;
                    }
                    // Check the count against what is left before reserving or looping on it.
                    if (count > Limits::kMaxInstances
                        || static_cast<std::uint64_t>(count) * kMinInstanceRecordBytes > reader.remaining())
                    {
                        result.error
                            = saveError(std::format("the script save claims {} instances, which is impossible", count));
                        return result;
                    }

                    std::map<InstanceId, Instance> parsed;
                    std::map<ScriptId, LocalIndices> indices; // names to positions, once per script
                    for (std::uint32_t i = 0; i < count; ++i)
                    {
                        const InstanceId id = reader.u32();
                        Instance instance;
                        instance.mSelf = reader.u32();
                        const std::uint16_t nameLength = reader.u16();
                        if (nameLength > kMaxScriptNameBytes)
                        {
                            result.error = saveError("a script name in the save is too long");
                            return result;
                        }
                        const std::string name = reader.text(nameLength);
                        const std::uint16_t shorts = reader.u16();
                        const std::uint16_t longs = reader.u16();
                        const std::uint16_t floats = reader.u16();
                        if (!reader.ok())
                        {
                            result.error = saveError("the script save is truncated");
                            return result;
                        }
                        const bool byName = blob.formatVersion >= Limits::kSaveFormatVersion;
                        if (!byName
                            && static_cast<std::size_t>(shorts) * 2 + static_cast<std::size_t>(longs) * 4
                                    + static_cast<std::size_t>(floats) * 4
                                > reader.remaining())
                        {
                            result.error = saveError("the script save is truncated");
                            return result;
                        }
                        if (id == 0 || id >= nextId || parsed.contains(id))
                        {
                            result.error = saveError(std::format("the script save has a bad instance id {}", id));
                            return result;
                        }
                        const auto script = mScriptByName.find(lowerText(name));
                        if (script == mScriptByName.end())
                        {
                            result.error
                                = saveError(std::format("the save refers to script '{}', which is not compiled", name));
                            return result;
                        }
                        const CompiledScript& compiled = *mScripts[script->second - 1];
                        if (byName)
                        {
                            if (visibleCount(compiled, 's') != shorts || visibleCount(compiled, 'l') != longs
                                || visibleCount(compiled, 'f') != floats)
                            {
                                result.error = saveError(
                                    std::format("the variables of script '{}' differ from the ones in the save", name));
                                return result;
                            }
                            instance.mScript = script->second;
                            instance.mShorts.assign(compiled.mLocals.get('s').size(), 0);
                            instance.mLongs.assign(compiled.mLocals.get('l').size(), 0);
                            instance.mFloats.assign(compiled.mLocals.get('f').size(), 0.0f);
                            auto indexed = indices.find(script->second);
                            if (indexed == indices.end())
                            {
                                indexed = indices.emplace(script->second, LocalIndices(compiled)).first;
                            }
                            const LocalIndices& index = indexed->second;
                            std::vector<bool> shortSeen(shorts, false);
                            std::vector<bool> longSeen(instance.mLongs.size(), false);
                            std::vector<bool> floatSeen(instance.mFloats.size(), false);
                            const auto readIndex = [&](const std::unordered_map<std::string, std::size_t>& names,
                                                       std::vector<bool>& seen, std::size_t& position) {
                                const std::uint16_t length = reader.u16();
                                if (length > Limits::kMaxIdentifierLength)
                                {
                                    return false;
                                }
                                const std::string variable = reader.text(length);
                                const auto found = names.find(variable);
                                if (!reader.ok() || found == names.end() || seen[found->second])
                                {
                                    return false;
                                }
                                seen[found->second] = true;
                                position = found->second;
                                return true;
                            };
                            bool good = true;
                            for (std::uint16_t entry = 0; good && entry < shorts; ++entry)
                            {
                                std::size_t at = 0;
                                good = readIndex(index.mShorts, shortSeen, at);
                                const std::uint16_t value = reader.u16();
                                if (good && reader.ok())
                                {
                                    instance.mShorts[at] = static_cast<std::int16_t>(value);
                                }
                            }
                            for (std::uint16_t entry = 0; good && entry < longs; ++entry)
                            {
                                std::size_t at = 0;
                                good = readIndex(index.mLongs, longSeen, at);
                                const std::uint32_t value = reader.u32();
                                if (good && reader.ok())
                                {
                                    instance.mLongs[at] = static_cast<std::int32_t>(value);
                                }
                            }
                            for (std::uint16_t entry = 0; good && entry < floats; ++entry)
                            {
                                std::size_t at = 0;
                                good = readIndex(index.mFloats, floatSeen, at);
                                const std::uint32_t bits = reader.u32();
                                if (good && reader.ok())
                                {
                                    std::memcpy(&instance.mFloats[at], &bits, sizeof(bits));
                                }
                            }
                            if (!good || !reader.ok())
                            {
                                result.error = saveError(
                                    std::format("the variables of script '{}' differ from the ones in the save", name));
                                return result;
                            }
                            parsed.emplace(id, std::move(instance));
                            continue;
                        }
                        if (compiled.mLocals.get('s').size() != shorts || compiled.mLocals.get('l').size() != longs
                            || compiled.mLocals.get('f').size() != floats)
                        {
                            result.error = saveError(
                                std::format("the variables of script '{}' differ from the ones in the save", name));
                            return result;
                        }
                        instance.mScript = script->second;
                        instance.mShorts.resize(shorts);
                        instance.mLongs.resize(longs);
                        instance.mFloats.resize(floats);
                        for (std::int16_t& value : instance.mShorts)
                        {
                            value = static_cast<std::int16_t>(reader.u16());
                        }
                        for (std::int32_t& value : instance.mLongs)
                        {
                            value = static_cast<std::int32_t>(reader.u32());
                        }
                        for (float& value : instance.mFloats)
                        {
                            const std::uint32_t bits = reader.u32();
                            std::memcpy(&value, &bits, sizeof(value));
                        }
                        parsed.emplace(id, std::move(instance));
                    }
                    if (!reader.ok() || reader.remaining() != 0)
                    {
                        result.error = saveError("the script save has the wrong size");
                        return result;
                    }

                    mInstances = std::move(parsed);
                    mNextInstance = nextId;
                    result.ok = true;
                }
                catch (...)
                {
                    result.ok = false;
                    result.error = saveError("the script save could not be read");
                }
                return result;
            }

            bool inspectScript(ScriptId script, std::size_t& blocks, std::size_t& unknownFunctions) const
            {
                if (script == 0 || script > mScripts.size())
                {
                    return false;
                }
                blocks = mScripts[script - 1]->mBlocks.size();
                unknownFunctions = mUnknownCount;
                return true;
            }

        private:
            bool hasCodes(int pairs = 2) const { return mNextCode + pairs - 1 <= kLastFunctionCode; }

            // Add an entry to the table and install its two opcodes (plain and explicit reference).
            FunctionEntry& addEntry(const std::string& name, const std::string& spelling, bool install = true)
            {
                mFunctions.emplace_back();
                FunctionEntry& entry = mFunctions.back();
                entry.mName = name;
                entry.mSpelling = spelling;
                entry.mCode = mNextCode;
                mNextCode += 2;
                mByName.emplace(name, &entry);
                if (install)
                {
                    installOpcodes(entry);
                }
                return entry;
            }

            void installOpcodes(FunctionEntry& entry)
            {
                mInterpreter.installSegment3<OpFunction>(entry.mCode, entry, false);
                mInterpreter.installSegment3<OpFunction>(entry.mCode + 1, entry, true);
            }

            // The unknown functions one compile() call adds. Rolled back unless commit() is called.
            class UnknownTransaction
            {
            public:
                explicit UnknownTransaction(RuntimeImpl& runtime)
                    : mUnknownBefore(runtime.mUnknownCount)
                    , mDotBefore(runtime.mDotUnknownCount)
                    , mRuntime(runtime)
                    , mFunctionsBefore(runtime.mFunctions.size())
                    , mCodeBefore(runtime.mNextCode)
                {
                }
                UnknownTransaction(const UnknownTransaction&) = delete;
                UnknownTransaction& operator=(const UnknownTransaction&) = delete;

                ~UnknownTransaction()
                {
                    if (mCommitted)
                    {
                        return;
                    }
                    try
                    {
                        while (mRuntime.mFunctions.size() > mFunctionsBefore)
                        {
                            mRuntime.mByName.erase(mRuntime.mFunctions.back().mName);
                            mRuntime.mFunctions.pop_back();
                        }
                        mRuntime.mNextCode = mCodeBefore;
                        mRuntime.mUnknownCount = mUnknownBefore;
                        mRuntime.mDotUnknownCount = mDotBefore;
                    }
                    catch (...)
                    {
                    }
                }

                void commit()
                {
                    mCommitted = true;
                    for (std::size_t i = mFunctionsBefore; i < mRuntime.mFunctions.size(); ++i)
                    {
                        mRuntime.installOpcodes(mRuntime.mFunctions[i]);
                    }
                }

                std::size_t mUnknownBefore;
                std::size_t mDotBefore;

            private:
                RuntimeImpl& mRuntime;
                std::size_t mFunctionsBefore;
                int mCodeBefore;
                bool mCommitted = false;
            };

            ObScriptWorld& mWorld;
            UnknownFunctionPolicy mPolicy;
            Interpreter::Interpreter mInterpreter;
            std::deque<FunctionEntry> mFunctions; // stable addresses: the opcodes point into it
            std::unordered_map<std::string, FunctionEntry*> mByName;
            std::vector<std::unique_ptr<CompiledScript>> mScripts; // ScriptId - 1
            std::map<std::string, ScriptId> mScriptByName;
            std::map<InstanceId, Instance> mInstances; // ordered, so that saves are deterministic
            InstanceId mNextInstance = 1;
            int mNextCode = kFirstFunctionCode;
            std::size_t mRegisteredCount = 0;
            std::size_t mUnknownCount = 0;
            std::size_t mDotUnknownCount = 0; ///< Unknown names that stood before a dot (their own bucket).
            std::size_t mDeclaredCount = 0;
            std::uint32_t mRunDepth = 0;
        };
    }

    std::unique_ptr<ObScriptContract> createRuntime(ObScriptWorld& world, UnknownFunctionPolicy policy)
    {
        return std::make_unique<RuntimeImpl>(world, policy);
    }

    bool inspect(const ObScriptContract& runtime, ScriptId script, std::size_t& blocks, std::size_t& unknownFunctions)
    {
        const RuntimeImpl* impl = dynamic_cast<const RuntimeImpl*>(&runtime);
        if (impl == nullptr)
        {
            return false;
        }
        return impl->inspectScript(script, blocks, unknownFunctions);
    }
}
