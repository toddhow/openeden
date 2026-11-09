#ifndef CSM_PHYSICS_OWNEDSANDBOX_H
#define CSM_PHYSICS_OWNEDSANDBOX_H

// Remembers which editor module built the simulation sandbox's ragdoll, so that withdrawing that module (a callback of
// it threw, or it was unloaded) releases the ragdoll and the ground it left in the world. Review of 2026-11-09, C5.
// No Qt and no render types.

#include <openeden/editor_module/types.hpp>

#include <functional>
#include <string>

namespace CSMPhysics
{
    /// A SimulationSandbox that forwards every call to another one and notes the owner of what is built.
    ///
    /// The Editor Module API gives a module the sandbox without saying who it is, so the owner is whatever
    /// \a currentOwner answers when a build succeeds (Studio answers with the module of the active asset, which is the
    /// module whose command or panel is running). An empty answer means "unknown": such a ragdoll is only released by
    /// release(), by a later build, or when all modules go.
    ///
    /// Ownership: does not own \a inner, which must outlive it. Main thread only.
    class OwnedSandbox final : public openeden::editor::SimulationSandbox
    {
    public:
        using OwnerFunction = std::function<std::string()>;

        OwnedSandbox(openeden::editor::SimulationSandbox& inner, OwnerFunction currentOwner);

        bool build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
            const openeden::editor::Vec3& offsetM, double yawRadians, std::string& error) override;
        bool built() const override;
        std::vector<std::string> bodyNames() const override;
        bool push(std::string_view bone, const openeden::editor::Vec3& impulseNs,
            const openeden::editor::Vec3& worldPointM) override;
        std::size_t pendingPushes() const override;
        bool bodyPose(std::string_view bone, openeden::editor::SandboxBodyPose& out) const override;
        bool reset(std::string& error) override;
        void release() override;

        /// Releases what is built if \a moduleId built it. Does nothing for another module, or for an empty id.
        void releaseIfOwnedBy(const std::string& moduleId);
        /// The module that built what is built now; empty when nothing is built or the owner was not known.
        const std::string& owner() const { return mOwner; }

    private:
        openeden::editor::SimulationSandbox& mInner;
        OwnerFunction mCurrentOwner;
        std::string mOwner;
    };
}

#endif
