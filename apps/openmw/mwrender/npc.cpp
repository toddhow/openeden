#include "npc.hpp"

#include <components/settings/values.hpp>
#include <components/animation/context.hpp>
#include <components/animation/controllermap.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/mwanimation/attach.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/decorate.hpp>
#include <components/mwanimation/face.hpp>
#include <components/mwanimation/groups.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/vsgutil/readnode.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/removechild.hpp>
#include <components/vsgutil/searchbyname.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
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

#include "bodyparts.hpp"
#include "env.hpp"
#include "transparency.hpp"

namespace MWRender
{
    Npc::Npc(const MWAnim::Context& mwctx, const MWWorld::Ptr& ptr, ViewMode viewMode)
        : Wielding(mwctx)
        , mPtr(ptr)
        , mViewMode(viewMode)
        , mNpcType(getNpcType(ptr))
    {
        mPartContext = &mContext;
        mNpc = mPtr.get<ESM::NPC>()->mBase;

        mFace = std::make_unique<MWAnim::Face>();

        for (size_t i = 0; i < ESM::PRT_Count; i++)
        {
            mPartslots[i] = -1; // each slot is empty
            mPartPriorities[i] = 0;
        }

        updateNpcBase();
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
            clearControllers();
            for (size_t i = 0; i < ESM::PRT_Count; ++i)
                removeIndividualPart(static_cast<ESM::PartReferenceType>(i));
            removePath(mScabbard);
            removePath(mHolsteredShield);
            for (auto& path : mQuiver)
                removePath(path);
            mQuiver.clear();
            mHolsteredWeaponSwitch = {};
            mHolsteredShieldSwitch = {};
        }
    }

    void Npc::rebuild()
    {
        updateNpcBase();

        compile();

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
                return mPartslots[i];
        }
        return -1;
    }

    void Npc::updateNpcBase()
    {
        for (size_t i = 0; i < ESM::PRT_Count; i++)
            removeIndividualPart((ESM::PartReferenceType)i);
        removePath(mScabbard);
        removePath(mHolsteredShield);
        for (auto& path : mQuiver)
            removePath(path);
        mQuiver.clear();

        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mNpc->mRace);
        NpcType curType = getNpcType();
        bool isWerewolf = (curType == NpcType::Werewolf);
        bool isVampire = (curType == NpcType::Vampire);
        bool isFemale = !mNpc->isMale();

        auto& bodyPartStore = store.get<ESM::BodyPart>();
        mHeadModel = getHeadModel(isWerewolf, isVampire, isFemale, mNpc->mRace, mNpc->mHead, bodyPartStore);
        mHairModel = getHairModel(isWerewolf, mNpc->mHair, bodyPartStore);

        bool is1stPerson = mViewMode == ViewMode::FirstPerson;
        bool isBeast = (race->mData.mFlags & ESM::Race::Beast) != 0;

        std::string defaultSkeleton = mContext.pickSkeleton(is1stPerson, isFemale, isBeast, isWerewolf);

        std::string smodel = defaultSkeleton;
        if (!is1stPerson && !isWerewolf && !mNpc->mModel.empty())
            smodel = mNpc->mModel;

        std::vector<std::string> sources;
        if (!is1stPerson)
        {
            const auto& base = mContext.files.baseanim;
            // openeden-vsg: the animation source is [Models] xbaseanim, as in OpenMW's NpcAnimation (task 0012).
            if (smodel != base && !isWerewolf)
                sources.emplace_back(Settings::models().mXbaseanim.get().value());

            if (smodel != defaultSkeleton && base != defaultSkeleton)
                sources.emplace_back(defaultSkeleton);

            sources.emplace_back(smodel);

            if (!isWerewolf && mNpc->mRace.contains("argonian"))
                sources.emplace_back(Settings::models().mXargonianswimkna.get().value());
        }
        else
        {
            const auto& base = mContext.firstPersonFiles.baseanim;
            if (smodel != base && !isWerewolf)
                sources.emplace_back(Settings::models().mXbaseanim1st.get().value());
            sources.emplace_back(smodel);
        }

        vsg::ref_ptr<const vsg::Node> node = mContext.readActor(smodel);

        animation = std::make_unique<MWAnim::Play>(MWAnim::Bones(static_cast<const vsg::Node&>(*node), MWAnim::groups), MWAnim::sNumBlendMasks);
        if (Settings::game().mWeaponSheathing || Settings::game().mShieldSheathing)
        {
            addCustomBones(defaultSkeleton);
            if (smodel != defaultSkeleton)
                addCustomBones(smodel);
        }
        animation->addAnimSources(mContext.readAnimations(sources), smodel);
        assignBones();
        // Another skeleton may have the shield's sheath bone or not: as OpenMW's viewChange in NpcAnimation::rebuild.
        updateCarriedLeftVisible();
        updateParts();
    }

    void Npc::updateEquipment()
    {
        updateCarriedLeftVisible();
        updateParts();
        compile();
    }

    void Npc::clearControllers()
    {
        autoPlay.clear();
        mStaticControllers.clear();
        mFace->clear();
        mWeaponControllers.clear();
    }

    void Npc::updateParts()
    {
        NpcType curType = getNpcType();
        if (curType != mNpcType)
        {
            mNpcType = curType;
            rebuild();
            return;
        }

        clearControllers();
        soundsToPlay.clear();

        static const struct
        {
            int slot;
            int basePriority;
        } slotlist[] = { // FIXME: Priority is based on the number of reserved slots. There should be a better way.
            { MWWorld::InventoryStore::Slot_Robe, 11 }, { MWWorld::InventoryStore::Slot_Skirt, 3 },
            { MWWorld::InventoryStore::Slot_Helmet, 0 }, { MWWorld::InventoryStore::Slot_Cuirass, 0 },
            { MWWorld::InventoryStore::Slot_Greaves, 0 }, { MWWorld::InventoryStore::Slot_LeftPauldron, 0 },
            { MWWorld::InventoryStore::Slot_RightPauldron, 0 }, { MWWorld::InventoryStore::Slot_Boots, 0 },
            { MWWorld::InventoryStore::Slot_LeftGauntlet, 0 }, { MWWorld::InventoryStore::Slot_RightGauntlet, 0 },
            { MWWorld::InventoryStore::Slot_Shirt, 0 }, { MWWorld::InventoryStore::Slot_Pants, 0 },
            { MWWorld::InventoryStore::Slot_CarriedLeft, 0 }, { MWWorld::InventoryStore::Slot_CarriedRight, 0 }
        };
        static const size_t slotlistsize = sizeof(slotlist) / sizeof(slotlist[0]);

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        for (size_t i = 0; i < slotlistsize && mViewMode != ViewMode::HeadOnly; i++)
        {
            auto store = inv.getSlot(slotlist[i].slot);

            removePartGroup(slotlist[i].slot);

            if (store == inv.end())
                continue;

            if (slotlist[i].slot == MWWorld::InventoryStore::Slot_Helmet)
                removeIndividualPart(ESM::PRT_Hair);

            int prio = 1;
            auto glowColor = getGlowColor(*store);
            if (store->getType() == ESM::Clothing::sRecordId)
            {
                prio = ((slotlist[i].basePriority + 1) << 1) + 0;
                const ESM::Clothing* clothes = store->get<ESM::Clothing>()->mBase;
                addPartGroup(slotlist[i].slot, prio, clothes->mParts.mParts, glowColor);
            }
            else if (store->getType() == ESM::Armor::sRecordId)
            {
                prio = ((slotlist[i].basePriority + 1) << 1) + 1;
                const ESM::Armor* armor = store->get<ESM::Armor>()->mBase;
                addPartGroup(slotlist[i].slot, prio, armor->mParts.mParts, glowColor);
            }

            if (slotlist[i].slot == MWWorld::InventoryStore::Slot_Robe)
            {
                ESM::PartReferenceType parts[] = { ESM::PRT_Groin, ESM::PRT_Skirt, ESM::PRT_RLeg, ESM::PRT_LLeg,
                    ESM::PRT_RUpperarm, ESM::PRT_LUpperarm, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm,
                    ESM::PRT_LForearm, ESM::PRT_Cuirass };
                size_t parts_size = sizeof(parts) / sizeof(parts[0]);
                for (size_t p = 0; p < parts_size; ++p)
                    reserveIndividualPart(parts[p], slotlist[i].slot, prio);
            }
            else if (slotlist[i].slot == MWWorld::InventoryStore::Slot_Skirt)
            {
                reserveIndividualPart(ESM::PRT_Groin, slotlist[i].slot, prio);
                reserveIndividualPart(ESM::PRT_RLeg, slotlist[i].slot, prio);
                reserveIndividualPart(ESM::PRT_LLeg, slotlist[i].slot, prio);
            }
        }

        if (mViewMode != ViewMode::FirstPerson)
        {
            if (mPartPriorities[ESM::PRT_Head] < 1 && !mHeadModel.empty())
                addOrReplaceIndividualPart(ESM::PRT_Head, -1, 1, mHeadModel);
            if (mPartPriorities[ESM::PRT_Hair] < 1 && mPartPriorities[ESM::PRT_Head] <= 1 && !mHairModel.empty())
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

        const std::vector<const ESM::BodyPart*>& parts
            = getBodyParts(race, !mNpc->isMale(), mViewMode == ViewMode::FirstPerson, isWerewolf,
                MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>());
        for (int part = ESM::PRT_Neck; part < ESM::PRT_Count; ++part)
        {
            if (mPartPriorities[part] < 1)
            {
                const ESM::BodyPart* bodypart = parts[part];
                if (bodypart)
                    addOrReplaceIndividualPart((ESM::PartReferenceType)part, -1, 1, bodypart->mModel);
            }
        }
    }

    void Npc::insertBoundedPart(ESM::PartReferenceType type, const std::string& model, const std::string& bonename,
        std::optional<vsg::vec4> glowColor, const ESM::Light* light)
    {
        auto node = vsgUtil::readNode(model, mPartContext->partBoneOptions[type]);
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
            return;

        Anim::Context ctx{ { transform() }, path, &animation->bones, &mPartContext->mask };
        Anim::Update* dst = &mStaticControllers;
        if (type == ESM::PRT_Head)
        {
            mFace->loadTags(*node);
            dst = &*mFace;
        }
        else if (type == ESM::PRT_Weapon)
            dst = &mWeaponControllers;

        auto offset = autoPlay.maxPhaseGroup();
        result.link(ctx, [this, dst, type, offset](const Anim::Controller* ctrl, vsg::Object* o) {
            if (ctrl->hints.autoPlay && type != ESM::PRT_Head)
                autoPlay.add(ctrl, o, offset);
            else
                dst->add(ctrl, o);
        });
        mParts[type] = path;
    }

    void Npc::update(float dt)
    {
        if (mQuiverDirty)
        {
            // Here and not in show(), which an animation thread may call: compiling is for the main thread.
            mQuiverDirty = false;
            updateQuiver();
            compile();
        }
        mFace->update(dt);
        Wielding::update(dt);
    }

    void Npc::removeIndividualPart(ESM::PartReferenceType type)
    {
        mPartPriorities[type] = 0;
        mPartslots[type] = -1;

        removePath(mParts[type]);
    }

    void Npc::removePath(std::vector<vsg::Node*>& path)
    {
        if (!path.empty())
        {
            mPartContext->compileContext->detach(vsg::ref_ptr{path.back()});
            vsgUtil::prune<vsg::Group>(path, nodeToAddChildrenTo());
            path.clear();
        }
    }

    void Npc::reserveIndividualPart(ESM::PartReferenceType type, int group, int priority)
    {
        if (priority > mPartPriorities[type])
        {
            removeIndividualPart(type);
            mPartPriorities[type] = priority;
            mPartslots[type] = group;
        }
    }

    void Npc::removePartGroup(int group)
    {
        for (int i = 0; i < ESM::PRT_Count; i++)
        {
            if (mPartslots[i] == group)
                removeIndividualPart((ESM::PartReferenceType)i);
        }
    }

    bool Npc::addOrReplaceIndividualPart(ESM::PartReferenceType type, int group, int priority, const std::string& mesh,
        std::optional<vsg::vec4> glowColor, const ESM::Light* light)
    {
        if (priority <= mPartPriorities[type])
            return false;

        removeIndividualPart(type);
        mPartslots[type] = group;
        mPartPriorities[type] = priority;
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

        insertBoundedPart(type, mesh, bonename, glowColor, light);

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto csi = inv.getSlot(group < 0 ? MWWorld::InventoryStore::Slot_Helmet : group);
        if (csi != inv.end())
        {
            const auto soundId = csi->getClass().getSound(*csi);
            if (!soundId.empty())
                soundsToPlay.emplace(soundId);
        }
        return true;
    }

    void Npc::addPartGroup(
        int group, int priority, const std::vector<ESM::PartReference>& parts, std::optional<vsg::vec4> glowColor)
    {
        bool firstPerson = (mViewMode == ViewMode::FirstPerson);
        bool female = !mNpc->isMale();
        auto& bodyPartStore = MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>();
        for (const ESM::PartReference& part : parts)
        {
            if (auto bodypart = getBodyPart(firstPerson, female, part, bodyPartStore))
                addOrReplaceIndividualPart(
                    (ESM::PartReferenceType)part.mPart, group, priority, bodypart->mModel, glowColor);
            else
                reserveIndividualPart((ESM::PartReferenceType)part.mPart, group, priority);
        }
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
        // OpenMW's ActorAnimation::getShieldMesh at 442eec6e2f: a worn shield uses its body part's model, with the ground
        // model as the fallback when the armor has no shield part. Empty when the part names something unusable.
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
        // Adds "_sh" before the extension, as OpenMW's addSuffixBeforeExtension. Records name models without "meshes/", which
        // the VFS needs.
        std::string sheathModel(const std::string& record)
        {
            const std::string model(
                Misc::ResourceHelpers::correctMeshPath(VFS::Path::toNormalized(record)).value());
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

    void Npc::addCustomBones(const std::string& model)
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
                if (auto node = mContext.readActor(std::string(name.value())))
                    animation->bones.addCustomBones(*node);
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
        const std::string& bone, std::optional<vsg::vec4> glowColor, bool animate)
    {
        if (glowColor)
            addEnv(node, glowColor, getTransparency(*this));
        auto path = MWAnim::attachBonesAndNode(node, *nodeToAddChildrenTo(), animation->bones, bone);
        if (path.empty())
            return path;
        // A holstered bow keeps still: the weapon's own animation is for when it is drawn.
        if (animate)
        {
            Anim::Context ctx{ { transform() }, path, &animation->bones, &mPartContext->mask };
            auto offset = autoPlay.maxPhaseGroup();
            result.link(ctx, [this, offset](const Anim::Controller* ctrl, vsg::Object* o) {
                if (ctrl->hints.autoPlay)
                    autoPlay.add(ctrl, o, offset);
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
        removePath(mScabbard);
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
        const bool animate = weaponType->mWeaponClass != ESM::WeaponType::Ranged;
        const std::string scabbard = sheathModel(mesh);
        auto weaponSwitch = vsg::Switch::create();
        const bool visible = canShowWeapon && !isShown(Wield::Weapon);

        if (!modelExists(scabbard))
        {
            // No scabbard: the weapon itself.
            if (!canShowWeapon)
                return;
            auto node = mContext.readNode(mesh);
            auto result = MWAnim::decorate(node, nullptr);
            weaponSwitch->addChild(visible, node);
            mScabbard = attachHolstered(weaponSwitch, result, bone, getGlowColor(*weapon), animate);
        }
        else
        {
            auto node = readHolstered(scabbard);
            if (!node)
                return;
            auto result = MWAnim::decorate(node, nullptr);
            if (vsg::Group* weaponNode = findGroup(*node, "Bip01 Weapon"))
            {
                // An empty weapon node places the common weapon mesh, so the _sh file need not hold it. Its contents
                // go behind a switch, because the drawn weapon must not show in the scabbard too.
                vsg::Group::Children contents;
                contents.swap(weaponNode->children);
                if (contents.empty())
                {
                    auto weaponMesh = mContext.readNode(mesh);
                    MWAnim::decorate(weaponMesh, nullptr);
                    contents.push_back(weaponMesh);
                }
                const auto glowColor = getGlowColor(*weapon);
                for (auto& child : contents)
                {
                    if (glowColor)
                        addEnv(child, glowColor, getTransparency(*this));
                    weaponSwitch->addChild(visible, child);
                }
                weaponNode->addChild(weaponSwitch);
            }
            mScabbard = attachHolstered(node, result, bone, {}, animate);
        }
        if (canShowWeapon)
            mHolsteredWeaponSwitch = weaponSwitch;
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
            auto node = mContext.readNode(model);
            auto result = MWAnim::decorate(node, nullptr);
            auto path = attachHolstered(node, result, slots[i], glowColor, false);
            if (!path.empty())
                mQuiver.push_back(std::move(path));
        }
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
            auto node = mContext.readActor(holstered);
            if (!node || !findGroup(*node, "Bip01 Sheath"))
                return {};
        }
        return mesh;
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
        removePath(mHolsteredShield);
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
        const auto glowColor = getGlowColor(*shield);
        const bool hasHolsteredModel = modelExists(holstered);
        auto node = hasHolsteredModel ? readHolstered(holstered) : mContext.readNode(mesh);
        if (!node)
            return;
        auto result = MWAnim::decorate(node, nullptr);
        // An empty sheath node places the common shield mesh, so the _sh file need not hold it.
        if (vsg::Group* sheath = hasHolsteredModel ? findGroup(*node, "Bip01 Sheath") : nullptr;
            sheath && sheath->children.empty())
        {
            auto shieldMesh = mContext.readNode(mesh);
            MWAnim::decorate(shieldMesh, nullptr);
            sheath->addChild(shieldMesh);
        }
        mHolsteredShieldSwitch = vsg::Switch::create();
        mHolsteredShieldSwitch->addChild(!isShown(Wield::CarriedLeft), node);
        mHolsteredShield = attachHolstered(mHolsteredShieldSwitch, result, bone, glowColor, true);
    }

    void Npc::show(Wield w, bool visible)
    {
        Wielding::show(w, visible);
        if (w == Wield::Weapon && mHolsteredWeaponSwitch)
            mHolsteredWeaponSwitch->setAllChildren(!visible);
        else if (w == Wield::CarriedLeft && mHolsteredShieldSwitch)
            mHolsteredShieldSwitch->setAllChildren(!visible);
        if ((w == Wield::Weapon || w == Wield::Ammo) && mViewMode != ViewMode::HeadOnly && Settings::game().mWeaponSheathing)
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
            std::string model = std::string(ammo->getClass().getModel(*ammo));
            auto node = mPartContext->readNode(model);
            auto result = MWAnim::cloneIfRequired(node);
            addSwitch(node, Wield::Ammo);
            mAttachAmmo->subgraphRequiresLocalFrustum = true;
            mAttachAmmo->addChild(node);
        }
    }

    void Npc::setFacialState(bool enable, bool talking, float loudness)
    {
        mFace->enabled = enable;
        mFace->talking = talking;
        mFace->loudness = loudness;
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
