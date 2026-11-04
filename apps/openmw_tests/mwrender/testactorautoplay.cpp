// Tests for task 0074b, the review of task 0074: an equipment change leaves an actor's own auto-play groups
// (MWAnim::Object::autoPlay: the inventory lights of MWRender::addItemLight) running, through the code the game runs
// (MWAnim::Wielding::clearPartControllers). Written before the implementation and run red against it (the lists were
// emptied with autoPlay).

#include "apps/openmw/mwrender/itemlight.hpp"

#include <components/animation/autoplay.hpp>
#include <components/animation/controller.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/wielding.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/lighting/Light.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

namespace
{
    constexpr float frameTime = 1.f / 60.f;

    // MWAnim::Object's destructor detaches its node through the context's compile context; here that does nothing.
    vsg::ref_ptr<vsgUtil::CompileContext> quietCompileContext()
    {
        vsg::ref_ptr<vsgUtil::CompileContext> compile(new vsgUtil::CompileContext);
        compile->onDetached = [](vsg::ref_ptr<vsg::Object>) {};
        return compile;
    }

    ESM::Light flickeringLight()
    {
        ESM::Light light;
        light.blank();
        // Not ESM::Light::Carry: an inventory light (MWRender::addItemLightsAndListener).
        light.mData.mFlags = ESM::Light::Flicker;
        light.mData.mRadius = 128;
        light.mData.mColor = 0x0040a0ff;
        return light;
    }

    // The controllers of every auto-play group, with the group's phase group.
    std::vector<std::tuple<int, const Anim::Controller*, vsg::Object*>> autoPlayControllers(const Anim::AutoPlay& clock)
    {
        std::vector<std::tuple<int, const Anim::Controller*, vsg::Object*>> result;
        for (const auto& group : clock.groups)
        {
            for (const auto& [controller, target] : group.controllers)
                result.emplace_back(group.phaseGroup, controller, target);
        }
        return result;
    }

    vsg::Light* findLight(const Anim::AutoPlay& clock)
    {
        for (const auto& [phaseGroup, controller, target] : autoPlayControllers(clock))
        {
            if (auto light = dynamic_cast<vsg::Light*>(target))
                return light;
        }
        return nullptr;
    }

    // The colours a light takes over `frames` frames of the actor's own auto-play (MWAnim::Object::update).
    std::set<std::tuple<float, float, float>> lightColours(Anim::AutoPlay& clock, vsg::Light& light, int frames)
    {
        std::set<std::tuple<float, float, float>> colours;
        for (int i = 0; i < frames; ++i)
        {
            clock.update(frameTime);
            colours.emplace(light.color.r, light.color.g, light.color.b);
        }
        return colours;
    }

    // The part of Npc's equipment change that empties lists (Npc::clearControllers calls clearPartControllers).
    struct TestWielding : public MWAnim::Wielding
    {
        using MWAnim::Wielding::clearPartControllers;
        using MWAnim::Wielding::Wielding;
    };

    TEST(ActorAutoPlayTest, an_inventory_light_keeps_flickering_through_an_equipment_change)
    {
        MWAnim::Context context;
        context.compileContext = quietCompileContext();
        TestWielding actor(context);
        const ESM::Light record = flickeringLight();
        MWRender::addItemLight(actor, MWWorld::ConstPtr(), record);
        vsg::Light* light = findLight(actor.autoPlay);
        ASSERT_NE(light, nullptr);
        lightColours(actor.autoPlay, *light, 30);
        const float timerBefore = actor.autoPlay.getOrCreateGroup().timer;

        actor.clearPartControllers();

        ASSERT_EQ(findLight(actor.autoPlay), light);
        EXPECT_EQ(actor.autoPlay.getOrCreateGroup().timer, timerBefore);
        // Flickering: its colour changes from frame to frame.
        EXPECT_GT(lightColours(actor.autoPlay, *light, 30).size(), 10u);
    }
}
