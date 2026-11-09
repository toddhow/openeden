// Tests for StudioWorld::PreStepHook (task 0065, stage A): something that must act on the world just before a step,
// outside it. Written from the spec alone, before the implementation exists.
//
// Contract: hooks run in registration order at the start of every step (stepOnce and the ticks that call it), before
// the observer's beforeStep and before the adapter's pre-step listeners; adding one twice or removing an unknown one
// does nothing; a hook that throws is logged and the step still runs, and so do the others.

#include "apps/opencs/model/physics/studioworld.hpp"

#include <opas/physics/PhysicsWorld.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        class Recorder final : public StudioWorld::PreStepHook, public StudioWorld::StepObserver
        {
        public:
            Recorder(std::string name, std::vector<std::string>& log, bool throws = false)
                : mName(std::move(name))
                , mLog(log)
                , mThrows(throws)
            {
            }

            void beforeStep() override
            {
                mLog.push_back(mName);
                if (mThrows)
                    throw std::runtime_error("hook failed on purpose");
            }

            void beforeStep(float) override { mLog.push_back(mName + "-observer"); }
            void afterStep(float) override {}

        private:
            std::string mName;
            std::vector<std::string>& mLog;
            bool mThrows;
        };

        class ListenerRecorder final : public opas::physics::StepListener
        {
        public:
            explicit ListenerRecorder(std::vector<std::string>& log)
                : mLog(log)
            {
            }
            void onPreStep(opas::physics::PhysicsWorld&, float) override { mLog.push_back("pre-listener"); }
            void onPostStep(opas::physics::PhysicsWorld&, float) override {}

        private:
            std::vector<std::string>& mLog;
        };

        TEST(CSMPhysicsStudioWorldHooks, hooksRunInRegistrationOrderBeforeObserverAndListenersOnEveryStep)
        {
            StudioWorld world;
            std::vector<std::string> log;
            Recorder first("first", log), second("second", log);
            ListenerRecorder listener(log);
            ASSERT_TRUE(world.cmWorld().addStepListener(listener).isValid());
            world.setStepObserver(&second); // as the observer too, to see the order against the hooks
            world.addPreStepHook(&first);
            world.addPreStepHook(&second);

            world.stepOnce();
            world.stepOnce();

            const std::vector<std::string> expected{ "first", "second", "second-observer", "pre-listener", "first",
                "second", "second-observer", "pre-listener" };
            EXPECT_EQ(log, expected);
            world.setStepObserver(nullptr);
        }

        TEST(CSMPhysicsStudioWorldHooks, tickRunsHooksOnlyWhenItSteps)
        {
            StudioWorld world;
            std::vector<std::string> log;
            Recorder hook("hook", log);
            world.addPreStepHook(&hook);
            EXPECT_FALSE(world.tick());
            EXPECT_TRUE(log.empty());
            world.play();
            EXPECT_TRUE(world.tick());
            EXPECT_EQ(log.size(), 1u);
        }

        TEST(CSMPhysicsStudioWorldHooks, addingTwiceRunsOnceAndRemovingStopsIt)
        {
            StudioWorld world;
            std::vector<std::string> log;
            Recorder hook("hook", log);
            world.addPreStepHook(&hook);
            world.addPreStepHook(&hook);
            world.stepOnce();
            EXPECT_EQ(log.size(), 1u);

            world.removePreStepHook(&hook);
            world.removePreStepHook(&hook); // not there any more: nothing happens
            world.stepOnce();
            EXPECT_EQ(log.size(), 1u);
        }

        TEST(CSMPhysicsStudioWorldHooks, aThrowingHookIsContainedAndTheStepAndTheOtherHooksStillRun)
        {
            StudioWorld world;
            std::vector<std::string> log;
            Recorder bad("bad", log, true), good("good", log);
            world.addPreStepHook(&bad);
            world.addPreStepHook(&good);

            EXPECT_NO_THROW(world.stepOnce());
            const std::vector<std::string> expected{ "bad", "good" };
            EXPECT_EQ(log, expected);
            EXPECT_EQ(world.stepIndex(), 1u);
        }

        // ---- review of 2026-11-09 (lane C: C4) ----------------------------------------------------------------------

        // Counts its calls; throws on the calls whose number (from 1) is in 'throwOn', or on every call.
        class Counter final : public StudioWorld::PreStepHook
        {
        public:
            explicit Counter(bool alwaysThrows = false, std::vector<int> throwOn = {})
                : mAlways(alwaysThrows)
                , mThrowOn(std::move(throwOn))
            {
            }
            void beforeStep() override
            {
                ++calls;
                if (mAlways || std::find(mThrowOn.begin(), mThrowOn.end(), calls) != mThrowOn.end())
                    throw std::runtime_error("counter failed on purpose");
            }
            int calls = 0;

        private:
            bool mAlways;
            std::vector<int> mThrowOn;
        };

        // Runs a function inside the step, as a hook (the function may add or remove hooks).
        class Action final : public StudioWorld::PreStepHook
        {
        public:
            explicit Action(std::function<void()> action)
                : mAction(std::move(action))
            {
            }
            void beforeStep() override
            {
                ++calls;
                if (mAction)
                    mAction();
            }
            int calls = 0;

        private:
            std::function<void()> mAction;
        };

        TEST(CSMPhysicsStudioWorldHooks, aHookThatKeepsThrowingIsWithdrawnAfterTheLimitSoItCannotFloodTheLog)
        {
            StudioWorld world;
            Counter bad(true), good;
            world.addPreStepHook(&bad);
            world.addPreStepHook(&good);
            for (int i = 0; i < 50; ++i)
                world.stepOnce();
            EXPECT_EQ(bad.calls, StudioWorld::sPreStepHookFailureLimit);
            EXPECT_EQ(good.calls, 50) << "the other hook is never affected";
            EXPECT_EQ(world.stepIndex(), 50u);
        }

        TEST(CSMPhysicsStudioWorldHooks, onlyConsecutiveFailuresCountTowardsTheLimit)
        {
            StudioWorld world;
            Counter flaky(false, { 1, 3, 5, 7, 9, 11 });
            world.addPreStepHook(&flaky);
            for (int i = 0; i < 12; ++i)
                world.stepOnce();
            EXPECT_EQ(flaky.calls, 12) << "a hook that works in between is never withdrawn";
        }

        TEST(CSMPhysicsStudioWorldHooks, aWithdrawnHookCanBeAddedAgainAndGetsAFreshCount)
        {
            StudioWorld world;
            Counter bad(true);
            world.addPreStepHook(&bad);
            for (int i = 0; i < 10; ++i)
                world.stepOnce();
            ASSERT_EQ(bad.calls, StudioWorld::sPreStepHookFailureLimit);
            world.addPreStepHook(&bad);
            for (int i = 0; i < 10; ++i)
                world.stepOnce();
            EXPECT_EQ(bad.calls, 2 * StudioWorld::sPreStepHookFailureLimit);
        }

        TEST(CSMPhysicsStudioWorldHooks, aHookRemovingItselfDuringTheStepDoesNotSkipTheNextHook)
        {
            StudioWorld world;
            Action* selfPtr = nullptr;
            Action self([&] { world.removePreStepHook(selfPtr); });
            selfPtr = &self;
            Counter next, third;
            world.addPreStepHook(&self);
            world.addPreStepHook(&next);
            world.addPreStepHook(&third);
            world.stepOnce();
            EXPECT_EQ(self.calls, 1);
            EXPECT_EQ(next.calls, 1) << "removing a hook from inside a step must not skip the one after it";
            EXPECT_EQ(third.calls, 1);
            world.stepOnce();
            EXPECT_EQ(self.calls, 1) << "it is gone";
            EXPECT_EQ(next.calls, 2);
        }

        TEST(CSMPhysicsStudioWorldHooks, aHookRemovingALaterHookStopsItBeforeItRunsAndSkipsNoOther)
        {
            StudioWorld world;
            Counter victim, last;
            Action remover([&] { world.removePreStepHook(&victim); });
            world.addPreStepHook(&remover);
            world.addPreStepHook(&victim);
            world.addPreStepHook(&last);
            world.stepOnce();
            EXPECT_EQ(victim.calls, 0);
            EXPECT_EQ(last.calls, 1);
        }

        TEST(CSMPhysicsStudioWorldHooks, aHookAddedDuringTheStepRunsFromTheNextStepAndNothingIsSkipped)
        {
            StudioWorld world;
            Counter added, other;
            Action adder([&] { world.addPreStepHook(&added); });
            world.addPreStepHook(&adder);
            world.addPreStepHook(&other);
            world.stepOnce();
            EXPECT_EQ(other.calls, 1);
            EXPECT_LE(added.calls, 1);
            world.stepOnce();
            EXPECT_EQ(added.calls >= 1, true);
            EXPECT_EQ(other.calls, 2);
        }
    }
}
