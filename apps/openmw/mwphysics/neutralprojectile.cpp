#include "neutralprojectile.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics::NeutralProjectile
{
    namespace
    {
        bool finite(const Vec& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // The smallest t in [0, 1] at which the segment p0 + t d is inside the vertical cylinder of radius r round
        // (cx, cy) and between the heights zLow and zHigh; -1 when it never is. d may be zero in x and y.
        double cylinderHit(const Vec& p0, const Vec& d, double cx, double cy, double r, double zLow, double zHigh)
        {
            const double ox = p0.x - cx;
            const double oy = p0.y - cy;
            const double a = d.x * d.x + d.y * d.y;
            const double c = ox * ox + oy * oy - r * r;
            double tEnter = 0.0;
            double tExit = 1.0;
            if (a > 1e-18)
            {
                const double b = ox * d.x + oy * d.y;
                const double disc = b * b - a * c;
                if (disc < 0.0)
                    return -1.0;
                const double root = std::sqrt(disc);
                tEnter = (-b - root) / a;
                tExit = (-b + root) / a;
            }
            else if (c > 0.0)
                return -1.0; // moving straight up or down outside the cylinder
            // The part of the segment between the two planes.
            if (std::abs(d.z) > 1e-18)
            {
                const double t0 = (zLow - p0.z) / d.z;
                const double t1 = (zHigh - p0.z) / d.z;
                tEnter = std::max(tEnter, std::min(t0, t1));
                tExit = std::min(tExit, std::max(t0, t1));
            }
            else if (p0.z < zLow || p0.z > zHigh)
                return -1.0;
            tEnter = std::max(tEnter, 0.0);
            tExit = std::min(tExit, 1.0);
            return tEnter <= tExit ? tEnter : -1.0;
        }

        // The smallest t in [0, 1] at which the segment is inside the sphere; -1 when never.
        double sphereHit(const Vec& p0, const Vec& d, double cx, double cy, double cz, double r)
        {
            const double ox = p0.x - cx;
            const double oy = p0.y - cy;
            const double oz = p0.z - cz;
            const double a = d.x * d.x + d.y * d.y + d.z * d.z;
            const double c = ox * ox + oy * oy + oz * oz - r * r;
            if (c <= 0.0)
                return 0.0; // starts inside
            if (a <= 1e-18)
                return -1.0;
            const double b = ox * d.x + oy * d.y + oz * d.z;
            const double disc = b * b - a * c;
            if (disc < 0.0)
                return -1.0;
            const double t = (-b - std::sqrt(disc)) / a;
            return (t >= 0.0 && t <= 1.0) ? t : -1.0;
        }

        Vec unit(const Vec& v)
        {
            const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (!(length > 1e-12))
                return {};
            return { v.x / length, v.y / length, v.z / length };
        }
    }

    double segmentCapsule(const Vec& p0, const Vec& p1, const NeutralSweep::Candidate& capsule)
    {
        if (!finite(p0) || !finite(p1) || !std::isfinite(capsule.mX) || !std::isfinite(capsule.mY)
            || !std::isfinite(capsule.mZ) || !std::isfinite(capsule.mRadius) || !std::isfinite(capsule.mHeight)
            || !(capsule.mRadius > 0.f) || capsule.mHeight < 2.f * capsule.mRadius)
            return -1.0;
        const Vec d{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        const double r = static_cast<double>(capsule.mRadius);
        const double bottom = capsule.mZ + r; // centre of the lower cap
        const double top = capsule.mZ + static_cast<double>(capsule.mHeight) - r; // centre of the upper cap
        double best = cylinderHit(p0, d, capsule.mX, capsule.mY, r, bottom, top);
        for (const double centre : { bottom, top })
        {
            const double t = sphereHit(p0, d, capsule.mX, capsule.mY, centre, r);
            if (t >= 0.0 && (best < 0.0 || t < best))
                best = t;
        }
        return best;
    }

    float speedOf(const Shot& shot)
    {
        const Vec& v = shot.mVelocity;
        return static_cast<float>(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    }

    StepResult step(Shot& shot, float dt, std::span<const NeutralSweep::Candidate> candidates, WorldRay* world)
    {
        StepResult result;
        result.mAt = shot.mPosition;
        result.mDirection = unit(shot.mVelocity);
        if (!std::isfinite(dt) || !(dt > 0.f))
            return result;
        const double h = static_cast<double>(dt);
        // Semi-implicit Euler: the velocity first, then the position with the new velocity.
        Vec velocity = shot.mVelocity;
        velocity.z -= static_cast<double>(shot.mGravity) * h;
        Vec end{ shot.mPosition.x + velocity.x * h, shot.mPosition.y + velocity.y * h,
            shot.mPosition.z + velocity.z * h };
        const double length = std::sqrt((end.x - shot.mPosition.x) * (end.x - shot.mPosition.x)
            + (end.y - shot.mPosition.y) * (end.y - shot.mPosition.y)
            + (end.z - shot.mPosition.z) * (end.z - shot.mPosition.z));
        // The range cuts the step short.
        bool expired = false;
        double reach = 1.0; // the fraction of the step that is flown
        if (length > 0.0 && static_cast<double>(shot.mRangeLeft) <= length)
        {
            reach = std::max(0.0, static_cast<double>(shot.mRangeLeft) / length);
            expired = true;
        }
        const Vec from = shot.mPosition;
        const Vec to{ from.x + (end.x - from.x) * reach, from.y + (end.y - from.y) * reach,
            from.z + (end.z - from.z) * reach };

        // What the segment meets: the nearest actor or player capsule, and the nearest static body.
        double actorT = -1.0;
        const NeutralSweep::Candidate* actor = nullptr;
        for (const NeutralSweep::Candidate& c : candidates)
        {
            const bool isOwner = c.mTarget == openeden::sdk::game::SweepTarget::Player
                ? shot.mOwner.player
                : (!shot.mOwner.player && c.mActor == shot.mOwner.actor);
            if (isOwner)
                continue;
            const double t = segmentCapsule(from, to, c);
            if (t >= 0.0 && (actorT < 0.0 || t < actorT))
            {
                actorT = t;
                actor = &c;
            }
        }
        double worldT = -1.0;
        if (world != nullptr)
        {
            double fraction = 0.0;
            if (world->cast(from, to, fraction) && std::isfinite(fraction) && fraction >= 0.0 && fraction <= 1.0)
                worldT = fraction;
        }

        double hitT = -1.0;
        Outcome outcome = Outcome::Flying;
        if (actorT >= 0.0 && (worldT < 0.0 || actorT <= worldT))
        {
            hitT = actorT;
            outcome = actor->mTarget == openeden::sdk::game::SweepTarget::Player ? Outcome::HitPlayer
                                                                                    : Outcome::HitActor;
            if (outcome == Outcome::HitActor)
                result.mActor = actor->mActor;
        }
        else if (worldT >= 0.0)
        {
            hitT = worldT;
            outcome = Outcome::HitWorld;
        }

        if (hitT >= 0.0)
        {
            const Vec at{ from.x + (to.x - from.x) * hitT, from.y + (to.y - from.y) * hitT,
                from.z + (to.z - from.z) * hitT };
            const double flown = length * reach * hitT;
            shot.mPosition = at;
            shot.mVelocity = velocity;
            shot.mTravelled += static_cast<float>(flown);
            shot.mRangeLeft = std::max(0.f, shot.mRangeLeft - static_cast<float>(flown));
            shot.mAge += static_cast<float>(h * reach * hitT);
            result.mOutcome = outcome;
            result.mAt = at;
            result.mDirection = unit(velocity);
            return result;
        }
        const double flown = length * reach;
        shot.mPosition = to;
        shot.mVelocity = velocity;
        shot.mTravelled += static_cast<float>(flown);
        shot.mRangeLeft = std::max(0.f, shot.mRangeLeft - static_cast<float>(flown));
        shot.mAge += static_cast<float>(h * reach);
        result.mAt = to;
        result.mDirection = unit(velocity);
        result.mOutcome = expired ? Outcome::Expired : Outcome::Flying;
        return result;
    }
}
