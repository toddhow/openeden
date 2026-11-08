#ifndef OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PAPYRUSVMUTIL_H
#define OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PAPYRUSVMUTIL_H

// Task 0093 stage c: helpers for the tests that run generated scripts on the VM (no game asset anywhere).

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/script_papyrus/papyrusruntime.hpp>

#include "papyrustestutil.hpp"

namespace PapyrusVmTest
{
    using namespace PapyrusTest;
    using namespace PexAsm;
    using namespace openeden::script;

    /// The world of the tests: forms have class names, reports and log lines are collected.
    class VmWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId form, std::string& className) const override
        {
            const auto found = mClasses.find(form);
            if (found == mClasses.end())
            {
                return false;
            }
            className = found->second;
            return true;
        }
        void report(std::string_view message) override { mReports.emplace_back(message); }
        void log(Severity, std::string_view message) override { mLogs.emplace_back(message); }

        bool logged(std::string_view part) const
        {
            for (const std::string& line : mLogs)
            {
                if (line.find(part) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
        std::size_t loggedCount(std::string_view part) const
        {
            std::size_t count = 0;
            for (const std::string& line : mLogs)
            {
                if (line.find(part) != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        std::map<FormId, std::string> mClasses;
        std::vector<std::string> mReports;
        std::vector<std::string> mLogs;
    };

    inline bool hasText(const ErrorReport& error, std::string_view part)
    {
        return error.message.find(part) != std::string::npos;
    }

    /// One runtime with a world, scripts generated for one target, and short ways to run them.
    class Vm
    {
    public:
        explicit Vm(Target target = Target::SkyrimSE, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant,
            const ScriptPapyrus::RuntimeOptions& options = {})
            : mTarget(target)
            , mRuntime(ScriptPapyrus::createRuntime(mWorld, policy, options))
        {
        }

        /// Loads a generated script; a failed load fails the test with the reader's message.
        bool add(const ScriptBuilder& script)
        {
            const LoadResult result = mRuntime->loadScript(script.build(optionsFor(mTarget)).mBytes, script.name());
            if (!result.ok)
            {
                ADD_FAILURE() << "load of " << script.name()
                              << " failed: " << (result.reports.empty() ? "no report" : result.reports[0].format());
            }
            return result.ok;
        }

        /// A new instance on a new form; 0 (and a test failure) when attach refused it.
        InstanceId make(std::string_view scriptName)
        {
            AttachKey key;
            key.kind = AttachKind::Form;
            key.formId = 0x10000u + mNextForm++;
            const InstanceId id = mRuntime->attach(key, scriptName);
            if (id == 0)
            {
                ADD_FAILURE() << "attach of " << scriptName << " refused";
            }
            return id;
        }

        /// A new instance, with its queued OnInit already run.
        InstanceId makeAndInit(std::string_view scriptName)
        {
            const InstanceId id = make(scriptName);
            tick();
            return id;
        }

        ScriptPapyrus::PapyrusRuntime& rt() { return *mRuntime; }

        TickResult tick(double time = 0.0) { return mRuntime->tick(time); }

        CallResult call(InstanceId instance, std::string_view function, std::vector<PapyrusValue> arguments = {})
        {
            return mRuntime->call(instance, function, arguments);
        }

        /// Runs a function that must finish, and returns its value (None when it failed; a test failure then).
        PapyrusValue run(InstanceId instance, std::string_view function, std::vector<PapyrusValue> arguments = {})
        {
            const CallResult result = call(instance, function, std::move(arguments));
            EXPECT_TRUE(result.ok) << function << ": " << result.error.format();
            return result.ok ? result.returned : PapyrusValue{};
        }

        std::int32_t runInt(InstanceId instance, std::string_view function, std::vector<PapyrusValue> arguments = {})
        {
            const PapyrusValue value = run(instance, function, std::move(arguments));
            EXPECT_EQ(value.kind, PapyrusKind::Int) << function;
            return value.integer;
        }

        std::int32_t varInt(InstanceId instance, std::string_view name) const
        {
            PapyrusValue value;
            EXPECT_TRUE(mRuntime->getVariable(instance, name, value)) << name;
            return value.integer;
        }

        VmWorld mWorld;

    private:
        Target mTarget;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        std::uint32_t mNextForm = 0;
    };

    /// A native script class: "Class" with global or instance native functions, to be declared by the caller.
    inline ScriptBuilder nativeClass(const std::string& name, const std::string& parent = {})
    {
        ScriptBuilder script(name);
        if (!parent.empty())
        {
            script.parent(parent);
        }
        return script;
    }

    inline const Target kBothTargets[] = { Target::SkyrimSE, Target::Fallout4 };
}

#endif
