#ifndef OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_STUBWORLD_H
#define OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_STUBWORLD_H

// Task 0092: the stub world the ObScript tests run their generated scripts against. No game data: every name below is
// invented for the tests.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openeden/script/obscript.hpp>

#include <components/script_obscript/obscriptruntime.hpp>

namespace ObScriptTest
{
    inline std::string lowered(std::string_view text)
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

    constexpr openeden::script::FormId sPlayer = 0x00000007;
    constexpr openeden::script::FormId sQuest = 0x00000100;
    constexpr openeden::script::FormId sGold = 0x00000200;
    constexpr openeden::script::FormId sCrate = 0x00000300;

    class StubWorld : public openeden::script::ObScriptWorld
    {
    public:
        StubWorld()
        {
            mForms = { { "player", sPlayer }, { "myquest", sQuest }, { "gold001", sGold }, { "crate", sCrate } };
            mVariables[{ sQuest, "stage" }] = openeden::script::Value::makeInteger(3);
            mGlobals["testglobal"] = openeden::script::Value::makeInteger(0);
            mGlobals["gamehour"] = openeden::script::Value::makeFloat(8.5f);
        }

        bool resolveForm(std::string_view editorId, openeden::script::FormId& form) const override
        {
            const auto it = mForms.find(lowered(editorId));
            if (it == mForms.end())
            {
                return false;
            }
            form = it->second;
            return true;
        }

        bool findVariable(
            openeden::script::FormId owner, std::string_view name, openeden::script::ValueKind& kind) const override
        {
            const auto it = mVariables.find({ owner, lowered(name) });
            if (it == mVariables.end())
            {
                return false;
            }
            kind = it->second.kind;
            return true;
        }

        openeden::script::Value getVariable(openeden::script::FormId owner, std::string_view name) const override
        {
            const auto it = mVariables.find({ owner, lowered(name) });
            return it == mVariables.end() ? openeden::script::Value{} : it->second;
        }

        void setVariable(openeden::script::FormId owner, std::string_view name, openeden::script::Value value) override
        {
            mVariables[{ owner, lowered(name) }] = value;
        }

        openeden::script::ValueKind globalKind(std::string_view name) const override
        {
            const auto it = mGlobals.find(lowered(name));
            return it == mGlobals.end() ? openeden::script::ValueKind::String : it->second.kind;
        }

        openeden::script::Value getGlobal(std::string_view name) const override
        {
            const auto it = mGlobals.find(lowered(name));
            return it == mGlobals.end() ? openeden::script::Value{} : it->second;
        }

        void setGlobal(std::string_view name, openeden::script::Value value) override
        {
            mGlobals[lowered(name)] = value;
        }

        void report(std::string_view message) override { mMessages.emplace_back(message); }

        std::map<std::string, openeden::script::FormId> mForms;
        std::map<std::pair<openeden::script::FormId, std::string>, openeden::script::Value> mVariables;
        std::map<std::string, openeden::script::Value> mGlobals;
        std::vector<std::string> mMessages;
    };

    // One recorded call of a stub function.
    struct Call
    {
        std::string mFunction;
        openeden::script::FormId mSelf = 0;
        openeden::script::FormId mReference = 0;
        std::vector<std::string> mArguments;
    };

    inline std::string describe(const openeden::script::Value& value)
    {
        switch (value.kind)
        {
            case openeden::script::ValueKind::Integer:
                return "i" + std::to_string(value.integer);
            case openeden::script::ValueKind::Float:
                return "f" + std::to_string(value.number);
            case openeden::script::ValueKind::Form:
                return "form" + std::to_string(static_cast<std::uint32_t>(value.integer));
            case openeden::script::ValueKind::String:
                return "s" + std::string(value.text);
        }
        return {};
    }

    // A runtime with a few stub functions registered. The functions record their calls in mCalls.
    struct RuntimeFixture
    {
        explicit RuntimeFixture(
            openeden::script::UnknownFunctionPolicy policy = openeden::script::UnknownFunctionPolicy::Tolerant)
            : mRuntime(ScriptObscript::createRuntime(mWorld, policy))
        {
            using namespace openeden::script;
            registerStub("AddItem", "rl", ValueKind::Integer);
            registerStub("GetItemCount", "r", ValueKind::Integer);
            registerStub("ScaleBy", "fl", ValueKind::Float);
            registerStub("Say", "S", ValueKind::Integer);
            registerStub("Greet", "l/l", ValueKind::Integer);
        }

        bool registerStub(std::string name, std::string signature, openeden::script::ValueKind returns)
        {
            using namespace openeden::script;
            FunctionDescriptor descriptor;
            descriptor.name = name;
            descriptor.signature = std::move(signature);
            descriptor.returns = returns;
            ErrorReport error;
            return mRuntime->registerFunction(
                descriptor,
                [this, name, returns](const CallArgs& args) {
                    Call call;
                    call.mFunction = name;
                    call.mSelf = args.self;
                    call.mReference = args.reference;
                    for (const Value& argument : args.arguments)
                    {
                        call.mArguments.push_back(describe(argument));
                    }
                    mCalls.push_back(call);
                    if (name == "GetItemCount")
                    {
                        const auto owner = mItems.find(args.reference != 0 ? args.reference : args.self);
                        return Value::makeInteger(owner == mItems.end() ? 0 : owner->second);
                    }
                    if (name == "AddItem" && args.arguments.size() == 2)
                    {
                        mItems[args.reference != 0 ? args.reference : args.self] += args.arguments[1].integer;
                    }
                    if (name == "ScaleBy" && args.arguments.size() == 2)
                    {
                        return Value::makeFloat(
                            args.arguments[0].number * static_cast<float>(args.arguments[1].integer));
                    }
                    return returns == ValueKind::Float ? Value::makeFloat(0.0f) : Value::makeInteger(1);
                },
                error);
        }

        // Compile and require success; returns the script id.
        openeden::script::ScriptId compileOk(std::string name, std::string text,
            openeden::script::ScriptKind kind = openeden::script::ScriptKind::Script)
        {
            openeden::script::ScriptSource source;
            source.name = std::move(name);
            source.text = std::move(text);
            source.kind = kind;
            const openeden::script::CompileResult result = mRuntime->compile(source);
            for (const auto& report : result.reports)
            {
                mLastReports.push_back(report.format());
            }
            return result.ok ? result.script : 0;
        }

        StubWorld mWorld;
        std::unique_ptr<openeden::script::ObScriptContract> mRuntime;
        std::vector<Call> mCalls;
        std::map<openeden::script::FormId, std::int32_t> mItems;
        std::vector<std::string> mLastReports;
    };
}

#endif
