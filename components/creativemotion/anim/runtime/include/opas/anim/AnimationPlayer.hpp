#pragma once

// One character's animation output (task 0020, stage C3c): plays a clip and
// switches between clips with inertialized transitions (Inertialization.hpp)
// or, for comparison, crossfades. Body-part layering is done by combining the
// poses of several players with applyMaskedLayer (PoseBlend.hpp); the graph
// (C3d) drives that.
//
// Ownership: a plain value; buffers are sized for boneCount at construction,
// and nothing allocates afterwards.

#include "opas/anim/ClipPlayer.hpp"
#include "opas/anim/HelperSolver.hpp"
#include "opas/anim/Inertialization.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace opas::anim {

enum class TransitionMode { Inertialize, Crossfade };

struct Transition {
    double durationS = 0.0; // 0 (or negative, non-finite): switch at once
    TransitionMode mode = TransitionMode::Inertialize;
};

class AnimationPlayer {
public:
    explicit AnimationPlayer(std::size_t boneCount,
                            HelperSolver helpers = HelperSolver{rig::Rig{}}); // allocates here only

    void setHelperSolver(HelperSolver solver);
    [[nodiscard]] const HelperSolver& helperSolver() const noexcept { return helperSolver_; }

    void setExertion(double exertion) noexcept;
    [[nodiscard]] double exertion() const noexcept { return exertion_; }

    // Switches to `binding`, playing as `options` say. pose() is updated at
    // once: without a transition (nothing playing yet, or durationS 0) it is
    // the new clip at its start time; with one it stays the current output.
    //   Inertialize: the offset runs from the current output to the new
    //   clip's pose; its rate comes from the output one advance earlier and
    //   the new clip one advance before its start time, so the output keeps
    //   its velocity at the switch; the old clip is no longer sampled.
    //   Crossfade: the old clip keeps playing, and the output blends from it
    //   to the new clip with weight elapsed / durationS (linear; over within
    //   1e-9 s of durationS, as summed steps round). A switch
    //   during a crossfade freezes the current output as the pose it fades
    //   from.
    // A switch during an inertialized transition starts a new one from the
    // current output. Fails, changing nothing, if binding is null or its
    // targetBoneCount() differs from boneCount (message in *error).
    // Allocates nothing.
    bool play(std::shared_ptr<const ClipBinding> binding, const PlayOptions& options = {},
              const Transition& transition = {}, std::string* error = nullptr);

    // Advances by dtS and updates pose(); dtS 0 (or negative, non-finite) is a
    // paused step that changes nothing. Events come from the new (current)
    // clip only. Root motion: the current clip's; during a crossfade, the two
    // clips' deltas blended by the fade weight at the end of this advance
    // (translations linearly, yaw linearly), the old delta being zero when
    // fading from a frozen pose. Not playing: an empty step and the pose unchanged.
    // Allocates nothing.
    PlayerStep advance(double dtS, std::span<FiredEvent> events);

    // The output pose (boneCount local transforms); identity transforms until
    // the first play().
    [[nodiscard]] std::span<const math::Transform> pose() const { return pose_; }
    [[nodiscard]] const ClipPlayer& current() const { return current_; }
    [[nodiscard]] bool inTransition() const;
    [[nodiscard]] std::size_t boneCount() const { return pose_.size(); }

private:
    ClipPlayer current_;
    ClipPlayer outgoing_; // crossfade only
    Inertializer inertializer_;
    double fadeElapsedS_ = 0.0;
    double fadeDurationS_ = 0.0;
    bool fading_ = false;
    bool outgoingFrozen_ = false; // crossfade from a frozen pose
    double lastDtS_ = 0.0;
    HelperSolver helperSolver_;
    double exertion_ = 0.0;
    double timeS_ = 0.0;
    std::vector<math::Transform> pose_;
    std::vector<math::Transform> previousPose_; // output one advance earlier
    std::vector<math::Transform> incoming_;     // scratch: the current clip's pose
    std::vector<math::Transform> fadeFrom_;     // scratch or frozen: the pose faded from
};

} // namespace opas::anim
