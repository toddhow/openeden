#include "scene.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include <vsg/io/Options.h>
#include <vsg/nodes/Switch.h>

#include <components/animation/transform.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/constants.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/mwanimation/create.hpp>
#include <components/mwanimation/sceneryanim.hpp>
#include <components/render/pointshadowcache.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/operation.hpp>
#include <components/vsgutil/removechild.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include <components/resource/resourcesystem.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/neutrallight.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"

#include "bin.hpp"
#include "env.hpp"
#include "mask.hpp"
#include "npc.hpp"
#include <cstdlib>
#include "skyrimactor.hpp"
#include "skyrimidle.hpp"
#include "skyrimlip.hpp"

#include <components/mwanimation/sceneryanim.hpp>
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

    // Task 0096 stage f (`[Game] animate placed scenery keyframes`): a static with a keyframe file and no animation of
    // its own is read again as an animated object and its looping group plays, from a phase taken from the reference
    // number (MWAnim::SceneryKeyframes). Anything else is returned as it was.
    std::unique_ptr<MWAnim::Object> Scene::giveSceneryKeyframes(std::unique_ptr<MWAnim::Object> plain,
        const MWWorld::Ptr& ptr, const std::string& mesh, const ESM::Light* light, MWAnim::Context& ctx)
    {
        const auto hasAutoPlay = [](const MWAnim::Object& object) {
            return std::any_of(object.autoPlay.groups.begin(), object.autoPlay.groups.end(),
                [](const auto& group) { return !group.controllers.empty(); });
        };
        if (!plain || mesh.empty() || !ctx.hasKeyframeFile || !ctx.hasKeyframeFile(mesh))
            return plain;
        if (!MWAnim::SceneryKeyframes::applies(true, true, hasAutoPlay(*plain)))
            return plain;
        std::unique_ptr<MWAnim::Object> animated;
        try
        {
            animated = MWAnim::createObject(mesh, true, light, ctx);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Scenery keyframes: can not read '" << mesh
                                << "' as an animated model: " << e.what();
            return plain;
        }
        if (!animated || !animated->animation || hasAutoPlay(*animated))
            return plain;
        const auto sources = ctx.readAnimation(mesh);
        const std::string group = sources ? MWAnim::SceneryKeyframes::chooseGroup(*sources->tags) : std::string();
        const ESM::RefNum refNum = ptr.getCellRef().getRefNum();
        const std::uint64_t id
            = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(refNum.mContentFile)) << 32) | refNum.mIndex;
        if (!MWAnim::SceneryKeyframes::start(*animated->animation, group, id))
            return plain;
        {
            // One line per model (the first object of each), so a log shows which scenery got keyframes.
            static std::mutex mutex;
            static std::set<std::string> logged;
            const std::lock_guard<std::mutex> lock(mutex);
            if (logged.insert(mesh).second)
                Log(Debug::Info) << "Scenery keyframes: " << mesh << " plays group '" << group << "' (first object "
                                 << ptr.getCellRef().getRefId() << ", phase " << MWAnim::SceneryKeyframes::phase(id)
                                 << ")";
        }
        auto driver = vsg::ref_ptr<MWAnim::SceneryKeyframes::Driver>(
            new MWAnim::SceneryKeyframes::Driver(animated->animation.get()));
        animated->keepAlive.emplace_back(driver);
        animated->autoPlay.add(driver, animated->transform());
        return animated;
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
        if (Settings::game().mAnimatePlacedSceneryKeyframes && !ptr.getClass().useAnim())
            anim = giveSceneryKeyframes(std::move(anim), ptr, mesh, light, ctx);

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

    namespace
    {
        // Tasks 0132 and 0145: loops the ambient idle clip of the actor's sex on its bones. The clip is decoded on a worker
        // (started by the first actor of the skeleton and sex, shared by the rest); until it is ready, and when the files
        // are missing or damaged, the actor stays in the rest pose (the loader logs a failure once). Nothing here waits.
        void attachNeutralIdle(const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance,
            MWAnim::ObjectAnimation& anim, const VFS::Manager& vfs)
        {
            if (!anim.animation)
                return;
            // The ordinary standing idle of a person (task 0145: `mt_idle` in the male or female folder; the unarmed
            // `h2h_idle` of the first slice is a fighting stance).
            SkyrimIdle::ClipFuture future = SkyrimIdle::loadIdleAsync(
                vfs, appearance.skeletonPath, appearance.female ? "female" : "male", SkyrimIdle::sAmbientIdle);
            SkyrimIdle::Controller::BoneMap bones;
            anim.animation->bones.forEach([&bones](const std::string& name, Anim::Bone& bone) {
                if (!bone.path.empty())
                    bones.emplace(name, bone.path.back());
            });
            // Out of step with the next actor, and the same on every visit: from the reference alone.
            const std::string id = ptr.getCellRef().getRefId().serializeText();
            const float phase = MWAnim::SceneryKeyframes::phase(static_cast<std::uint64_t>(std::hash<std::string>{}(id)));
            auto controller = vsg::ref_ptr<SkyrimIdle::Controller>(
                new SkyrimIdle::Controller(std::move(future), std::move(bones), phase));
            anim.addController(controller, anim.transform());
            anim.keepAlive.push_back(controller);
        }
    }

    void Scene::insertNeutralActor(const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance)
    {
        // Task 0121 (decision 0017, stage N5-2): a game pack's actor, drawn in its rest pose. The object is the
        // ordinary animation object a creature gets (bones plus skinned geometry under one root), so the engine's
        // animation system links the skins; nothing moves it yet. Kept in the plain object group, not the actor group:
        // Class::isActor() is false for a neutral object, and removeObject looks the cell up by that.
        Misc::LoadTrace::Scope traceCreate("neutralactor.create");
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (resources == nullptr || !resources->builder || resources->getVFS() == nullptr)
            return;
        const SkyrimModel::BuildContext buildContext{ *resources->builder, resources->textureOptions, nullptr,
            Bin_DepthSorted, "actor" };
        SkyrimActor::Report report;
        vsg::ref_ptr<vsg::Node> node
            = SkyrimActor::buildActorNode(appearance, *resources->getVFS(), buildContext, report);
        if (!node)
        {
            Log(Debug::Warning) << "Skyrim actor " << ptr.getCellRef().getRefId() << " not drawn: " << report.mFailure;
            return;
        }
        std::unique_ptr<MWAnim::ObjectAnimation> anim
            = MWAnim::create<MWAnim::ObjectAnimation>(context, node, nullptr, true);
        traceCreate.end();
        Log(Debug::Verbose) << "Skyrim actor " << ptr.getCellRef().getRefId() << ": " << report.mPartsDrawn << " of "
                            << report.mPartsAsked << " parts drawn, " << report.mBones << " bones, " << report.mDraws
                            << " draws | " << SkyrimActor::describeTotals();
        if (Settings::game().mNeutralActorIdles)
            attachNeutralIdle(ptr, appearance, *anim, *resources->getVFS());
        if (report.mLip)
            anim->keepAlive.push_back(report.mLip); // found again by setNeutralMouth; dies with the object
        loadTransform(ptr, *anim->transform());
        Misc::LoadTrace::Scope traceCompile("neutralactor.compile");
        if (!context.compileContext->compile(anim->node()))
            return;
        addObject(ptr, std::move(anim), false);
    }

    void Scene::setNeutralMouth(const MWWorld::Ptr& ptr, float aah, float bigAah)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        for (const vsg::ref_ptr<vsg::Object>& kept : iter->second->keepAlive)
            if (auto* morph = dynamic_cast<SkyrimLip::HeadMorph*>(kept.get()))
            {
                static const bool trace = std::getenv("OPENEDEN_NEUTRAL_LIP_TRACE") != nullptr;
                if (morph->setWeights({ TriMorph::quantise(aah), TriMorph::quantise(bigAah) }) && trace)
                    Log(Debug::Info) << "NLIP aah=" << morph->weights().mAah << " bigaah=" << morph->weights().mBigAah
                                     << " uploads=" << morph->uploads();
                return;
            }
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
