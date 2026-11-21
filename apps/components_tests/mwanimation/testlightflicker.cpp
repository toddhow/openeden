// Task 0355 stage h: a Skyrim light's own flicker and pulse (components/mwanimation/lightflicker.*, light.cpp).
// Pure maths first (the packed period and amplitude, the seeded walk), then the controller reached through
// MWAnim::addLight as the engine reaches it. No device.
#include <components/animation/autoplay.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/rng.hpp>
#include <components/mwanimation/light.hpp>
#include <components/mwanimation/lightflicker.hpp>

#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace MWAnim::NeutralFlicker;

    constexpr float kFrame = 1.f / 60.f;

    std::vector<float> trace(const Params& p, bool pulse, int frames, float dt = kFrame)
    {
        Walk w(p, pulse);
        std::vector<float> out;
        for (int i = 0; i < frames; ++i)
            out.push_back(w.step(dt));
        return out;
    }

    int turns(const std::vector<float>& v)
    {
        int n = 0;
        for (std::size_t i = 2; i < v.size(); ++i)
            if ((v[i - 1] - v[i - 2]) * (v[i] - v[i - 1]) < 0.f)
                ++n;
        return n;
    }

    TEST(NeutralFlickerPack, aUsablePairRoundTripsToMillisecondsAndThousandths)
    {
        const std::int32_t field = packTimeField(0.3333f, 0.4f);
        ASSERT_NE(field, 0);
        const auto p = unpack(field, 77);
        ASSERT_TRUE(p.has_value());
        EXPECT_NEAR(p->mPeriod, 0.333f, 0.0011f);
        EXPECT_NEAR(p->mAmplitude, 0.4f, 0.0011f);
        EXPECT_EQ(p->mSeed, 77u);
    }

    TEST(NeutralFlickerPack, aPairThatCannotBeUsedPacksToZeroAndUnpacksToNothing)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_EQ(packTimeField(0.f, 0.5f), 0);
        EXPECT_EQ(packTimeField(0.5f, 0.f), 0);
        EXPECT_EQ(packTimeField(-1.f, 0.5f), 0);
        EXPECT_EQ(packTimeField(0.5f, -0.2f), 0);
        EXPECT_EQ(packTimeField(nan, 0.5f), 0);
        EXPECT_EQ(packTimeField(0.5f, nan), 0);
        EXPECT_EQ(packTimeField(inf, 0.5f), 0);
        EXPECT_EQ(packTimeField(0.5f, 0.0004f), 0);
        EXPECT_FALSE(unpack(0, 5).has_value());
        EXPECT_FALSE(unpack(-1, 5).has_value());
    }

    TEST(NeutralFlickerPack, periodAndAmplitudeAreClampedToTheirLimits)
    {
        const auto big = unpack(packTimeField(500.f, 5.f), 1);
        ASSERT_TRUE(big.has_value());
        EXPECT_NEAR(big->mPeriod, kMaxPeriod, 0.001f);
        EXPECT_NEAR(big->mAmplitude, kMaxAmplitude, 0.0011f);
        const auto tiny = unpack(packTimeField(0.001f, 0.1f), 1);
        ASSERT_TRUE(tiny.has_value());
        EXPECT_NEAR(tiny->mPeriod, kMinPeriod, 0.001f);
    }

    TEST(NeutralFlickerWalk, theBrightnessStaysBetweenTheDipAndFull)
    {
        for (const bool pulse : { false, true })
            for (const float amp : { 0.1f, 0.4f, 0.95f })
            {
                const Params p{ 0.25f, amp, 12345u };
                for (const float b : trace(p, pulse, 6000))
                {
                    EXPECT_GE(b, 1.f - amp - 1e-4f) << "pulse " << pulse << " amp " << amp;
                    EXPECT_LE(b, 1.f + 1e-4f);
                }
            }
    }

    TEST(NeutralFlickerWalk, theSameSeedGivesTheSameBrightnessAndAnotherSeedAnotherOne)
    {
        const Params a{ 0.2f, 0.5f, 1u };
        const Params b{ 0.2f, 0.5f, 2u };
        EXPECT_EQ(trace(a, false, 600), trace(a, false, 600));
        EXPECT_NE(trace(a, false, 600), trace(b, false, 600));
    }

    TEST(NeutralFlickerWalk, aShorterPeriodChangesDirectionMoreOften)
    {
        const int fast = turns(trace({ 0.05f, 0.5f, 9u }, false, 3600));
        const int slow = turns(trace({ 1.0f, 0.5f, 9u }, false, 3600));
        EXPECT_GT(fast, slow * 4);
        EXPECT_GT(slow, 0);
    }

    TEST(NeutralFlickerWalk, aBiggerAmplitudeSpreadsTheBrightnessWider)
    {
        auto spread = [](float amp) {
            const auto v = trace({ 0.2f, amp, 3u }, false, 3600);
            return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end());
        };
        EXPECT_GT(spread(0.8f), spread(0.2f) * 2.f);
        EXPECT_LE(spread(0.2f), 0.2f + 1e-4f);
    }

    TEST(NeutralFlickerWalk, aFlickerVisitsMoreThanOneLevelWithinItsRange)
    {
        const auto v = trace({ 0.1f, 0.6f, 4u }, false, 1200);
        EXPECT_LT(*std::min_element(v.begin(), v.end()), 0.6f);
        EXPECT_GT(*std::max_element(v.begin(), v.end()), 0.9f);
    }

    TEST(NeutralFlickerWalk, aPulseReachesBothEndsWithinOneAndAHalfPeriods)
    {
        const Params p{ 1.0f, 0.5f, 6u };
        const auto v = trace(p, true, 90); // 1.5 s
        EXPECT_LT(*std::min_element(v.begin(), v.end()), 0.52f);
        EXPECT_GT(*std::max_element(v.begin(), v.end()), 0.98f);
    }

    TEST(NeutralFlickerWalk, aPulseCyclesInAboutOnePeriod)
    {
        // 10 s of a 0.5 s period: about 20 maxima, not a few and not a hundred.
        const auto v = trace({ 0.5f, 0.5f, 6u }, true, 600);
        int maxima = 0;
        for (std::size_t i = 1; i + 1 < v.size(); ++i)
            if (v[i] > v[i - 1] && v[i] >= v[i + 1])
                ++maxima;
        EXPECT_GE(maxima, 16);
        EXPECT_LE(maxima, 24);
    }

    TEST(NeutralFlickerWalk, aStepThatIsZeroNegativeOrNotFiniteChangesNothingAndAHugeOneStaysInRange)
    {
        Walk w({ 0.2f, 0.5f, 8u }, false);
        for (int i = 0; i < 30; ++i)
            w.step(kFrame);
        const float before = w.brightness();
        EXPECT_EQ(w.step(0.f), before);
        EXPECT_EQ(w.step(-3.f), before);
        EXPECT_EQ(w.step(std::numeric_limits<float>::quiet_NaN()), before);
        EXPECT_EQ(w.step(std::numeric_limits<float>::infinity()), before);
        const float after = w.step(1e9f);
        EXPECT_GE(after, 0.5f - 1e-4f);
        EXPECT_LE(after, 1.f + 1e-4f);
    }

    TEST(NeutralFlickerWalk, anAmplitudeOrPeriodOutOfRangeIsHandledLikeTheLimits)
    {
        for (const float b : trace({ 0.f, 5.f, 1u }, false, 600))
        {
            EXPECT_TRUE(std::isfinite(b));
            EXPECT_GE(b, 0.05f - 1e-4f);
            EXPECT_LE(b, 1.f + 1e-4f);
        }
    }

    // ---- through MWAnim::addLight, as the engine reaches the controller ----

    ESM::Light record(int flags, float period, float amp, std::int32_t seed)
    {
        ESM::Light light;
        light.blank();
        light.mData.mFlags = flags;
        light.mData.mRadius = 256;
        light.mData.mColor = 0x0040a0e0;
        light.mData.mTime = packTimeField(period, amp);
        light.mData.mValue = seed;
        return light;
    }

    struct Placed
    {
        vsg::ref_ptr<vsg::Group> mParent = vsg::Group::create();
        Anim::AutoPlay mAutoPlay;
        vsg::Light* mLight = nullptr;
        vsg::vec3 mBase;
        explicit Placed(const ESM::Light& r)
        {
            Anim::Controllers& c = mAutoPlay.getOrCreateGroup().controllers;
            vsg::Node* node = MWAnim::addLight(mParent.get(), mParent.get(), r, c);
            mLight = dynamic_cast<vsg::Light*>(node);
            EXPECT_NE(mLight, nullptr);
            mBase = mLight->color;
        }
        float frame(float dt)
        {
            mAutoPlay.update(dt);
            return mLight->color.z / mBase.z;
        }
    };

    TEST(NeutralFlickerLight, aNeutralFlickerLightUsesItsOwnRangeNotMorrowindsQuarterToFull)
    {
        const int flags = ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralFlicker;
        Placed placed(record(flags, 0.1f, 0.2f, 42));
        float lo = 10.f, hi = -10.f;
        for (int i = 0; i < 2400; ++i)
        {
            const float b = placed.frame(kFrame);
            lo = std::min(lo, b);
            hi = std::max(hi, b);
        }
        EXPECT_GE(lo, 0.8f - 1e-3f);
        EXPECT_LE(hi, 1.f + 1e-3f);
        EXPECT_GT(hi - lo, 0.1f);
    }

    TEST(NeutralFlickerLight, theSameSeedFlickersTheSameWithoutTheGlobalGenerator)
    {
        const int flags = ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralFlicker;
        const Misc::Rng::Generator saved = Misc::Rng::getGenerator();
        Misc::Rng::getGenerator().seed(1);
        Placed a(record(flags, 0.1f, 0.5f, 42));
        Misc::Rng::getGenerator().seed(999);
        Placed b(record(flags, 0.1f, 0.5f, 42));
        Placed c(record(flags, 0.1f, 0.5f, 43));
        Placed d(record(flags, 0.1f, 0.5f, 42));
        bool differs = false;
        for (int i = 0; i < 300; ++i)
        {
            const float va = a.frame(kFrame);
            EXPECT_EQ(va, b.frame(kFrame));
            differs = differs || c.frame(kFrame) != d.frame(kFrame);
        }
        EXPECT_TRUE(differs);
        Misc::Rng::getGenerator() = saved;
    }

    TEST(NeutralFlickerLight, aPulseLightWithTheFlagPulsesInItsRange)
    {
        const int flags = ESM::Light::Dynamic | ESM::Light::Pulse | ESM::Light::NeutralFlicker;
        Placed placed(record(flags, 0.5f, 0.5f, 7));
        float lo = 10.f, hi = -10.f;
        for (int i = 0; i < 1200; ++i)
        {
            const float b = placed.frame(kFrame);
            lo = std::min(lo, b);
            hi = std::max(hi, b);
        }
        EXPECT_LT(lo, 0.55f);
        EXPECT_GT(hi, 0.95f);
        EXPECT_GE(lo, 0.5f - 1e-3f);
    }

    TEST(NeutralFlickerLight, theFlagWithAnUnusablePackedFieldKeepsMorrowindsRule)
    {
        const int flags = ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralFlicker;
        Placed placed(record(flags, 0.f, 0.f, 5));
        // Morrowind's rule starts at 0.675 and walks in [0.25, 1]: its first brightness is near 0.675 (a neutral
        // walk's would be near 1).
        EXPECT_NEAR(placed.frame(kFrame), 0.675f, 0.2f);
    }

    TEST(NeutralFlickerLight, aLightWithoutTheFlagIgnoresItsTimeAndValueFields)
    {
        // Each light runs on its own after the global generator is seeded the same, so the Morrowind rule's draws
        // match.
        const Misc::Rng::Generator saved = Misc::Rng::getGenerator();
        auto run = [](const ESM::Light& r) {
            Misc::Rng::getGenerator().seed(5);
            Placed placed(r);
            std::vector<float> out;
            for (int i = 0; i < 600; ++i)
                out.push_back(placed.frame(kFrame));
            return out;
        };
        const auto withFields = run(record(ESM::Light::Dynamic | ESM::Light::Flicker, 0.1f, 0.2f, 42));
        const auto without = run(record(ESM::Light::Dynamic | ESM::Light::Flicker, 0.f, 0.f, 0));
        EXPECT_EQ(withFields, without);
        Misc::Rng::getGenerator() = saved;
    }
}
