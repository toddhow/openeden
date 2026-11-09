#ifndef CREATIVEMOTION_EDITOR_SANDBOX_H
#define CREATIVEMOTION_EDITOR_SANDBOX_H

// The push sandbox of CreativeMotion's editor module (task 0065, stage C): a panel, two commands and a gizmo over the
// host's simulation sandbox (Editor Module API 1.3). Built against the Editor Module API and CreativeMotion's rig
// headers only.

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <opas/rig/Rig.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace creativemotion_editor
{
    inline constexpr const char* kSandboxPanelId = "creativemotion.sandbox";
    inline constexpr const char* kSandboxPushCommandId = "creativemotion.sandbox.push";
    inline constexpr const char* kSandboxResetCommandId = "creativemotion.sandbox.reset";
    inline constexpr const char* kSandboxGizmoProviderId = "creativemotion.sandbox.gizmos";

    inline constexpr const char* kSandboxBoneId = "sandbox.bone";
    inline constexpr const char* kSandboxDirectionId = "sandbox.direction";
    inline constexpr const char* kSandboxMagnitudeId = "sandbox.magnitudeNs";
    inline constexpr const char* kSandboxStatusId = "sandbox.status";

    /// The ragdoll is built with its lowest body this far above the ground (meters), so that it falls a little.
    inline constexpr double kDropClearanceM = 0.3;
    /// The push magnitude's default and bounds, newton seconds (a 5 kg body leaves at 10 m/s with 50).
    inline constexpr double kDefaultPushNs = 50.0;
    inline constexpr double kMaxPushNs = 5000.0;

    /// How far up (meters) the rig's rest pose must be moved so that the lowest point of its bodies, bounded by their
    /// shapes, is kDropClearanceM above the ground at z = 0.
    double dropOffsetM(const opas::rig::Rig& rig);

    /// The sandbox of the module. Created by the module with its context; the module forwards what it learns (opened
    /// assets, the host's selection) and the controller does the rest. All main thread.
    class SandboxController
    {
    public:
        explicit SandboxController(openeden::editor::EditorContext& context);
        ~SandboxController();
        SandboxController(const SandboxController&) = delete;
        SandboxController& operator=(const SandboxController&) = delete;

        /// An asset of the rig or profile type was opened: remember its path (the sandbox is built from files). A rig
        /// is read here for its bones and shapes; one that cannot be read is forgotten.
        void noteRigOpened(const std::filesystem::path& path);
        void noteProfileOpened(const std::filesystem::path& path);
        /// The host's selection changed to a joint: if that bone carries a body it becomes the bone to push.
        void noteJointSelected(std::string_view bone);

        /// The simulation took a step: the bodies moved, so ask for a redraw while the sandbox is built.
        void noteStepped();

        /// The host has a sandbox and a rig and a profile are known.
        bool available() const;

        openeden::editor::PropertySource* properties();
        openeden::editor::Gizmo* gizmo();

        /// Builds the sandbox if needed, then queues the push described by the panel. Reports through the context's
        /// log; \return whether a push was queued.
        bool push();
        /// Builds the sandbox if needed, otherwise puts the ragdoll back. \return whether it is built afterwards.
        bool reset();

    private:
        class Properties;
        class SandboxGizmo;
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        std::unique_ptr<Properties> mProperties; ///< made on first use, owned here
        std::unique_ptr<SandboxGizmo> mGizmo;
    };
}

#endif
