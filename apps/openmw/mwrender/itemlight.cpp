#include "itemlight.hpp"

#include <vsg/nodes/Group.h>

#include <components/animation/update.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/light.hpp>
#include <components/mwanimation/object.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/removechild.hpp>

#include "../mwworld/containerstore.hpp"

namespace
{
    static const std::string sItemLights = "itemlights";
}
namespace MWRender
{
    struct ItemLight
    {
        vsg::ref_ptr<vsg::Node> node;
        Anim::AutoUpdate update;
    };

    using ItemLights = std::map<MWWorld::ConstPtr, ItemLight>;

    class UpdateItemLights : public MWWorld::ContainerStoreListener
    {
    public:
        UpdateItemLights(MWAnim::Object& obj)
            : object(obj)
        {
        }
        MWAnim::Object& object;
        void itemAdded(const MWWorld::ConstPtr& item, int count) override { addItemLightIfRequired(object, item); }
        void itemRemoved(const MWWorld::ConstPtr& item, int count) override { removeItemLight(object, item); }
    };

    void addItemLight(MWAnim::Object& obj, const MWWorld::ConstPtr& item, const ESM::Light& light)
    {
        ItemLights lights;
        obj.node()->getValue(sItemLights, lights);
        if (lights.find(item) != lights.end())
            return;
        ItemLight l;
        l.node = MWAnim::addLight(
            obj.nodeToAddChildrenTo(), obj.nodeToAddChildrenTo(), light, obj.autoPlay.getOrCreateGroup().controllers);
        lights[item] = l;
        obj.node()->setValue(sItemLights, lights);
    }

    void removeItemLight(MWAnim::Object& obj, const MWWorld::ConstPtr& item)
    {
        if (item.getType() != ESM::Light::sRecordId)
            return;
        // Task 0074d (review of 0074b, item A): as OpenMW 0.51's ActorAnimation::itemRemoved and removeHiddenItemLight
        // (442eec6e2f), the light goes only when none of the item is left, and the item is forgotten with it. Before,
        // removing one of a stack took the light away, and the item stayed in the map, so adding it again
        // (addItemLight returns early for an item it knows) never brought the light back.
        if (item.getCellRef().getCount() != 0)
            return;
        if (!obj.node()->getAuxiliary())
            return;
        ItemLights lights;
        obj.node()->getValue(sItemLights, lights);
        auto l = lights.find(item);
        if (l == lights.end())
            return;
        vsgUtil::removeChild(obj.nodeToAddChildrenTo(), l->second.node);
        // Kept until the frames already queued have drawn, as every node that leaves the scene during play.
        obj.context().compileContext->detach(l->second.node);

        auto& ctrls = obj.autoPlay.getOrCreateGroup().controllers;
        auto citr = ctrls.begin();
        while (citr != ctrls.end())
        {
            if (citr->second == l->second.node.get())
                citr = ctrls.erase(citr);
            else
                ++citr;
        }

        lights.erase(l);
        obj.node()->setValue(sItemLights, lights);
    }

    void addItemLightsAndListener(MWAnim::Object& obj, MWWorld::ContainerStore& store)
    {
        for (auto iter = store.cbegin(MWWorld::ContainerStore::Type_Light); iter != store.cend(); ++iter)
        {
            auto light = iter->get<ESM::Light>()->mBase;
            if (!(light->mData.mFlags & ESM::Light::Carry))
                addItemLight(obj, *iter, *light);
        }
        store.setContListener(new UpdateItemLights(obj));
    }

    void removeListener(MWWorld::ContainerStore& store)
    {
        if (auto listener = store.getContListener())
        {
            delete listener;
            store.setContListener(nullptr);
        }
    }

    void addItemLightIfRequired(MWAnim::Object& obj, const MWWorld::ConstPtr& item)
    {
        if (item.getType() != ESM::Light::sRecordId)
            return;
        auto light = item.get<ESM::Light>()->mBase;
        if (!(light->mData.mFlags & ESM::Light::Carry))
            addItemLight(obj, item, *light);
    }
}
