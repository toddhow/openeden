#include "papyrusruntime.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>

#include "papyruslimits.hpp"
#include "papyrusvm.hpp"
#include "pexfile.hpp"

// The contract surface of the Papyrus runtime (task 0093): loading and linking scripts (stage b), and, from stage c,
// attaching instances, running calls and events, native registration and reading variables. The interpreter is in
// papyrusinterp.cpp, linking and compiling in papyrusprepare.cpp, the heap in papyrusheap.cpp.

namespace ScriptPapyrus
{
    using namespace openeden::script;

    const char* errorKindName(ErrorKind kind) noexcept
    {
        switch (kind)
        {
            case ErrorKind::None:
                return "none";
            case ErrorKind::BadScript:
                return "bad script";
            case ErrorKind::NoSuchInstance:
                return "no such instance";
            case ErrorKind::NoSuchFunction:
                return "no such function";
            case ErrorKind::NoneReceiver:
                return "none receiver";
            case ErrorKind::MissingClass:
                return "missing class";
            case ErrorKind::TypeMismatch:
                return "type mismatch";
            case ErrorKind::ArgumentCount:
                return "argument count";
            case ErrorKind::DivideByZero:
                return "divide by zero";
            case ErrorKind::IndexOutOfRange:
                return "index out of range";
            case ErrorKind::LimitExceeded:
                return "limit exceeded";
            case ErrorKind::BudgetExceeded:
                return "budget exceeded";
            case ErrorKind::DepthExceeded:
                return "call depth";
            case ErrorKind::UnsupportedOpcode:
                return "unsupported opcode";
            case ErrorKind::UnregisteredNative:
                return "unregistered native";
            case ErrorKind::LatentUnsupported:
                return "latent unsupported";
            case ErrorKind::NativeFailed:
                return "native failed";
            case ErrorKind::Internal:
                return "internal";
            case ErrorKind::Count:
                break;
        }
        return "unknown";
    }

    namespace Vm
    {
        namespace
        {
            constexpr std::size_t kMaxQueuedStacks = 1u << 16;
            constexpr std::size_t kMaxStacksPerTick = 1u << 20;
            constexpr std::uint32_t kHardArrayCeiling = 65536;
            constexpr std::uint64_t kFallbackInstructions = 1'000'000;
            constexpr std::uint32_t kFallbackDepth = 64;

            ErrorReport plainReport(std::string_view script, std::string message)
            {
                ErrorReport report;
                report.script = std::string(script);
                report.message = std::move(message);
                return report;
            }

            // The function of \a file called \a lowerName (a state function, or "<property>.get" / ".set").
            const Pex::Function* findDeclared(const Pex::File& file, const std::string& lowerName)
            {
                const Pex::Object& object = file.mObjects[0];
                for (const Pex::State& state : object.mStates)
                {
                    for (const Pex::Function& function : state.mFunctions)
                    {
                        if (Pex::lowered(file.str(function.mName)) == lowerName)
                        {
                            return &function;
                        }
                    }
                }
                for (const Pex::Property& property : object.mProperties)
                {
                    if ((property.mFlags & Pex::Property::kAuto) != 0)
                    {
                        continue;
                    }
                    const std::string base = Pex::lowered(file.str(property.mName));
                    if ((property.mFlags & Pex::Property::kRead) != 0 && base + ".get" == lowerName)
                    {
                        return &property.mGetter;
                    }
                    if ((property.mFlags & Pex::Property::kWrite) != 0 && base + ".set" == lowerName)
                    {
                        return &property.mSetter;
                    }
                }
                return nullptr;
            }
        }

        Runtime::Runtime(PapyrusWorld& world, UnknownFunctionPolicy policy, const RuntimeOptions& options)
            : mWorld(world)
            , mPolicy(policy)
            , mOptions(options)
        {
            mOptions.maxArrayLength = std::min(mOptions.maxArrayLength, kHardArrayCeiling);
            if (mOptions.defaultBudget.maxInstructions == 0)
            {
                mOptions.defaultBudget.maxInstructions = kFallbackInstructions;
            }
            if (mOptions.defaultBudget.maxCallDepth == 0)
            {
                mOptions.defaultBudget.maxCallDepth = kFallbackDepth;
            }
            // Name 0 and string 0 are the empty ones: the empty state, the empty string.
            nameId("");
            internString("");
            mArgScratch.resize(kMaxCallArgs);
        }

        // ---- loading and linking (stage b)
        // ---------------------------------------------------------------------------

        LoadResult Runtime::loadScript(std::span<const std::uint8_t> bytes, std::string_view name) noexcept
        {
            LoadResult result;
            try
            {
                if (mInNative)
                {
                    result.reports.push_back(plainReport(name, "a script cannot be loaded from inside a native"));
                    return result;
                }
                return load(bytes, name);
            }
            catch (...)
            {
                result.ok = false;
                result.script = 0;
                try
                {
                    result.reports.clear();
                    result.reports.push_back(
                        plainReport(name, "out of memory or an internal error while loading the script"));
                }
                catch (...)
                {
                    result.reports.clear();
                }
            }
            return result;
        }

        LinkReport Runtime::link() const noexcept
        {
            try
            {
                return buildLinkReport(nullptr);
            }
            catch (...)
            {
                return {};
            }
        }

        std::vector<std::pair<std::string, std::uint64_t>> Runtime::nativeCallSites() const
        {
            std::map<std::string, std::uint64_t> counts;
            buildLinkReport(&counts);
            std::vector<std::pair<std::string, std::uint64_t>> result(counts.begin(), counts.end());
            std::stable_sort(
                result.begin(), result.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            return result;
        }

        LoadResult Runtime::load(std::span<const std::uint8_t> bytes, std::string_view name)
        {
            LoadResult result;
            Pex::ParseOptions options;
            options.mStrictSizeConvention = mPolicy == UnknownFunctionPolicy::Strict;
            Pex::ParseResult parsed = Pex::parse(bytes, name, options);
            result.reports = std::move(parsed.mReports);
            if (!parsed.mOk)
            {
                return result;
            }
            const Pex::File& file = parsed.mFile;
            const Pex::Object& object = file.mObjects[0];
            const std::string className(file.str(object.mName));
            const std::string lowerClass = Pex::lowered(className);
            if (mByName.contains(lowerClass))
            {
                result.reports.push_back(plainReport(name, "a script named '" + className + "' is already loaded"));
                return result;
            }
            if (mScripts.size() >= Limits::kMaxScripts)
            {
                result.reports.push_back(plainReport(
                    name, "the runtime holds the cap of " + std::to_string(Limits::kMaxScripts) + " scripts"));
                return result;
            }

            Script script;
            script.mFileName = std::string(name);
            script.mClass = className;
            script.mParent = std::string(file.str(object.mParent));
            for (const Pex::State& state : object.mStates)
            {
                for (const Pex::Function& function : state.mFunctions)
                {
                    script.mFunctions.emplace(Pex::lowered(file.str(function.mName)), function.isNative());
                }
            }
            script.mFile = std::move(parsed.mFile);

            // One log line for each deviant file, naming the convention (the reader's warning already does).
            for (const ErrorReport& report : result.reports)
            {
                if (report.severity == Severity::Warning)
                {
                    mWorld.log(Severity::Warning, report.format());
                }
            }

            result.info.className = script.mClass;
            result.info.parent = script.mParent;
            result.info.major = script.mFile.mMajor;
            result.info.minor = script.mFile.mMinor;
            result.info.gameId = script.mFile.mGame == Pex::Game::Fallout4 ? 2 : 1;
            result.info.bigEndian = script.mFile.mBigEndian;
            result.info.sizeConvention = script.mFile.mObjects[0].mSizeConvention;
            result.ok = true;
            result.script = static_cast<ScriptId>(mScripts.size() + 1);
            mByName.emplace(lowerClass, mScripts.size());
            mScriptByClassId.emplace(nameId(className), static_cast<std::uint32_t>(mScripts.size()));
            mScripts.push_back(std::move(script));
            return result;
        }

        // link(): the targets that can be known statically. A receiver's class comes from the declared type of a local,
        // a parameter or a variable (or "self"); a function is looked for in that class and then up its parent chain.
        LinkReport Runtime::buildLinkReport(std::map<std::string, std::uint64_t>* natives) const
        {
            LinkReport report;
            auto note = [&report](const Script& script, const std::string& where, const std::string& text) {
                if (report.unresolved.size() < Limits::kMaxLinkReports)
                {
                    ErrorReport entry;
                    entry.severity = Severity::Warning;
                    entry.script = script.mFileName;
                    entry.message = where + ": " + text;
                    report.unresolved.push_back(std::move(entry));
                }
                else
                {
                    ++report.unresolvedOmitted;
                }
            };

            enum class Found
            {
                Script,
                MissingClass,
                MissingFunction,
                Dynamic,
            };

            // A receiver of one of these types has no class to look in.
            auto isPrimitiveType = [](const std::string& lowerType) {
                return lowerType.empty() || lowerType == "int" || lowerType == "float" || lowerType == "bool"
                    || lowerType == "string" || lowerType == "var" || lowerType == "none" || lowerType.ends_with("[]");
            };

            // Look for \a function from \a start up the parent chain.
            auto lookup
                = [this](const Script* start, const std::string& lowerFunction, bool& native, const Script*& where) {
                      const Script* current = start;
                      for (std::size_t depth = 0; current != nullptr && depth < kMaxParentDepth; ++depth)
                      {
                          auto found = current->mFunctions.find(lowerFunction);
                          if (found != current->mFunctions.end())
                          {
                              native = found->second;
                              where = current;
                              return Found::Script;
                          }
                          if (current->mParent.empty())
                          {
                              return Found::MissingFunction;
                          }
                          current = find(Pex::lowered(current->mParent));
                          if (current == nullptr)
                          {
                              return Found::MissingClass;
                          }
                      }
                      return Found::MissingFunction;
                  };

            for (const Script& script : mScripts)
            {
                const Pex::File& file = script.mFile;
                const Pex::Object& object = file.mObjects[0];
                std::unordered_map<std::string, std::string> objectTypes;
                for (const Pex::Variable& variable : object.mVariables)
                {
                    objectTypes.emplace(Pex::lowered(file.str(variable.mName)), std::string(file.str(variable.mType)));
                }

                auto scan = [&](const Pex::Function& function, const std::string& functionName) {
                    std::unordered_map<std::string, std::string> types;
                    for (const Pex::Parameter& p : function.mParameters)
                    {
                        types.emplace(Pex::lowered(file.str(p.mName)), std::string(file.str(p.mType)));
                    }
                    for (const Pex::Parameter& p : function.mLocals)
                    {
                        types.emplace(Pex::lowered(file.str(p.mName)), std::string(file.str(p.mType)));
                    }
                    for (std::size_t i = 0; i < function.mInstructions.size(); ++i)
                    {
                        const Pex::Instruction& instruction = function.mInstructions[i];
                        if (instruction.mOpcode != Pex::Op::CallMethod && instruction.mOpcode != Pex::Op::CallStatic
                            && instruction.mOpcode != Pex::Op::CallParent)
                        {
                            continue;
                        }
                        ++report.callSites;
                        const std::string where = script.mClass + "." + functionName + "@" + std::to_string(i);
                        const Pex::Value* operands = function.mOperands.data() + instruction.mFirstOperand;
                        const char* verb = "";
                        std::string targetClass; // shown in the note
                        std::string lowerFunction;
                        std::string shownFunction;
                        const Script* start = nullptr;
                        Found outcome = Found::Dynamic;
                        bool native = false;
                        const Script* declaring = nullptr;

                        if (instruction.mOpcode == Pex::Op::CallStatic)
                        {
                            verb = "callstatic";
                            targetClass = std::string(file.str(operands[0].mString));
                            shownFunction = std::string(file.str(operands[1].mString));
                            lowerFunction = Pex::lowered(shownFunction);
                            start = find(Pex::lowered(targetClass));
                            outcome = start == nullptr ? Found::MissingClass : Found::Dynamic;
                        }
                        else if (instruction.mOpcode == Pex::Op::CallParent)
                        {
                            verb = "callparent";
                            shownFunction = std::string(file.str(operands[0].mString));
                            lowerFunction = Pex::lowered(shownFunction);
                            targetClass = script.mParent;
                            start = script.mParent.empty() ? nullptr : find(Pex::lowered(script.mParent));
                            outcome = start == nullptr ? Found::MissingClass : Found::Dynamic;
                        }
                        else
                        {
                            verb = "callmethod";
                            shownFunction = std::string(file.str(operands[0].mString));
                            lowerFunction = Pex::lowered(shownFunction);
                            const std::string receiver = Pex::lowered(file.str(operands[1].mString));
                            std::string type;
                            if (receiver == "self")
                            {
                                type = Pex::lowered(script.mClass);
                            }
                            else if (auto local = types.find(receiver); local != types.end())
                            {
                                type = local->second;
                            }
                            else if (auto variable = objectTypes.find(receiver); variable != objectTypes.end())
                            {
                                type = variable->second;
                            }
                            if (isPrimitiveType(Pex::lowered(type)))
                            {
                                outcome = Found::Dynamic;
                            }
                            else
                            {
                                targetClass = type;
                                start = find(Pex::lowered(type));
                                outcome = start == nullptr ? Found::MissingClass : Found::Dynamic;
                            }
                        }

                        if (start != nullptr)
                        {
                            outcome = lookup(start, lowerFunction, native, declaring);
                        }
                        switch (outcome)
                        {
                            case Found::Script:
                                if (native)
                                {
                                    ++report.resolvedNative;
                                    if (natives != nullptr && declaring != nullptr)
                                    {
                                        ++(*natives)[Pex::lowered(declaring->mClass) + "." + lowerFunction];
                                    }
                                }
                                else
                                {
                                    ++report.resolvedScript;
                                }
                                break;
                            case Found::MissingClass:
                                ++report.missingClass;
                                note(script, where,
                                    std::string(verb) + " " + shownFunction + ": class '" + targetClass
                                        + "' is not loaded");
                                break;
                            case Found::MissingFunction:
                                ++report.missingFunction;
                                note(script, where,
                                    std::string(verb) + " " + shownFunction + ": class '" + targetClass
                                        + "' has no such function");
                                break;
                            case Found::Dynamic:
                                ++report.dynamicReceiver;
                                note(script, where,
                                    std::string(verb) + " " + shownFunction
                                        + ": the receiver's class is not known statically");
                                break;
                        }
                    }
                };

                for (const Pex::Property& property : object.mProperties)
                {
                    if ((property.mFlags & Pex::Property::kAuto) != 0)
                    {
                        continue;
                    }
                    if ((property.mFlags & Pex::Property::kRead) != 0)
                    {
                        scan(property.mGetter, std::string(file.str(property.mName)) + ".get");
                    }
                    if ((property.mFlags & Pex::Property::kWrite) != 0)
                    {
                        scan(property.mSetter, std::string(file.str(property.mName)) + ".set");
                    }
                }
                for (const Pex::State& state : object.mStates)
                {
                    for (const Pex::Function& function : state.mFunctions)
                    {
                        scan(function, std::string(file.str(function.mName)));
                    }
                }
            }
            return report;
        }

        // ---- natives
        // -------------------------------------------------------------------------------------------------

        bool Runtime::registerNative(
            const NativeDescriptor& descriptor, NativeHandler handler, ErrorReport& error) noexcept
        {
            try
            {
                return registerNativeImpl(descriptor, std::move(handler), error);
            }
            catch (...)
            {
                try
                {
                    error = plainReport("", "out of memory or an internal error while registering a native");
                }
                catch (...)
                {
                }
                return false;
            }
        }

        bool Runtime::registerNativeImpl(const NativeDescriptor& descriptor, NativeHandler handler, ErrorReport& error)
        {
            const std::string label = descriptor.className + "." + descriptor.functionName;
            if (!handler)
            {
                error = plainReport(label, "the native has no handler");
                return false;
            }
            if (mInNative)
            {
                error = plainReport(label, "a native cannot be registered from inside a native");
                return false;
            }
            const std::uint32_t classId = nameId(descriptor.className);
            const std::uint32_t funcId = nameId(descriptor.functionName);
            const std::uint32_t scriptIndex = scriptByClassId(classId);
            if (scriptIndex != kNoScript)
            {
                const Script& script = mScripts[scriptIndex];
                const Pex::Function* declared = findDeclared(script.mFile, Pex::lowered(descriptor.functionName));
                if (declared == nullptr)
                {
                    error = plainReport(script.mFileName,
                        "the class '" + script.mClass + "' has no function '" + descriptor.functionName + "'");
                    return false;
                }
                if (!declared->isNative())
                {
                    error = plainReport(script.mFileName, "'" + label + "' is a script function, not a native");
                    return false;
                }
                if (declared->isGlobal() != descriptor.global)
                {
                    error = plainReport(script.mFileName,
                        "'" + label + "' is declared " + (declared->isGlobal() ? "global" : "not global")
                            + " and registered " + (descriptor.global ? "global" : "not global"));
                    return false;
                }
            }
            const std::uint64_t key = (static_cast<std::uint64_t>(classId) << 32) | funcId;
            if (mNativeByKey.contains(key))
            {
                error = plainReport("", "'" + label + "' is already registered");
                return false;
            }
            NativeEntry entry;
            entry.desc = descriptor;
            entry.fn = std::move(handler);
            entry.classId = classId;
            entry.funcId = funcId;
            mNativeByKey.emplace(key, static_cast<std::uint32_t>(mNatives.size()));
            mNatives.push_back(std::move(entry));
            return true;
        }

        // ---- errors
        // --------------------------------------------------------------------------------------------------

        ErrorReport Runtime::takeError()
        {
            ErrorReport report;
            report.script = mErrFile;
            report.message
                = (mErrLoc.empty() ? std::string() : mErrLoc + ": ") + "[" + errorKindName(mErrKind) + "] " + mErrMsg;
            ++mStats.errors[static_cast<std::size_t>(mErrKind)];
            if (mLoggedOnce.insert("error:" + report.message).second)
            {
                mWorld.log(Severity::Error, report.format());
            }
            mErrKind = ErrorKind::None;
            mErrMsg.clear();
            mErrLoc.clear();
            mErrFile.clear();
            return report;
        }

        ErrorReport Runtime::apiError(ErrorKind kind, const std::string& message)
        {
            ErrorReport report;
            report.message = std::string("[") + errorKindName(kind) + "] " + message;
            ++mStats.errors[static_cast<std::size_t>(kind)];
            if (mLoggedOnce.insert("error:" + report.message).second)
            {
                mWorld.log(Severity::Error, report.format());
            }
            return report;
        }

        // ---- stacks
        // --------------------------------------------------------------------------------------------------

        Stack* Runtime::acquireStack()
        {
            Stack* stack;
            if (!mFreeStacks.empty())
            {
                stack = mFreeStacks.back();
                mFreeStacks.pop_back();
            }
            else
            {
                mStackPool.push_back(std::make_unique<Stack>());
                stack = mStackPool.back().get();
            }
            stack->instance = 0;
            stack->eventName = 0;
            stack->argCount = 0;
            stack->started = false;
            stack->frames.clear();
            stack->values.clear();
            stack->result = Val{};
            return stack;
        }

        void Runtime::releaseStack(Stack* stack)
        {
            stack->frames.clear();
            stack->values.clear();
            stack->result = Val{};
            mFreeStacks.push_back(stack);
        }

        std::uint64_t Runtime::effectiveInstructions(const Budget& budget) const
        {
            return budget.maxInstructions != 0 ? budget.maxInstructions : mOptions.defaultBudget.maxInstructions;
        }

        std::uint32_t Runtime::effectiveDepth(const Budget& budget) const
        {
            return budget.maxCallDepth != 0 ? budget.maxCallDepth : mOptions.defaultBudget.maxCallDepth;
        }

        // Converts the arguments into the stack's first values. The stack is a root while this runs, because a string
        // argument can start a collection.
        bool Runtime::fillArguments(Stack& stack, std::span<const PapyrusValue> arguments)
        {
            if (arguments.size() > kMaxCallArgs)
            {
                return fail(ErrorKind::ArgumentCount,
                    std::to_string(arguments.size()) + " arguments are over the cap of "
                        + std::to_string(kMaxCallArgs));
            }
            mRunning.push_back(&stack);
            bool ok = true;
            stack.values.clear();
            for (const PapyrusValue& argument : arguments)
            {
                Val converted;
                if (!fromPapyrus(argument, converted))
                {
                    ok = false;
                    break;
                }
                stack.values.push_back(converted);
            }
            mRunning.pop_back();
            stack.argCount = static_cast<std::uint32_t>(arguments.size());
            return ok;
        }

        int Runtime::runStack(
            Stack& stack, bool isEvent, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed)
        {
            struct RunningGuard
            {
                std::vector<Stack*>& mList;
                ~RunningGuard() { mList.pop_back(); }
            };
            mRunning.push_back(&stack);
            RunningGuard guard{ mRunning };
            executed = 0;

            Instance& instance = mInstances[stack.instance - 1];
            const Resolved resolved = findFunction(instance.script, instance.stateId, stack.eventName);
            if (!resolved.found)
            {
                if (isEvent)
                {
                    ++mStats.ignoredEvents;
                    return 1;
                }
                fail(ErrorKind::NoSuchFunction,
                    "the class '" + mScripts[instance.script].mClass + "' has no function '"
                        + std::string(nameText(stack.eventName)) + "'");
                return 2;
            }
            const CFunc& function = mScripts[resolved.script].mFuncs[resolved.func];
            if (function.native)
            {
                fail(ErrorKind::TypeMismatch,
                    "'" + std::string(nameText(function.name)) + "' is native; it cannot be started directly");
                return 2;
            }
            if (stack.argCount != function.paramCount)
            {
                fail(ErrorKind::ArgumentCount,
                    mScripts[resolved.script].mClass + "." + std::string(nameText(function.name)) + " takes "
                        + std::to_string(function.paramCount) + " arguments but " + std::to_string(stack.argCount)
                        + " were given");
                return 2;
            }
            for (std::uint32_t i = 0; i < stack.argCount; ++i)
            {
                mArgScratch[i] = stack.values[i];
            }
            stack.values.clear();
            const Val self = function.global ? Val{} : Val::makeInstance(stack.instance);
            if (!pushFrame(
                    stack, resolved.script, resolved.func, self, mArgScratch.data(), stack.argCount, COp{}, maxDepth))
            {
                return 2;
            }
            --mStats.scriptCalls; // the first frame is the start, not a call
            stack.started = true;
            return execute(stack, maxInstructions, maxDepth, executed) ? 0 : 2;
        }

        bool Runtime::queueEvent(std::uint32_t instanceId, std::uint32_t eventName,
            std::span<const PapyrusValue> arguments, ErrorReport& error)
        {
            if (instanceId == 0 || instanceId > mInstances.size())
            {
                error
                    = apiError(ErrorKind::NoSuchInstance, "instance " + std::to_string(instanceId) + " does not exist");
                return false;
            }
            if (mReady.size() - mReadyHead >= kMaxQueuedStacks)
            {
                error = apiError(
                    ErrorKind::LimitExceeded, "more than " + std::to_string(kMaxQueuedStacks) + " stacks are waiting");
                return false;
            }
            Stack* stack = acquireStack();
            stack->instance = instanceId;
            stack->eventName = eventName;
            if (!fillArguments(*stack, arguments))
            {
                error = takeError();
                releaseStack(stack);
                return false;
            }
            mReady.push_back(stack);
            return true;
        }

        // ---- instances, calls, ticks
        // ---------------------------------------------------------------------------------

        InstanceId Runtime::attach(const AttachKey& key, std::string_view scriptName) noexcept
        {
            try
            {
                return attachImpl(key, scriptName);
            }
            catch (...)
            {
                mErrKind = ErrorKind::None;
                return 0;
            }
        }

        InstanceId Runtime::attachImpl(const AttachKey& key, std::string_view scriptName)
        {
            const std::uint32_t lowered = findNameId(scriptName);
            const std::uint32_t scriptIndex = lowered == kNoScript ? kNoScript : scriptByClassId(lowered);
            if (scriptIndex == kNoScript)
            {
                mWorld.log(Severity::Warning, "attach: the script '" + std::string(scriptName) + "' is not loaded");
                return 0;
            }
            if (mByKey.contains(key))
            {
                return 0;
            }
            if (mInstances.size() >= mOptions.maxInstances)
            {
                mWorld.log(Severity::Warning,
                    "attach: the runtime holds the cap of " + std::to_string(mOptions.maxInstances) + " instances");
                return 0;
            }
            if (!prepare(scriptIndex))
            {
                return 0;
            }
            const Script& script = mScripts[scriptIndex];
            Instance instance;
            instance.key = key;
            instance.script = scriptIndex;
            instance.stateName = script.mAutoStateName;
            instance.stateId = script.mAutoStateId;
            instance.vars.resize(script.totalVars());
            for (const std::uint32_t owner : script.mChain)
            {
                const Script& chainScript = mScripts[owner];
                for (std::size_t i = 0; i < chainScript.mVars.size(); ++i)
                {
                    instance.vars[chainScript.mVarBase + i] = chainScript.mVars[i].initial;
                }
            }
            mInstances.push_back(std::move(instance));
            const std::uint32_t id = static_cast<std::uint32_t>(mInstances.size());
            mByKey.emplace(key, id);
            ErrorReport error;
            if (!queueEvent(id, nameId("OnInit"), {}, error))
            {
                mWorld.log(Severity::Warning, "attach: OnInit could not be queued: " + error.format());
            }
            return id;
        }

        CallResult Runtime::send(
            InstanceId instance, std::string_view eventName, std::span<const PapyrusValue> arguments) noexcept
        {
            try
            {
                return sendImpl(instance, eventName, arguments);
            }
            catch (...)
            {
                CallResult result;
                result.error.message = "[internal] out of memory or an internal error while queueing an event";
                return result;
            }
        }

        CallResult Runtime::sendImpl(
            InstanceId instance, std::string_view eventName, std::span<const PapyrusValue> arguments)
        {
            CallResult result;
            ErrorReport error;
            if (!queueEvent(instance, nameId(eventName), arguments, error))
            {
                result.error = std::move(error);
                return result;
            }
            result.ok = true;
            return result;
        }

        CallResult Runtime::call(
            InstanceId instance, std::string_view functionName, std::span<const PapyrusValue> arguments) noexcept
        {
            try
            {
                return callImpl(instance, functionName, arguments);
            }
            catch (...)
            {
                mErrKind = ErrorKind::None;
                CallResult result;
                result.error.message = "[internal] out of memory or an internal error while running a call";
                return result;
            }
        }

        CallResult Runtime::callImpl(
            InstanceId instance, std::string_view functionName, std::span<const PapyrusValue> arguments)
        {
            CallResult result;
            if (mInNative)
            {
                result.error = apiError(ErrorKind::Internal, "call() cannot be used from inside a native");
                return result;
            }
            if (instance == 0 || instance > mInstances.size())
            {
                result.error
                    = apiError(ErrorKind::NoSuchInstance, "instance " + std::to_string(instance) + " does not exist");
                return result;
            }
            Stack* stack = acquireStack();
            stack->instance = instance;
            stack->eventName = nameId(functionName);
            if (!fillArguments(*stack, arguments))
            {
                result.error = takeError();
                releaseStack(stack);
                return result;
            }
            std::uint64_t executed = 0;
            const int outcome = runStack(*stack, false, effectiveInstructions({}), effectiveDepth({}), executed);
            ++mStats.stacksRun;
            ++mStats.stacksFinished;
            result.instructionsExecuted = executed;
            if (outcome == 0)
            {
                result.ok = true;
                toPapyrus(stack->result, result.returned);
            }
            else
            {
                result.error = takeError();
            }
            releaseStack(stack);
            return result;
        }

        TickResult Runtime::tick(double, const Budget& budget) noexcept
        {
            try
            {
                return tickImpl(budget);
            }
            catch (...)
            {
                mErrKind = ErrorKind::None;
                return {};
            }
        }

        TickResult Runtime::tickImpl(const Budget& budget)
        {
            TickResult result;
            if (mInNative)
            {
                result.errors.push_back(apiError(ErrorKind::Internal, "tick() cannot be used from inside a native"));
                return result;
            }
            const std::uint64_t maxInstructions = effectiveInstructions(budget);
            const std::uint32_t maxDepth = effectiveDepth(budget);
            std::size_t processed = 0;
            while (mReadyHead < mReady.size() && processed < kMaxStacksPerTick)
            {
                Stack* stack = mReady[mReadyHead++];
                ++processed;
                std::uint64_t executed = 0;
                const int outcome = runStack(*stack, true, maxInstructions, maxDepth, executed);
                ++result.stacksRun;
                ++result.stacksFinished;
                ++mStats.stacksRun;
                ++mStats.stacksFinished;
                result.instructionsExecuted += executed;
                if (outcome == 2)
                {
                    result.errors.push_back(takeError());
                }
                releaseStack(stack);
            }
            if (mReadyHead >= mReady.size())
            {
                mReady.clear();
                mReadyHead = 0;
            }
            return result;
        }

        // ---- save and restore (stage e)
        // ------------------------------------------------------------------------------

        SaveBlob Runtime::serialise() const noexcept
        {
            return {};
        }

        RestoreResult Runtime::restore(const SaveBlob&) noexcept
        {
            RestoreResult result;
            result.error.message = "restore is not implemented yet (task 0093 stage e)";
            return result;
        }

        // ---- reading and setting variables
        // ---------------------------------------------------------------------------

        bool Runtime::storable(const TypeRef& type, const PapyrusValue& value) const
        {
            switch (type.kind)
            {
                case TypeKind::Int:
                case TypeKind::Float:
                case TypeKind::Bool:
                    return value.kind == PapyrusKind::Int || value.kind == PapyrusKind::Float
                        || value.kind == PapyrusKind::Bool;
                case TypeKind::String:
                    return value.kind == PapyrusKind::String || value.kind == PapyrusKind::None;
                case TypeKind::Object:
                    return value.kind == PapyrusKind::Object || value.kind == PapyrusKind::None;
                case TypeKind::Array:
                    return value.kind == PapyrusKind::Array || value.kind == PapyrusKind::None;
                case TypeKind::Struct:
                    return value.kind == PapyrusKind::Struct || value.kind == PapyrusKind::None;
                default:
                    return true;
            }
        }

        // A variable by name, or the backing variable of an auto property.
        namespace
        {
            bool locateSlot(const std::vector<Script>& scripts, const Instance& instance, std::uint32_t name,
                std::uint32_t& slot, const TypeRef*& type)
            {
                for (const std::uint32_t owner : scripts[instance.script].mChain)
                {
                    const Script& script = scripts[owner];
                    const auto variable = script.mVarIndex.find(name);
                    if (variable != script.mVarIndex.end())
                    {
                        slot = script.mVarBase + variable->second;
                        type = &script.mVars[variable->second].type;
                        return true;
                    }
                    const auto property = script.mProps.find(name);
                    if (property != script.mProps.end() && property->second.autoProperty)
                    {
                        slot = script.mVarBase + property->second.backing;
                        type = &script.mVars[property->second.backing].type;
                        return true;
                    }
                }
                return false;
            }
        }

        bool Runtime::getVariable(InstanceId instance, std::string_view name, PapyrusValue& out) const noexcept
        {
            try
            {
                return getVariableImpl(instance, name, out);
            }
            catch (...)
            {
                return false;
            }
        }

        bool Runtime::getVariableImpl(InstanceId instance, std::string_view name, PapyrusValue& out) const
        {
            if (instance == 0 || instance > mInstances.size())
            {
                return false;
            }
            const std::uint32_t id = findNameId(name);
            if (id == kNoScript)
            {
                return false;
            }
            const Instance& held = mInstances[instance - 1];
            std::uint32_t slot = 0;
            const TypeRef* type = nullptr;
            if (!locateSlot(mScripts, held, id, slot, type) || slot >= held.vars.size())
            {
                return false;
            }
            return toPapyrus(held.vars[slot], out);
        }

        bool Runtime::setVariable(InstanceId instance, std::string_view name, const PapyrusValue& value) noexcept
        {
            try
            {
                return setVariableImpl(instance, name, value);
            }
            catch (...)
            {
                mErrKind = ErrorKind::None;
                return false;
            }
        }

        bool Runtime::setVariableImpl(InstanceId instance, std::string_view name, const PapyrusValue& value)
        {
            if (instance == 0 || instance > mInstances.size())
            {
                return false;
            }
            const std::uint32_t id = findNameId(name);
            if (id == kNoScript)
            {
                return false;
            }
            std::uint32_t slot = 0;
            const TypeRef* type = nullptr;
            if (!locateSlot(mScripts, mInstances[instance - 1], id, slot, type) || !storable(*type, value))
            {
                return false;
            }
            const TypeRef declared = *type;
            Val converted;
            if (!fromPapyrus(value, converted))
            {
                mErrKind = ErrorKind::None;
                return false;
            }
            mInstances[instance - 1].vars[slot] = coerce(converted, declared);
            return true;
        }

        std::string Runtime::stateOf(InstanceId instance) const
        {
            if (instance == 0 || instance > mInstances.size())
            {
                return {};
            }
            return std::string(textOf(Val::makeString(mInstances[instance - 1].stateName)));
        }
    }

    std::unique_ptr<PapyrusRuntime> createRuntime(
        PapyrusWorld& world, UnknownFunctionPolicy policy, const RuntimeOptions& options)
    {
        return std::make_unique<Vm::Runtime>(world, policy, options);
    }
}
