#include "opas/anim/Inertialization.hpp"

#include <algorithm>
#include <cmath>

namespace opas::anim
{
    namespace
    {

        // Offsets shorter than this (metres, or radians) count as none: there is no
        // direction to decay along.
        constexpr double kMinOffset = 1e-12;

        // The time rule from Bollo (GDC 2018): when the offset already moves toward
        // zero, the transition is shortened so the curve cannot overshoot.
        constexpr double kShortenFactor = 5.0;

        // A curve shorter than this (seconds) is no curve: the offset is treated as
        // already gone, rather than dividing by a vanishing time.
        constexpr double kMinCurveS = 1e-9;
        // Elapsed time is summed per step, so 30 steps of 1/60 s give
        // 0.49999999999999994; within this of the end the transition counts as over.
        constexpr double kEndToleranceS = 1e-9;

        double finiteOrZero(double v)
        {
            return std::isfinite(v) ? v : 0.0;
        }

        // The angle of a rotation about `axis` (signed); continuous past pi for a
        // quaternion already aligned to the hemisphere of the one the axis came from.
        double angleAbout(math::Quat q, math::Vec3 axis)
        {
            const double along = q.x * axis.x + q.y * axis.y + q.z * axis.z;
            return 2.0 * std::atan2(along, q.w);
        }

    } // namespace

    Inertializer::Inertializer(std::size_t boneCount)
        : bones_(boneCount)
    {
    }

    Inertializer::Curve Inertializer::makeCurve(double x0, double v0, double durationS)
    {
        Curve c;
        if (!(x0 > kMinOffset))
        {
            return c; // no offset: zero from the start
        }
        c.x0 = x0;
        c.v0 = std::min(finiteOrZero(v0), 0.0); // only toward zero (header)
        double t = durationS;
        if (c.v0 < 0.0)
        {
            t = std::min(t, -kShortenFactor * x0 / c.v0);
        }
        if (!(t >= kMinCurveS))
        {
            return Curve{};
        }
        c.durationS = t;
        c.a0 = std::max(0.0, (-8.0 * c.v0 * t - 20.0 * x0) / (t * t));
        // The quintic with x(T) = x'(T) = x''(T) = 0 for the given x0, v0, a0.
        const double t2 = t * t;
        const double t3 = t2 * t;
        c.c3 = -(3.0 * c.a0 * t2 + 12.0 * c.v0 * t + 20.0 * x0) / (2.0 * t3);
        c.c4 = (3.0 * c.a0 * t2 + 16.0 * c.v0 * t + 30.0 * x0) / (2.0 * t3 * t);
        c.c5 = -(c.a0 * t2 + 6.0 * c.v0 * t + 12.0 * x0) / (2.0 * t3 * t2);
        return c;
    }

    double Inertializer::evaluate(const Curve& c, double t)
    {
        if (!(t < c.durationS))
        {
            return 0.0;
        }
        return ((((c.c5 * t + c.c4) * t + c.c3) * t + 0.5 * c.a0) * t + c.v0) * t + c.x0;
    }

    void Inertializer::start(std::span<const math::Transform> previousOutgoing,
        std::span<const math::Transform> outgoing, std::span<const math::Transform> incoming, double dtS,
        double durationS)
    {
        start(previousOutgoing, outgoing, incoming, incoming, dtS, durationS);
    }

    void Inertializer::start(std::span<const math::Transform> previousOutgoing,
        std::span<const math::Transform> outgoing, std::span<const math::Transform> previousIncoming,
        std::span<const math::Transform> incoming, double dtS, double durationS)
    {
        const std::size_t n = bones_.size();
        if (!(std::isfinite(durationS) && durationS > 0.0) || previousOutgoing.size() != n || outgoing.size() != n
            || previousIncoming.size() != n || incoming.size() != n)
        {
            clear();
            return;
        }
        const bool haveVelocity = std::isfinite(dtS) && dtS > 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            BoneOffset& bone = bones_[i];
            bone = BoneOffset{};

            // Translation: a direction and a length.
            const math::Vec3 d = outgoing[i].translation - incoming[i].translation;
            const double x0 = math::length(d);
            if (x0 > kMinOffset)
            {
                bone.direction = d / x0;
                double v0 = 0.0;
                if (haveVelocity)
                {
                    const double xPrev
                        = math::dot(previousOutgoing[i].translation - previousIncoming[i].translation, bone.direction);
                    v0 = (x0 - xPrev) / dtS;
                }
                bone.translation = makeCurve(x0, v0, durationS);
            }

            // Rotation: an axis and an angle, shortest path.
            math::Quat q0 = outgoing[i].rotation * math::conjugate(incoming[i].rotation);
            if (q0.w < 0.0)
            {
                q0 = math::Quat{ -q0.x, -q0.y, -q0.z, -q0.w };
            }
            const double s = std::sqrt(q0.x * q0.x + q0.y * q0.y + q0.z * q0.z);
            const double angle = 2.0 * std::atan2(s, q0.w);
            if (angle > kMinOffset)
            {
                bone.axis = math::Vec3{ q0.x, q0.y, q0.z } / s;
                double v0 = 0.0;
                if (haveVelocity)
                {
                    math::Quat qPrev = previousOutgoing[i].rotation * math::conjugate(previousIncoming[i].rotation);
                    if (math::dot(qPrev, q0) < 0.0)
                    {
                        qPrev = math::Quat{ -qPrev.x, -qPrev.y, -qPrev.z, -qPrev.w };
                    }
                    v0 = (angle - angleAbout(qPrev, bone.axis)) / dtS;
                }
                bone.rotation = makeCurve(angle, v0, durationS);
            }
        }
        durationS_ = durationS;
        elapsedS_ = 0.0;
        active_ = true;
    }

    void Inertializer::apply(std::span<math::Transform> pose) const
    {
        if (!active_ || pose.size() != bones_.size())
        {
            return;
        }
        for (std::size_t i = 0; i < bones_.size(); ++i)
        {
            const BoneOffset& bone = bones_[i];
            math::Transform& t = pose[i];
            const double move = evaluate(bone.translation, elapsedS_);
            if (move != 0.0)
            {
                t.translation = t.translation + bone.direction * move;
            }
            const double turn = evaluate(bone.rotation, elapsedS_);
            if (turn != 0.0)
            {
                t.rotation = math::normalized(math::fromAxisAngle(bone.axis, turn) * t.rotation);
            }
        }
    }

    void Inertializer::advanceAndApply(double dtS, std::span<math::Transform> pose)
    {
        if (!active_)
        {
            return;
        }
        elapsedS_ += std::isfinite(dtS) && dtS > 0.0 ? dtS : 0.0;
        if (!(elapsedS_ < durationS_ - kEndToleranceS))
        {
            clear();
            return;
        }
        apply(pose);
    }

    void Inertializer::clear()
    {
        active_ = false;
        elapsedS_ = 0.0;
        durationS_ = 0.0;
    }

} // namespace opas::anim
