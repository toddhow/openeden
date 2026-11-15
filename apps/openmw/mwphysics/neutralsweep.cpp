#include "neutralsweep.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics::NeutralSweep
{
    using openeden::sdk::game::SweepHit;
    using openeden::sdk::game::SweepRequest;
    using openeden::sdk::game::SweepTarget;

    namespace
    {
        bool before(const SweepHit& a, const SweepHit& b)
        {
            if (a.distance != b.distance)
                return a.distance < b.distance;
            if (a.target != b.target)
                return a.target < b.target; // Actor (0) before Player (1)
            if (a.actor.reference != b.actor.reference)
                return a.actor.reference < b.actor.reference;
            return a.actor.session < b.actor.session;
        }
    }

    std::size_t arcSweep(const SweepRequest& request, const Self& self, std::span<const Candidate> candidates,
        std::span<SweepHit> out, std::size_t* dropped)
    {
        if (dropped != nullptr)
            *dropped = 0;
        const std::size_t capacity = std::min(out.size(), openeden::sdk::game::kMaxSweepHits);
        if (capacity == 0)
            return 0;
        const double forwardX = std::sin(static_cast<double>(request.heading));
        const double forwardY = std::cos(static_cast<double>(request.heading));
        const double bandLow = request.z - static_cast<double>(request.heightBelow);
        const double bandHigh = request.z + static_cast<double>(request.heightAbove);
        std::size_t count = 0;
        for (const Candidate& c : candidates)
        {
            if (!std::isfinite(c.mX) || !std::isfinite(c.mY) || !std::isfinite(c.mZ) || !std::isfinite(c.mRadius)
                || !std::isfinite(c.mHeight) || c.mRadius <= 0.f)
                continue;
            if (c.mTarget == SweepTarget::Player ? self.mPlayer : (!self.mPlayer && c.mActor == self.mActor))
                continue;
            // The capsule's vertical extent must meet the band.
            if (c.mZ + static_cast<double>(c.mHeight) < bandLow || c.mZ > bandHigh)
                continue;
            const double dx = c.mX - request.x;
            const double dy = c.mY - request.y;
            const double centreDistance = std::sqrt(dx * dx + dy * dy);
            const double radius = static_cast<double>(c.mRadius);
            const double surface = std::max(0.0, centreDistance - radius);
            if (surface > static_cast<double>(request.reach))
                continue;
            if (centreDistance > radius)
            {
                // The angle between the centre line and the capsule's centre, less the angle the body itself covers.
                const double cosine = std::clamp((dx * forwardX + dy * forwardY) / centreDistance, -1.0, 1.0);
                const double angle = std::acos(cosine);
                const double covered = std::asin(std::min(1.0, radius / centreDistance));
                if (angle - covered > static_cast<double>(request.halfAngle) + 1e-9)
                    continue;
            }
            SweepHit hit;
            hit.target = c.mTarget;
            hit.actor = c.mTarget == SweepTarget::Actor ? c.mActor : openeden::sdk::game::ActorHandle{};
            // The touched point: on the capsule's side facing the attacker, at the height the band and the body share.
            const double towardX = centreDistance > 0.0 ? dx / centreDistance : 0.0;
            const double towardY = centreDistance > 0.0 ? dy / centreDistance : 0.0;
            const double inset = std::min(radius, centreDistance);
            hit.x = c.mX - towardX * inset;
            hit.y = c.mY - towardY * inset;
            hit.z = std::clamp(request.z, c.mZ, c.mZ + static_cast<double>(c.mHeight));
            hit.distance = static_cast<float>(surface);
            // Insertion into the sorted prefix; beyond the capacity the farthest falls out.
            std::size_t at = count;
            while (at > 0 && before(hit, out[at - 1]))
                --at;
            if (count < capacity)
            {
                for (std::size_t i = count; i > at; --i)
                    out[i] = out[i - 1];
                out[at] = hit;
                ++count;
            }
            else
            {
                if (dropped != nullptr)
                    ++*dropped;
                if (at == capacity)
                    continue;
                for (std::size_t i = capacity - 1; i > at; --i)
                    out[i] = out[i - 1];
                out[at] = hit;
            }
        }
        return count;
    }
}
