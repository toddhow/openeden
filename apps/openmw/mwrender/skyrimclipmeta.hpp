#ifndef OPENMW_MWRENDER_SKYRIMCLIPMETA_HPP
#define OPENMW_MWRENDER_SKYRIMCLIPMETA_HPP

// Task 0170 (combat stage S5-2): the length, playback speed and events of a character clip, from the `.hkx` clip and
// the sidecar `meshes/animationdata/<project>.txt` (components/havokpack/animationdata.hpp). The link is by name and
// duration (HavokPack::linkClip, docs/research/0054-provenance.md F44 to F53). Combat reads the result on the main
// thread through the pose controller; the files are read on a worker.

#include <future>
#include <memory>
#include <string>
#include <string_view>

#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/clipevents.hpp>
#include <components/vfs/manager.hpp>

#include "skyrimidle.hpp"

namespace MWRender::SkyrimClipMeta
{
    using MetaFuture = std::shared_future<std::shared_ptr<const HavokPack::ClipMeta>>;

    /// The meta of the clip `clipStem` of `duration` animation seconds: the events and speed of the first generator of
    /// `project` linked to it. A clip no generator links to gets a meta with its length and no events (speed 1). Pure.
    [[nodiscard]] std::shared_ptr<const HavokPack::ClipMeta> makeClipMeta(const HavokPack::AnimationProject& project,
        const HavokPack::MotionTable& motion, std::string_view clipStem, float duration);

    /// Starts a worker that waits for the decoded clip (`clip`, from SkyrimIdle::loadIdleAsync), reads the sidecars of
    /// the actor's project (`defaultmale` or `defaultfemale`, parsed once per process) and makes the meta. The future
    /// holds null when the clip failed to decode. Never throws; `vfs` must outlive the worker.
    [[nodiscard]] MetaFuture loadAsync(
        const VFS::Manager& vfs, bool female, std::string clipStem, SkyrimIdle::ClipFuture clip);

    /// Task 0212: the same for a creature (or any project by name): the sidecars are `animationdata/<project>.txt` and
    /// `animationdata/boundanims/anims_<project>.txt` (`wolfproject`). An empty `project` is the person's, by `female`.
    [[nodiscard]] MetaFuture loadAsync(const VFS::Manager& vfs, bool female, const std::string& project,
        std::string clipStem, SkyrimIdle::ClipFuture clip);
}

#endif
