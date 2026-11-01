#pragma once

// Inertialized transitions (task 0020, stage C3c), after Bollo,
// "Inertialization: High-Performance Animation Transitions in Gears of War",
// GDC 2018. At a transition the offset from the incoming pose to the
// outgoing pose is recorded per bone and decays to zero over the transition
// time, so only the incoming animation is sampled afterwards.
//
// Per bone, the translation offset is a direction and a length x, and the
// rotation offset (outgoing * conjugate(incoming), shortest path) is an axis
// and an angle x. Each x decays along the quintic in elapsed time t with
//   x(0) = x0, x'(0) = v0, x''(0) = a0, x(T) = x'(T) = x''(T) = 0,
// where v0 is the rate of change of x from the poses one step earlier (the
// offset from the previous incoming pose to the previous outgoing pose,
// projected on the same direction or axis), clamped so the offset only moves
// toward zero (v0 <= 0); T is shortened to -5 x0 / v0 when that is smaller;
// and a0 = max(0, (-8 v0 T - 20 x0) / T^2). The curve depends only on elapsed
// time, so it is the same at any step rate.
//
// Ownership: a plain value; buffers are sized once at construction.

#include "opas/math/Transform.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace opas::anim
{

    class Inertializer
    {
    public:
        explicit Inertializer(std::size_t boneCount); // allocates here only

        // Starts a transition of durationS from `outgoing` to `incoming` (both
        // poses at the transition step); previousOutgoing is the outgoing pose
        // dtS earlier, for the velocity (dtS <= 0 or non-finite means no
        // velocity). durationS <= 0 or non-finite, or any pose not boneCount long,
        // clears instead (no transition). Allocates nothing.
        void start(std::span<const math::Transform> previousOutgoing, std::span<const math::Transform> outgoing,
            std::span<const math::Transform> incoming, double dtS, double durationS);

        // As above, with the incoming pose dtS earlier too, so the offset's rate
        // is the outgoing velocity minus the incoming one and the output
        // (incoming + offset) keeps the outgoing velocity at the switch even when
        // the incoming animation moves. The overload above assumes an incoming
        // pose that is still (previousIncoming = incoming).
        void start(std::span<const math::Transform> previousOutgoing, std::span<const math::Transform> outgoing,
            std::span<const math::Transform> previousIncoming, std::span<const math::Transform> incoming, double dtS,
            double durationS);

        // Adds the offset at the current elapsed time to `pose` (the incoming pose
        // sampled at the same time). Right after start (elapsed 0) this turns the
        // incoming pose into the outgoing pose. Leaves pose unchanged when not
        // active or when pose is not boneCount long. Allocates nothing.
        void apply(std::span<math::Transform> pose) const;

        // Advances elapsed time by dtS (negative or non-finite counts as 0), then
        // applies. Each offset is 0 from its own (possibly shortened) end time on;
        // once elapsed reaches the start's durationS (within 1e-9 s, as summed
        // steps round) the transition ends: pose is left exactly as given and
        // active() is false.
        void advanceAndApply(double dtS, std::span<math::Transform> pose);

        void clear();
        [[nodiscard]] bool active() const { return active_; }
        [[nodiscard]] double elapsedS() const { return elapsedS_; }
        [[nodiscard]] std::size_t boneCount() const { return bones_.size(); }

    private:
        // One decaying scalar: the quintic's coefficients and its end time.
        struct Curve
        {
            double x0 = 0.0;
            double v0 = 0.0;
            double a0 = 0.0;
            double durationS = 0.0;
            double c3 = 0.0;
            double c4 = 0.0;
            double c5 = 0.0;
        };
        struct BoneOffset
        {
            math::Vec3 direction; // unit, or zero when there is no translation offset
            math::Vec3 axis; // unit, or zero when there is no rotation offset
            Curve translation;
            Curve rotation;
        };

        // The curve for offset x0 with rate v0, ending by durationS (header rules).
        static Curve makeCurve(double x0, double v0, double durationS);
        // The offset at elapsed time t; 0 from the curve's end on.
        static double evaluate(const Curve& curve, double t);

        std::vector<BoneOffset> bones_;
        double durationS_ = 0.0;
        double elapsedS_ = 0.0;
        bool active_ = false;
    };

} // namespace opas::anim
