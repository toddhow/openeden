#include "obscriptruntime.hpp"

namespace ScriptObscript
{
    namespace
    {
        // Stage a stub (task 0092): the contract compiles and links, nothing works yet.
        class StubRuntime final : public openeden::script::ObScriptContract
        {
        public:
            bool registerFunction(const openeden::script::FunctionDescriptor&, openeden::script::FunctionHandler,
                openeden::script::ErrorReport& error) noexcept override
            {
                error.message = "not implemented";
                return false;
            }

            openeden::script::CompileResult compile(const openeden::script::ScriptSource&) noexcept override
            {
                return {};
            }

            openeden::script::InstanceId createInstance(
                openeden::script::ScriptId, openeden::script::FormId) noexcept override
            {
                return 0;
            }

            openeden::script::RunResult run(
                openeden::script::InstanceId, std::string_view, const openeden::script::Budget&) noexcept override
            {
                return {};
            }

            openeden::script::Value getLocal(openeden::script::InstanceId, std::string_view) const noexcept override
            {
                return {};
            }

            bool setLocal(openeden::script::InstanceId, std::string_view, openeden::script::Value) noexcept override
            {
                return false;
            }

            openeden::script::SaveBlob serialise() const noexcept override { return {}; }

            openeden::script::RestoreResult restore(const openeden::script::SaveBlob&) noexcept override { return {}; }
        };
    }

    std::unique_ptr<openeden::script::ObScriptContract> createRuntime(
        openeden::script::ObScriptWorld&, openeden::script::UnknownFunctionPolicy)
    {
        return std::make_unique<StubRuntime>();
    }
}
