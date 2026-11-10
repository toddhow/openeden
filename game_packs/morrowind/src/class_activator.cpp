#include <components/misc/vsgmath.hpp>
#include <game_morrowind/class_activator.hpp>

#include <vsg/maths/quat.h>

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadsndg.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/misc/rng.hpp>
#include <components/vfs/pathutil.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>

#include <apps/openmw/mwworld/action.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/failedaction.hpp>
#include <apps/openmw/mwworld/nullaction.hpp>
#include <apps/openmw/mwworld/ptr.hpp>

#include <apps/openmw/mwphysics/physicssystem.hpp>

#include <apps/openmw/mwrender/renderinginterface.hpp>
#include <apps/openmw/mwrender/vismask.hpp>

#include <apps/openmw/mwgui/tooltips.hpp>

#include <apps/openmw/mwmechanics/npcstats.hpp>

#include <apps/openmw/mwclass/classmodel.hpp>

namespace MWClass
{
    Activator::Activator()
        : MWWorld::RegisteredClass<Activator>(ESM::Activator::sRecordId)
    {
    }

    void Activator::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    void Activator::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Activator::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::Layers::WORLD);
    }

    std::string_view Activator::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Activator>(ptr);
    }

    bool Activator::isActivator() const
    {
        return true;
    }

    bool Activator::useAnim() const
    {
        return true;
    }

    std::string_view Activator::getName(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mName;
    }

    ESM::RefId Activator::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mScript;
    }

    bool Activator::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        return !getName(ptr).empty();
    }

    MWGui::ToolTipInfo Activator::getToolTipInfo(const MWWorld::ConstPtr& ptr, int count) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        MWGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + MWGui::ToolTips::getCountString(count);

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += MWGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        return info;
    }

    std::unique_ptr<MWWorld::Action> Activator::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        if (actor.getClass().isNpc() && actor.getClass().getNpcStats(actor).isWerewolf())
        {
            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
            const ESM::Sound* sound = store.get<ESM::Sound>().searchRandom("WolfActivator", prng);

            std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::FailedAction>("#{sWerewolfRefusal}");
            if (sound)
                action->setSound(sound->mId);

            return action;
        }
        return std::make_unique<MWWorld::NullAction>();
    }

    MWWorld::Ptr Activator::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    ESM::RefId Activator::getSoundIdFromSndGen(const MWWorld::Ptr& ptr, std::string_view name) const
    {
        // Assume it's not empty, since we wouldn't have gotten the soundgen otherwise
        const std::string_view model = getModel(ptr);
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::RefId* creatureId = nullptr;

        for (const ESM::Creature& iter : store.get<ESM::Creature>())
        {
            if (!iter.mModel.empty() && Misc::StringUtils::ciEqual(model, iter.mModel))
            {
                creatureId = !iter.mOriginal.empty() ? &iter.mOriginal : &iter.mId;
                break;
            }
        }

        int type = getSndGenTypeFromName(name);

        std::vector<const ESM::SoundGenerator*> fallbacksounds;
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        if (creatureId && !creatureId->empty())
        {
            std::vector<const ESM::SoundGenerator*> sounds;
            for (auto sound = store.get<ESM::SoundGenerator>().begin(); sound != store.get<ESM::SoundGenerator>().end();
                 ++sound)
            {
                if (type == sound->mType && !sound->mCreature.empty() && (*creatureId == sound->mCreature))
                    sounds.push_back(&*sound);
                if (type == sound->mType && sound->mCreature.empty())
                    fallbacksounds.push_back(&*sound);
            }

            if (!sounds.empty())
                return sounds[Misc::Rng::rollDice(sounds.size(), prng)]->mSound;
            if (!fallbacksounds.empty())
                return fallbacksounds[Misc::Rng::rollDice(fallbacksounds.size(), prng)]->mSound;
        }
        else
        {
            // The activator doesn't have a corresponding creature ID, but we can try to use the defaults
            for (auto sound = store.get<ESM::SoundGenerator>().begin(); sound != store.get<ESM::SoundGenerator>().end();
                 ++sound)
                if (type == sound->mType && sound->mCreature.empty())
                    fallbacksounds.push_back(&*sound);

            if (!fallbacksounds.empty())
                return fallbacksounds[Misc::Rng::rollDice(fallbacksounds.size(), prng)]->mSound;
        }

        return ESM::RefId();
    }

    int Activator::getSndGenTypeFromName(std::string_view name)
    {
        if (name == "left")
            return ESM::SoundGenerator::LeftFoot;
        if (name == "right")
            return ESM::SoundGenerator::RightFoot;
        if (name == "swimleft")
            return ESM::SoundGenerator::SwimLeft;
        if (name == "swimright")
            return ESM::SoundGenerator::SwimRight;
        if (name == "moan")
            return ESM::SoundGenerator::Moan;
        if (name == "roar")
            return ESM::SoundGenerator::Roar;
        if (name == "scream")
            return ESM::SoundGenerator::Scream;
        if (name == "land")
            return ESM::SoundGenerator::Land;

        throw std::runtime_error("Unexpected soundgen type: " + std::string(name));
    }
}
