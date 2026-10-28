#pragma once

// Plays an animation graph for one character (task 0020, stage C3d): holds the
// parameter values, the current state and any transition, and produces a pose,
// root motion and events per step. Time is passed in by the caller.
//
// Ownership: a plain value. It copies the graph and shares the clip bindings
// (std::shared_ptr, as the players do: one binding serves every character
// with that skeleton). Buffers are sized in create(); nothing allocates after.

#include "opas/anim/AnimGraph.hpp"
#include "opas/anim/ClipBinding.hpp"
#include "opas/anim/ClipPlayer.hpp"
#include "opas/anim/HelperSolver.hpp"
#include "opas/anim/Inertialization.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::anim {

// A clip the graph's states name, bound to the character's skeleton.
struct NamedClip {
    std::string name;
    std::shared_ptr<const ClipBinding> binding;
};

struct GraphStep {
    RootMotionDelta rootMotion;
    std::size_t eventsFired = 0;   // written to the events span, in order
    std::size_t eventsDropped = 0; // fired but beyond the span's size
    int transition = -1;           // index of the transition taken this step, or -1
};

class GraphInstance {
public:
    // Fails (message in *error) if the graph is invalid (validateAnimGraph), a
    // clip name has no NamedClip (or a null binding), a binding's
    // targetBoneCount() differs from boneCount, or two NamedClips share a name.
    // Warnings (event conditions no clip of their state can fire) go to
    // *warnings. Starts in initialState at time 0, parameters at their
    // defaults; pose() is that state's pose at time 0. Allocates here only.
    static std::optional<GraphInstance>
    create(const AnimGraph& graph, std::span<const NamedClip> clips, std::size_t boneCount,
           std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);

    [[nodiscard]] std::optional<std::size_t> parameterIndex(std::string_view id) const;
    // Sets a parameter for the next advance. A bool parameter takes any non-zero
    // value as true (stored as 1). False, changing nothing, for an unknown
    // index or id or a non-finite value. Allocates nothing.
    bool setParameter(std::size_t index, double value);
    bool setParameter(std::string_view id, double value);
    [[nodiscard]] double parameter(std::size_t index) const;

    void setHelperSolver(HelperSolver solver);
    [[nodiscard]] const HelperSolver& helperSolver() const noexcept;

    void setExertion(double exertion) noexcept;
    [[nodiscard]] double exertion() const noexcept;

    // One step, in this order:
    //   1. dtS <= 0 or non-finite: a paused step that changes nothing.
    //   2. The current state advances by dtS (a crossfade's old state too)
    //      and fires its events, written to `events` up to its size.
    //   3. The current state's transitions are tried, in order of priority
    //      (higher first), then those with a named `from` before "*", then
    //      graph order; the first whose conditions all hold is taken. Number
    //      and bool conditions read the parameters as set before this step;
    //      event conditions see every event step 2 fired (also those that did
    //      not fit in `events`); clipEnd holds once a non-looping state has
    //      reached its end. At most one transition per step.
    //   4. A transition enters its target state at time 0 and starts with the
    //      transition's duration and mode as AnimationPlayer::play does (the
    //      output keeps its velocity when inertialized); pose() is then still
    //      step 2's output, also for a duration of 0, whose new state shows
    //      from the next step. Without one, pose() is step 2's output.
    // Root motion is the current state's; during a crossfade the two states'
    // deltas blend by the fade weight, as in AnimationPlayer.
    //
    // A clip state plays its clip at the state's speed, looping or holding the
    // end. A 1D blend state: with parameter p and samples at a_0 < ... < a_n,
    // p <= a_0 plays sample 0 alone, p >= a_n the last alone, and between
    // a_k and a_k+1 the two blend with weight (p - a_k) / (a_k+1 - a_k) for
    // the second (weights read at the start of the step). The samples stay in
    // step by one phase in [0, 1): the blend's duration is the weighted mean
    // of the samples' durations, and each active sample advances its own clip
    // time by speed * dtS * its duration / that duration, so its clip-time
    // spans join exactly from step to step (events once per pass); a sample
    // becoming active starts at phase * its own duration, and the phase follows
    // the sample with the largest weight (the first on ties), whose end also
    // ends a non-looping blend. Pose: blendPoses of the two samples. Root
    // motion: the samples' deltas over their clip-time spans, weighted.
    // Events: those of the sample with the largest weight. For an inertialized
    // switch into a blend, the blend one step before its start is sampled at
    // phase -speed * dt / its duration (dt of the last step). Allocates
    // nothing.
    GraphStep advance(double dtS, std::span<FiredEvent> events);

    [[nodiscard]] std::span<const math::Transform> pose() const { return pose_; }
    [[nodiscard]] std::size_t currentState() const { return current_.state; }
    [[nodiscard]] const std::string& currentStateId() const;
    [[nodiscard]] bool inTransition() const;
    [[nodiscard]] const AnimGraph& graph() const { return graph_; }

private:
    GraphInstance() = default;

    // A state being played: its index and its clock (clip time for a clip
    // state, phase for a blend).
    static constexpr std::size_t kNoSample = static_cast<std::size_t>(-1);
    struct StatePlay {
        std::size_t state = 0;
        double timeS = 0.0;
        double phase = 0.0;
        bool ended = false; // non-looping state reached its end
        // A blend's active samples keep their own clip time, advanced
        // continuously like a player's, so each step's clip-time span starts
        // exactly where the last one ended (events fire once per pass); a
        // sample becoming active starts at phase * its duration.
        std::array<std::size_t, 2> slotSample{kNoSample, kNoSample};
        std::array<double, 2> slotTimeS{0.0, 0.0};
    };

    // The clips a state plays: one for a clip state; the samples (and their
    // `at` values and the blend parameter's index) for a blend.
    struct StateClips {
        std::vector<std::shared_ptr<const ClipBinding>> samples;
        std::vector<double> at;
        int parameter = -1; // -1 for a clip state
    };
    struct ResolvedCondition {
        ConditionKind kind = ConditionKind::Parameter;
        int parameter = -1;
        CompareOp op = CompareOp::Equal;
        double value = 0.0;
        std::string event;
    };
    struct ResolvedTransition {
        std::size_t from = 0; // unused when any
        std::size_t to = 0;
        bool any = false; // from "*"
        std::vector<ResolvedCondition> conditions;
    };
    // A blend's two active samples and the second's weight (equal indices
    // when one sample plays alone).
    struct BlendWeights {
        std::size_t first = 0;
        std::size_t second = 0;
        double secondWeight = 0.0;
    };
    // The clip-time span a step covered on the clip events come from.
    struct EventSpan {
        const ClipBinding* clip = nullptr;
        TimeMode mode = TimeMode::Loop;
        double fromS = 0.0;
        double deltaS = 0.0;
    };
    struct StateStep {
        PlayerStep step;
        EventSpan span;
    };

    [[nodiscard]] BlendWeights weights(std::size_t state) const;
    [[nodiscard]] double blendDuration(std::size_t state, const BlendWeights& w) const;
    // A blend sample's clip time: its own if it is an active slot, else
    // phase * its duration.
    static double sampleTime(const StatePlay& play, std::size_t sample, double durationS);
    // Samples a state at its clock plus an offset (clip seconds for a clip
    // state, phase for a blend) into out.
    void sampleState(const StatePlay& play, double offset, std::span<math::Transform> out);
    StateStep stepState(StatePlay& play, double dt, std::span<FiredEvent> events);
    static bool firedIn(const EventSpan& span, std::string_view name);
    [[nodiscard]] bool holds(const ResolvedTransition& t, const EventSpan& span) const;
    void enter(std::size_t transitionIndex);

    AnimGraph graph_;
    std::vector<StateClips> states_;
    std::vector<ResolvedTransition> transitions_;
    std::vector<std::vector<std::size_t>> transitionsOfState_; // in trial order
    std::vector<double> parameters_;
    StatePlay current_;
    StatePlay outgoing_; // crossfade only
    Inertializer inertializer_{0};
    double fadeElapsedS_ = 0.0;
    double fadeDurationS_ = 0.0;
    bool fading_ = false;
    bool outgoingFrozen_ = false;
    bool cutPending_ = false; // a zero-length transition shows its state next step
    double lastDtS_ = 0.0;
    HelperSolver helperSolver_{rig::Rig{}};
    double exertion_ = 0.0;
    double timeS_ = 0.0;
    std::vector<math::Transform> pose_;
    std::vector<math::Transform> previousPose_;
    std::vector<math::Transform> incoming_; // the current state's pose
    std::vector<math::Transform> fadeFrom_; // the pose faded from, or scratch
    std::vector<math::Transform> scratchA_;
    std::vector<math::Transform> scratchB_;
};

} // namespace opas::anim
