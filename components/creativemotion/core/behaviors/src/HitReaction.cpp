#include "opas/behaviors/HitReaction.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace opas::behaviors
{
    namespace
    {

        // Distance of bodies not connected at all (never within reach).
        constexpr int kUnreachable = std::numeric_limits<int>::max();

    } // namespace

    std::optional<HitReaction> HitReaction::create(const rig::Rig& rig, const rig::Profile& profile, std::string* error)
    {
        if (!profile.hitReaction)
        {
            if (error != nullptr)
            {
                *error = "the profile has no hit reaction tuning (profile version 2, \"hitReaction\")";
            }
            return std::nullopt;
        }
        HitReaction reaction;
        reaction.tuning_ = *profile.hitReaction;
        const std::size_t n = rig.bodies.size();
        reaction.bodyCount_ = n;

        // One edge per joint: the joint's body and the body it connects to.
        std::vector<std::vector<std::size_t>> neighbours(n);
        for (const rig::Joint& joint : rig.joints)
        {
            const std::optional<std::size_t> child = rig::findBody(rig, joint.bone);
            const std::optional<std::size_t> parent = child ? rig::parentBody(rig, *child) : std::nullopt;
            if (!child || !parent)
            {
                if (error != nullptr)
                {
                    *error = "joint on \"" + joint.bone + "\" does not connect two bodies";
                }
                return std::nullopt;
            }
            reaction.jointChild_.push_back(*child);
            reaction.jointParent_.push_back(*parent);
            neighbours[*child].push_back(*parent);
            neighbours[*parent].push_back(*child);
        }
        // Breadth-first search from every body, once.
        reaction.distance_.assign(n * n, kUnreachable);
        for (std::size_t from = 0; from < n; ++from)
        {
            std::deque<std::size_t> queue{ from };
            reaction.distance_[from * n + from] = 0;
            while (!queue.empty())
            {
                const std::size_t body = queue.front();
                queue.pop_front();
                for (const std::size_t next : neighbours[body])
                {
                    int& d = reaction.distance_[from * n + next];
                    if (d == kUnreachable)
                    {
                        d = reaction.distance_[from * n + body] + 1;
                        queue.push_back(next);
                    }
                }
            }
        }
        reaction.drop_.assign(rig.joints.size(), 0.0);
        reaction.hitS_.assign(rig.joints.size(), 0.0);
        return reaction;
    }

    int HitReaction::bodyDistance(std::size_t a, std::size_t b) const
    {
        return distance_[a * bodyCount_ + b];
    }

    double HitReaction::remainingDrop(std::size_t rigJoint, double timeS) const
    {
        const double elapsed = std::max(0.0, timeS - hitS_[rigJoint]);
        return drop_[rigJoint] * std::exp(-elapsed / tuning_.recoverTimeS);
    }

    void HitReaction::hit(std::size_t rigBody, double timeS)
    {
        if (rigBody >= bodyCount_)
        {
            return;
        }
        for (std::size_t j = 0; j < drop_.size(); ++j)
        {
            const int d = std::min(bodyDistance(jointChild_[j], rigBody), bodyDistance(jointParent_[j], rigBody));
            if (d > tuning_.reachJoints)
            {
                continue;
            }
            const double fresh = tuning_.dropFraction * std::pow(tuning_.spread, d);
            drop_[j] = std::max(remainingDrop(j, timeS), fresh);
            hitS_[j] = timeS;
        }
    }

    double HitReaction::stiffnessMultiplier(std::size_t rigJoint, double timeS) const
    {
        if (rigJoint >= drop_.size() || drop_[rigJoint] == 0.0)
        {
            return 1.0;
        }
        return 1.0 - remainingDrop(rigJoint, timeS);
    }

} // namespace opas::behaviors
