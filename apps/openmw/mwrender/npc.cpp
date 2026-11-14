#include "npc.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string_view>

#include <components/animation/context.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/skin.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/attach.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/decorate.hpp>
#include <components/mwanimation/face.hpp>
#include <components/mwanimation/groups.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/render/materialoverrides.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/readnode.hpp>
#include <components/vsgutil/removechild.hpp>
#include <components/vsgutil/searchbyname.hpp>
#include <vsg/core/Array.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Switch.h>
#include <vsg/utils/SharedObjects.h>

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/weapontype.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "autoplaytrace.hpp"
#include "bodyparts.hpp"
#include "env.hpp"
#include "equipmentupdate.hpp"
#include "gltfclips.hpp"
#include "mergedbody.hpp"
#include "partreaderrorlog.hpp"
#include "transparency.hpp"

namespace MWRender
{
    namespace
    {
        // Task 0027: body parts of the skin type mark their model as skin for the material model (a no-op without
        // `[Rendering] pbr`). Called before the part is loaded, so the loader sees the mark.
        void markSkinModel(const std::string& model)
        {
            if (model.empty())
                return;
            if (auto& table = MWBase::Environment::get().getResourceSystem()->materialTable)
                table->markSubsurfaceModel(model);
        }

        // Task 0069 stage e, second review (finding 2): OPENEDEN_PARTSLOTS_TRACE set to anything but empty or 0 logs,
        // for every NPC, a "PARTSLOTS <record id> <reference number> v<view>" line for each part node it attaches
        // (with the model) and detaches (with whether a node was there), and after each updateParts the group and
        // priority of all its part slots: a developer trace to compare the part bookkeeping of two builds line by
        // line. Read once.
        bool partSlotsTraceOn()
        {
            static const bool on = [] {
                const char* value = std::getenv("OPENEDEN_PARTSLOTS_TRACE");
                return value != nullptr && *value != '\0' && std::string_view(value) != "0";
            }();
            return on;
        }
    }

    Npc::Npc(const MWAnim::Context& mwctx, const MWWorld::Ptr& ptr, ViewMode viewMode)
        : Wielding(mwctx)
        , mPtr(ptr)
        , mViewMode(viewMode)
        , mNpcType(getNpcType(ptr))
    {
        mPartContext = &mContext;
        mNpc = mPtr.get<ESM::NPC>()->mBase;
        if (autoPlayTraceOn() && autoPlayTraced(mNpc->mId.toString(), mNpc->mName))
            mAutoPlayTraced = mNpc->mId.toString();
        if (partSlotsTraceOn())
        {
            const ESM::RefNum refNum = mPtr.getCellRef().getRefNum();
            mPartSlotsTraced = mNpc->mId.toString() + " " + std::to_string(refNum.mIndex) + ":"
                + std::to_string(refNum.mContentFile) + " v" + std::to_string(static_cast<int>(mViewMode));
        }

        mFace = std::make_unique<MWAnim::Face>();

        updateNpcBase();
    }

    Npc::Npc(const MWAnim::Context& mwctx, TestSetup)
        : Wielding(mwctx)
        , mViewMode(ViewMode::Normal)
        , mNpcType(NpcType::Normal)
    {
        mPartContext = &mContext;
        mNpc = nullptr;
        mFace = std::make_unique<MWAnim::Face>();
    }

    Npc::~Npc() {}

    Npc::NpcType Npc::getNpcType() const
    {
        const MWWorld::Class& cls = mPtr.getClass();
        // Dead vampires should typically stay vampires.
        if (mNpcType == NpcType::Vampire && cls.getNpcStats(mPtr).isDead() && !cls.getNpcStats(mPtr).isWerewolf())
            return mNpcType;
        return getNpcType(mPtr);
    }

    Npc::NpcType Npc::getNpcType(const MWWorld::Ptr& ptr)
    {
        const MWWorld::Class& cls = ptr.getClass();
        Npc::NpcType curType = NpcType::Normal;
        if (cls.getCreatureStats(ptr).getMagicEffects().getOrDefault(ESM::MagicEffect::Vampirism).getMagnitude() > 0)
            curType = NpcType::Vampire;
        if (cls.getNpcStats(ptr).isWerewolf())
            curType = NpcType::Werewolf;
        return curType;
    }

    void Npc::compile()
    {
        if (!mPartContext->compileContext->compile(node()))
        {
            // The merged body goes with the stock parts (review finding 2): an uncompiled clone must not be drawn.
            removeAllParts();
            mHolsteredWeaponSwitch = {};
            mHolsteredShieldSwitch = {};
        }
    }

    void Npc::removeAllParts()
    {
        clearControllers();
        for (size_t i = 0; i < ESM::PRT_Count; ++i)
            removeIndividualPart(static_cast<ESM::PartReferenceType>(i));
        forgetMergedBody();
        removeHolstered(mScabbard, mScabbardClock);
        removeHolstered(mHolsteredShield, mHolsteredShieldClock);
        for (auto& path : mQuiver)
            removePath(path);
        mQuiver.clear();
    }

    void Npc::rebuild()
    {
        rebuild(mViewMode, mPartContext);
    }

    void Npc::rebuild(ViewMode view, const MWAnim::Context* partContext)
    {
        // Task 0074e (finding 1 of the review of 0074d): runRebuild reads everything first, so that a read that throws
        // leaves the actor as it was, and gives the CharacterController the new animation on every way out.
        runRebuild(
            makeBasePlan(view, partContext),
            [this] {
                updateCarriedLeftVisible();
                updateParts();
            },
            [this] { refreshController(); });
    }

    void Npc::refreshController()
    {
        // vsgopenmw-fixme(dependency-policy)
        MWBase::Environment::get().getMechanicsManager()->forceStateUpdate(mPtr);
    }

    int Npc::getSlot(const std::vector<const vsg::Node*>& path) const
    {
        for (int i = 0; i < ESM::PRT_Count; ++i)
        {
            const auto& comparePath = mParts[i];
            if (comparePath.empty())
                continue;
            auto i1 = comparePath.begin();
            auto i2 = path.begin();
            while (i2 != path.end() && i1 != comparePath.end())
            {
                if (*i1 == *i2)
                    ++i1;
                ++i2;
            }
            if (i1 == comparePath.end())
                return mSlots.group(static_cast<ESM::PartReferenceType>(i));
        }
        return -1;
    }

    Npc::AnimationSources Npc::animationSources(const MWAnim::Context& context, bool firstPerson, bool female,
        bool beast, bool werewolf, const std::string& customModel, const ESM::RefId& race)
    {
        AnimationSources result;
        result.defaultSkeleton = context.pickSkeleton(firstPerson, female, beast, werewolf);

        result.skeleton = result.defaultSkeleton;
        if (!firstPerson && !werewolf && !customModel.empty())
            result.skeleton = customModel;
        const std::string& smodel = result.skeleton;
        const std::string& defaultSkeleton = result.defaultSkeleton;

        if (!firstPerson)
        {
            const auto& base = context.files.baseanim;
            // openeden-vsg: the animation source is [Models] xbaseanim, as in OpenMW's NpcAnimation (task 0012).
            if (smodel != base && !werewolf)
                result.sources.emplace_back(Settings::models().mXbaseanim.get().value());

            if (smodel != defaultSkeleton && base != defaultSkeleton)
                result.sources.emplace_back(defaultSkeleton);

            result.sources.emplace_back(smodel);

            if (!werewolf && race.contains("argonian"))
                result.sources.emplace_back(Settings::models().mXargonianswimkna.get().value());
        }
        else
        {
            const auto& base = context.firstPersonFiles.baseanim;
            if (smodel != base && !werewolf)
                result.sources.emplace_back(Settings::models().mXbaseanim1st.get().value());
            result.sources.emplace_back(smodel);
        }
        return result;
    }

    std::vector<Npc::PartModel> Npc::defaultPartModels(const ESM::NPC& npc)
    {
        std::vector<PartModel> result;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const auto& bodyPartStore = store.get<ESM::BodyPart>();
        const bool female = !npc.isMale();

        // The same calls, in the same order, as updateNpcBase makes for a human or beast that is not a werewolf or
        // vampire.
        const std::string head = getHeadModel(false, false, female, npc.mRace, npc.mHead, bodyPartStore);
        if (!head.empty())
        {
            markSkinModel(head);
            result.push_back({ ESM::PRT_Head, head });
        }
        const std::string hair = getHairModel(false, npc.mHair, bodyPartStore);
        if (!hair.empty())
            result.push_back({ ESM::PRT_Hair, hair });

        const std::vector<const ESM::BodyPart*>& parts = getBodyParts(npc.mRace, female, false, false, bodyPartStore);
        for (int part = ESM::PRT_Neck; part < ESM::PRT_Count; ++part)
        {
            if (const ESM::BodyPart* bodypart = parts[part])
            {
                markSkinModel(bodypart->mModel);
                result.push_back({ static_cast<ESM::PartReferenceType>(part), bodypart->mModel });
            }
        }
        return result;
    }

    Npc::BasePlan Npc::makeBasePlan(ViewMode view, const MWAnim::Context* partContext)
    {
        BasePlan plan;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mNpc->mRace);
        NpcType curType = getNpcType();
        bool isWerewolf = (curType == NpcType::Werewolf);
        bool isVampire = (curType == NpcType::Vampire);
        bool isFemale = !mNpc->isMale();

        auto& bodyPartStore = store.get<ESM::BodyPart>();
        plan.mHeadModel = getHeadModel(isWerewolf, isVampire, isFemale, mNpc->mRace, mNpc->mHead, bodyPartStore);
        plan.mHairModel = getHairModel(isWerewolf, mNpc->mHair, bodyPartStore);

        bool is1stPerson = view == ViewMode::FirstPerson;
        bool isBeast = (race->mData.mFlags & ESM::Race::Beast) != 0;

        AnimationSources animSources
            = animationSources(mContext, is1stPerson, isFemale, isBeast, isWerewolf, mNpc->mModel, mNpc->mRace);
        const std::string& defaultSkeleton = animSources.defaultSkeleton;
        const std::string& smodel = animSources.skeleton;
        // Compared as normalised mesh paths: a record names the default skeleton as "base_animKnA.nif" while the
        // settings say "meshes/base_animkna.nif" (review finding 11; correctMeshPath always adds the prefix).
        const auto meshPath = [](const std::string& model) {
            const VFS::Path::Normalized path(model);
            return path.value().starts_with("meshes/") ? path : Misc::ResourceHelpers::correctMeshPath(path);
        };
        plan.mDefaultSkeleton = meshPath(smodel) == meshPath(defaultSkeleton);
        if (!plan.mDefaultSkeleton && MergedBodies::enabled())
            Log(Debug::Info) << "Merged bodies: " << mPtr.getCellRef().getRefId().toString() << " uses the skeleton '"
                             << smodel << "', not its race's default '" << defaultSkeleton << "'";
        plan.mNpcType = curType;
        plan.mViewMode = view;
        plan.mPartContext = partContext;
        plan.mSkeleton = smodel;
        plan.mAnimations = std::move(animSources.sources);
        if (Settings::game().mWeaponSheathing || Settings::game().mShieldSheathing)
        {
            appendCustomBoneFiles(defaultSkeleton, plan.mCustomBones);
            if (smodel != defaultSkeleton)
                appendCustomBoneFiles(smodel, plan.mCustomBones);
        }
        return plan;
    }

    void Npc::applyBasePlan(const BasePlan& plan)
    {
        // Task 0074e (finding 1 of the review of 0074d): every read first, into locals. A read can throw
        // (vsgUtil::readNode and vsg::read_cast read through vsgAdapters::vfs, whose VFS::Manager::get can throw on
        // an Eden Archive entry that fails its check or on an I/O error), and the actor's CharacterController,
        // Actor::mHead, mSpine and mRoot, the Player's neck and the pose and IK bone pointers all point into
        // `animation` and into its bones, which only forceStateUpdate and assignBones refresh. Until the commit below
        // the actor is as it was (its animation and bones, and since 0074f its type, view mode and part context).
        Misc::LoadTrace::Scope traceSkel("npc.readactor");
        vsg::ref_ptr<const vsg::Node> skeleton = mContext.readActor(plan.mSkeleton);
        traceSkel.end();
        std::vector<vsg::ref_ptr<const vsg::Node>> customBones;
        for (const std::string& file : plan.mCustomBones)
            if (auto bones = mContext.readActor(file))
                customBones.push_back(std::move(bones));
        Misc::LoadTrace::Scope traceAnims("npc.readanimations");
        const std::vector<vsg::ref_ptr<Anim::ControllerMap>> animations = mContext.readAnimations(plan.mAnimations);
        traceAnims.end();

        // Nothing below reads a file or throws. Task 0074f (finding 2 of the review of 0074e): the type, view mode and
        // part context the plan was made for are the actor's from here on, and not before: set earlier (updateParts
        // and Player::setViewMode did), a read that threw left the werewolf or vampire with its human base for good
        // (the next updateParts saw no change) and the player in a view whose Play was never built (a repeated
        // setViewMode returned at once).
        if (plan.mNpcType)
            mNpcType = *plan.mNpcType;
        if (plan.mViewMode)
            mViewMode = *plan.mViewMode;
        if (plan.mPartContext)
            mPartContext = plan.mPartContext;
        removeAllParts();
        mHeadModel = plan.mHeadModel;
        mHairModel = plan.mHairModel;
        mDefaultSkeleton = plan.mDefaultSkeleton;
        animation = std::make_unique<MWAnim::Play>(
            MWAnim::Bones(static_cast<const vsg::Node&>(*skeleton), MWAnim::groups), MWAnim::sNumBlendMasks);
        for (const auto& bones : customBones)
            animation->bones.addCustomBones(*bones);
        animation->addAnimSources(animations, plan.mSkeleton);
        // Task 0123: CreativeMotion's authored locomotion clips (glTF), baked for this skeleton. Only with
        // [Game] inertial locomotion on; null otherwise. The file read is cached, and a failure to read is logged once
        // inside (the update has no catch for a throw here).
        if (auto locomotion = GltfClips::loadLocomotionClips(mContext, animation->bones))
            animation->addAnimSources({ locomotion }, "humanoid-locomotion.glb");
        assignBones();
    }

    void Npc::updateNpcBase()
    {
        // The constructor's path (a throw here ends the actor's construction); a rebuild goes through runRebuild.
        applyBasePlan(makeBasePlan());
        Misc::LoadTrace::Scope traceParts("npc.updateparts");
        // Another skeleton may have the shield's sheath bone or not: as OpenMW's viewChange in NpcAnimation::rebuild.
        updateCarriedLeftVisible();
        updateParts();
    }

    void Npc::updateEquipment()
    {
        // Task 0069 stage e review: an equipment change's cost, split into the part update and the compile.
        Misc::LoadTrace::Scope traceEquipment("npc.updateequipment");
        // Task 0074d (review of 0074b, finding 1): compiled also when updateParts throws (runEquipmentUpdate).
        // Task 0074e (finding 5 of the review of 0074d): through runCompiledUpdate, which a test reaches.
        runCompiledUpdate([this] {
            updateCarriedLeftVisible();
            updateParts();
        });
    }

    void Npc::compileTraced()
    {
        Misc::LoadTrace::Scope traceCompile("npc.compile");
        compile();
    }

    void Npc::clearControllers()
    {
        // Task 0074b: not autoPlay, the object's own auto-play groups. Since the parts' auto-play controllers run in
        // their own clocks (mPartControllers) it holds only the inventory lights (addItemLight, added once when the
        // actor's CharacterController is made), which nothing would add again: emptying it froze their flicker for
        // the rest of the actor's life.
        clearPartControllers();
        mFace->clear();
    }

    void Npc::updateParts()
    {
        NpcType curType = getNpcType();
        if (curType != mNpcType)
        {
            // Task 0074f (finding 2): not mNpcType = curType here: the rebuild's plan carries the type, and the actor
            // takes it once the plan's files are read. A rebuild that throws is tried again at the next updateParts.
            rebuild();
            return;
        }

        if (!mAutoPlayTraced.empty())
            traceAutoPlayEvent(mAutoPlayTraced, "updateParts");
        // Task 0074b, finding 6: what stays (parts, the merged body) is linked again on every way out of the update,
        // also when it throws.
        runPartUpdate([this] {
            // Task 0069 stage e, second review (finding 5): the node work and the relinking timed apart.
            Misc::LoadTrace::Scope traceNodes("npc.partnodes");
            updatePartNodes();
        });
        if (!mPartSlotsTraced.empty())
            tracePartSlots();
    }

    void Npc::tracePartSlots() const
    {
        std::ostringstream line;
        line << "PARTSLOTS " << mPartSlotsTraced << " slots";
        for (int part = 0; part < ESM::PRT_Count; ++part)
            line << ' ' << mSlots.groups()[part] << ':' << mSlots.priorities()[part];
        Log(Debug::Info) << line.str();
    }

    void Npc::relinkKeptControllers()
    {
        Misc::LoadTrace::Scope traceRelink("npc.relink");
        // Task 0074: the parts that stayed (body parts, head, hair) get back the controllers clearControllers dropped.
        relinkSurvivingParts();
        // Task 0069 stage e review: the merged body's copy, kept or new (attachMergedBody recorded its controllers).
        if (mMergedNode)
        {
            Misc::LoadTrace::Scope traceMerged("npc.mergedbody.relink");
            relinkMergedBody();
        }
    }

    void Npc::updatePartNodes()
    {
        Misc::LoadTrace::Scope traceInv("npc.inventory");
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        traceInv.end();
        // The equipment pass (task 0069 stage e review: its slot list, priorities and reservations are PartSlots').
        // Second review, finding 11: one Attach for the whole pass, holding two pointers (inside the small buffer of
        // every standard library's std::function), and one list of worn parts kept between updates.
        struct WornSlot
        {
            int mInventorySlot = 0;
            std::optional<vsg::vec4> mGlowColor;
        } current;
        const PartSlots::Attach attach = [this, &current](ESM::PartReferenceType part, const ESM::BodyPart& bodyPart) {
            attachPart(part, current.mInventorySlot, bodyPart.mModel, current.mGlowColor, nullptr);
        };
        for (const PartSlots::EquipmentSlot& slot : PartSlots::equipmentSlots())
        {
            if (mViewMode == ViewMode::HeadOnly)
                break;
            auto store = inv.getSlot(slot.mInventorySlot);
            PartSlots::Worn worn = PartSlots::Worn::Nothing;
            mWornParts.clear();
            current.mInventorySlot = slot.mInventorySlot;
            current.mGlowColor.reset();
            if (store != inv.end())
            {
                current.mGlowColor = getGlowColor(*store);
                if (store->getType() == ESM::Clothing::sRecordId)
                {
                    worn = PartSlots::Worn::Clothing;
                    wornParts(store->get<ESM::Clothing>()->mBase->mParts.mParts, mWornParts);
                }
                else if (store->getType() == ESM::Armor::sRecordId)
                {
                    worn = PartSlots::Worn::Armor;
                    wornParts(store->get<ESM::Armor>()->mBase->mParts.mParts, mWornParts);
                }
                else
                    worn = PartSlots::Worn::Other;
            }
            mSlots.wear(slot, worn, mWornParts, *this, attach);
        }

        if (mViewMode != ViewMode::FirstPerson)
        {
            if (mSlots.priority(ESM::PRT_Head) < 1 && !mHeadModel.empty())
            {
                markSkinModel(mHeadModel);
                addOrReplaceIndividualPart(ESM::PRT_Head, -1, 1, mHeadModel);
            }
            if (mSlots.priority(ESM::PRT_Hair) < 1 && mSlots.priority(ESM::PRT_Head) <= 1 && !mHairModel.empty())
                addOrReplaceIndividualPart(ESM::PRT_Hair, -1, 1, mHairModel);
        }
        if (mViewMode == ViewMode::HeadOnly)
            return;

        addWeapon();
        addCarriedLeft();
        addAmmo();
        updateSheathing();

        bool isWerewolf = (getNpcType() == NpcType::Werewolf);
        ESM::RefId race = isWerewolf ? ESM::RefId::stringRefId("werewolf") : mNpc->mRace;

        // Task 0069 stage d: the merged body, when it may be drawn, takes the slots it covers.
        const bool merged = attachMergedBody(isWerewolf);

        const std::vector<const ESM::BodyPart*>& parts
            = getBodyParts(race, !mNpc->isMale(), mViewMode == ViewMode::FirstPerson, isWerewolf,
                MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>());
        const std::span<const int> skip = merged ? std::span<const int>(mMergedSlots) : std::span<const int>();
        mSlots.addBodyParts(parts, skip, *this, [this](ESM::PartReferenceType part, const ESM::BodyPart& bodyPart) {
            markSkinModel(bodyPart.mModel);
            attachPart(part, -1, bodyPart.mModel, {}, nullptr);
        });
    }

    bool Npc::insertBoundedPart(ESM::PartReferenceType type, const std::string& model, const std::string& bonename,
        std::optional<vsg::vec4> glowColor, const ESM::Light* light)
    {
        if (!mPartSlotsTraced.empty())
            Log(Debug::Info) << "PARTSLOTS " << mPartSlotsTraced << " attach " << static_cast<int>(type) << ' '
                             << model;
        Misc::LoadTrace::Scope traceRead("npc.part.read");
        // Task 0074f (finding 1): a weapon that cannot be read leaves no placeholder behind, whatever was there.
        if (type == ESM::PRT_Weapon)
            mAttachAmmo = nullptr;
        vsg::ref_ptr<vsg::Node> node;
        try
        {
            node = vsgUtil::readNode(model, mPartContext->partBoneOptions[type]);
        }
        catch (const std::exception& e)
        {
            // Task 0074e (finding 7 of the review of 0074d), as OpenMW 0.51 (NpcAnimation::addOrReplaceIndividualPart,
            // 442eec6e2f): the part's model cannot be read (vsgUtil::readNode reads through vsgAdapters::vfs, whose
            // VFS::Manager::get can throw), so the part is skipped and the update goes on. Nothing has changed yet:
            // the slot stays taken (PartSlots::take ran before) and the body part under it stays hidden, as in 0.51.
            logPartReadError("Error adding NPC part: ", model, e);
            return false;
        }
        traceRead.end();
        Misc::LoadTrace::Scope traceAttach("npc.part.attach");
        bool mirror = bonename.find("Left") != std::string::npos;
        auto result = MWAnim::decorate(node, light, mirror);
        if (glowColor)
            addEnv(node, glowColor, getTransparency(*this));
        if (type == ESM::PRT_Weapon)
        {
            mAttachAmmo = result.placeholders.attachAmmo;
            addSwitch(node, Wield::Weapon);
        }
        else if (type == ESM::PRT_Shield)
            addSwitch(node, Wield::CarriedLeft);

        auto path = MWAnim::attachBonesAndNode(node, *nodeToAddChildrenTo(), animation->bones, bonename);
        if (path.empty())
            return true;

        Anim::Context ctx{ { transform() }, path, &animation->bones, &mPartContext->mask };
        Anim::Update* dst = &mStaticControllers;
        PartControllerList dstList = PartControllerList::Static;
        if (type == ESM::PRT_Head)
        {
            mFace->loadTags(*node);
            dst = &*mFace;
            dstList = PartControllerList::Face;
        }
        else if (type == ESM::PRT_Weapon)
        {
            dst = &mWeaponControllers;
            dstList = PartControllerList::Weapon;
        }

        mPartControllers.beginPart(type);
        result.link(ctx, [this, dst, dstList, type](const Anim::Controller* ctrl, vsg::Object* o) {
            // Task 0074b, finding 1: in the part's own clock, which an update that keeps the part leaves running.
            if (ctrl->hints.autoPlay && type != ESM::PRT_Head)
                mPartControllers.addAutoPlay(type, ctrl, o);
            else
            {
                dst->add(ctrl, o);
                mPartControllers.record(type, ctrl, o, dstList);
            }
        });
        mParts[type] = path;
        return true;
    }

    bool Npc::attachMergedBody(bool isWerewolf)
    {
        mMergedSlots.clear();
        if (!MergedBodies::enabled() || mMergedRetired)
        {
            forgetMergedBody();
            return false;
        }

        // Second review, finding 5: the decision and the template lookup timed on their own (up to the hidden set).
        Misc::LoadTrace::Scope traceDecide("npc.mergedbody.decide");
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Race* race = store.get<ESM::Race>().search(mNpc->mRace);
        if (race == nullptr)
        {
            forgetMergedBody();
            MergedBodies::reportMissingRace(mNpc->mRace.getRefIdString());
            return false;
        }
        const bool female = !mNpc->isMale();
        Body::MergedBodyFacts actor;
        actor.mSettingOn = true;
        actor.mThirdPerson = mViewMode == ViewMode::Normal;
        actor.mWerewolf = isWerewolf;
        actor.mDefaultSkeleton = mDefaultSkeleton;
        // The cache facts are the template's; here only the actor's own are checked first.
        actor.mCacheLoaded = actor.mEntryPresent = actor.mKeyComputed = actor.mKeyCurrent = actor.mPayloadRead
            = actor.mBuilt = true;
        actor.mSlotsCovered = false;
        Body::MergedBodyReason reason = Body::decideMergedBody(actor);
        if (reason != Body::MergedBodyReason::Merged)
        {
            forgetMergedBody();
            // Logged once per race, sex and reason (review finding 11).
            MergedBodies::report(*race, female, reason);
            return false;
        }

        MergedBodies::TemplateResult result = MergedBodies::getTemplate(*race, female, *mPartContext);
        if (!result.mNode)
        {
            forgetMergedBody();
            MergedBodies::report(*race, female, result.mReason);
            return false;
        }
        // Stage e: the regions that equipment or a reservation covers are left out of this actor's draws, by the stock
        // parts' own rule (a slot's body part is not drawn while an inventory group holds the slot).
        const std::vector<int> hidden = Body::hiddenMergedSlots(result.mSlots, mSlots.groups(), mSlots.priorities());
        traceDecide.end();
        return attachMergedCopy(*race, female, result, hidden);
    }

    bool Npc::attachMergedCopy(
        const ESM::Race& race, bool female, const MergedBodies::TemplateResult& result, const std::vector<int>& hidden)
    {
        // Review finding 2: the actor's copy stays while its template does (an equipment change, a torch at dusk);
        // only its regions change, when the hidden set does. Its skins keep their matrices, so the motion of the first
        // frame after the change is right too (finding 5).
        if (mMergedNode && mMergedTemplate == result.mNode)
        {
            if (hidden != mMergedHidden)
            {
                Misc::LoadTrace::Scope traceRegions("npc.mergedbody.regions");
                if (!applyMergedRegions(race, female, result, hidden))
                    return false;
            }
            Misc::LoadTrace::Scope traceKeep("npc.mergedbody.keep");
            mSlots.releaseBodyParts(result.mSlots, *this);
            // Its controllers are added again at the end of the update (relinkKeptControllers).
            mMergedSlots = result.mSlots;
            return true;
        }

        Misc::LoadTrace::Scope traceDetach("npc.mergedbody.detach");
        forgetMergedBody();
        traceDetach.end();
        Misc::LoadTrace::Scope traceAttach("npc.mergedbody.attach");
        vsg::ref_ptr<vsg::Node> node = result.mNode;
        Misc::LoadTrace::Scope traceDecorate("npc.mergedbody.decorate");
        auto decorated = MWAnim::decorate(node, nullptr, false);
        traceDecorate.end();
        mMergedNode = node;
        mMergedTemplate = result.mNode;
        Misc::LoadTrace::Scope traceRegions("npc.mergedbody.regions");
        const bool regionsApplied = applyMergedRegions(race, female, result, hidden);
        traceRegions.end();
        if (!regionsApplied)
            return false;

        // The stock parts of the merged slots go (body parts are added with group -1 and priority 1).
        mSlots.releaseBodyParts(result.mSlots, *this);
        // Any bone serves as the attachment: the skin's matrices are relative to it, and the vertices are in the
        // skeleton's space with the inverse of each bone's rest matrix (mergedbody.cpp).
        Misc::LoadTrace::Scope traceLink("npc.mergedbody.link");
        auto path = MWAnim::attachBonesAndNode(
            node, *nodeToAddChildrenTo(), animation->bones, MWAnim::sPartBones.at(ESM::PRT_Groin));
        if (path.empty())
        {
            forgetMergedBody();
            MergedBodies::report(race, female, Body::MergedBodyReason::BuildFailed);
            return false;
        }
        Anim::Context ctx{ { transform() }, path, &animation->bones, &mPartContext->mask };
        // Recorded, and added at the end of the update through relinkMergedBody (relinkKeptControllers), which leaves
        // out the skins of draws nothing of is drawn.
        decorated.link(ctx, [this, &decorated](const Anim::Controller* ctrl, vsg::Object* o) {
            // The skin itself (a clone of the copy's), so that one whose draw is drawn again can restart its motion.
            Anim::Skin* skin = nullptr;
            for (const auto& [clone, target] : decorated.clonedControllers)
            {
                if (clone == ctrl)
                    skin = dynamic_cast<Anim::Skin*>(clone);
            }
            mMergedControllers.push_back({ ctrl, o, skin });
        });
        mMergedBody = path;
        mMergedSlots = result.mSlots;
        if (!mMergedDrawn)
        {
            mMergedDrawn = true;
            Log(Debug::Info) << "Merged bodies: " << mPtr.getCellRef().getRefId().toString()
                             << " draws the merged body";
        }
        return true;
    }

    bool Npc::applyMergedRegions(
        const ESM::Race& race, bool female, const MergedBodies::TemplateResult& result, const std::vector<int>& hidden)
    {
        MergedBodies::RegionChange change;
        if (!MergedBodies::applyHiddenRegions(*mMergedNode, *result.mNode, result.mDrawRuns, hidden, change))
        {
            // The fallback of stage d: the stock parts entirely. Only an actor that has drawn the merged body retires
            // (one swap each way); one first built dressed may still take it once undressed. Logged once per race and
            // sex (review finding 4).
            MergedBodies::reportRegionsRefused(race, female);
            mMergedRetired = mMergedDrawn;
            forgetMergedBody();
            return false;
        }
        // The draws taken out go as a removed part does: a frame still in flight may use them.
        for (const auto& removed : change.mRemoved)
            mPartContext->compileContext->detach(removed);
        change.mRemoved.clear();
        // Review finding 5: a draw that is drawn again had its skin left out (finding 3); its previous matrices are
        // those of when it was hidden, so its first frame would show that whole motion. It restarts without motion.
        // Second review, finding 1: which draws and which skins is Body::drawsDrawnAgain and Body::linksOfDraws
        // (tested); a new copy has no regions yet (forgetMergedBody), so nothing of it is drawn again.
        const std::vector<std::size_t> again = Body::drawsDrawnAgain(mMergedRegions.mDrawn, change.mDrawn);
        std::size_t restarted = 0;
        if (!again.empty())
        {
            const std::vector<const void*> drawTargets(change.mBoneMatrices.begin(), change.mBoneMatrices.end());
            std::vector<const void*> linkTargets;
            linkTargets.reserve(mMergedControllers.size());
            for (const MergedLink& link : mMergedControllers)
                linkTargets.push_back(link.mSkin != nullptr ? link.mTarget : nullptr);
            for (const std::size_t index : Body::linksOfDraws(again, drawTargets, linkTargets))
            {
                mMergedControllers[index].mSkin->forgetPreviousMatrices();
                ++restarted;
            }
            // Every draw has its own bone matrices and one skin that writes them: a draw drawn again without exactly
            // one skin restarted means the draws and the links no longer line up.
            static bool mismatchReported = false;
            if (restarted != again.size() && !mismatchReported)
            {
                mismatchReported = true;
                Log(Debug::Warning) << "Merged bodies: " << race.mId.getRefIdString() << " "
                                    << (female ? "female" : "male") << ": " << again.size()
                                    << " draws are drawn again but " << restarted << " skins restarted";
            }
        }
        // The trace's check of the two (value scopes): draws drawn again, and skins restarted.
        Misc::LoadTrace::Scope traceAgain("npc.mergedbody.drawnagain", static_cast<std::int64_t>(again.size()));
        Misc::LoadTrace::Scope traceRestarted("npc.mergedbody.restarted", static_cast<std::int64_t>(restarted));
        mMergedRegions = std::move(change);
        mMergedHidden = hidden;
        return true;
    }

    void Npc::relinkMergedBody()
    {
        std::size_t allMatrices = 0;
        std::size_t drawnMatrices = 0;
        for (const MergedLink& link : mMergedControllers)
        {
            // Review finding 3: the skin of a draw that equipment hides entirely would compute its bone matrices every
            // frame for nothing; it is left out until the draw is drawn again.
            bool drawn = true;
            for (std::size_t i = 0; i < mMergedRegions.mBoneMatrices.size(); ++i)
            {
                if (mMergedRegions.mBoneMatrices[i] == link.mTarget)
                    drawn = mMergedRegions.mDrawn[i];
            }
            if (Misc::LoadTrace::enabled())
            {
                if (const auto* matrices = dynamic_cast<const vsg::mat4Array*>(link.mTarget))
                {
                    allMatrices += matrices->size();
                    drawnMatrices += drawn ? matrices->size() : 0;
                }
            }
            if (drawn)
                mStaticControllers.add(link.mController, link.mTarget);
        }
        // The bone matrices the merged body's skins write each frame: of all its draws, and of the drawn ones.
        Misc::LoadTrace::Scope traceAll("npc.mergedbody.matrices", static_cast<std::int64_t>(allMatrices));
        Misc::LoadTrace::Scope traceDrawn("npc.mergedbody.matrices.drawn", static_cast<std::int64_t>(drawnMatrices));
    }

    void Npc::forgetMergedBody()
    {
        removePath(mMergedBody);
        mMergedSlots.clear();
        mMergedControllers.clear();
        mMergedNode = {};
        mMergedTemplate = {};
        mMergedHidden.clear();
        mMergedRegions = {};
    }

    void Npc::update(float dt)
    {
        // Task 0074e (finding 3 of the review of 0074d): update() runs on an UpdateThreads worker for every actor but
        // the player (Scene::update, MWRender::Scene::Cell::Update::run), where nothing may be compiled or the scene
        // graph edited. A pending quiver change (mQuiverDirty, set by show()) is done on the main thread by
        // updatePendingQuiver: Scene::update after the workers have finished, Player::update, Preview::animate.
        mFace->update(dt);
        Wielding::update(dt);
        // Task 0074b: the parts' own auto-play clocks and the holstered weapon's and shield's, after the object's own
        // groups as when they were all in autoPlay (MWAnim::ObjectAnimation::update).
        mPartControllers.update(dt);
        mScabbardClock.update(dt);
        mHolsteredShieldClock.update(dt);
        if (!mAutoPlayTraced.empty())
            traceAutoPlay(mAutoPlayTraced, mAutoPlayFrame++, dt, autoPlay, mPartControllers.clocks(),
                { mStaticControllers.controllers.size(), mWeaponControllers.controllers.size(),
                    mFace->controllers.size() });
    }

    void Npc::updatePendingQuiver()
    {
        runPendingQuiver([this] { updateQuiver(); });
    }

    void Npc::updateOnMainThread()
    {
        updatePendingQuiver();
    }

    void Npc::logQuiverError(const std::exception& error) const
    {
        // A test actor has no record (TestSetup).
        const std::string actor = mPtr.isEmpty() ? std::string("an actor") : mPtr.getCellRef().getRefId().toString();
        Log(Debug::Error) << "Error updating the quiver of " << actor << ": " << error.what();
    }

    void Npc::removeIndividualPart(ESM::PartReferenceType type)
    {
        mSlots.remove(type, *this);
    }

    void Npc::detachPart(ESM::PartReferenceType type)
    {
        if (!mPartSlotsTraced.empty())
            Log(Debug::Info) << "PARTSLOTS " << mPartSlotsTraced << " detach " << static_cast<int>(type) << ' '
                             << (mParts[type].empty() ? 0 : 1);
        // Task 0074d (first review of 0074, finding 7): a part inserted earlier in the same update had its controllers
        // added to the live lists already; they go with it, or they would run on its node in the deletion queue and
        // then on freed memory (a mod record can make a part take a slot another part of the same update held). Its
        // auto-play controllers go with its clock (forget).
        for (const auto& entry : mPartControllers.entries(type))
        {
            switch (entry.mList)
            {
                case PartControllerList::Static:
                    unlinkController(mStaticControllers, entry.mController, entry.mTarget);
                    break;
                case PartControllerList::Face:
                    unlinkController(*mFace, entry.mController, entry.mTarget);
                    break;
                case PartControllerList::Weapon:
                    unlinkController(mWeaponControllers, entry.mController, entry.mTarget);
                    break;
                case PartControllerList::AutoPlay:
                    break;
            }
        }
        mPartControllers.forget(type);
        removePath(mParts[type]);
        // Task 0074f (finding 1 of the review of 0074e): the ammunition placeholder is a node of the weapon's subgraph,
        // which has just left the scene; addAmmo would attach the arrow to it.
        if (type == ESM::PRT_Weapon)
            mAttachAmmo = nullptr;
    }

    void Npc::beforeWornPart(ESM::PartReferenceType /*type*/, const ESM::BodyPart& bodyPart)
    {
        // Second review, finding 10: where Npc::addPartGroup marked it before PartSlots (6151cceb4b^), just before
        // addOrReplaceIndividualPart, whether the part then took its slot or not.
        if (bodyPart.mData.mType == ESM::BodyPart::MT_Skin)
            markSkinModel(bodyPart.mModel);
    }

    void Npc::relinkSurvivingParts()
    {
        mPartControllers.relinkSurvivors(
            [this](std::size_t /*part*/, const std::vector<PartControllers<ESM::PRT_Count>::Entry>& entries) {
                for (const auto& entry : entries)
                {
                    switch (entry.mList)
                    {
                        case PartControllerList::Static:
                            mStaticControllers.add(entry.mController, entry.mTarget);
                            break;
                        case PartControllerList::Face:
                            mFace->add(entry.mController, entry.mTarget);
                            break;
                        case PartControllerList::Weapon:
                            mWeaponControllers.add(entry.mController, entry.mTarget);
                            break;
                        case PartControllerList::AutoPlay:
                            // Task 0074b: the part's own clock was never emptied and keeps running; nothing to add.
                            break;
                    }
                }
            });
    }

    void Npc::removeHolstered(std::vector<vsg::Node*>& path, Anim::AutoPlay& clock)
    {
        // The clock first: its controllers live in the path's node.
        clock.clear();
        removePath(path);
    }

    void Npc::removePath(std::vector<vsg::Node*>& path)
    {
        if (!path.empty())
        {
            mPartContext->compileContext->detach(vsg::ref_ptr{ path.back() });
            vsgUtil::prune<vsg::Group>(path, nodeToAddChildrenTo());
            path.clear();
        }
    }

    void Npc::reserveIndividualPart(ESM::PartReferenceType type, int group, int priority)
    {
        mSlots.reserve(type, group, priority, *this);
    }

    bool Npc::addOrReplaceIndividualPart(ESM::PartReferenceType type, int group, int priority, const std::string& mesh,
        std::optional<vsg::vec4> glowColor, const ESM::Light* light)
    {
        if (!mSlots.take(type, group, priority, *this))
            return false;
        attachPart(type, group, mesh, glowColor, light);
        return true;
    }

    void Npc::attachPart(ESM::PartReferenceType type, int group, const std::string& mesh,
        std::optional<vsg::vec4> glowColor, const ESM::Light* light)
    {
        vsg::ref_ptr<vsg::Node> animGroup;
        std::string bonename = MWAnim::sPartBones.at(type);
        if (type == ESM::PRT_Weapon)
        {
            const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
            auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (weapon != inv.end() && weapon->getType() == ESM::Weapon::sRecordId)
            {
                int weaponType = weapon->get<ESM::Weapon>()->mBase->mData.mType;
                const std::string weaponBonename = MWMechanics::getWeaponType(weaponType)->mAttachBone;

                if (weaponBonename != bonename)
                {
                    if (animation->bones.search(weaponBonename))
                        bonename = weaponBonename;
                }
            }
        }

        // A part whose model cannot be read makes no sound, as in OpenMW 0.51 (it returns before the sound).
        if (!insertBoundedPart(type, mesh, bonename, glowColor, light))
            return;

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto csi = inv.getSlot(group < 0 ? MWWorld::InventoryStore::Slot_Helmet : group);
        if (csi != inv.end())
        {
            const auto soundId = csi->getClass().getSound(*csi);
            if (!soundId.empty())
                soundsToPlay.emplace(soundId);
        }
    }

    void Npc::wornParts(const std::vector<ESM::PartReference>& parts, std::vector<PartSlots::WornPart>& out) const
    {
        bool firstPerson = (mViewMode == ViewMode::FirstPerson);
        bool female = !mNpc->isMale();
        auto& bodyPartStore = MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>();
        for (const ESM::PartReference& part : parts)
            out.push_back({ static_cast<ESM::PartReferenceType>(part.mPart),
                getBodyPart(firstPerson, female, part, bodyPartStore) });
    }

    void Npc::addWeapon()
    {
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon != inv.end())
        {
            std::string mesh = std::string(weapon->getClass().getModel(*weapon));
            addOrReplaceIndividualPart(
                ESM::PRT_Weapon, MWWorld::InventoryStore::Slot_CarriedRight, 1, mesh, getGlowColor(*weapon));
        }
        else
            removeIndividualPart(ESM::PRT_Weapon);
    }

    namespace
    {
        // OpenMW's ActorAnimation::getShieldMesh at 442eec6e2f: a worn shield uses its body part's model, with the
        // ground model as the fallback when the armor has no shield part. Empty when the part names something unusable.
        std::string getShieldMesh(const MWWorld::ConstPtr& shield, bool female)
        {
            const std::vector<ESM::PartReference>& parts = shield.get<ESM::Armor>()->mBase->mParts.mParts;
            auto& partStore = MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>();
            for (const ESM::PartReference& part : parts)
            {
                if (part.mPart != ESM::PRT_Shield)
                    continue;
                const ESM::RefId* name = nullptr;
                if (female && !part.mFemale.empty())
                    name = &part.mFemale;
                else if (!part.mMale.empty())
                    name = &part.mMale;
                if (name && !name->empty())
                {
                    const ESM::BodyPart* bodypart = partStore.search(*name);
                    if (bodypart == nullptr || bodypart->mData.mType != ESM::BodyPart::MT_Armor)
                        return {};
                    if (!bodypart->mModel.empty())
                        return bodypart->mModel;
                }
            }
            return std::string(shield.getClass().getModel(shield));
        }
    }

    namespace
    {
        // Adds "_sh" before the extension, as OpenMW's addSuffixBeforeExtension. Records name models without "meshes/",
        // which the VFS needs.
        std::string sheathModel(const std::string& record)
        {
            const std::string model(Misc::ResourceHelpers::correctMeshPath(VFS::Path::toNormalized(record)).value());
            const size_t dot = model.rfind('.');
            return dot == std::string::npos ? model + "_sh" : model.substr(0, dot) + "_sh" + model.substr(dot);
        }

        bool modelExists(const std::string& model)
        {
            return MWBase::Environment::get().getResourceSystem()->getVFS()->exists(VFS::Path::toNormalized(model));
        }

        vsg::Group* findGroup(vsg::Node& node, const std::string& name)
        {
            auto found = vsgUtil::SearchByName::search(node, name,
                [](const std::string& a, const std::string& b) { return Misc::StringUtils::ciEqual(a, b); });
            return found ? found->cast<vsg::Group>() : nullptr;
        }
    }

    void Npc::appendCustomBoneFiles(const std::string& model, std::vector<std::string>& files) const
    {
        // As OpenMW's injectCustomBones: every .nif in the animations folder named after the skeleton.
        std::string folder = Misc::StringUtils::lowerCase(model);
        std::replace(folder.begin(), folder.end(), '\\', '/');
        if (folder.starts_with("meshes/"))
            folder.replace(0, 6, "animations");
        else
            folder.insert(0, "animations/");
        const size_t dot = folder.rfind('.');
        if (dot == std::string::npos)
            return;
        folder.resize(dot);
        folder += '/';
        for (const VFS::Path::Normalized& name :
            MWBase::Environment::get().getResourceSystem()->getVFS()->getRecursiveDirectoryIterator(folder))
        {
            if (Misc::getFileExtension(name) == "nif")
                files.emplace_back(name.value());
        }
    }

    vsg::ref_ptr<vsg::Node> Npc::readHolstered(const std::string& model) const
    {
        // A copy of its own, because the sheath nodes in it are changed.
        auto options = vsg::Options::create(*mContext.actorOptions);
        options->sharedObjects = {};
        return vsgUtil::readNode(model, options);
    }

    std::vector<vsg::Node*> Npc::attachHolstered(vsg::ref_ptr<vsg::Node> node, MWAnim::PlaceholderResult& result,
        const std::string& bone, std::optional<vsg::vec4> glowColor, Anim::AutoPlay* clock)
    {
        if (glowColor)
            addEnv(node, glowColor, getTransparency(*this));
        auto path = MWAnim::attachBonesAndNode(node, *nodeToAddChildrenTo(), animation->bones, bone);
        if (path.empty())
            return path;
        // A holstered bow keeps still: the weapon's own animation is for when it is drawn (no clock).
        if (clock != nullptr)
        {
            Anim::Context ctx{ { transform() }, path, &animation->bones, &mPartContext->mask };
            // Task 0074b: the auto-play controllers in the holstered item's own clock, emptied with its path
            // (removeHolstered); the object's autoPlay is no longer emptied on every update.
            result.link(ctx, [this, clock](const Anim::Controller* ctrl, vsg::Object* o) {
                if (ctrl->hints.autoPlay)
                    clock->add(ctrl, o);
                else
                    mStaticControllers.add(ctrl, o);
            });
        }
        return path;
    }

    void Npc::updateCarriedLeftVisible()
    {
        if (!Settings::game().mShieldSheathing)
            return;
        int weaptype = ESM::Weapon::None;
        MWMechanics::getActiveWeapon(mPtr, &weaptype);
        show(Wield::CarriedLeft,
            !hasSheathedShield() && !(MWMechanics::getWeaponType(weaptype)->mFlags & ESM::WeaponType::TwoHanded));
    }

    void Npc::updateSheathing()
    {
        if (Settings::game().mWeaponSheathing)
        {
            updateHolsteredWeapon();
            updateQuiver();
        }
        if (Settings::game().mShieldSheathing)
            updateHolsteredShield();
    }

    void Npc::updateHolsteredWeapon()
    {
        // As OpenMW's ActorAnimation::updateHolsteredWeapon.
        removeHolstered(mScabbard, mScabbardClock);
        mHolsteredWeaponSwitch = {};

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
            return;
        const auto* weaponType = MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType);
        const std::string bone = weaponType->mSheathingBone;
        const std::string mesh = std::string(weapon->getClass().getModel(*weapon));
        if (mesh.empty() || bone.empty() || !animation->bones.search(bone))
            return;

        // Since throwing weapons stack themselves, do not show such a weapon itself.
        const bool canShowWeapon = weaponType->mWeaponClass != ESM::WeaponType::Thrown;
        Anim::AutoPlay* clock = weaponType->mWeaponClass != ESM::WeaponType::Ranged ? &mScabbardClock : nullptr;
        const std::string scabbard = sheathModel(mesh);
        const bool hasScabbard = modelExists(scabbard);
        // No scabbard: the weapon itself, which a thrown weapon does not show.
        if (!hasScabbard && !canShowWeapon)
            return;
        auto weaponSwitch = vsg::Switch::create();
        const bool visible = canShowWeapon && !isShown(Wield::Weapon);
        if (!attachHolsteredWeapon(
                mesh, scabbard, hasScabbard, bone, visible, clock, getGlowColor(*weapon), *weaponSwitch))
            return;
        if (canShowWeapon)
            mHolsteredWeaponSwitch = weaponSwitch;
    }

    bool Npc::attachHolsteredWeapon(const std::string& mesh, const std::string& scabbard, bool hasScabbard,
        const std::string& bone, bool visible, Anim::AutoPlay* clock, std::optional<vsg::vec4> glowColor,
        vsg::Switch& weaponSwitch)
    {
        // Task 0074h: every read comes first and is guarded per part (insertBoundedPart's rule); a model that cannot
        // be read leaves nothing attached (mScabbard stays empty, as removeHolstered left it) and the caller sets no
        // switch.
        if (!hasScabbard)
        {
            auto node = readGuarded(mesh, false);
            if (!node)
                return false;
            auto result = MWAnim::decorate(node, nullptr);
            weaponSwitch.addChild(visible, node);
            mScabbard = attachHolstered(vsg::ref_ptr<vsg::Switch>(&weaponSwitch), result, bone, glowColor, clock);
            return true;
        }
        auto node = readGuarded(scabbard, true);
        if (!node)
            return false;
        auto result = MWAnim::decorate(node, nullptr);
        if (vsg::Group* weaponNode = findGroup(*node, "Bip01 Weapon"))
        {
            // An empty weapon node places the common weapon mesh, so the _sh file need not hold it. Read it before
            // the node is changed.
            vsg::ref_ptr<vsg::Node> weaponMesh;
            if (weaponNode->children.empty())
            {
                weaponMesh = readGuarded(mesh, false);
                if (!weaponMesh)
                    return false;
                MWAnim::decorate(weaponMesh, nullptr);
            }
            // The contents go behind a switch, because the drawn weapon must not show in the scabbard too.
            vsg::Group::Children contents;
            contents.swap(weaponNode->children);
            if (weaponMesh)
                contents.push_back(weaponMesh);
            for (auto& child : contents)
            {
                if (glowColor)
                    addEnv(child, glowColor, getTransparency(*this));
                weaponSwitch.addChild(visible, child);
            }
            weaponNode->addChild(vsg::ref_ptr<vsg::Switch>(&weaponSwitch));
        }
        mScabbard = attachHolstered(node, result, bone, {}, clock);
        return true;
    }

    vsg::ref_ptr<vsg::Node> Npc::readGuarded(const std::string& model, bool holstered) const
    {
        try
        {
            return holstered ? readHolstered(model) : mContext.readNode(model);
        }
        catch (const std::exception& e)
        {
            logPartReadError("Error adding NPC holstered item '" + model + "': ", model, e);
            return {};
        }
    }

    void Npc::updateQuiver()
    {
        // As OpenMW's ActorAnimation::updateQuiver. Each child of "Bip01 Ammo" holds one arrow or throwing weapon.
        for (auto& path : mQuiver)
            removePath(path);
        mQuiver.clear();

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
            return;
        auto* ammoBone = animation->bones.search("Bip01 Ammo");
        if (!ammoBone)
            return;

        const auto* weaponType = MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType);
        auto ammo = weapon;
        int ammoCount = 0;
        if (weaponType->mWeaponClass == ESM::WeaponType::Thrown)
        {
            // Throwing weapons use no ammunition: they stack, less the one in hand.
            ammoCount = ammo->getCellRef().getCount();
            if (isShown(Wield::Weapon))
                --ammoCount;
        }
        else
        {
            ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammo == inv.end() || ammo->get<ESM::Weapon>()->mBase->mData.mType != weaponType->mAmmoType)
                return;
            ammoCount = ammo->getCellRef().getCount();
            if (isShown(Wield::Ammo))
                --ammoCount;
        }

        // Sorted, as Bones keeps no order and which slots fill first should not change between runs.
        std::vector<std::string> slots;
        animation->bones.forEach([&](const std::string& name, Anim::Bone& bone) {
            if (bone.path.size() >= 2 && bone.path[bone.path.size() - 2].get() == ammoBone->node())
                slots.push_back(name);
        });
        std::sort(slots.begin(), slots.end());

        const std::string model = std::string(ammo->getClass().getModel(*ammo));
        const auto glowColor = getGlowColor(*ammo);
        for (int i = 0; i < ammoCount && i < static_cast<int>(slots.size()); ++i)
        {
            // Task 0074h: every arrow is the same model, so one that cannot be read ends the quiver.
            if (!attachQuiverArrow(model, slots[i], glowColor))
                break;
        }
    }

    bool Npc::attachQuiverArrow(const std::string& model, const std::string& bone, std::optional<vsg::vec4> glowColor)
    {
        vsg::ref_ptr<vsg::Node> node;
        try
        {
            node = mContext.readNode(model);
        }
        catch (const std::exception& e)
        {
            logPartReadError("Error adding NPC quiver arrow '" + model + "': ", model, e);
            return false;
        }
        auto result = MWAnim::decorate(node, nullptr);
        auto path = attachHolstered(node, result, bone, glowColor, nullptr);
        if (!path.empty())
            mQuiver.push_back(std::move(path));
        return true;
    }

    std::string Npc::getSheathedShieldMesh(const MWWorld::ConstPtr& shield) const
    {
        // As OpenMW's NpcAnimation::getSheathedShieldMesh.
        std::string mesh = getShieldMesh(shield, !mNpc->isMale());
        if (mesh.empty())
            return {};
        const std::string holstered = sheathModel(mesh);
        if (modelExists(holstered))
        {
            // Only searched, so the shared read will do.
            if (!holsteredModelHasSheath(holstered))
                return {};
        }
        return mesh;
    }

    bool Npc::holsteredModelHasSheath(const std::string& holstered) const
    {
        // Task 0074h: called from hasSheathedShield and canAnimateShield, which the mechanics reach, so a read that
        // throws counts as "no sheath" (the shield is then not shown holstered) instead of ending the session.
        try
        {
            auto node = mContext.readActor(holstered);
            return node && findGroup(*node, "Bip01 Sheath");
        }
        catch (const std::exception& e)
        {
            logPartReadError("Error adding NPC holstered item '" + holstered + "': ", holstered, e);
            return false;
        }
    }

    bool Npc::hasSheathedShield() const
    {
        if (!Settings::game().mShieldSheathing || !mPtr.getClass().hasInventoryStore(mPtr))
            return false;
        if (mPtr.getClass().getCreatureStats(mPtr).getDrawState() != MWMechanics::DrawState::Nothing)
            return false;
        // In first person the back is out of view, so the hand is emptied with or without the bone, as OpenMW's
        // NpcAnimation::updateCarriedLeftVisible.
        if (mViewMode != ViewMode::FirstPerson && !animation->bones.search("Bip01 AttachShield"))
            return false;
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto shield = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        return shield != inv.end() && shield->getType() == ESM::Armor::sRecordId
            && !getSheathedShieldMesh(*shield).empty();
    }

    bool Npc::canAnimateShield() const
    {
        // As OpenMW's ActorAnimation::useShieldAnimations, whose text key checks are in CharacterController.
        if (!Settings::game().mShieldSheathing || !mPtr.getClass().hasInventoryStore(mPtr))
            return false;
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        auto shield = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (weapon == inv.end() || shield == inv.end() || shield->getType() != ESM::Armor::sRecordId
            || getSheathedShieldMesh(*shield).empty())
            return false;
        if (weapon->getType() == ESM::Weapon::sRecordId)
            return !(MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mFlags
                & ESM::WeaponType::TwoHanded);
        return weapon->getType() == ESM::Lockpick::sRecordId || weapon->getType() == ESM::Probe::sRecordId;
    }

    void Npc::updateHolsteredShield()
    {
        // As OpenMW's ActorAnimation::updateHolsteredShield, with a switch in place of rebuilding on every show.
        removeHolstered(mHolsteredShield, mHolsteredShieldClock);
        mHolsteredShieldSwitch = {};

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto shield = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (shield == inv.end() || shield->getType() != ESM::Armor::sRecordId)
            return;
        // Can not show holstered shields with two-handed weapons at all.
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end())
            return;
        if (weapon->getType() == ESM::Weapon::sRecordId
            && (MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mFlags
                & ESM::WeaponType::TwoHanded))
            return;
        const std::string mesh = getSheathedShieldMesh(*shield);
        const std::string bone = "Bip01 AttachShield";
        if (mesh.empty() || !animation->bones.search(bone))
            return;

        const std::string holstered = sheathModel(mesh);
        attachHolsteredShield(mesh, holstered, modelExists(holstered), bone, getGlowColor(*shield));
    }

    bool Npc::attachHolsteredShield(const std::string& mesh, const std::string& holstered, bool hasHolsteredModel,
        const std::string& bone, std::optional<vsg::vec4> glowColor)
    {
        // Task 0074h: every read is guarded per part; a model that cannot be read leaves no shield, no switch and no
        // path (updateHolsteredShield emptied them first).
        auto node = readGuarded(hasHolsteredModel ? holstered : mesh, hasHolsteredModel);
        if (!node)
            return false;
        auto result = MWAnim::decorate(node, nullptr);
        // An empty sheath node places the common shield mesh, so the _sh file need not hold it.
        if (vsg::Group* sheath = hasHolsteredModel ? findGroup(*node, "Bip01 Sheath") : nullptr;
            sheath && sheath->children.empty())
        {
            auto shieldMesh = readGuarded(mesh, false);
            if (!shieldMesh)
                return false;
            MWAnim::decorate(shieldMesh, nullptr);
            sheath->addChild(shieldMesh);
        }
        mHolsteredShieldSwitch = vsg::Switch::create();
        mHolsteredShieldSwitch->addChild(!isShown(Wield::CarriedLeft), node);
        mHolsteredShield = attachHolstered(mHolsteredShieldSwitch, result, bone, glowColor, &mHolsteredShieldClock);
        return true;
    }

    void Npc::show(Wield w, bool visible)
    {
        Wielding::show(w, visible);
        if (w == Wield::Weapon && mHolsteredWeaponSwitch)
            mHolsteredWeaponSwitch->setAllChildren(!visible);
        else if (w == Wield::CarriedLeft && mHolsteredShieldSwitch)
            mHolsteredShieldSwitch->setAllChildren(!visible);
        if ((w == Wield::Weapon || w == Wield::Ammo) && mViewMode != ViewMode::HeadOnly
            && Settings::game().mWeaponSheathing)
            mQuiverDirty = true;
    }

    void Npc::addCarriedLeft()
    {
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto iter = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (iter != inv.end())
        {
            std::string mesh = std::string(iter->getClass().getModel(*iter));
            // For shields we must try to use the body part model
            if (iter->getType() == ESM::Armor::sRecordId)
            {
                mesh = getShieldMesh(*iter, !mNpc->isMale());
            }
            const ESM::Light* light{};
            if (iter->getType() == ESM::Light::sRecordId)
                light = iter->get<ESM::Light>()->mBase;
            if (mesh.empty())
                reserveIndividualPart(ESM::PRT_Shield, MWWorld::InventoryStore::Slot_CarriedLeft, 1);
            else
                addOrReplaceIndividualPart(
                    ESM::PRT_Shield, MWWorld::InventoryStore::Slot_CarriedLeft, 1, mesh, getGlowColor(*iter), light);
        }
        else
            removeIndividualPart(ESM::PRT_Shield);
    }

    void Npc::addAmmo()
    {
        if (!mAttachAmmo)
            return;

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
        if (ammo != inv.end())
        {
            attachAmmoModel(std::string(ammo->getClass().getModel(*ammo)));
        }
    }

    bool Npc::attachAmmoModel(const std::string& model)
    {
        // Task 0074h: the read is guarded per part like insertBoundedPart's. Nothing has changed before it succeeds:
        // mAttachAmmo (the weapon part's placeholder, valid while the weapon part is) keeps its children, so the arrow
        // is simply missing and the update goes on.
        vsg::ref_ptr<vsg::Node> node;
        try
        {
            node = mPartContext->readNode(model);
        }
        catch (const std::exception& e)
        {
            logPartReadError("Error adding NPC ammunition '" + model + "': ", model, e);
            return false;
        }
        auto result = MWAnim::cloneIfRequired(node);
        addSwitch(node, Wield::Ammo);
        mAttachAmmo->subgraphRequiresLocalFrustum = true;
        mAttachAmmo->addChild(node);
        return true;
    }

    void Npc::setFacialState(bool enable, bool talking, float loudness)
    {
        mFace->enabled = enable;
        mFace->talking = talking;
        mFace->loudness = loudness;
    }

    void Npc::setFacialSpeech(const ::Face::SpeechFrame& frame)
    {
        mFace->speech = frame;
    }

    void Npc::setVampire(bool vampire)
    {
        if (mNpcType == NpcType::Werewolf) // we can't have werewolf vampires, can we
            return;
        if ((mNpcType == NpcType::Vampire) != vampire)
        {
            if (mPtr == MWMechanics::getPlayer())
                MWBase::Environment::get().getWorld()->reattachPlayerCamera();
            else
                rebuild();
        }
    }

    void Npc::updatePtr(const MWWorld::Ptr& updated)
    {
        mPtr = updated;
    }

}
