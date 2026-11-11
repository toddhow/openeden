// Tests for the pure rotation maths of the ragdoll bone driver (task 0106, stage b): apps/openmw/mwrender/
// ragdollbonemath.hpp. Written from the spec before the header existed.
//
// In plain words: on the frame a ragdoll is made, the offset between its bone axes and the actor's bone axes is
// captured once; after that the actor's bone turns by exactly what the ragdoll's bone turns, so frame one draws the
// animation's pose with no pop.

#include "apps/openmw/mwrender/ragdollbonemath.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

namespace
{
    using MWRender::RagdollBones::bindDelta;
    using MWRender::RagdollBones::compose;
    using MWRender::RagdollBones::drivenPelvisPosition;
    using MWRender::RagdollBones::drivenWorldRotation;
    using MWRender::RagdollBones::localFromWorld;

    vsg::dquat axisAngle(const vsg::dvec3& axis, double angle)
    {
        const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        const double s = std::sin(0.5 * angle) / length;
        return vsg::dquat(axis.x * s, axis.y * s, axis.z * s, std::cos(0.5 * angle));
    }

    // Rotates v by the unit quaternion q (v + 2w(u x v) + 2 u x (u x v)).
    vsg::dvec3 rotate(const vsg::dquat& q, const vsg::dvec3& v)
    {
        const vsg::dvec3 u(q.x, q.y, q.z);
        const vsg::dvec3 t = 2.0 * vsg::cross(u, v);
        return v + q.w * t + vsg::cross(u, t);
    }

    ::testing::AssertionResult sameRotation(const vsg::dquat& a, const vsg::dquat& b, double tolerance = 1e-9)
    {
        for (const vsg::dvec3 axis : { vsg::dvec3(1, 0, 0), vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, 1) })
        {
            const vsg::dvec3 ra = rotate(a, axis);
            const vsg::dvec3 rb = rotate(b, axis);
            if (std::abs(ra.x - rb.x) > tolerance || std::abs(ra.y - rb.y) > tolerance
                || std::abs(ra.z - rb.z) > tolerance)
                return ::testing::AssertionFailure()
                    << "rotations differ on axis (" << axis.x << ", " << axis.y << ", " << axis.z << "): (" << ra.x
                    << ", " << ra.y << ", " << ra.z << ") against (" << rb.x << ", " << rb.y << ", " << rb.z << ")";
        }
        return ::testing::AssertionSuccess();
    }

    vsg::dquat randomRotation(std::mt19937& gen)
    {
        std::uniform_real_distribution<double> unit(-1.0, 1.0);
        vsg::dvec3 axis(unit(gen), unit(gen), unit(gen));
        if (vsg::length(axis) < 1e-3)
            axis = vsg::dvec3(0, 0, 1);
        return axisAngle(axis, unit(gen) * 3.14159265358979);
    }

    TEST(RagdollBoneMath, onTheSpawnFrameTheDrivenRotationIsTheActorsOwn)
    {
        std::mt19937 gen(1234);
        for (int i = 0; i < 200; ++i)
        {
            const vsg::dquat ragdollBone = randomRotation(gen);
            const vsg::dquat actorBone = randomRotation(gen);
            const vsg::dquat delta = bindDelta(ragdollBone, actorBone);
            EXPECT_TRUE(sameRotation(drivenWorldRotation(ragdollBone, delta), actorBone));
        }
    }

    TEST(RagdollBoneMath, aRagdollBoneThatTurnsByQTurnsTheActorBoneByQAboutTheSameWorldAxis)
    {
        std::mt19937 gen(99);
        for (int i = 0; i < 200; ++i)
        {
            const vsg::dquat ragdollStart = randomRotation(gen);
            const vsg::dquat actorStart = randomRotation(gen);
            const vsg::dquat turn = randomRotation(gen); // a world-space turn
            const vsg::dquat delta = bindDelta(ragdollStart, actorStart);
            const vsg::dquat ragdollNow = compose(turn, ragdollStart);
            EXPECT_TRUE(sameRotation(drivenWorldRotation(ragdollNow, delta), compose(turn, actorStart)));
        }
    }

    TEST(RagdollBoneMath, aQuarterTurnAboutZMovesTheActorsForwardAxisFromYToMinusX)
    {
        const vsg::dquat identity(0, 0, 0, 1);
        const vsg::dquat actor = axisAngle(vsg::dvec3(1, 0, 0), 0.7); // some bone orientation
        const vsg::dquat delta = bindDelta(identity, actor);
        const vsg::dquat ragdollNow = axisAngle(vsg::dvec3(0, 0, 1), 0.5 * 3.14159265358979);
        const vsg::dquat driven = drivenWorldRotation(ragdollNow, delta);
        // The bone's own +Y axis in world space: it started at rotate(actor, Y) and has turned a quarter about Z.
        const vsg::dvec3 before = rotate(actor, vsg::dvec3(0, 1, 0));
        const vsg::dvec3 after = rotate(driven, vsg::dvec3(0, 1, 0));
        EXPECT_NEAR(after.x, -before.y, 1e-9);
        EXPECT_NEAR(after.y, before.x, 1e-9);
        EXPECT_NEAR(after.z, before.z, 1e-9);
    }

    TEST(RagdollBoneMath, theLocalRotationComposedWithTheParentGivesTheWantedWorldRotation)
    {
        std::mt19937 gen(7);
        for (int i = 0; i < 200; ++i)
        {
            const vsg::dquat parent = randomRotation(gen);
            const vsg::dquat wanted = randomRotation(gen);
            const vsg::dquat local = localFromWorld(parent, wanted);
            EXPECT_TRUE(sameRotation(compose(parent, local), wanted));
        }
    }

    TEST(RagdollBoneMath, aBoneWithAnIdentityParentKeepsTheWantedRotationAsItsLocal)
    {
        const vsg::dquat wanted = axisAngle(vsg::dvec3(0.2, 0.5, -0.3), 1.1);
        EXPECT_TRUE(sameRotation(localFromWorld(vsg::dquat(0, 0, 0, 1), wanted), wanted));
    }

    TEST(RagdollBoneMath, thePelvisFollowsTheRagdollsDisplacementFromItsSpawn)
    {
        const vsg::dvec3 actorAtSpawn(1000.0, -2000.0, 150.0);
        const vsg::dvec3 ragdollAtSpawn(990.0, -2010.0, 140.0);
        EXPECT_EQ(drivenPelvisPosition(actorAtSpawn, ragdollAtSpawn, ragdollAtSpawn), actorAtSpawn);
        const vsg::dvec3 ragdollNow(1010.0, -2030.0, 40.0);
        const vsg::dvec3 driven = drivenPelvisPosition(actorAtSpawn, ragdollAtSpawn, ragdollNow);
        EXPECT_DOUBLE_EQ(driven.x, 1020.0);
        EXPECT_DOUBLE_EQ(driven.y, -2020.0);
        EXPECT_DOUBLE_EQ(driven.z, 50.0);
    }
}
