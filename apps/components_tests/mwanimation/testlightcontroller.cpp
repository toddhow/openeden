// Tests for the flickering and pulsing light controller (components/mwanimation/light.cpp, task 0074c). The controller
// is run with the timer of the auto-play group it joins (Anim::AutoUpdate::update passes the group's timer to
// Controller::run) and works out its own time step from that timer. An inventory light added to an actor long after
// the actor's group 0 started (apps/openmw/mwrender/itemlight.cpp, addItemLight) took the group's whole timer as its
// first step: its colour then swung by hundreds of times the light's colour, negative on some frames. OpenMW 0.51's
// SceneUtil::LightController starts its clock at its first run (components/sceneutil/lightcontroller.cpp at
// 442eec6e2f), so its first step is 0. The controller is reached through MWAnim::addLight, as every caller in the game
// reaches it, and run as the engine runs it; no device.
//
// Bounds used below, from light.cpp's rule (the same as OpenMW's): each run, ticks = step * 15 * 0.25 + ticks * 0.75,
// so with a constant frame time dt the ticks stay below 15 * dt; the brightness moves by ticks * speed (speed 0.1 for
// Flicker and Pulse, 0.05 for the slow kinds) towards a phase in [0.25, 1] and can pass it by one move at most, so it
// stays in [0.25 - 15 * dt * speed, 1 + 15 * dt * speed]; it starts at 0.675. The colour is the base colour (the
// record's colour, negated for a Negative light) times the brightness.

#include <components/animation/autoplay.hpp>
#include <components/animation/update.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/rng.hpp>
#include <components/mwanimation/light.hpp>

#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    constexpr unsigned sSeed = 74;
    // 1/64 s and 512 s are exact in float, and so is every time 512 + n / 64 used here: two lights that differ only by
    // the age of their group see exactly the same steps.
    constexpr float sFrame = 1.f / 64.f;
    constexpr float sOldGroup = 512.f;
    constexpr int sFrames = 2000; // about 31 s of frames: many phase changes
    constexpr float sRestBrightness = 0.675f; // the controller's starting brightness (light.cpp; OpenMW's mBrightness)

    struct Kind
    {
        const char* mName;
        int mFlags;
        float mSpeed;
    };

    const std::array<Kind, 5> sKinds{ {
        { "Flicker", ESM::Light::Flicker, 0.1f },
        { "FlickerSlow", ESM::Light::FlickerSlow, 0.05f },
        { "Pulse", ESM::Light::Pulse, 0.1f },
        { "PulseSlow", ESM::Light::PulseSlow, 0.05f },
        { "Negative Flicker", ESM::Light::Negative | ESM::Light::Flicker, 0.1f },
    } };

    ESM::Light lightRecord(int flags)
    {
        ESM::Light light;
        light.blank();
        light.mData.mFlags = flags;
        light.mData.mRadius = 256;
        light.mData.mColor = 0x0040a0e0; // red 0xe0, green 0xa0, blue 0x40 (MWAnim::rgbColor reads red first)
        return light;
    }

    // The global generator is shared with other tests: each test here seeds it and puts it back.
    class LightControllerTest : public ::testing::Test
    {
    protected:
        void SetUp() override { mSaved = Misc::Rng::getGenerator(); }
        void TearDown() override { Misc::Rng::getGenerator() = mSaved; }

    private:
        Misc::Rng::Generator mSaved;
    };

    // An actor's auto-play clock whose group 0 has run for `age` seconds when the light joins it, as addItemLight adds
    // an inventory light to an actor that already has one (MWAnim::addLight into autoPlay.getOrCreateGroup()).
    struct LightInGroup
    {
        vsg::ref_ptr<vsg::Group> mParent = vsg::Group::create();
        Anim::AutoPlay mAutoPlay;
        vsg::Light* mLight = nullptr;
        vsg::vec3 mBase;

        LightInGroup(const ESM::Light& record, float age)
        {
            mAutoPlay.getOrCreateGroup();
            mAutoPlay.update(age);
            Anim::Controllers& controllers = mAutoPlay.getOrCreateGroup().controllers;
            vsg::Node* node = MWAnim::addLight(mParent.get(), mParent.get(), record, controllers);
            mLight = dynamic_cast<vsg::Light*>(node);
            EXPECT_NE(mLight, nullptr);
            EXPECT_EQ(controllers.size(), 1u);
            if (!controllers.empty())
            {
                EXPECT_EQ(controllers.front().second, node);
            }
            if (mLight != nullptr)
            {
                mBase = mLight->color;
            }
        }

        // One frame of the engine (MWAnim::Object::update runs autoPlay.update(dt)); returns the brightness of each
        // channel, the colour over the base colour.
        vsg::vec3 frame(float dt)
        {
            mAutoPlay.update(dt);
            return brightness();
        }

        vsg::vec3 brightness() const
        {
            return { mLight->color.r / mBase.r, mLight->color.g / mBase.g, mLight->color.b / mBase.b };
        }
    };

    // OpenMW 0.51's SceneUtil::LightController::operator() (components/sceneutil/lightcontroller.cpp at 442eec6e2f)
    // for one light of one kind, with its own generator: the step is the time since its last run, 0 at its first run.
    // (OpenMW marks "not started" with mStartTime == 0, which a first run at exactly 0 would not clear; this model
    // keeps a flag, since a game's simulation time is never 0 at a light's first run.)
    struct OpenMwRule
    {
        explicit OpenMwRule(const Kind& kind)
            : mFlicker((kind.mFlags & (ESM::Light::Flicker | ESM::Light::FlickerSlow)) != 0)
            , mSpeed((kind.mFlags & (ESM::Light::Flicker | ESM::Light::Pulse)) != 0 ? 0.1f : 0.05f)
        {
            mPhase = 0.25f + Misc::Rng::rollClosedProbability(mRng) * 0.75f;
        }

        float run(float time)
        {
            if (!mStarted)
            {
                mStarted = true;
                mLast = time;
            }
            constexpr float updateRate = 15.f;
            mTicksToAdvance = (time - mLast) * updateRate * 0.25f + mTicksToAdvance * 0.75f;
            mLast = time;
            if (mBrightness >= mPhase)
                mBrightness -= mTicksToAdvance * mSpeed;
            else
                mBrightness += mTicksToAdvance * mSpeed;
            if (std::abs(mBrightness - mPhase) < mSpeed)
            {
                if (mFlicker)
                    mPhase = 0.25f + Misc::Rng::rollClosedProbability(mRng) * 0.75f;
                else
                    mPhase = mPhase <= 0.5f ? 1.f : 0.25f;
            }
            return mBrightness;
        }

        Misc::Rng::Generator mRng{ sSeed };
        bool mFlicker;
        float mSpeed;
        float mPhase = 0.f;
        float mBrightness = sRestBrightness;
        float mTicksToAdvance = 0.f;
        bool mStarted = false;
        float mLast = 0.f;
    };

    void expectNormalBrightness(const vsg::vec3& brightness, const Kind& kind, float dt, int frame)
    {
        const float move = 15.f * dt * kind.mSpeed;
        for (int channel = 0; channel < 3; ++channel)
        {
            EXPECT_GE(brightness[channel], 0.25f - move - 1e-5f) << kind.mName << " frame " << frame;
            EXPECT_LE(brightness[channel], 1.f + move + 1e-5f) << kind.mName << " frame " << frame;
        }
    }

    TEST_F(LightControllerTest, a_light_joining_a_group_that_ran_for_ten_minutes_starts_at_its_rest_brightness)
    {
        for (const Kind& kind : sKinds)
        {
            Misc::Rng::init(sSeed);
            LightInGroup light(lightRecord(kind.mFlags), 600.f);
            const vsg::vec3 first = light.frame(1.f / 60.f);
            // The first step is 0, so the first run leaves the starting brightness, whatever the phase.
            for (int channel = 0; channel < 3; ++channel)
            {
                EXPECT_FLOAT_EQ(first[channel], sRestBrightness) << kind.mName;
            }
        }
    }

    TEST_F(LightControllerTest, a_light_joining_a_group_that_ran_for_ten_minutes_stays_in_its_normal_range)
    {
        constexpr float dt = 1.f / 60.f;
        for (const Kind& kind : sKinds)
        {
            Misc::Rng::init(sSeed);
            LightInGroup light(lightRecord(kind.mFlags), 600.f);
            for (int frame = 0; frame < 300; ++frame)
            {
                // Never darker than a quarter less one move, never brighter than full plus one move: in particular a
                // light that is not Negative never gives negative light, and a Negative one never gives light.
                expectNormalBrightness(light.frame(dt), kind, dt, frame);
                for (int channel = 0; channel < 3; ++channel)
                {
                    EXPECT_GE(light.mLight->color[channel] * light.mBase[channel], 0.f) << kind.mName;
                }
            }
        }
    }

    TEST_F(LightControllerTest, a_light_flickers_the_same_however_long_its_group_had_run)
    {
        for (const Kind& kind : sKinds)
        {
            Misc::Rng::init(sSeed);
            LightInGroup fresh(lightRecord(kind.mFlags), 0.f);
            std::vector<vsg::vec3> freshFrames;
            for (int frame = 0; frame < sFrames; ++frame)
                freshFrames.push_back(fresh.frame(sFrame));

            Misc::Rng::init(sSeed);
            LightInGroup late(lightRecord(kind.mFlags), sOldGroup);
            for (int frame = 0; frame < sFrames; ++frame)
            {
                const vsg::vec3 lateFrame = late.frame(sFrame);
                for (int channel = 0; channel < 3; ++channel)
                {
                    ASSERT_FLOAT_EQ(lateFrame[channel], freshFrames[frame][channel])
                        << kind.mName << " frame " << frame << " channel " << channel;
                }
            }
        }
    }

    // Runs from time 0, where the controller's first step was 0 before task 0074c too: this case passes on the code
    // before the fix, which shows that the reference is the rule the controller had, and after it, which shows that
    // every run after the first is unchanged.
    TEST_F(LightControllerTest, after_its_first_run_a_light_follows_the_rule_of_openmw)
    {
        for (const Kind& kind : sKinds)
        {
            Misc::Rng::init(sSeed);
            vsg::ref_ptr<vsg::Group> parent = vsg::Group::create();
            // The engine's runner of controllers at a given time (Anim::AutoUpdate without its clock).
            Anim::Update update;
            auto* light = dynamic_cast<vsg::Light*>(
                MWAnim::addLight(parent.get(), parent.get(), lightRecord(kind.mFlags), update.controllers));
            ASSERT_NE(light, nullptr);
            const vsg::vec3 base = light->color;
            OpenMwRule rule(kind);
            for (int frame = 0; frame < sFrames; ++frame)
            {
                const float time = frame * sFrame;
                update.update(time);
                const float expected = rule.run(time);
                ASSERT_NEAR(light->color.r / base.r, expected, 1e-5f) << kind.mName << " frame " << frame;
            }
        }
    }
}
