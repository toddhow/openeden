#ifndef CREATIVEMOTION_EDITOR_DOCUMENTS_H
#define CREATIVEMOTION_EDITOR_DOCUMENTS_H

// The Rig & Motor Limits Inspector of CreativeMotion's editor module (docs/tasks/0032). Built against the Editor
// Module API (sdk/include/openeden/editor_module) and CreativeMotion's rig headers only.

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>
#include <openeden/version.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace creativemotion_editor
{
    inline constexpr const char* kRigFormat = "opas.rig";
    inline constexpr const char* kProfileFormat = "opas.profile";
    /// The handle at each joint's origin; a click on it selects the joint (the host reports it as a "handle"
    /// selection).
    inline constexpr const char* kJointHandlePrefix = "creativemotion.joint.";

    /// Implemented by the rig document (API 1.2, task 0091): which joint the panel and the limits drawing single out.
    /// The module sets it from the host's selection; the host never sees this interface.
    class JointSelectable
    {
    public:
        virtual ~JointSelectable() = default;
        /// The rig's joints by bone name, in rig order.
        virtual std::vector<std::string> jointNames() const = 0;
        /// A bone name that is not a joint of the rig, or an empty one, clears the selection.
        virtual void setSelectedJoint(const std::string& bone) = 0;
        virtual const std::string& selectedJoint() const = 0;
    };

    /// A rig file as an asset. Null with \a error set (the loader's located message) if it cannot be read or is
    /// invalid.
    std::unique_ptr<openeden::editor::AssetDocument> openRig(const std::filesystem::path& path, std::string& error);

    /// A profile file as an asset: checked on its own, as the loader does (no rig is known).
    std::unique_ptr<openeden::editor::AssetDocument> openProfile(const std::filesystem::path& path, std::string& error);

    /// The module's descriptor, for tests that link the sources in. The library exports the same one under the API's
    /// entry point name (openeden_editor_module_descriptor).
    const openeden::editor::EditorModuleDescriptor* moduleDescriptor();
}

#endif
