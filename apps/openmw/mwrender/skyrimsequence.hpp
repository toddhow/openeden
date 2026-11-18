#ifndef OPENMW_MWRENDER_SKYRIMSEQUENCE_HPP
#define OPENMW_MWRENDER_SKYRIMSEQUENCE_HPP

// Task 0232: a Skyrim door or chest that swings and opens. The model's NiControllerSequence "Open" (and "Close" when it
// has one) moves some named nodes; this draws those nodes' shapes under one Anim::Transform each and sets that
// transform from the sequence (components/nifscene/sequence.hpp does the maths). The rest of the model is built as the
// static reader builds it.
//
// Why not the neutral actors' path (MWAnim bones and a SkyrimPose controller): an actor's skeleton is a separate file
// of named bones and its skins follow them; here the moving parts are rigid pieces of the model's own node tree, whose
// shapes the flattener has already baked in model space. A part's transform is therefore the DELTA from the rest pose
// in model space (NifScene::Sequences::delta), which is also what a collision shape would follow.
//
// Threads: Model is immutable once made and shared. Instance::run is called from the animation update (a worker, as
// every auto-play controller); setProgress and setTarget come from the main thread and only write atomics.
// Allocation: run and apply allocate nothing.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/nifscene/scene.hpp>
#include <components/nifscene/sequence.hpp>
#include <components/vfs/manager.hpp>

#include "skyrimmodel.hpp"

namespace MWRender::SkyrimSequence
{
    /// A model with an Open sequence that moves at least one node. Immutable.
    struct Model
    {
        /// Only the nodes and sequences are kept (no geometry): what the maths needs.
        NifScene::Scene mScene;
        std::size_t mOpen = NifScene::Sequences::sNoSequence;
        std::size_t mClose = NifScene::Sequences::sNoSequence;
        float mSeconds = 1.f; ///< how long a full opening takes, from the sequence (clamped to a sane range)
        /// Task 0289: the sequences come from Havok clips (a puzzle pillar, a portcullis). Such a model is drawn in the
        /// file's rest pose until a named animation plays: the door progress (setProgress, setTarget) does not apply.
        bool mHavok = false;
        NifScene::Flat::FlatModel mStatic; ///< everything no sequence moves
        struct Part
        {
            std::uint32_t mNode = 0; ///< index into mScene.mNodes
            NifScene::Flat::FlatModel mFlat;
        };
        std::vector<Part> mParts;
    };

    /// Caps (a model with more is not animated).
    inline constexpr std::size_t kMaxParts = 32;
    inline constexpr float kMinSeconds = 0.2f;
    inline constexpr float kMaxSeconds = 3.f;

    /// The model for a Skyrim NIF path, or null when the file cannot be read or has no usable Open sequence. Cached by
    /// path (a null answer too). Thread safe. Never throws.
    [[nodiscard]] std::shared_ptr<const Model> loadModel(
        const VFS::Manager& vfs, const std::string& path, bool surfaceMaps, bool havokClips = false);

    /// The pure half of loadModel: from a scene and the flattener's limits. Null when there is no Open sequence or
    /// nothing it moves draws.
    [[nodiscard]] std::shared_ptr<const Model> makeModel(
        NifScene::Scene scene, const NifScene::Flat::Limits& baseLimits, bool havok = false);

    /// The sequence driver of one door or chest. `parts` pairs a model node with the transform that draws it.
    class Instance : public Anim::Controller
    {
    public:
        using PartList = std::vector<std::pair<std::uint32_t, vsg::ref_ptr<Anim::Transform>>>;

        Instance(std::shared_ptr<const Model> model, PartList parts);

        /// Main thread. Shows the door `progress` open (0..1); closing picks the Close sequence when there is one.
        /// Stops any timed motion.
        void setProgress(float progress, bool closing);
        /// Main thread. The lid moves on its own toward open or closed over the sequence's own time.
        void setTarget(bool open);

        [[nodiscard]] float progress() const { return mApplied.load(std::memory_order_relaxed); }
        [[nodiscard]] const Model& model() const { return *mModel; }

        /// Sets every part's transform for a state. Public so a test can drive it without a clock.
        void apply(float progress, bool closing) const;
        /// Task 0289. Sets every part's transform from `sequence` at its own time `time`. Public for tests.
        void applyAt(const NifScene::Sequence& sequence, float time) const;
        /// Task 0289, main thread. Starts the animation a game script calls `event` (a sequence name such as
        /// "trigger01" or "open", ASCII case-insensitive; "StartState02" holds the first pose of "trigger02"). It plays
        /// once from the next run and holds its end. False when the model has no such sequence.
        bool play(std::string_view event);
        /// Seconds the animation takes; 0 when there is none of that name or it is a held start state.
        [[nodiscard]] float secondsOf(std::string_view event) const;

        void run(vsg::Object& target, float time) const override;

    private:
        std::shared_ptr<const Model> mModel;
        PartList mParts;
        std::atomic<float> mProgress{ 0.f };
        std::atomic<bool> mClosing{ false };
        std::atomic<bool> mTimed{ false };
        std::atomic<bool> mTargetOpen{ false };
        std::atomic<std::size_t> mNamedSequence{ 0 }; ///< task 0289
        std::atomic<bool> mNamedHold{ false };
        std::atomic<std::uint32_t> mNamedSerial{ 0 };
        mutable std::uint32_t mNamedSeen = 0;
        mutable bool mNamedActive = false;
        mutable bool mNamedDone = false;
        mutable float mNamedStart = 0.f;
        mutable std::atomic<float> mApplied{ 0.f };
        mutable bool mHaveApplied = false;
        mutable bool mAppliedClosing = false;
        mutable float mLastTime = -1.f;
        mutable float mTimedProgress = 0.f;
    };

    /// The sequence driver among an animation object's kept references (MWAnim::Object::keepAlive), or null.
    [[nodiscard]] Instance* findInstance(const std::vector<vsg::ref_ptr<vsg::Object>>& keepAlive);

    struct Built
    {
        vsg::ref_ptr<vsg::Node> mNode;
        vsg::ref_ptr<Instance> mInstance;
    };

    /// Draws one instance: the static shapes plus one Anim::Transform per moving part. Not shared between references.
    [[nodiscard]] Built build(const std::shared_ptr<const Model>& model, const SkyrimModel::BuildContext& context);
}

#endif
