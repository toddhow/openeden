// Tests for task 0074d, the review of task 0074b (finding A): an inventory light (a light record without the Carry
// flag, MWRender::addItemLight) leaves an actor only when none of the item is left, as OpenMW 0.51's
// ActorAnimation::itemRemoved, and can come back after that: MWRender::removeItemLight forgets the item when it removes
// its light, so a later addItemLight for the same item adds it again. Written from the task's test specification
// before the implementation and run red against it.

#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwrender/itemlight.hpp"
#include "apps/openmw/mwworld/cellref.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

#include <components/animation/autoplay.hpp>
#include <components/animation/controller.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/object.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/lighting/Light.h>
#include <vsg/nodes/Node.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
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

    ESM::Light inventoryLightRecord()
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_inventory_light");
        // Not ESM::Light::Carry: an inventory light (MWRender::addItemLightsAndListener).
        light.mData.mFlags = ESM::Light::Flicker;
        light.mData.mRadius = 128;
        light.mData.mColor = 0x0040a0ff;
        return light;
    }

    ESM::CellRef lightCellRef(const ESM::Light& record, int count)
    {
        // LiveCellRef's constructor looks the class up (MWWorld::Class::get).
        MWClass::Light::registerSelf();
        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = record.mId;
        cellRef.mCount = count;
        return cellRef;
    }

    // A stack of an inventory light, as a ContainerStore holds it: one object whose count changes.
    struct InventoryLight
    {
        explicit InventoryLight(int count)
            : mCellRef(lightCellRef(mRecord, count))
            , mRef(mCellRef, &mRecord)
        {
        }

        ESM::Light mRecord = inventoryLightRecord();
        ESM::CellRef mCellRef;
        MWWorld::LiveCellRef<ESM::Light> mRef;

        MWWorld::ConstPtr ptr() const { return MWWorld::ConstPtr(&mRef); }

        // One of the stack was taken away (ContainerStore::remove), some is left.
        void setCount(int count) { mRef.mRef.setCount(count); }

        // The last of the stack was taken away. MWWorld::CellRef::setCount(0) also removes the reference's script from
        // the World (there is none here), so the count is set through a new CellRef of the same reference.
        void setNoneLeft()
        {
            ESM::CellRef none = mCellRef;
            none.mCount = 0;
            mRef.mRef = MWWorld::CellRef(none);
        }
    };

    // Every light in a subgraph.
    class CollectLights : public vsg::Visitor
    {
    public:
        CollectLights() { overrideMask = vsg::MASK_ALL; }

        using vsg::Visitor::apply;

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::Light& light) override
        {
            mLights.push_back(&light);
            light.traverse(*this);
        }

        std::vector<vsg::Light*> mLights;
    };

    std::vector<vsg::Light*> lightsUnder(vsg::Node& node)
    {
        CollectLights visitor;
        node.accept(visitor);
        return visitor.mLights;
    }

    // The controllers of an auto-play clock that run on `target`.
    std::vector<const Anim::Controller*> controllersOn(const Anim::AutoPlay& clock, const vsg::Object* target)
    {
        std::vector<const Anim::Controller*> result;
        for (const Anim::AutoPlay::Group& group : clock.groups)
        {
            for (const auto& entry : group.controllers)
            {
                if (entry.second == target)
                    result.push_back(entry.first);
            }
        }
        return result;
    }

    // How many times `controller` is in any group of an auto-play clock.
    std::size_t countIn(const Anim::AutoPlay& clock, const Anim::Controller* controller)
    {
        std::size_t count = 0;
        for (const Anim::AutoPlay::Group& group : clock.groups)
        {
            for (const auto& entry : group.controllers)
            {
                if (entry.first == controller)
                    ++count;
            }
        }
        return count;
    }

    // The colours a light takes over `frames` frames of the object's own auto-play (MWAnim::Object::update).
    std::set<std::tuple<float, float, float>> lightColours(MWAnim::Object& object, const vsg::Light& light, int frames)
    {
        std::set<std::tuple<float, float, float>> colours;
        for (int i = 0; i < frames; ++i)
        {
            object.update(frameTime);
            colours.emplace(light.color.r, light.color.g, light.color.b);
        }
        return colours;
    }

    struct ItemLightsTest : public ::testing::Test
    {
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<MWAnim::Object> mObject;

        void SetUp() override
        {
            mContext.compileContext = quietCompileContext();
            mObject = std::make_unique<MWAnim::Object>(mContext);
        }

        std::vector<vsg::Light*> lights() { return lightsUnder(*mObject->node()); }
    };

    // L1, finding A: the light stays while some of the item is left.
    TEST_F(ItemLightsTest, the_light_stays_while_some_of_the_item_is_left)
    {
        MWAnim::Object& object = *mObject;
        InventoryLight item(2);
        MWRender::addItemLight(object, item.ptr(), item.mRecord);
        const std::vector<vsg::Light*> added = lights();
        ASSERT_EQ(added.size(), 1u);
        const vsg::ref_ptr<vsg::Light> light(added[0]);
        const std::vector<const Anim::Controller*> controllers = controllersOn(object.autoPlay, light.get());
        ASSERT_EQ(controllers.size(), 1u);
        const vsg::ref_ptr<const Anim::Controller> controller(controllers[0]);

        item.setCount(1);
        MWRender::removeItemLight(object, item.ptr());

        EXPECT_EQ(lights(), std::vector<vsg::Light*>{ light.get() });
        EXPECT_EQ(
            controllersOn(object.autoPlay, light.get()), std::vector<const Anim::Controller*>{ controller.get() });
        EXPECT_GT(lightColours(object, *light, 30).size(), 10u);

        // The item is still known, so it is not added a second time.
        MWRender::addItemLight(object, item.ptr(), item.mRecord);

        EXPECT_EQ(lights(), std::vector<vsg::Light*>{ light.get() });
        EXPECT_EQ(countIn(object.autoPlay, controller.get()), 1u);
    }

    // L2, finding A: with none of the item left the light goes, and the same item's light can come back.
    TEST_F(ItemLightsTest, the_light_goes_when_none_is_left_and_comes_back_with_the_item)
    {
        MWAnim::Object& object = *mObject;
        InventoryLight item(1);
        MWRender::addItemLight(object, item.ptr(), item.mRecord);
        const std::vector<vsg::Light*> added = lights();
        ASSERT_EQ(added.size(), 1u);
        // Held, so that neither pointer can be reused by the light added again.
        const vsg::ref_ptr<vsg::Light> oldLight(added[0]);
        const std::vector<const Anim::Controller*> controllers = controllersOn(object.autoPlay, oldLight.get());
        ASSERT_EQ(controllers.size(), 1u);
        const vsg::ref_ptr<const Anim::Controller> oldController(controllers[0]);

        item.setNoneLeft();
        MWRender::removeItemLight(object, item.ptr());

        EXPECT_TRUE(lights().empty());
        EXPECT_EQ(countIn(object.autoPlay, oldController.get()), 0u);
        EXPECT_TRUE(controllersOn(object.autoPlay, oldLight.get()).empty());

        // The item is back in the inventory.
        item.setCount(1);
        MWRender::addItemLight(object, item.ptr(), item.mRecord);

        const std::vector<vsg::Light*> again = lights();
        ASSERT_EQ(again.size(), 1u);
        EXPECT_NE(again[0], oldLight.get());
        EXPECT_EQ(controllersOn(object.autoPlay, again[0]).size(), 1u);
        EXPECT_GT(lightColours(object, *again[0], 30).size(), 10u);
    }
}
