#include "scene.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <vsg/io/Options.h>
#include <vsg/nodes/Switch.h>

#include <components/animation/transform.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/mwanimation/create.hpp>
#include <components/render/pointshadowcache.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/operation.hpp>
#include <components/vsgutil/removechild.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/neutrallight.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"

#include "env.hpp"
#include "mask.hpp"
#include "npc.hpp"
#include "switches.hpp"
#include "wieldingcreature.hpp"

namespace
{
    void loadTransform(const MWWorld::Ptr& ptr, Anim::Transform& trafo)
    {
        const float* f = ptr.getRefData().getPosition().pos;
        trafo.translation = { f[0], f[1], f[2] };
        const float scale = ptr.getCellRef().getScale();
        vsg::vec3 scaleVec(scale, scale, scale);
        ptr.getClass().adjustScale(ptr, scaleVec, true);
        trafo.scale = scaleVec;
    }
}

namespace MWRender
{
    struct Scene::Cell
    {
        struct Update : public vsg::Operation
        {
            Scene::Cell& cell;
            float dt = 0;
            Update(Scene::Cell& c)
                : cell(c)
            {
            }
            void run() override
            {
                for (auto& [p, o] : cell.objects)
                    o->update(dt);
            }
        };
        vsg::ref_ptr<vsg::Group> group;
        Objects objects;
        // The doors among `objects` (task 0077: their swing is a moving caster); raw pointers into `objects`.
        std::vector<MWAnim::Object*> doors;
        vsg::ref_ptr<Update> update;
        // openeden-vsg: switches of NIF NightDaySwitch nodes in this cell (task 0012).
        std::vector<vsg::ref_ptr<vsg::Switch>> nightDaySwitches;
    };

    Scene::Scene(size_t updateThreads)
    {
        mActorGroup = vsg::Switch::create();
        mObjectGroup = vsg::Switch::create();
        mUpdateThreads = vsgUtil::UpdateThreads::create(updateThreads);
    }

    Scene::~Scene() {}

    vsg::ref_ptr<vsg::Node> Scene::getObjects()
    {
        return mObjectGroup;
    }

    vsg::ref_ptr<vsg::Node> Scene::getActors()
    {
        return mActorGroup;
    }

    Scene::Cell& Scene::getOrCreateCell(const MWWorld::Ptr& ptr, bool actor)
    {
        auto& map = actor ? mActorCells : mObjectCells;
        auto found = map.find(ptr.getCell());
        if (found == map.end())
        {
            auto cellnode = vsg::Group::create();
            if (actor)
                mActorGroup->addChild(Mask_Actor, cellnode);
            else
                mObjectGroup->addChild(Mask_Object, cellnode);
            auto& cell = map[ptr.getCell()];
            cell.group = cellnode;
            cell.update = new Cell::Update(cell);
            return cell;
        }
        else
            return found->second;
    }

    Scene::Cell* Scene::getCell(const MWWorld::Ptr& ptr, bool actor)
    {
        auto& map = actor ? mActorCells : mObjectCells;
        auto found = map.find(ptr.getCell());
        if (found != map.end())
            return &found->second;
        return {};
    }

    void Scene::insertModel(const MWWorld::Ptr& ptr, const std::string& mesh, bool allowLight)
    {
        const ESM::Light* light{};
        // A neutral light (task 0113): the engine's own light record made from the base's LightComponent, alive for
        // the call only (createObject copies what it needs).
        std::optional<ESM::Light> neutralLight;
        if (allowLight && ptr.getType() == ESM::Light::sRecordId)
            light = ptr.get<ESM::Light>()->mBase;
        else if (allowLight && ptr.getType() == MWWorld::NeutralBase::sRecordId)
        {
            const MWWorld::NeutralBase* base = ptr.get<MWWorld::NeutralBase>()->mBase;
            if (base->mLight && !base->mLight->offByDefault)
            {
                neutralLight = MWWorld::makeNeutralEsmLight(*base->mLight);
                light = &*neutralLight;
            }
        }

        MWAnim::Context& ctx = allowLight ? context : noParticlesContext;

        Misc::LoadTrace::Scope traceCreate("model.create");
        auto anim = MWAnim::createObject(mesh, ptr.getClass().useAnim(), light, ctx);
        traceCreate.end();

        // Graphic herbalism changes one plant's switch, so that plant needs its own copy.
        const bool harvestable = Settings::game().mGraphicHerbalism && ptr.getType() == ESM::Container::sRecordId
            && (ptr.get<ESM::Container>()->mBase->mFlags & ESM::Container::Organic);
        if (harvestable)
            makeSwitchesUnique(*anim->nodeToAddChildrenTo(), Constants::HerbalismLabel);

        loadTransform(ptr, *anim->transform());

        if (auto color = getGlowColor(ptr))
        {
            auto sg = createEnv(*color);
            anim->getOrCreateStateGroup()->stateCommands = sg->stateCommands;
        }

        Misc::LoadTrace::Scope traceCompile("model.compile");
        const bool compiled = context.compileContext->compile(anim->node());
        traceCompile.end();
        if (!compiled)
            return;

        auto& cell = getOrCreateCell(ptr, false);
        cell.group->addChild(anim->node());

        if (Settings::game().mDayNightSwitches)
        {
            for (vsg::Switch* nightDay : findSwitches(*anim->node(), Constants::NightDayLabel))
            {
                setSingleChildOn(*nightDay, nightDay->children.size() > mNightDayMode ? mNightDayMode : 0);
                cell.nightDaySwitches.emplace_back(nightDay);
            }
        }
        if (harvestable && ptr.getRefData().getCustomData() != nullptr
            && !ptr.getClass().getContainerStore(ptr).hasVisibleItems())
        {
            for (vsg::Switch* herbalism : findSwitches(*anim->node(), Constants::HerbalismLabel))
                setSingleChildOn(*herbalism, 1);
        }

        cell.objects.emplace(ptr, std::move(anim));
    }

    void Scene::moveNightDaySwitches(MWAnim::Object& object, Cell& from, Cell* to)
    {
        // Instances of one static model share their switch, so a cell may hold it once per instance: move one
        // registration per switch of this object, never all of them.
        for (vsg::Switch* nightDay : findSwitches(*object.node(), Constants::NightDayLabel))
        {
            auto found = std::find_if(from.nightDaySwitches.begin(), from.nightDaySwitches.end(),
                [nightDay](const vsg::ref_ptr<vsg::Switch>& registered) { return registered.get() == nightDay; });
            if (found == from.nightDaySwitches.end())
                continue;
            if (to)
                to->nightDaySwitches.push_back(*found);
            from.nightDaySwitches.erase(found);
        }
    }

    void Scene::setNightDayMode(unsigned int mode)
    {
        if (mode == mNightDayMode)
            return;
        mNightDayMode = mode;
        for (auto& [store, cell] : mObjectCells)
            for (auto& nightDay : cell.nightDaySwitches)
                setSingleChildOn(*nightDay, nightDay->children.size() > mode ? mode : 0);
    }

    void Scene::insertCreature(const MWWorld::Ptr& ptr, const std::string& mesh, bool weaponsShields)
    {
        bool biped = ptr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Bipedal;
        Misc::LoadTrace::Scope traceCreate("creature.create");
        std::unique_ptr<MWAnim::Object> anim;
        if (weaponsShields)
        {
            anim = MWAnim::createCreature<WieldingCreature>(mesh, biped, context);
            static_cast<WieldingCreature&>(*anim).setup(ptr);
        }
        else
            anim = MWAnim::createCreature(mesh, biped, context);
        traceCreate.end();
        Misc::LoadTrace::Scope traceCompile("creature.compile");
        loadTransform(ptr, *anim->transform());

        if (!context.compileContext->compile(anim->node()))
            return;

        addObject(ptr, std::move(anim), true);
    }

    void Scene::addObject(const MWWorld::Ptr& ptr, std::unique_ptr<MWAnim::Object> anim, bool actor)
    {
        auto& cell = getOrCreateCell(ptr, actor);
        cell.group->addChild(anim->node());
        if (!actor && ptr.getType() == ESM::Door::sRecordId)
            cell.doors.push_back(anim.get());
        cell.objects.emplace(ptr, std::move(anim));
        ++mChangeCounter;
    }

    void Scene::insertNPC(const MWWorld::Ptr& ptr)
    {
        Misc::LoadTrace::Scope traceCreate("npc.create");
        std::unique_ptr<MWAnim::Object> anim = std::make_unique<Npc>(context, ptr);
        traceCreate.end();
        Misc::LoadTrace::Scope traceCompile("npc.compile");
        loadTransform(ptr, *anim->transform());
        if (!context.compileContext->compile(anim->node()))
            return;

        addObject(ptr, std::move(anim), true);
    }

    bool Scene::removeObject(const MWWorld::Ptr& ptr)
    {
        auto cell = getCell(ptr, ptr.getClass().isActor());
        if (!cell)
            return false;
        auto iter = cell->objects.find(ptr);
        if (iter != cell->objects.end())
        {
            moveNightDaySwitches(*iter->second, *cell, nullptr);
            vsgUtil::removeChild(cell->group, iter->second->node());
            cell->doors.erase(
                std::remove(cell->doors.begin(), cell->doors.end(), iter->second.get()), cell->doors.end());
            cell->objects.erase(iter);
            ++mChangeCounter;
            return true;
        }
        return false;
    }

    void Scene::removeCell(const MWWorld::CellStore* store)
    {
        ++mChangeCounter;
        auto iter = mActorCells.find(store);
        if (iter != mActorCells.end())
        {
            vsgUtil::removeSwitchChild(mActorGroup, iter->second.group);
            mActorCells.erase(iter);
        }
        auto iter2 = mObjectCells.find(store);
        if (iter2 != mObjectCells.end())
        {
            vsgUtil::removeSwitchChild(mObjectGroup, iter2->second.group);
            mObjectCells.erase(iter2);
        }
    }

    void Scene::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur)
    {
        bool actor = cur.getClass().isActor();
        auto oldcell = getCell(old, actor);
        if (!oldcell)
            return;
        auto& cell = getOrCreateCell(cur, actor);
        auto iter = oldcell->objects.find(old);
        if (iter != oldcell->objects.end())
        {
            auto& anim = iter->second;
            moveNightDaySwitches(*anim, *oldcell, &cell);
            const auto door = std::find(oldcell->doors.begin(), oldcell->doors.end(), anim.get());
            if (door != oldcell->doors.end())
            {
                oldcell->doors.erase(door);
                cell.doors.push_back(anim.get());
            }
            cell.group->addChild(anim->node());
            vsgUtil::removeChild(oldcell->group, anim->node());
            cell.objects[cur] = std::move(anim);
            oldcell->objects.erase(iter);
        }
    }

    MWAnim::Object* Scene::getObject(const MWWorld::Ptr& ptr)
    {
        auto cell = getCell(ptr, ptr.getClass().isActor());
        if (!cell)
            return {};
        auto iter = cell->objects.find(ptr);
        if (iter != cell->objects.end())
            return iter->second.get();
        return {};
    }

    MWWorld::Ptr Scene::getPtr(const vsg::Node* node)
    {
        auto p = getPtr(node, mActorCells);
        if (!p.isEmpty())
            return p;
        return getPtr(node, mObjectCells);
    }

    MWWorld::Ptr Scene::getPtr(const vsg::Node* node, CellMap& map)
    {
        for (auto& [cellstore, cell] : map)
        {
            for (auto& [p, o] : cell.objects)
            {
                if (o->node() == node || o->transform() == node)
                    return p;
            }
        }
        return {};
    }

    void Scene::update(float dt)
    {
        std::vector<vsg::Operation*> operations;
        operations.reserve(mActorCells.size() + mObjectCells.size());
        auto addOperations = [&operations, dt](CellMap& map) {
            for (auto& [cellstore, cell] : map)
            {
                cell.update->dt = dt;
                operations.emplace_back(cell.update);
            }
        };
        addOperations(mActorCells);
        addOperations(mObjectCells);
        // vsgopenmw-optimization-mwrender-scene-updatethreads
        mUpdateThreads->run(operations.begin(), operations.end());
        // Task 0074e (finding 3 of the review of 0074d): the workers are idle again (run ends in waitIdle), so this is
        // the main thread alone: the quiver changes the actors' show() asked for, and their compile. Task 0074f
        // (finding 4 of the review of 0074e): through the object's own hook, which a test reaches.
        for (auto& [cellstore, cell] : mActorCells)
            for (auto& [ptr, object] : cell.objects)
                object->updateOnMainThread();
    }

    namespace
    {
        // Spheres that hold an actor (feet at the transform's origin; reach of a weapon included) and a door (it
        // swings about a hinge at its origin, up to its width). Game units.
        constexpr float actorMotionRadius = 150.f;
        constexpr double actorMotionCentreHeight = 64.0;
        constexpr float doorMotionRadius = 160.f;

        void addMoving(Render::PointShadow::MotionTracker& tracker, const MWAnim::Object& object, float radius,
            double centreHeight)
        {
            const Anim::Transform* t = object.transform();
            if (!t)
                return;
            Render::PointShadow::MotionTracker::Pose pose{};
            pose[0] = t->translation[0];
            pose[1] = t->translation[1];
            pose[2] = t->translation[2];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    pose[static_cast<std::size_t>(3 + i * 3 + j)] = t->rotation(i, j);
            // The key only recognises the object next frame; it is never followed.
            tracker.add(reinterpret_cast<std::uintptr_t>(&object), pose,
                vsg::dvec3(t->translation[0], t->translation[1], t->translation[2] + centreHeight), radius);
        }
    }

    void Scene::collectMoving(Render::PointShadow::MotionTracker& tracker) const
    {
        for (const auto& [cellstore, cell] : mActorCells)
            for (const auto& [ptr, object] : cell.objects)
                addMoving(tracker, *object, actorMotionRadius, actorMotionCentreHeight);
        for (const auto& [cellstore, cell] : mObjectCells)
            for (const MWAnim::Object* door : cell.doors)
                addMoving(tracker, *door, doorMotionRadius, 0.0);
    }

    vsg::ref_ptr<vsgUtil::UpdateThreads> Scene::getUpdateThreads()
    {
        return mUpdateThreads;
    }
}
