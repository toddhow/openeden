// Task 0362 item 2: the movement amplitude of a Skyrim light (LIGH DATA +36): a flickering or pulsing neutral light
// wanders round its place. Pure maths of MWAnim::NeutralFlicker::Walk first, then through MWAnim::addLight. No device.
#include <components/animation/autoplay.hpp>
#include <components/animation/pointshadowcarrier.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/light.hpp>
#include <components/mwanimation/lightflicker.hpp>

#include <vsg/lighting/PointLight.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace MWAnim::NeutralFlicker;

    constexpr float kFrame = 1.f / 60.f;

    Params params(float movement, std::uint32_t seed = 42)
    {
        Params p;
        p.mPeriod = 0.5f;
        p.mAmplitude = 0.4f;
        p.mSeed = seed;
        p.mMovement = movement;
        return p;
    }

    TEST(LightMovementWalk, noMovementHasNoOffsetAndTheBrightnessIsTheSame)
    {
        Walk still(params(0.f), false);
        Walk moving(params(6.f), false);
        for (int i = 0; i < 1200; ++i)
        {
            const float a = still.step(kFrame);
            const float b = moving.step(kFrame);
            EXPECT_EQ(a, b) << "the brightness must not depend on the movement amplitude, frame " << i;
            EXPECT_EQ(still.offset(), (std::array<float, 3>{ 0.f, 0.f, 0.f }));
        }
    }

    TEST(LightMovementWalk, theOffsetStaysInsideTheCubeAndReallyMoves)
    {
        for (const bool pulse : { false, true })
        {
            Walk walk(params(8.f), pulse);
            float lo = 100.f, hi = -100.f;
            for (int i = 0; i < 3600; ++i)
            {
                walk.step(kFrame);
                for (const float v : walk.offset())
                {
                    ASSERT_LE(std::fabs(v), 8.f + 1e-4f);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
            EXPECT_LT(lo, -4.f) << pulse;
            EXPECT_GT(hi, 4.f) << pulse;
        }
    }

    TEST(LightMovementWalk, theOffsetIsContinuousAMoveIsEasedNotAJump)
    {
        Walk walk(params(8.f), false);
        std::array<float, 3> last = walk.offset();
        float biggest = 0.f;
        for (int i = 0; i < 3600; ++i)
        {
            walk.step(kFrame);
            for (std::size_t k = 0; k < 3; ++k)
                biggest = std::max(biggest, std::fabs(walk.offset()[k] - last[k]));
            last = walk.offset();
        }
        // a leg of 0.5 s is 30 frames and the largest swing is 16 units: the eased step is at most 1.5 * 16 / 30
        EXPECT_LT(biggest, 1.0f);
    }

    TEST(LightMovementWalk, theSameSeedGivesTheSameOffsetsAndAnotherSeedGivesOthers)
    {
        Walk a(params(8.f, 7), false), b(params(8.f, 7), false), c(params(8.f, 8), false);
        bool differs = false;
        for (int i = 0; i < 600; ++i)
        {
            a.step(kFrame);
            b.step(kFrame);
            c.step(kFrame);
            EXPECT_EQ(a.offset(), b.offset());
            differs = differs || a.offset() != c.offset();
        }
        EXPECT_TRUE(differs);
    }

    TEST(LightMovementWalk, aFastLightMovesNoFasterThanTheSpeedLimit)
    {
        // period 0.08 s with an amplitude of 64: cut to 40 * 0.04 = 1.6 units
        Params p = params(64.f);
        p.mPeriod = 0.08f;
        Walk walk(p, false);
        float biggest = 0.f;
        std::array<float, 3> last = walk.offset();
        for (int i = 0; i < 3600; ++i)
        {
            walk.step(kFrame);
            for (std::size_t k = 0; k < 3; ++k)
            {
                biggest = std::max(biggest, std::fabs(walk.offset()[k]));
                EXPECT_LE(std::fabs(walk.offset()[k] - last[k]) / kFrame, kMaxMovementSpeed * 1.6f);
            }
            last = walk.offset();
        }
        EXPECT_LE(biggest, 1.6f + 1e-3f);
        EXPECT_GT(biggest, 0.5f);
    }

    TEST(LightMovementWalk, aMovementThatIsNotUsableIsNoMovementAndALargeOneIsCut)
    {
        for (const float bad :
            { -3.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            Walk walk(params(bad), false);
            for (int i = 0; i < 100; ++i)
            {
                walk.step(kFrame);
                EXPECT_EQ(walk.offset(), (std::array<float, 3>{ 0.f, 0.f, 0.f })) << bad;
            }
        }
        Walk big(params(1e6f), false);
        for (int i = 0; i < 600; ++i)
        {
            big.step(kFrame);
            for (const float v : big.offset())
                ASSERT_LE(std::fabs(v), kMaxMovement + 1e-4f);
        }
    }

    ESM::Light record(float movement)
    {
        ESM::Light light;
        light.blank();
        light.mData.mFlags = ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralFlicker;
        light.mData.mRadius = 256;
        light.mData.mColor = 0x0040a0e0;
        light.mData.mTime = packTimeField(0.5f, 0.4f);
        light.mData.mValue = 42;
        light.mNeutralMovement = movement;
        return light;
    }

    struct Placed
    {
        vsg::ref_ptr<vsg::Group> mParent = vsg::Group::create();
        Anim::AutoPlay mAutoPlay;
        vsg::PointLight* mLight = nullptr;
        explicit Placed(const ESM::Light& r)
        {
            Anim::Controllers& c = mAutoPlay.getOrCreateGroup().controllers;
            mLight = dynamic_cast<vsg::PointLight*>(MWAnim::addLight(mParent.get(), mParent.get(), r, c));
            EXPECT_NE(mLight, nullptr);
        }
    };

    TEST(LightMovementLight, aMovingLightMovesItsPositionAroundTheOriginalOne)
    {
        Placed placed(record(6.f));
        placed.mLight->position = vsg::dvec3(100.0, 200.0, 300.0);
        float spread = 0.f;
        for (int i = 0; i < 1800; ++i)
        {
            placed.mAutoPlay.update(kFrame);
            const vsg::dvec3 d = placed.mLight->position - vsg::dvec3(100.0, 200.0, 300.0);
            ASSERT_LE(std::fabs(d.x), 6.f + 1e-3f);
            ASSERT_LE(std::fabs(d.y), 6.f + 1e-3f);
            ASSERT_LE(std::fabs(d.z), 6.f + 1e-3f);
            spread = std::max(spread, static_cast<float>(std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z)));
        }
        EXPECT_GT(spread, 3.f);
    }

    TEST(LightMovementLight, aLightWithNoMovementNeverMovesAndNeitherDoesMorrowindsRule)
    {
        Placed still(record(0.f));
        still.mLight->position = vsg::dvec3(1.0, 2.0, 3.0);
        ESM::Light morrowind = record(5.f);
        morrowind.mData.mFlags = ESM::Light::Dynamic | ESM::Light::Flicker; // no NeutralFlicker: Morrowind's rule
        Placed old(morrowind);
        old.mLight->position = vsg::dvec3(1.0, 2.0, 3.0);
        for (int i = 0; i < 600; ++i)
        {
            still.mAutoPlay.update(kFrame);
            old.mAutoPlay.update(kFrame);
            EXPECT_EQ(still.mLight->position, vsg::dvec3(1.0, 2.0, 3.0));
            EXPECT_EQ(old.mLight->position, vsg::dvec3(1.0, 2.0, 3.0));
        }
    }

    TEST(LightMovementLight, aLightThatHoldsAPointShadowSlotDoesNotMoveAndMovesAgainWhenItLetsGo)
    {
        Placed placed(record(6.f));
        placed.mLight->position = vsg::dvec3(0.0, 0.0, 0.0);
        for (int i = 0; i < 120; ++i)
            placed.mAutoPlay.update(kFrame);
        Anim::setPointShadowOwner(3, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(placed.mLight)));
        const vsg::dvec3 held = placed.mLight->position;
        for (int i = 0; i < 600; ++i)
        {
            placed.mAutoPlay.update(kFrame);
            ASSERT_EQ(placed.mLight->position, held);
        }
        Anim::setPointShadowOwner(3, 0);
        bool moved = false;
        for (int i = 0; i < 600; ++i)
        {
            placed.mAutoPlay.update(kFrame);
            moved = moved || placed.mLight->position != held;
        }
        EXPECT_TRUE(moved);
    }

    TEST(LightMovementLight, aBlankRecordHasNoMovement)
    {
        ESM::Light light = record(9.f);
        light.blank();
        EXPECT_EQ(light.mNeutralMovement, 0.f);
    }
}
