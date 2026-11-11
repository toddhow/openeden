#include <components/inertialmotion/footlock.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    using namespace InertialMotion;

    constexpr float sDt = 1.f / 60.f;

    float distance(const vsg::vec2& a, const vsg::vec2& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    // What the solver is asked for: the animated ankle moved toward the lock by the weight.
    vsg::vec2 blended(const FootLockResult& r, const vsg::vec2& animated)
    {
        return { animated.x + (r.mTarget.x - animated.x) * r.mWeight,
            animated.y + (r.mTarget.y - animated.y) * r.mWeight };
    }

    // Engages a lock on a still, planted foot at lastShown and runs until the weight has settled.
    FootLockResult engage(FootLock& lock, const vsg::vec2& animated, const vsg::vec2& lastShown)
    {
        const FootLockParameters params;
        FootLockResult r = lock.update(animated, lastShown, true, 0.f, sDt, params);
        for (int i = 0; i < 30; ++i)
            r = lock.update(animated, lastShown, true, 0.f, sDt, params);
        return r;
    }

    TEST(FootLock, DoesNothingWhileTheBodyMoves)
    {
        FootLock lock;
        const FootLockParameters params;
        for (int i = 0; i < 120; ++i)
        {
            const FootLockResult r = lock.update({ 5.f, 7.f }, { 5.f, 7.f }, true, 100.f, sDt, params);
            EXPECT_EQ(r.mWeight, 0.f);
            EXPECT_EQ(r.mTarget.x, 5.f);
            EXPECT_EQ(r.mTarget.y, 7.f);
        }
        EXPECT_FALSE(lock.isEngaged());
    }

    TEST(FootLock, DoesNothingForAFootThatIsNotOnTheGround)
    {
        FootLock lock;
        const FootLockParameters params;
        for (int i = 0; i < 120; ++i)
        {
            const FootLockResult r = lock.update({ 5.f, 7.f }, { 5.f, 7.f }, false, 0.f, sDt, params);
            EXPECT_EQ(r.mWeight, 0.f);
        }
        EXPECT_FALSE(lock.isEngaged());
    }

    TEST(FootLock, EngagesAtTheShownPositionSoNothingPops)
    {
        // The animation has already blended 6 units away from where the foot was drawn last frame.
        FootLock lock;
        const FootLockParameters params;
        const FootLockResult r = lock.update({ 6.f, 0.f }, { 0.f, 0.f }, true, 0.f, sDt, params);
        EXPECT_TRUE(lock.isEngaged());
        // the hold starts where the foot was drawn, then drifts by at most one frame's worth of slide
        EXPECT_NEAR(r.mTarget.x, 0.f, params.mSlideSpeed * sDt + 1e-4f);
        EXPECT_FLOAT_EQ(r.mTarget.y, 0.f);
        EXPECT_GT(r.mWeight, 0.f);
        EXPECT_LT(r.mWeight, 1.f);
    }

    TEST(FootLock, AShownPositionFarFromTheAnimatedOneIsNotAPlaceToHold)
    {
        // A teleport: the foot was drawn 500 units away last frame. The hold starts at the animated position.
        FootLock lock;
        const FootLockParameters params;
        const FootLockResult r = lock.update({ 6.f, 0.f }, { 500.f, 0.f }, true, 0.f, sDt, params);
        EXPECT_NEAR(r.mTarget.x, 6.f, 1e-4f);
        EXPECT_NEAR(r.mTarget.y, 0.f, 1e-4f);
        // and a pose that moved only a little since last frame still starts at the shown position
        FootLock other;
        const FootLockResult near = other.update({ 6.f, 0.f }, { 0.f, 0.f }, true, 0.f, sDt, params);
        EXPECT_NEAR(near.mTarget.x, 0.f, params.mSlideSpeed * sDt + 1e-4f);
    }

    TEST(FootLock, WeightReachesOneWithinTwoTenthsOfASecond)
    {
        FootLock lock;
        const FootLockParameters params;
        FootLockResult r{};
        for (int i = 0; i < 12; ++i)
            r = lock.update({ 3.f, 0.f }, { 3.f, 0.f }, true, 0.f, sDt, params);
        EXPECT_GT(r.mWeight, 0.99f);
    }

    TEST(FootLock, TheLockSlidesTowardTheAnimatedFootNoFasterThanTheSlideSpeed)
    {
        FootLock lock;
        const FootLockParameters params;
        const vsg::vec2 animated(14.f, 0.f);
        FootLockResult r = engage(lock, animated, { 0.f, 0.f });
        vsg::vec2 previous = r.mTarget;
        float most = 0.f;
        for (int i = 0; i < 600; ++i)
        {
            r = lock.update(animated, previous, true, 0.f, sDt, params);
            most = std::max(most, distance(r.mTarget, previous) / sDt);
            previous = r.mTarget;
        }
        EXPECT_LE(most, params.mSlideSpeed + 1e-3f);
        // and it gets there: the foot is not held forever
        EXPECT_LT(distance(previous, animated), 1e-3f);
    }

    TEST(FootLock, TheSlideTakesAsLongAsTheSlideSpeedSays)
    {
        FootLock lock;
        const FootLockParameters params;
        const vsg::vec2 animated(12.f, 0.f);
        FootLockResult r = engage(lock, animated, { 0.f, 0.f });
        const float remaining = distance(r.mTarget, animated);
        int frames = 0;
        while (distance(r.mTarget, animated) > 1e-3f && frames < 6000)
        {
            r = lock.update(animated, r.mTarget, true, 0.f, sDt, params);
            ++frames;
        }
        const float seconds = static_cast<float>(frames) * sDt;
        EXPECT_NEAR(seconds, remaining / params.mSlideSpeed, 0.05f);
    }

    TEST(FootLock, TheSlideDistanceDoesNotDependOnTheFrameRate)
    {
        const FootLockParameters params;
        float travelled[3];
        const float steps[3] = { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f };
        for (int k = 0; k < 3; ++k)
        {
            FootLock lock;
            const vsg::vec2 animated(40.f, 0.f);
            // engage in one step, then measure one second of sliding
            FootLockResult r = lock.update(animated, { 0.f, 0.f }, true, 0.f, steps[k], params);
            for (float t = 0.f; t < 0.5f; t += steps[k])
                r = lock.update(animated, r.mTarget, true, 0.f, steps[k], params);
            const vsg::vec2 start = r.mTarget;
            for (float t = 0.f; t < 1.f - 1e-4f; t += steps[k])
                r = lock.update(animated, r.mTarget, true, 0.f, steps[k], params);
            travelled[k] = distance(r.mTarget, start);
        }
        EXPECT_NEAR(travelled[0], travelled[1], 0.5f);
        EXPECT_NEAR(travelled[2], travelled[1], 0.5f);
    }

    TEST(FootLock, TheLockNeverStaysFartherThanTheMaxDistanceFromTheAnimatedFoot)
    {
        FootLock lock;
        const FootLockParameters params;
        engage(lock, { 0.f, 0.f }, { 0.f, 0.f });
        // the animation jumps 50 units away (a turn in place, a teleported pose)
        const vsg::vec2 animated(50.f, 0.f);
        FootLockResult r = lock.update(animated, { 0.f, 0.f }, true, 0.f, sDt, params);
        EXPECT_LE(distance(r.mTarget, animated), params.mMaxDistance + 1e-3f);
        for (int i = 0; i < 100; ++i)
        {
            r = lock.update(animated, r.mTarget, true, 0.f, sDt, params);
            EXPECT_LE(distance(r.mTarget, animated), params.mMaxDistance + 1e-3f);
        }
    }

    TEST(FootLock, ReleasesWhenTheBodyStartsToMove)
    {
        FootLock lock;
        const FootLockParameters params;
        engage(lock, { 0.f, 0.f }, { 0.f, 0.f });
        ASSERT_TRUE(lock.isEngaged());
        FootLockResult r{};
        for (int i = 0; i < 30; ++i)
            r = lock.update({ 4.f, 0.f }, { 0.f, 0.f }, true, 120.f, sDt, params);
        EXPECT_EQ(r.mWeight, 0.f);
        EXPECT_FALSE(lock.isEngaged());
        EXPECT_EQ(r.mTarget.x, 4.f);
    }

    TEST(FootLock, ReleasesWhenTheFootLiftsOff)
    {
        FootLock lock;
        const FootLockParameters params;
        engage(lock, { 0.f, 0.f }, { 0.f, 0.f });
        FootLockResult r{};
        for (int i = 0; i < 30; ++i)
            r = lock.update({ 4.f, 0.f }, { 0.f, 0.f }, false, 0.f, sDt, params);
        EXPECT_EQ(r.mWeight, 0.f);
        EXPECT_FALSE(lock.isEngaged());
    }

    TEST(FootLock, WeightStaysBetweenZeroAndOneAndTheBlendBetweenTheTwoPositions)
    {
        FootLock lock;
        const FootLockParameters params;
        const vsg::vec2 animated(9.f, -3.f);
        vsg::vec2 shown(0.f, 0.f);
        for (int i = 0; i < 600; ++i)
        {
            const bool still = (i / 100) % 2 == 0;
            const FootLockResult r = lock.update(animated, shown, true, still ? 0.f : 90.f, sDt, params);
            EXPECT_GE(r.mWeight, 0.f);
            EXPECT_LE(r.mWeight, 1.f);
            const vsg::vec2 b = blended(r, animated);
            EXPECT_TRUE(std::isfinite(b.x) && std::isfinite(b.y));
            // between the animated foot and the lock target (both ends included)
            EXPECT_LE(distance(b, animated), distance(r.mTarget, animated) + 1e-3f);
            shown = b;
        }
    }

    TEST(FootLock, AMovingBodyLetsTheLockCatchUpFasterThanAStillOne)
    {
        const FootLockParameters params;
        auto closing = [&](float bodySpeed) {
            FootLock lock;
            engage(lock, { 0.f, 0.f }, { 0.f, 0.f });
            // one frame at the given body speed with the animated foot 12 units away
            const FootLockResult r = lock.update({ 12.f, 0.f }, { 0.f, 0.f }, true, bodySpeed, sDt, params);
            return distance(r.mTarget, { 0.f, 0.f });
        };
        EXPECT_GT(closing(60.f), closing(0.f));
    }

    TEST(FootLock, InvalidInputsChangeNothing)
    {
        FootLock lock;
        const FootLockParameters params;
        const FootLockResult before = engage(lock, { 2.f, 0.f }, { 0.f, 0.f });
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (float dt : { 0.f, -1.f, nan, std::numeric_limits<float>::infinity() })
        {
            const FootLockResult r = lock.update({ 2.f, 0.f }, { 0.f, 0.f }, true, 0.f, dt, params);
            EXPECT_EQ(r.mWeight, before.mWeight);
            EXPECT_EQ(r.mTarget.x, before.mTarget.x);
            EXPECT_EQ(r.mTarget.y, before.mTarget.y);
        }
        const FootLockResult r = lock.update({ nan, 0.f }, { 0.f, nan }, true, 0.f, sDt, params);
        EXPECT_TRUE(std::isfinite(r.mTarget.x) && std::isfinite(r.mTarget.y) && std::isfinite(r.mWeight));
    }

    TEST(FootLock, ResetForgetsTheLock)
    {
        FootLock lock;
        engage(lock, { 0.f, 0.f }, { 0.f, 0.f });
        lock.reset();
        EXPECT_FALSE(lock.isEngaged());
        const FootLockParameters params;
        const FootLockResult r = lock.update({ 3.f, 1.f }, { 3.f, 1.f }, true, 100.f, sDt, params);
        EXPECT_EQ(r.mWeight, 0.f);
    }

    TEST(FootLock, TwoLocksFedTheSameInputsAgreeBitForBit)
    {
        FootLock a;
        FootLock b;
        const FootLockParameters params;
        vsg::vec2 shownA(0.f, 0.f);
        vsg::vec2 shownB(0.f, 0.f);
        for (int i = 0; i < 500; ++i)
        {
            const vsg::vec2 animated(std::sin(static_cast<float>(i) * 0.05f) * 9.f, 1.f);
            const float speed = (i % 150) < 75 ? 0.f : 60.f;
            const FootLockResult ra = a.update(animated, shownA, i % 40 < 30, speed, sDt, params);
            const FootLockResult rb = b.update(animated, shownB, i % 40 < 30, speed, sDt, params);
            ASSERT_EQ(std::memcmp(&ra, &rb, sizeof(ra)), 0);
            shownA = blended(ra, animated);
            shownB = blended(rb, animated);
        }
    }
}
