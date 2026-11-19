#include "scene.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include <vsg/io/Options.h>
#include <vsg/nodes/Switch.h>

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/mwanimation/create.hpp>
#include <components/mwanimation/sceneryanim.hpp>
#include <components/render/pointshadowcache.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/computetransform.hpp>
#include <components/vsgutil/operation.hpp>
#include <components/vsgutil/removechild.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include <components/resource/resourcesystem.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/neutraldoor.hpp"
#include "../mwworld/neutrallight.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"

#include "bin.hpp"
#include "env.hpp"
#include "mask.hpp"
#include "npc.hpp"
#include "skyrimactor.hpp"
#include "skyrimclipmeta.hpp"
#include "skyrimfirstperson.hpp"
#include "skyrimheldgear.hpp"
#include "skyrimidle.hpp"
#include "skyrimlip.hpp"
#include "skyrimlocomotion.hpp"
#include "skyrimpose.hpp"
#include "skyrimragdoll.hpp"
#include "skyrimragdollmap.hpp"
#include "skyrimsequence.hpp"
#include <components/vsgutil/addchildren.hpp>
#include <cstdlib>

#include "switches.hpp"
#include "wieldingcreature.hpp"
#include <components/mwanimation/sceneryanim.hpp>

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

    std::unique_ptr<MWAnim::Object> Scene::makeNeutralSequenceObject(
        const MWWorld::Ptr& ptr, const std::string& mesh, MWAnim::Context& ctx)
    {
        const auto kind = ptr.get<MWWorld::NeutralBase>()->mBase->mKind;
        // Task 0289: activators and furniture (puzzle pillars, gates, levers) animated by Havok clips too.
        if (mesh.empty()
            || (kind != openeden::sdk::game::ObjectKind::Door && kind != openeden::sdk::game::ObjectKind::Container
                && kind != openeden::sdk::game::ObjectKind::Activator
                && kind != openeden::sdk::game::ObjectKind::Furniture))
            return nullptr;
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (resources == nullptr || !resources->builder || resources->getVFS() == nullptr)
            return nullptr;
        const std::shared_ptr<const SkyrimSequence::Model> model
            = SkyrimSequence::loadModel(*resources->getVFS(), mesh, Settings::game().mNeutralWorldMaterials,
                kind == openeden::sdk::game::ObjectKind::Activator || kind == openeden::sdk::game::ObjectKind::Furniture);
        if (!model)
            return nullptr;
        const SkyrimModel::BuildContext buildContext{ *resources->builder, resources->textureOptions, nullptr,
            Bin_DepthSorted, mesh, {} };
        SkyrimSequence::Built built = SkyrimSequence::build(model, buildContext);
        if (!built.mNode || !built.mInstance)
            return nullptr;
        auto object = std::make_unique<MWAnim::Object>(ctx);
        vsgUtil::addChildren(*object->nodeToAddChildrenTo(), *built.mNode);
        object->addController(built.mInstance, object->transform());
        object->keepAlive.push_back(built.mInstance);
        return object;
    }

    namespace
    {
        SkyrimSequence::Instance* sequenceOf(MWAnim::Object& object)
        {
            for (const vsg::ref_ptr<vsg::Object>& kept : object.keepAlive)
                if (auto* instance = dynamic_cast<SkyrimSequence::Instance*>(kept.get()))
                    return instance;
            return nullptr;
        }
    }

    bool Scene::neutralObjectHasSequence(const MWWorld::Ptr& ptr)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return false;
        const auto iter = cell->objects.find(ptr);
        return iter != cell->objects.end() && sequenceOf(*iter->second) != nullptr;
    }

    void Scene::setNeutralObjectOpen(const MWWorld::Ptr& ptr, float progress, bool closing)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        if (SkyrimSequence::Instance* instance = sequenceOf(*iter->second))
            instance->setProgress(progress, closing);
    }

    void Scene::setNeutralObjectTarget(const MWWorld::Ptr& ptr, bool open)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        SkyrimSequence::Instance* instance = sequenceOf(*iter->second);
        static const bool trace = std::getenv("OPENEDEN_NEUTRAL_SEQUENCE_TRACE") != nullptr;
        if (trace)
            Log(Debug::Info) << "NSEQ lid " << ptr.getCellRef().getRefId() << " target " << (open ? "open" : "closed")
                             << (instance != nullptr ? "" : " (no sequence on the object)");
        if (instance != nullptr)
            instance->setTarget(open);
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
                neutralLight = MWWorld::makeNeutralEsmLight(*base->mLight, Settings::game().mNeutralLightFadeGain);
                light = &*neutralLight;
                // OPENEDEN_NEUTRAL_LIGHT_TRACE=1 (developer aid, task 0355): what each neutral light becomes.
                static const bool lightTrace = std::getenv("OPENEDEN_NEUTRAL_LIGHT_TRACE") != nullptr;
                if (lightTrace)
                {
                    const ESM::Position& p = ptr.getRefData().getPosition();
                    const std::uint32_t c = neutralLight->mData.mColor;
                    Log(Debug::Info) << "NLIGHTOBJ " << ptr.getCellRef().getRefId().toDebugString() << " radius "
                                     << neutralLight->mData.mRadius << " colour " << (c & 255) << "," << ((c >> 8) & 255)
                                     << "," << ((c >> 16) & 255) << " fade " << base->mLight->fade << " flags "
                                     << base->mLight->flags << " pos " << p.pos[0] << "," << p.pos[1] << "," << p.pos[2]
                                     << " model " << (mesh.empty() ? "none" : mesh);
                }
            }
        }

        MWAnim::Context& ctx = allowLight ? context : noParticlesContext;

        Misc::LoadTrace::Scope traceCreate("model.create");
        // Task 0232: a Skyrim door or chest whose model has an Open sequence is built with its moving parts apart.
        std::unique_ptr<MWAnim::Object> anim;
        if (Settings::game().mNeutralObjectAnimations && ptr.getType() == MWWorld::NeutralBase::sRecordId)
            anim = makeNeutralSequenceObject(ptr, mesh, ctx);
        if (!anim)
            anim = MWAnim::createObject(mesh, ptr.getClass().useAnim(), light, ctx);
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
        // Tasks 0132 and 0145: loops the ambient idle clip of the actor's sex on its bones. The clip is decoded on a
        // worker (started by the first actor of the skeleton and sex, shared by the rest); until it is ready, and when
        // the files are missing or damaged, the actor stays in the rest pose (the loader logs a failure once). Nothing
        // here waits.
        void attachNeutralIdle(const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance,
            MWAnim::ObjectAnimation& anim, const VFS::Manager& vfs, bool forceLocomotion)
        {
            if (!anim.animation)
                return;
            // The ordinary standing idle of a person (task 0145: `mt_idle` in the male or female folder; the unarmed
            // `h2h_idle` of the first slice is a fighting stance).
            // Task 0212: a creature's clips are in its actor folder's animations folder, with no sex folder, under the
            // names the pack gives (an empty name is the person's).
            const bool creature = !appearance.animationProject.empty();
            const std::string_view sex = creature ? std::string_view() : appearance.female ? "female" : "male";
            const std::string_view idleName
                = !appearance.idleClip.empty() ? std::string_view(appearance.idleClip) : SkyrimIdle::sAmbientIdle;
            const std::string_view walkName
                = !appearance.walkClip.empty() ? std::string_view(appearance.walkClip) : SkyrimLocomotion::sWalkClip;
            const std::string_view runName
                = !appearance.runClip.empty() ? std::string_view(appearance.runClip) : SkyrimLocomotion::sRunClip;
            SkyrimIdle::ClipFuture future = SkyrimIdle::loadIdleAsync(vfs, appearance.skeletonPath, sex, idleName);
            SkyrimIdle::Controller::BoneMap bones;
            anim.animation->bones.forEach([&bones](const std::string& name, Anim::Bone& bone) {
                if (!bone.path.empty())
                    bones.emplace(name, bone.path.back());
            });
            // Out of step with the next actor, and the same on every visit: from the reference alone.
            const std::string id = ptr.getCellRef().getRefId().serializeText();
            const float phase
                = MWAnim::SceneryKeyframes::phase(static_cast<std::uint64_t>(std::hash<std::string>{}(id)));
            // Task 0135 stage S6b: the furniture pose controller, added after the idle so that it wins while it is on.
            auto addPose = [&]() {
                auto pose = vsg::ref_ptr<SkyrimPose::Controller>(
                    new SkyrimPose::Controller(vfs, appearance.skeletonPath, appearance.female, bones));
                pose->setProject(appearance.animationProject); // task 0212: empty for a person
                anim.addController(pose, anim.transform());
                anim.keepAlive.push_back(pose);
            };
            if (forceLocomotion || Settings::game().mNeutralActorLocomotion)
            {
                // Task 0148: the idle, walk and run cycles blended by the ground speed the physics side reports.
                HavokPack::Gait::Params gait;
                if (creature)
                {
                    // Task 0212: the creature's own speeds and strides; a left foot is not known for an animal, so the
                    // cycles start at 0.
                    if (appearance.walkSpeed > 0.f)
                        gait.mWalkSpeed = appearance.walkSpeed;
                    if (appearance.runSpeed > 0.f)
                        gait.mRunSpeed = appearance.runSpeed;
                    if (appearance.walkStride > 0.f)
                        gait.mWalkStride = appearance.walkStride;
                    if (appearance.runStride > 0.f)
                        gait.mRunStride = appearance.runStride;
                    gait.mWalkLeftFoot = 0.f;
                    gait.mRunLeftFoot = 0.f;
                }
                auto controller = vsg::ref_ptr<SkyrimLocomotion::Controller>(new SkyrimLocomotion::Controller(
                    std::move(future), SkyrimIdle::loadIdleAsync(vfs, appearance.skeletonPath, sex, walkName),
                    SkyrimIdle::loadIdleAsync(vfs, appearance.skeletonPath, sex, runName), bones, phase, gait));
                anim.addController(controller, anim.transform());
                anim.keepAlive.push_back(controller);
                addPose();
                return;
            }
            auto controller
                = vsg::ref_ptr<SkyrimIdle::Controller>(new SkyrimIdle::Controller(std::move(future), bones, phase));
            anim.addController(controller, anim.transform());
            anim.keepAlive.push_back(controller);
            addPose();
        }

        // Task 0174 (S5-5): the death ragdoll's bone table for a skeleton, made once per skeleton file and process
        // (the baked map is read from or written to <user data>/openeden/skeletonmaps). A skeleton that gives no table
        // (a creature's, a damaged one) is remembered as such and logged once.
        std::mutex sRagdollTableMutex;
        std::map<std::string, std::shared_ptr<const SkyrimRagdoll::BoneTable>> sRagdollTables;

        std::shared_ptr<const SkyrimRagdoll::BoneTable> ragdollTableFor(
            const std::string& skeletonPath, const VFS::Manager& vfs)
        {
            std::scoped_lock lock(sRagdollTableMutex);
            const auto found = sRagdollTables.find(skeletonPath);
            if (found != sRagdollTables.end())
                return found->second;
            std::shared_ptr<const SkyrimRagdoll::BoneTable> table;
            NifScene::Skeleton::Plan plan;
            std::string error;
            if (SkyrimActor::readSkeletonPlan(vfs, skeletonPath, plan, error))
                table = SkyrimRagdoll::makeBoneTable(plan, SkyrimRagdoll::configuredMapFolder(), error);
            if (table)
                Log(Debug::Info) << "Ragdoll bones for " << skeletonPath << ": " << table->resolved() << " of "
                                 << table->set().mMappings.size() << " rig bones (" << table->origin()
                                 << " skeleton map " << table->digest().substr(0, 16) << ")";
            else
                Log(Debug::Warning) << "No death ragdoll for the skeleton " << skeletonPath << ": " << error
                                    << " (its actors keep the death clip's last pose)";
            sRagdollTables.emplace(skeletonPath, table);
            return table;
        }

        void attachNeutralRagdoll(const openeden::sdk::game::ActorAppearance& appearance, MWAnim::ObjectAnimation& anim,
            const VFS::Manager& vfs)
        {
            if (!anim.animation)
                return;
            std::shared_ptr<const SkyrimRagdoll::BoneTable> table = ragdollTableFor(appearance.skeletonPath, vfs);
            if (!table)
                return;
            auto controller = vsg::ref_ptr<SkyrimRagdoll::Controller>(
                new SkyrimRagdoll::Controller(std::move(table), anim.animation->bones, anim.transform()));
            anim.addController(controller, anim.transform());
            anim.keepAlive.push_back(controller);
        }
    }

    std::unique_ptr<MWAnim::ObjectAnimation> Scene::makeNeutralActorObject(
        const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance, bool player)
    {
        // Task 0121 (decision 0017, stage N5-2): a game pack's actor, drawn in its rest pose. The object is the
        // ordinary animation object a creature gets (bones plus skinned geometry under one root), so the engine's
        // animation system links the skins. Task 0222: this is also how the player's own body is made (`player`: the
        // idle, walk and run controllers are always added and there is no ragdoll); the caller places and compiles it.
        Misc::LoadTrace::Scope traceCreate("neutralactor.create");
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (resources == nullptr || !resources->builder || resources->getVFS() == nullptr)
            return nullptr;
        const SkyrimModel::BuildContext buildContext{ *resources->builder, resources->textureOptions, nullptr,
            Bin_DepthSorted, "actor", {} };
        SkyrimActor::Report report;
        vsg::ref_ptr<vsg::Node> node
            = SkyrimActor::buildActorNode(appearance, *resources->getVFS(), buildContext, report);
        if (!node)
        {
            Log(Debug::Warning) << "Skyrim actor " << ptr.getCellRef().getRefId() << " not drawn: " << report.mFailure;
            return nullptr;
        }
        std::unique_ptr<MWAnim::ObjectAnimation> anim
            = MWAnim::create<MWAnim::ObjectAnimation>(context, node, nullptr, true);
        traceCreate.end();
        Log(Debug::Verbose) << "Skyrim actor " << ptr.getCellRef().getRefId() << ": " << report.mPartsDrawn << " of "
                            << report.mPartsAsked << " parts drawn, " << report.mBones << " bones, " << report.mDraws
                            << " draws | " << SkyrimActor::describeTotals();
        if (player || Settings::game().mNeutralActorIdles)
            attachNeutralIdle(ptr, appearance, *anim, *resources->getVFS(), player);
        // Task 0174: after the pose controllers, so that the ragdoll's bone writes win once it is armed.
        if (!player && Settings::game().mNeutralCombat && Settings::physics().mCreativeMotionRagdolls)
            attachNeutralRagdoll(appearance, *anim, *resources->getVFS());
        if (report.mLip)
            anim->keepAlive.push_back(report.mLip); // found again by setNeutralMouth; dies with the object
        // Task 0220: the weapon and shield the pack names (empty unless `[extension.skyrimse] held gear`).
        {
            SkyrimHeldGear::Report held;
            SkyrimHeldGear::attach(*anim, appearance, *resources->getVFS(), buildContext, held);
            if (held.mWeaponAttached || held.mShieldAttached || !held.mFailure.empty())
                Log(Debug::Verbose) << "Skyrim held gear " << ptr.getCellRef().getRefId() << ": weapon "
                                    << (held.mWeaponAttached ? held.mSheathNode : std::string("none")) << ", shield "
                                    << (held.mShieldAttached ? "yes" : "no")
                                    << (held.mFailure.empty() ? "" : " | " + held.mFailure);
        }
        return anim;
    }

    std::unique_ptr<MWAnim::ObjectAnimation> Scene::makeFirstPersonArms(
        const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance)
    {
        // Task 0245: the first-person arms. The same builder as any neutral actor (skeleton plus skinned parts), the
        // first-person skeleton and models, and one controller that plays the arms' own clips (not the third-person
        // idle, walk and run).
        Misc::LoadTrace::Scope traceCreate("firstperson.create");
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (resources == nullptr || !resources->builder || resources->getVFS() == nullptr)
            return nullptr;
        const SkyrimModel::BuildContext buildContext{ *resources->builder, resources->textureOptions, nullptr,
            Bin_DepthSorted, "actor", {} };
        SkyrimActor::Report report;
        vsg::ref_ptr<vsg::Node> node
            = SkyrimActor::buildActorNode(appearance, *resources->getVFS(), buildContext, report);
        if (!node)
        {
            Log(Debug::Warning) << "Skyrim first-person arms not drawn: " << report.mFailure;
            return nullptr;
        }
        std::unique_ptr<MWAnim::ObjectAnimation> anim
            = MWAnim::create<MWAnim::ObjectAnimation>(context, node, nullptr, true);
        Log(Debug::Verbose) << "Skyrim first-person arms: " << report.mPartsDrawn << " of " << report.mPartsAsked
                            << " parts drawn, " << report.mBones << " bones, " << report.mDraws << " draws";
        if (anim->animation)
        {
            SkyrimIdle::Controller::BoneMap bones;
            anim->animation->bones.forEach([&bones](const std::string& name, Anim::Bone& bone) {
                if (!bone.path.empty())
                    bones.emplace(name, bone.path.back());
            });
            auto arms = vsg::ref_ptr<SkyrimFirstPerson::ArmsController>(
                new SkyrimFirstPerson::ArmsController(*resources->getVFS(), std::move(bones)));
            arms->setIdle(SkyrimFirstPerson::idleClipFor(0, SkyrimFirstPerson::Stance::Sheathed));
            anim->addController(arms, anim->transform());
            anim->keepAlive.push_back(arms);
        }
        else
            Log(Debug::Warning) << "Skyrim first-person arms: the skeleton gave no bones; the arms stay still";
        SkyrimHeldGear::Report held;
        SkyrimHeldGear::attach(*anim, appearance, *resources->getVFS(), buildContext, held);
        if (held.mWeaponAttached || held.mShieldAttached || !held.mFailure.empty())
            Log(Debug::Verbose) << "Skyrim first-person held gear: weapon "
                                << (held.mWeaponAttached ? held.mSheathNode : std::string("none")) << ", shield "
                                << (held.mShieldAttached ? "yes" : "no")
                                << (held.mFailure.empty() ? "" : " | " + held.mFailure);
        return anim;
    }

    void Scene::insertNeutralActor(const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance)
    {
        // Kept in the plain object group, not the actor group: Class::isActor() is false for a neutral object, and
        // removeObject looks the cell up by that.
        std::unique_ptr<MWAnim::ObjectAnimation> anim = makeNeutralActorObject(ptr, appearance, false);
        if (!anim)
            return;
        loadTransform(ptr, *anim->transform());
        Misc::LoadTrace::Scope traceCompile("neutralactor.compile");
        if (!context.compileContext->compile(anim->node()))
            return;
        addObject(ptr, std::move(anim), false);
    }

    void Scene::setNeutralMouth(const MWWorld::Ptr& ptr, const std::array<float, 16>& visemes)
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
                if (morph->setVisemes(visemes) && trace)
                {
                    Log(Debug::Info) << "NLIP uploads=" << morph->uploads() << " w=" << [&] {
                        std::string s;
                        for (float w : morph->weights())
                            s += std::to_string(static_cast<int>(w * 16.f + 0.5f)) + ' ';
                        return s;
                    }();
                }
                return;
            }
    }

    void Scene::setNeutralActorSpeed(const MWWorld::Ptr& ptr, float unitsPerSecond)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        setNeutralObjectSpeed(*iter->second, unitsPerSecond);
    }

    void Scene::setNeutralObjectSpeed(MWAnim::Object& object, float unitsPerSecond)
    {
        for (const vsg::ref_ptr<vsg::Object>& kept : object.keepAlive)
            if (auto* loco = dynamic_cast<SkyrimLocomotion::Controller*>(kept.get()))
            {
                loco->setGroundSpeed(unitsPerSecond);
                return;
            }
    }

    void Scene::setNeutralActorPose(const MWWorld::Ptr& ptr, std::string_view clip, bool perSex)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        for (const vsg::ref_ptr<vsg::Object>& kept : iter->second->keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
            {
                pose->setClip(clip, perSex);
                return;
            }
    }

    void Scene::playNeutralActorClip(const MWWorld::Ptr& ptr, std::string_view clip, bool perSex)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        playNeutralObjectClip(*iter->second, clip, perSex);
    }

    void Scene::setNeutralObjectPose(MWAnim::Object& object, std::string_view clip, bool perSex)
    {
        for (const vsg::ref_ptr<vsg::Object>& kept : object.keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
            {
                pose->setClip(clip, perSex);
                return;
            }
    }

    void Scene::playNeutralObjectPaired(MWAnim::Object& object, std::string_view clip)
    {
        for (const vsg::ref_ptr<vsg::Object>& kept : object.keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
            {
                pose->playOnce(clip, false);
                return;
            }
    }

    openeden::sdk::game::PairedStatus Scene::neutralPairedClipInfo(
        std::string_view clip, openeden::sdk::game::PairedClipInfo& out)
    {
        out = {};
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (resources == nullptr || resources->getVFS() == nullptr)
            return openeden::sdk::game::PairedStatus::Failed;
        return SkyrimIdle::pairedClipInfo(*resources->getVFS(), clip, out);
    }

    void Scene::playNeutralObjectClip(MWAnim::Object& object, std::string_view clip, bool perSex)
    {
        if (!clip.empty())
            SkyrimHeldGear::noteCombat(object.keepAlive); // task 0220: the weapon comes to the hand
        for (const vsg::ref_ptr<vsg::Object>& kept : object.keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
            {
                pose->playOnce(clip, perSex);
                return;
            }
    }

    void Scene::setNeutralActorClipSpeed(const MWWorld::Ptr& ptr, float multiplier)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return;
        for (const vsg::ref_ptr<vsg::Object>& kept : iter->second->keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
            {
                pose->setRate(multiplier);
                return;
            }
    }

    MWWorld::Ptr Scene::findNeutralObject(std::uint32_t placement)
    {
        for (const CellMap* cells : { &mObjectCells, &mActorCells })
            for (const auto& [store, data] : *cells)
                for (const auto& [ptr, object] : data.objects)
                {
                    const auto id = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
                    if (id && *id == placement)
                        return ptr;
                }
        return MWWorld::Ptr();
    }

    std::vector<const Anim::Transform*> Scene::neutralObjectBonePath(const MWWorld::Ptr& ptr, const std::string& bone)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return {};
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end() || !iter->second)
            return {};
        const std::vector<const Anim::Transform*> nodes = iter->second->worldTransform(bone);
        if (nodes.size() < 2)
            return {};
        // The path starts with the object's own transform; the bone's place in the object's frame is the rest.
        return std::vector<const Anim::Transform*>(nodes.begin() + 1, nodes.end());
    }

    HavokPack::ClipProgress Scene::neutralActorClipProgress(const MWWorld::Ptr& ptr)
    {
        return neutralActorClipProgressWaiting(ptr, 0);
    }

    HavokPack::ClipProgress Scene::neutralActorClipProgressWaiting(const MWWorld::Ptr& ptr, int waitMilliseconds)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return {};
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return {};
        for (const vsg::ref_ptr<vsg::Object>& kept : iter->second->keepAlive)
            if (auto* pose = dynamic_cast<SkyrimPose::Controller*>(kept.get()))
                return pose->progress(std::chrono::milliseconds(waitMilliseconds));
        return {};
    }

    SkyrimRagdoll::Controller* Scene::findNeutralRagdoll(const MWWorld::Ptr& ptr)
    {
        auto cell = getCell(ptr, false);
        if (!cell)
            return nullptr;
        const auto iter = cell->objects.find(ptr);
        if (iter == cell->objects.end())
            return nullptr;
        for (const vsg::ref_ptr<vsg::Object>& kept : iter->second->keepAlive)
            if (auto* ragdoll = dynamic_cast<SkyrimRagdoll::Controller*>(kept.get()))
                return ragdoll;
        return nullptr;
    }

    bool Scene::neutralRagdollStartPose(const MWWorld::Ptr& ptr, MWPhysics::ActorStartPose& out)
    {
        SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr);
        return ragdoll != nullptr && ragdoll->startPose(out);
    }

    bool Scene::beginNeutralRagdoll(const MWWorld::Ptr& ptr, std::span<const std::string> names,
        std::span<const MWPhysics::RagdollBoneState> initial, std::string& reason)
    {
        SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr);
        if (ragdoll == nullptr)
        {
            reason = "the actor has no ragdoll controller";
            return false;
        }
        return ragdoll->arm(names, initial, reason);
    }

    void Scene::setNeutralRagdollStates(const MWWorld::Ptr& ptr, std::span<const MWPhysics::RagdollBoneState> states)
    {
        if (SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr))
            ragdoll->setStates(states);
    }

    void Scene::endNeutralRagdoll(const MWWorld::Ptr& ptr)
    {
        if (SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr))
            ragdoll->disarm();
    }

    bool Scene::beginNeutralRagdollFrozen(const MWWorld::Ptr& ptr, std::span<const std::string> names,
        std::span<const MWPhysics::RagdollBoneState> states, std::string& reason)
    {
        SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr);
        if (ragdoll == nullptr)
        {
            reason = "the actor has no ragdoll controller";
            return false;
        }
        return ragdoll->armFrozen(names, states, reason);
    }

    bool Scene::neutralRagdollDrivenPose(
        const MWWorld::Ptr& ptr, std::vector<std::string>& names, std::vector<MWPhysics::RagdollBoneState>& states)
    {
        SkyrimRagdoll::Controller* ragdoll = findNeutralRagdoll(ptr);
        return ragdoll != nullptr && ragdoll->drivenPose(names, states);
    }

    std::shared_ptr<const HavokPack::ClipMeta> Scene::neutralClipMeta(std::string_view clip, bool perSex)
    {
        // The player has no skeleton of its own here: the male character skeleton and the male project's sidecars give
        // the clip's timing (HitFrame, length), which is all the hidden swing clock needs.
        static constexpr std::string_view kSkeleton = "actors/character/character assets/skeleton.nif";
        // The lookup allocates nothing and a known meta is answered from the cache (review fix of task 0171: this was a
        // string key built per call). A read that failed is tried once more later and reported once per clip.
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const PlayerClipMetaCache::Answer answer = mPlayerClipMeta.lookup(clip, perSex, now);
        if (answer.mWarn)
            Log(Debug::Warning) << "Combat: the timing of the clip '" << clip << "' could not be read";
        if (answer.mStart)
        {
            Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
            if (resources == nullptr || resources->getVFS() == nullptr)
                return {};
            const VFS::Manager& vfs = *resources->getVFS();
            // Task 0259: a clip named `<creature folder>::<clip>` is a creature's (the werewolf beast's): its skeleton
            // is `actors/<folder>/character assets/skeleton.nif` and its sidecar `<folder>project`.
            const std::size_t separator = clip.find("::");
            if (separator != std::string_view::npos && separator > 0)
            {
                const std::string folder(clip.substr(0, separator));
                const std::string stem(clip.substr(separator + 2));
                SkyrimIdle::ClipFuture creatureClip = SkyrimIdle::loadIdleAsync(
                    vfs, "actors/" + folder + "/character assets/skeleton.nif", std::string_view(), stem);
                static const bool sCreatureRepeatable = std::getenv("OPENEDEN_FIXED_FRAME_TIME") != nullptr;
                mPlayerClipMeta.begin(clip, perSex,
                    SkyrimClipMeta::loadAsync(vfs, false, folder + "project", stem, creatureClip),
                    std::chrono::milliseconds(sCreatureRepeatable ? 2000 : 0));
                return answer.mMeta;
            }
            SkyrimIdle::ClipFuture decoded
                = SkyrimIdle::loadIdleAsync(vfs, std::string(kSkeleton), perSex ? "male" : "", clip);
            // Task 0183: a run with a fixed frame time (every repeatable test run) waits for the read, so the frame on
            // which the player's first swing can start does not depend on a worker's speed; a game that is played does
            // not block.
            static const bool sRepeatable = std::getenv("OPENEDEN_FIXED_FRAME_TIME") != nullptr;
            mPlayerClipMeta.begin(clip, perSex, SkyrimClipMeta::loadAsync(vfs, false, std::string(clip), decoded),
                std::chrono::milliseconds(sRepeatable ? 2000 : 0));
        }
        return answer.mMeta;
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
