#include "opas/anim/SwingTwist.hpp"

#include <cmath>

namespace opas::anim
{

    namespace
    {
        constexpr double kDegenerateTwistThresholdSq = 1e-12;

        bool isCanonicalNegative(math::Quat q)
        {
            if (q.w < 0.0 || std::signbit(q.w))
            {
                return true;
            }
            if (q.w > 0.0)
            {
                return false;
            }
            // w is zero (180 degree rotation): break tie lexicographically on (z, y, x)
            if (q.z < 0.0 || std::signbit(q.z))
            {
                return true;
            }
            if (q.z > 0.0)
            {
                return false;
            }
            if (q.y < 0.0 || std::signbit(q.y))
            {
                return true;
            }
            if (q.y > 0.0)
            {
                return false;
            }
            return q.x < 0.0 || std::signbit(q.x);
        }

    } // namespace

    SwingTwist decomposeSwingTwist(math::Quat q, math::Vec3 twistAxis)
    {
        math::Quat uq = math::normalized(q);
        // Double cover: align to positive canonical hemisphere so atan2 produces
        // identical angles in [-pi, pi] for both q and -q, even at w = 0 (180 deg).
        if (isCanonicalNegative(uq))
        {
            uq = { -uq.x, -uq.y, -uq.z, -uq.w };
        }

        const math::Vec3 u = math::normalized(twistAxis);
        const double dotVU = math::dot(math::Vec3{ uq.x, uq.y, uq.z }, u);
        const double lenSq = dotVU * dotVU + uq.w * uq.w;

        if (lenSq < kDegenerateTwistThresholdSq)
        {
            // Degenerate twist: swing is 180 degrees about an axis perpendicular to twistAxis
            return SwingTwist{
                .swing = uq,
                .twist = math::Quat{},
                .twistAngleRad = 0.0,
            };
        }

        const double twistLen = std::sqrt(lenSq);
        const math::Quat twist{
            (u.x * dotVU) / twistLen,
            (u.y * dotVU) / twistLen,
            (u.z * dotVU) / twistLen,
            uq.w / twistLen,
        };

        const double twistAngleRad = 2.0 * std::atan2(dotVU, uq.w);
        const math::Quat swing = math::normalized(uq * math::conjugate(twist));

        return SwingTwist{
            .swing = swing,
            .twist = twist,
            .twistAngleRad = twistAngleRad,
        };
    }

} // namespace opas::anim
