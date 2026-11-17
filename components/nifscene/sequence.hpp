#ifndef OPENEDEN_COMPONENTS_NIFSCENE_SEQUENCE_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_SEQUENCE_HPP

// Task 0232: playing a NiControllerSequence from a Nif::Scene, as pure maths. A sequence names nodes and gives each a
// transform track (translation, rotation, scale keys). `delta` answers: how far has this node moved from where the
// file's rest pose puts it, expressed in the model's own space, so that a renderer can draw the node's baked shapes
// under that matrix and a physics shape can follow it. Nested animated nodes are handled because the delta uses the
// whole parent chain.
//
// Matrices are column-major, translation in elements 12 to 14 (the layout vsg::mat4 and Jolt's Mat44 take).
// No engine, renderer or NIF record type appears here. Threads: pure. Allocation: `sample` and `delta` allocate
// nothing.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "scene.hpp"

namespace NifScene::Sequences
{
    using Mat4 = std::array<float, 16>;

    constexpr std::size_t sNoSequence = static_cast<std::size_t>(-1);

    [[nodiscard]] Mat4 identity();
    [[nodiscard]] Mat4 toMat4(const Transform& transform);
    [[nodiscard]] Mat4 multiply(const Mat4& left, const Mat4& right); // left * right
    /// The inverse of a rotation + uniform scale + translation matrix (what toMat4 makes).
    [[nodiscard]] Mat4 inverseAffine(const Mat4& matrix);

    /// The index of the first sequence with this name (ASCII case-insensitive), else sNoSequence.
    [[nodiscard]] std::size_t find(const Scene& scene, std::string_view name);

    /// Seconds one run of the sequence takes (stop - start over the frequency); 0 for a bad sequence.
    [[nodiscard]] float duration(const Sequence& sequence);

    /// The sequence's own time for `elapsed` seconds since it started: start + elapsed * frequency, held at stop
    /// (and at start for a negative elapsed). Looping and reversing are the player's business, not the door's.
    [[nodiscard]] float timeAt(const Sequence& sequence, float elapsed);

    /// Which sequence and which sequence time show a door or lid that is `progress` open (0 closed, 1 open, clamped).
    /// Opening, or closing with no Close sequence: the Open sequence at start + progress * length. Closing with a Close
    /// sequence: the Close sequence at start + (1 - progress) * length (its first pose is the open one). A bad
    /// `open` index gives sequence sNoSequence.
    struct Pose
    {
        std::size_t mSequence = sNoSequence;
        float mTime = 0.f;
    };
    [[nodiscard]] Pose poseFor(const Scene& scene, std::size_t open, std::size_t close, float progress, bool closing);

    /// One step of a door or lid: `progress` (0 closed .. 1 open) moves toward open (true) or closed (false) at one
    /// full run per `seconds`. A bad dt changes nothing; a `seconds` that is not positive jumps to the target.
    [[nodiscard]] float stepProgress(float progress, bool open, float dt, float seconds);

    /// The local transform of a node at `time`: the track's keys where it has them, else the interpolator's own
    /// value, else `rest`. A channel value that is not a number or beyond 1e30 (the NIF's "no value" marker) is
    /// treated as absent.
    [[nodiscard]] Transform sampleLocal(const Track& track, const Transform& rest, float time);

    /// The nodes the sequence moves: the first node of each track's name, in track order, without repeats. A track
    /// whose name matches no node is skipped.
    [[nodiscard]] std::vector<std::uint32_t> animatedNodes(const Scene& scene, const Sequence& sequence);

    /// The model-space matrix of a node in the file's own rest pose, and with the sequence at `time`.
    [[nodiscard]] Mat4 restWorld(const Scene& scene, std::uint32_t node);
    [[nodiscard]] Mat4 animatedWorld(const Scene& scene, const Sequence& sequence, std::uint32_t node, float time);

    /// animatedWorld * inverse(restWorld): a point of the node's baked geometry (in model space at rest) moved to
    /// where the sequence puts it at `time`. Identity at a time that matches the rest pose.
    [[nodiscard]] Mat4 delta(const Scene& scene, const Sequence& sequence, std::uint32_t node, float time);
}

#endif
