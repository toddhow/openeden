// Tests for StudioWorld::PreStepHook (task 0065, stage A): something that must act on the world just before a step,
// outside it. Written from the spec alone, before the implementation exists.
//
// Contract: hooks run in registration order at the start of every step (stepOnce and the ticks that call it), before
// the observer's beforeStep and before the adapter's pre-step listeners; adding one twice or removing an unknown one
// does nothing; a hook that throws is logged and the step still runs, and so do the others.

#include "apps/opencs/model/physics/studioworld.hpp"

#include <opas/physics/PhysicsWorld.hpp>

#include <gtest/gtest.h>

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
    }
}
