#include "papyrusruntime.hpp"

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

        class Runtime final : public PapyrusContract
        {
        public:
            Runtime(PapyrusWorld& world, UnknownFunctionPolicy policy)
                : mWorld(world)
                , mPolicy(policy)
            {
            }

            LoadResult loadScript(std::span<const std::uint8_t>, std::string_view name) noexcept override
            {
                LoadResult result;
                result.reports.push_back(notImplemented("loadScript"));
                result.reports.back().script = std::string(name);
                return result;
            }

            LinkReport link() const noexcept override { return {}; }

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
            PapyrusWorld& mWorld;
            UnknownFunctionPolicy mPolicy;
        };
    }

    std::unique_ptr<openeden::script::PapyrusContract> createRuntime(
        openeden::script::PapyrusWorld& world, openeden::script::UnknownFunctionPolicy policy)
    {
        return std::make_unique<Runtime>(world, policy);
    }
}
