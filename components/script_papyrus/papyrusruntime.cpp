#include "papyrusruntime.hpp"

#include <map>
#include <unordered_map>
#include <utility>

#include "papyruslimits.hpp"
#include "pexfile.hpp"

namespace ScriptPapyrus
{
    namespace
    {
        using namespace openeden::script;

        ErrorReport notImplemented(const char* what)
        {
            ErrorReport report;
            report.message = std::string(what) + " is not implemented yet (task 0093 stages c to e)";
            return report;
        }

        // A loaded script: the parsed file and what link() needs to find its functions.
        struct Script
        {
            std::string mFileName; ///< As given to loadScript, for reports.
            std::string mClass; ///< The object's name.
            std::string mParent; ///< The parent's name, or empty.
            Pex::File mFile;
            /// Lower-cased function name to "is native", over every state and property accessor of the object.
            std::unordered_map<std::string, bool> mFunctions;
        };

        // What a lookup of one function found.
        enum class Found
        {
            Script,
            MissingClass,
            MissingFunction,
            Dynamic,
        };

        constexpr std::size_t kMaxParentDepth = 64;

        // A receiver of one of these types has no class to look in.
        bool isPrimitiveType(const std::string& lowerType)
        {
            return lowerType.empty() || lowerType == "int" || lowerType == "float" || lowerType == "bool"
                || lowerType == "string" || lowerType == "var" || lowerType == "none" || lowerType.ends_with("[]");
        }

        class Runtime final : public PapyrusContract
        {
        public:
            Runtime(PapyrusWorld& world, UnknownFunctionPolicy policy)
                : mWorld(world)
                , mPolicy(policy)
            {
            }

            LoadResult loadScript(std::span<const std::uint8_t> bytes, std::string_view name) noexcept override
            {
                LoadResult result;
                try
                {
                    return load(bytes, name);
                }
                catch (...)
                {
                    result.ok = false;
                    result.script = 0;
                    try
                    {
                        ErrorReport report;
                        report.script = std::string(name);
                        report.message = "out of memory or an internal error while loading the script";
                        result.reports.push_back(std::move(report));
                    }
                    catch (...)
                    {
                        result.reports.clear();
                    }
                }
                return result;
            }

            LinkReport link() const noexcept override
            {
                try
                {
                    return buildLinkReport();
                }
                catch (...)
                {
                    return {};
                }
            }

            bool registerNative(const NativeDescriptor&, NativeHandler, ErrorReport& error) noexcept override
            {
                error = notImplemented("registerNative");
                return false;
            }

            void setPolicy(UnknownFunctionPolicy policy) noexcept override { mPolicy = policy; }

            InstanceId attach(const AttachKey&, std::string_view) noexcept override { return 0; }

            CallResult send(InstanceId, std::string_view, std::span<const PapyrusValue>) noexcept override
            {
                CallResult result;
                result.error = notImplemented("send");
                return result;
            }

            CallResult call(InstanceId, std::string_view, std::span<const PapyrusValue>) noexcept override
            {
                CallResult result;
                result.error = notImplemented("call");
                return result;
            }

            TickResult tick(double, const Budget&) noexcept override { return {}; }

            SaveBlob serialise() const noexcept override { return {}; }

            RestoreResult restore(const SaveBlob&) noexcept override
            {
                RestoreResult result;
                result.error = notImplemented("restore");
                return result;
            }

        private:
            LoadResult load(std::span<const std::uint8_t> bytes, std::string_view name);
            LinkReport buildLinkReport() const;

            const Script* find(const std::string& lowerClass) const
            {
                auto found = mByName.find(lowerClass);
                return found == mByName.end() ? nullptr : &mScripts[found->second];
            }

            PapyrusWorld& mWorld;
            UnknownFunctionPolicy mPolicy;
            std::vector<Script> mScripts; ///< In load order.
            std::map<std::string, std::size_t> mByName; ///< Lower-cased class name to index in mScripts.
        };

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
                ErrorReport report;
                report.script = std::string(name);
                report.message = "a script named '" + className + "' is already loaded";
                result.reports.push_back(std::move(report));
                return result;
            }
            if (mScripts.size() >= Limits::kMaxScripts)
            {
                ErrorReport report;
                report.script = std::string(name);
                report.message = "the runtime holds the cap of " + std::to_string(Limits::kMaxScripts) + " scripts";
                result.reports.push_back(std::move(report));
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
            mScripts.push_back(std::move(script));
            return result;
        }

        // link(): the targets that can be known statically. A receiver's class comes from the declared type of a local,
        // a parameter or a variable (or "self"); a function is looked for in that class and then up its parent chain.
        LinkReport Runtime::buildLinkReport() const
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

            // Look for \a function from \a start up the parent chain.
            auto lookup = [this](const Script* start, const std::string& lowerFunction, bool& native) {
                const Script* current = start;
                for (std::size_t depth = 0; current != nullptr && depth < kMaxParentDepth; ++depth)
                {
                    auto found = current->mFunctions.find(lowerFunction);
                    if (found != current->mFunctions.end())
                    {
                        native = found->second;
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
                            outcome = lookup(start, lowerFunction, native);
                        }
                        switch (outcome)
                        {
                            case Found::Script:
                                if (native)
                                {
                                    ++report.resolvedNative;
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
    }

    std::unique_ptr<openeden::script::PapyrusContract> createRuntime(
        openeden::script::PapyrusWorld& world, openeden::script::UnknownFunctionPolicy policy)
    {
        return std::make_unique<Runtime>(world, policy);
    }
}
