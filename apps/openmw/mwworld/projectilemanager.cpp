#include "projectilemanager.hpp"

#include <memory>
#include <optional>

#include <components/debug/debuglog.hpp>

#include <components/esm3/actoridconverter.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/projectilestate.hpp>

#include <components/esm/quaternion.hpp>
#include <components/esm/vector3.hpp>

#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/vsgadapters/osgcompat.hpp>
#include <components/vsgutil/convert.hpp>

#include <components/settings/values.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwrender/env.hpp"
#include "../mwrender/projectiles.hpp"
#include "../mwrender/rendermanager.hpp"

#include "../mwsound/sound.hpp"

#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/projectile.hpp"

namespace
{
    ESM::EffectList getMagicBoltData(std::vector<ESM::RefId>& projectileIDs, std::set<ESM::RefId>& sounds, float& speed,
        std::string& texture, std::string& sourceName, const ESM::RefId& id)
    {
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
        const ESM::EffectList* effects;
        if (const ESM::Spell* spell = esmStore.get<ESM::Spell>().search(id)) // check if it's a spell
        {
            sourceName = spell->mName;
            effects = &spell->mEffects;
        }
        else // check if it's an enchanted item
        {
            MWWorld::ManualRef ref(esmStore, id);
            MWWorld::Ptr ptr = ref.getPtr();
            const ESM::Enchantment* ench = esmStore.get<ESM::Enchantment>().find(ptr.getClass().getEnchantment(ptr));
            sourceName = ptr.getClass().getName(ptr);
            effects = &ench->mEffects;
        }

        int count = 0;
        speed = 0.0f;
        ESM::EffectList projectileEffects;
        for (const ESM::IndexedENAMstruct& effect : effects->mList)
        {
            const ESM::MagicEffect* magicEffect
                = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(effect.mData.mEffectID);

            // Speed of multi-effect projectiles should be the average of the constituent effects,
            // based on observation of the original engine.
            speed += magicEffect->mData.mSpeed;
            count++;

            if (effect.mData.mRange != ESM::RT_Target)
                continue;

            if (magicEffect->mBolt.empty())
                projectileIDs.emplace_back(ESM::RefId::stringRefId("VFX_DefaultBolt"));
            else
                projectileIDs.push_back(magicEffect->mBolt);

            if (!magicEffect->mBoltSound.empty())
                sounds.emplace(magicEffect->mBoltSound);
            else
                sounds.emplace(MWBase::Environment::get()
                                   .getESMStore()
                                   ->get<ESM::Skill>()
                                   .find(magicEffect->mData.mSchool)
                                   ->mSchool->mBoltSound);
            projectileEffects.mList.push_back(effect);
        }

        if (count != 0)
            speed /= count;

        // the particle texture is only used if there is only one projectile
        if (projectileEffects.mList.size() == 1)
        {
            const ESM::MagicEffect* magicEffect
                = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(
                    effects->mList.begin()->mData.mEffectID);
            texture = magicEffect->mParticle;
        }

        // insert a VFX_Multiple projectile if there are multiple projectile effects
        if (projectileEffects.mList.size() > 1)
        {
            const ESM::RefId projectileId
                = ESM::RefId::stringRefId("VFX_Multiple" + std::to_string(effects->mList.size()));
            projectileIDs.insert(projectileIDs.begin(), projectileId);
        }

        return projectileEffects;
    }

    vsg::vec3 getMagicBoltLightDiffuseColor(const ESM::EffectList& effects)
    {
        // Calculate combined light diffuse color from magical effects
        vsg::vec4 lightDiffuseColor = {};
        for (const ESM::IndexedENAMstruct& enam : effects.mList)
        {
            const ESM::MagicEffect* magicEffect
                = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(enam.mData.mEffectID);
            lightDiffuseColor += toVsg(magicEffect->getColor());
        }
        const size_t numberOfEffects = effects.mList.size();
        return vsgUtil::toVec3(lightDiffuseColor) / static_cast<float>(numberOfEffects);
    }

    osg::Quat lookAt(const osg::Vec3f& pos)
    {
        // Rotate the forward vector towards the position (used for gravity-affected projectiles)
        // Can't use Quat::makeRotate as the shortest angle contains undesirable local roll
        const float dist = pos.length();
        if (dist < 1e-4f)
            return {};

        const osg::Vec3f dir = pos / dist;
        osg::Vec3f right = dir ^ osg::Z_AXIS;
        if (right.normalize() < 1e-4f)
            right = osg::X_AXIS;

        const osg::Vec3f up = right ^ dir;

        osg::Matrixf mat(right.x(), right.y(), right.z(), 0.f, dir.x(), dir.y(), dir.z(), 0.f, up.x(), up.y(), up.z(),
            0.f, 0.f, 0.f, 0.f, 1.f);

        osg::Quat orient;
        orient.set(mat);
        return orient;
    }
}

namespace MWWorld
{

    ProjectileManager::ProjectileManager(MWRender::RenderManager* rendering, MWPhysics::PhysicsSystem* physics)
        : mRender(rendering)
        , mPhysics(physics)
        , mCleanupTimer(0.0f)
    {
    }

    void ProjectileManager::createModel(State& state, const std::string& model, const vsg::vec3& pos,
        const vsg::quat& orient, bool rotate, std::optional<vsg::vec3> lightDiffuseColor,
        std::optional<vsg::vec4> glowColor, std::string texture)
    {

        std::vector<std::string> additionalModels;
        if (state.mIdMagic.size() > 1)
        {
            for (size_t iter = 1; iter != state.mIdMagic.size(); ++iter)
            {
                const ESM::Weapon* weapon = MWBase::Environment::get().getWorld()->getStore().get<ESM::Weapon>().find(
                    state.mIdMagic.at(iter));
                additionalModels.push_back(weapon->mModel);
            }
        }

        state.handle = mRender->getProjectiles()->add(
            model, pos, orient, rotate, lightDiffuseColor, glowColor, texture, additionalModels);
    }

    void ProjectileManager::launchMagicBolt(
        const ESM::RefId& spellId, const Ptr& caster, const osg::Vec3f& fallbackDirection, ESM::RefNum item)
    {
        osg::Vec3f pos = caster.getRefData().getPosition().asVec3();
        if (caster.getClass().isActor())
        {
            // Note: we ignore the collision box offset, this is required to make some flying creatures work as
            // intended.
            pos.z() += mPhysics->getRenderingHalfExtents(caster).z() * 2 * Constants::TorsoHeight;
        }

        // Actors can't cast target spells underwater
        if (caster.getClass().isActor() && MWBase::Environment::get().getWorld()->isUnderwater(caster.getCell(), pos))
            return;

        osg::Quat orient;
        if (caster.getClass().isActor())
            orient = osg::Quat(caster.getRefData().getPosition().rot[0], osg::Vec3f(-1, 0, 0))
                * osg::Quat(caster.getRefData().getPosition().rot[2], osg::Vec3f(0, 0, -1));
        else
            orient.makeRotate(osg::Vec3f(0, 1, 0), osg::Vec3f(fallbackDirection));

        MagicBoltState state;
        state.mSpellId = spellId;
        state.mCasterHandle = caster;
        state.mItem = item;
        MWBase::Environment::get().getWorldModel()->registerPtr(caster);
        state.mCaster = caster.getCellRef().getRefNum();

        std::string texture;

        state.mEffects = getMagicBoltData(
            state.mIdMagic, state.mSoundIds, state.mSpeed, texture, state.mSourceName, state.mSpellId);

        // Non-projectile should have been removed by getMagicBoltData
        if (state.mEffects.mList.empty())
            return;

        if (!caster.getClass().isActor() && fallbackDirection.length2() <= 0)
        {
            Log(Debug::Warning) << "Unable to launch magic bolt (direction to target is empty)";
            return;
        }

        MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), state.mIdMagic.at(0));
        MWWorld::Ptr ptr = ref.getPtr();

        auto lightDiffuseColor = getMagicBoltLightDiffuseColor(state.mEffects);

        VFS::Path::Normalized model = ptr.getClass().getCorrectedModel(ptr);
        createModel(state, model.value(), toVsg(pos), toVsg(orient), true, lightDiffuseColor, {}, texture);

        MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
        for (const auto& soundid : state.mSoundIds)
        {
            MWBase::Sound* sound
                = sndMgr->playSound3D(pos, soundid, 1.0f, 1.0f, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
            if (sound)
                state.mSounds.push_back(sound);
        }

        // in case there are multiple effects, the model is a dummy without geometry. Use the second effect for physics
        // shape
        if (state.mIdMagic.size() > 1)
        {
            model = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(
                MWBase::Environment::get().getESMStore()->get<ESM::Weapon>().find(state.mIdMagic[1])->mModel));
        }
        state.mProjectileId = mPhysics->addProjectile(caster, pos, model, true);
        state.mToDelete = false;
        mMagicBolts.push_back(std::move(state));
    }

    void ProjectileManager::launchProjectile(const Ptr& actor, const ConstPtr& projectile, const osg::Vec3f& pos,
        const osg::Quat& orient, const Ptr& bow, float speed, float attackStrength)
    {
        ProjectileState state;
        state.mCaster = actor.getCellRef().getRefNum();
        state.mBowId = bow.getCellRef().getRefId();
        state.mVelocity = orient * osg::Vec3f(0, 1, 0) * speed;
        state.mIdArrow = projectile.getCellRef().getRefId();
        state.mCasterHandle = actor;
        state.mAttackStrength = attackStrength;

        MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), projectile.getCellRef().getRefId());
        MWWorld::Ptr ptr = ref.getPtr();

        const VFS::Path::Normalized model = ptr.getClass().getCorrectedModel(ptr);
        createModel(state, model.value(), toVsg(pos), toVsg(orient), false, {}, MWRender::getGlowColor(ptr));

        state.mProjectileId = mPhysics->addProjectile(actor, pos, model, false);
        state.mToDelete = false;
        mProjectiles.push_back(std::move(state));
    }

    void ProjectileManager::updateCasters()
    {
        for (auto& state : mProjectiles)
        {
            state.mCasterHandle = state.getCaster();
            mPhysics->setCaster(state.mProjectileId, state.mCasterHandle);
        }

        for (auto& state : mMagicBolts)
        {
            if (!state.mCaster.isSet())
                continue;

            state.mCasterHandle = state.getCaster();
            if (state.mCasterHandle.isEmpty())
            {
                Log(Debug::Error) << "Couldn't find caster with ID " << state.mCaster;
                cleanupMagicBolt(state);
                continue;
            }
            mPhysics->setCaster(state.mProjectileId, state.mCasterHandle);
        }
    }

    void ProjectileManager::update(float dt)
    {
        periodicCleanup(dt);
        moveProjectiles(dt);
        moveMagicBolts(dt);
    }

    void ProjectileManager::periodicCleanup(float dt)
    {
        mCleanupTimer -= dt;
        if (mCleanupTimer <= 0.0f)
        {
            mCleanupTimer = 2.0f;

            auto isCleanable = [](const ProjectileManager::State& state) -> bool {
                const float farawayThreshold = 72000.0f;
                osg::Vec3 playerPos = MWMechanics::getPlayer().getRefData().getPosition().asVec3();
                return vsg::length2(state.handle->position - toVsg(playerPos)) >= farawayThreshold * farawayThreshold;
            };

            for (auto& projectileState : mProjectiles)
            {
                if (isCleanable(projectileState))
                    cleanupProjectile(projectileState);
            }

            for (auto& magicBoltState : mMagicBolts)
            {
                if (isCleanable(magicBoltState))
                    cleanupMagicBolt(magicBoltState);
            }
        }
    }

    void ProjectileManager::moveMagicBolts(float duration)
    {
        const bool normaliseRaceSpeed = Settings::game().mNormaliseRaceSpeed;
        for (auto& magicBoltState : mMagicBolts)
        {
            if (magicBoltState.mToDelete)
                continue;

            auto* projectile = mPhysics->getProjectile(magicBoltState.mProjectileId);
            if (!projectile->isActive())
                continue;
            // If the actor caster is gone, the magic bolt needs to be removed from the scene during the next frame.
            MWWorld::Ptr caster = magicBoltState.getCaster();
            if (!caster.isEmpty() && caster.getClass().isActor())
            {
                if (caster.getCellRef().getCount() <= 0 || caster.getClass().getCreatureStats(caster).isDead())
                {
                    cleanupMagicBolt(magicBoltState);
                    continue;
                }
            }

            const auto& store = MWBase::Environment::get().getWorld()->getStore();
            auto orient = magicBoltState.handle->attitude;
            static float fTargetSpellMaxSpeed
                = store.get<ESM::GameSetting>().find("fTargetSpellMaxSpeed")->mValue.getFloat();
            float speed = fTargetSpellMaxSpeed * magicBoltState.mSpeed;
            if (!normaliseRaceSpeed && !caster.isEmpty() && caster.getClass().isNpc())
            {
                const auto npc = caster.get<ESM::NPC>()->mBase;
                const auto race = store.get<ESM::Race>().find(npc->mRace);
                speed *= npc->isMale() ? race->mData.mMaleWeight : race->mData.mFemaleWeight;
            }
            vsg::vec3 direction = vsg::normalize(orient * vsg::vec3(0, 1, 0));
            projectile->setVelocity(toOsg(direction) * speed);

            for (const auto& sound : magicBoltState.mSounds)
            {
                sound->setVelocity(toOsg(direction * speed));
            }

            // For AI actors, get combat targets to use in the ray cast. Only those targets will return a positive hit
            // result.
            std::vector<MWWorld::Ptr> targetActors;
            if (!caster.isEmpty() && caster.getClass().isActor() && caster != MWMechanics::getPlayer())
                caster.getClass().getCreatureStats(caster).getAiSequence().getCombatTargets(targetActors);
            projectile->setValidTargets(targetActors);
        }
    }

    void ProjectileManager::moveProjectiles(float duration)
    {
        for (auto& projectileState : mProjectiles)
        {
            if (projectileState.mToDelete)
                continue;

            auto* projectile = mPhysics->getProjectile(projectileState.mProjectileId);
            if (!projectile->isActive())
                continue;
            // gravity constant - must be way lower than the gravity affecting actors, since we're not
            // simulating aerodynamics at all
            projectileState.mVelocity
                -= osg::Vec3f(0, 0, Constants::GravityConst * Constants::UnitsPerMeter * 0.1f) * duration;

            projectile->setVelocity(projectileState.mVelocity);

            projectileState.handle->attitude = toVsg(lookAt(projectileState.mVelocity));

            MWWorld::Ptr caster = projectileState.getCaster();

            // For AI actors, get combat targets to use in the ray cast. Only those targets will return a positive hit
            // result.
            std::vector<MWWorld::Ptr> targetActors;
            if (!caster.isEmpty() && caster.getClass().isActor() && caster != MWMechanics::getPlayer())
                caster.getClass().getCreatureStats(caster).getAiSequence().getCombatTargets(targetActors);
            projectile->setValidTargets(targetActors);
        }
    }

    void ProjectileManager::processHits()
    {
        for (auto& projectileState : mProjectiles)
        {
            if (projectileState.mToDelete)
                continue;

            auto* projectile = mPhysics->getProjectile(projectileState.mProjectileId);

            const auto pos = projectile->getSimulationPosition();
            projectileState.handle->position = toVsg(pos);

            if (projectile->isActive())
                continue;

            const auto target = projectile->getTarget();
            auto caster = projectileState.getCaster();
            assert(target != caster);

            if (caster.isEmpty())
                caster = target;

            // Try to get a Ptr to the bow that was used. It might no longer exist.
            MWWorld::ManualRef projectileRef(*MWBase::Environment::get().getESMStore(), projectileState.mIdArrow);
            MWWorld::Ptr bow = projectileRef.getPtr();
            if (!caster.isEmpty() && projectileState.mIdArrow != projectileState.mBowId)
            {
                MWWorld::InventoryStore& inv = caster.getClass().getInventoryStore(caster);
                MWWorld::ContainerStoreIterator invIt = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (invIt != inv.end() && invIt->getCellRef().getRefId() == projectileState.mBowId)
                    bow = *invIt;
            }

            const auto hitPosition = projectile->getHitPosition();

            if (projectile->getHitWater())
                mRender->emitWaterRipple(pos);

            MWMechanics::projectileHit(
                caster, target, bow, projectileRef.getPtr(), hitPosition, projectileState.mAttackStrength);
            projectileState.mToDelete = true;
        }
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
        for (auto& magicBoltState : mMagicBolts)
        {
            if (magicBoltState.mToDelete)
                continue;

            auto* projectile = mPhysics->getProjectile(magicBoltState.mProjectileId);

            const auto pos = projectile->getSimulationPosition();
            magicBoltState.handle->position = toVsg(pos);
            for (const auto& sound : magicBoltState.mSounds)
                sound->setPosition(pos);

            const Ptr caster = magicBoltState.getCaster();

            const MWBase::World& world = *MWBase::Environment::get().getWorld();
            const bool active = projectile->isActive();
            if (active && !world.isUnderwater(caster.getCell(), pos))
                continue;

            const Ptr target = !active ? projectile->getTarget() : Ptr();

            assert(target != caster);

            MWMechanics::CastSpell cast(caster, target);
            cast.mHitPosition = !active ? projectile->getHitPosition() : pos;
            cast.mId = magicBoltState.mSpellId;
            cast.mSourceName = magicBoltState.mSourceName;
            cast.mItem = magicBoltState.mItem;
            // Grab original effect list so the indices are correct
            const ESM::EffectList* effects;
            if (const ESM::Spell* spell = esmStore.get<ESM::Spell>().search(magicBoltState.mSpellId))
                effects = &spell->mEffects;
            else
            {
                MWWorld::ManualRef ref(esmStore, magicBoltState.mSpellId);
                const MWWorld::Ptr& ptr = ref.getPtr();
                effects = &esmStore.get<ESM::Enchantment>().find(ptr.getClass().getEnchantment(ptr))->mEffects;
            }
            cast.inflict(target, *effects, ESM::RT_Target);

            magicBoltState.mToDelete = true;
        }

        for (auto& projectileState : mProjectiles)
        {
            if (projectileState.mToDelete)
                cleanupProjectile(projectileState);
        }

        for (auto& magicBoltState : mMagicBolts)
        {
            if (magicBoltState.mToDelete)
                cleanupMagicBolt(magicBoltState);
        }
        mProjectiles.erase(std::remove_if(mProjectiles.begin(), mProjectiles.end(),
                               [](const State& state) { return state.mToDelete; }),
            mProjectiles.end());
        mMagicBolts.erase(
            std::remove_if(mMagicBolts.begin(), mMagicBolts.end(), [](const State& state) { return state.mToDelete; }),
            mMagicBolts.end());
    }

    void ProjectileManager::cleanupProjectile(ProjectileManager::ProjectileState& state)
    {
        state.handle->remove = true;
        mPhysics->removeProjectile(state.mProjectileId);
        state.mToDelete = true;
    }

    void ProjectileManager::cleanupMagicBolt(ProjectileManager::MagicBoltState& state)
    {
        state.handle->remove = true;
        mPhysics->removeProjectile(state.mProjectileId);
        state.mToDelete = true;
        for (size_t soundIter = 0; soundIter != state.mSounds.size(); soundIter++)
        {
            MWBase::Environment::get().getSoundManager()->stopSound(state.mSounds.at(soundIter));
        }
    }

    void ProjectileManager::clear()
    {
        for (auto& mProjectile : mProjectiles)
            cleanupProjectile(mProjectile);
        mProjectiles.clear();

        for (auto& mMagicBolt : mMagicBolts)
            cleanupMagicBolt(mMagicBolt);
        mMagicBolts.clear();
    }

    void ProjectileManager::write(ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        for (const ProjectileState& projectile : mProjectiles)
        {
            writer.startRecord(ESM::REC_PROJ);

            ESM::ProjectileState state;
            state.mId = projectile.mIdArrow;
            state.mPosition = ESM::Vector3(toOsg(projectile.handle->position));
            state.mOrientation = ESM::Quaternion(toOsg(projectile.handle->attitude));
            state.mCaster = projectile.mCaster;

            state.mBowId = projectile.mBowId;
            state.mVelocity = vsg::vec3(projectile.mVelocity);
            state.mAttackStrength = projectile.mAttackStrength;

            state.save(writer);

            writer.endRecord(ESM::REC_PROJ);
        }

        for (const MagicBoltState& bolt : mMagicBolts)
        {
            writer.startRecord(ESM::REC_MPRJ);

            ESM::MagicBoltState state;
            state.mId = bolt.mIdMagic.at(0);
            state.mPosition = ESM::Vector3(toOsg(bolt.handle->position));
            state.mOrientation = ESM::Quaternion(toOsg(bolt.handle->attitude));
            state.mCaster = bolt.mCaster;
            state.mItem = bolt.mItem;
            state.mSpellId = bolt.mSpellId;
            state.mSpeed = bolt.mSpeed;

            state.save(writer);

            writer.endRecord(ESM::REC_MPRJ);
        }
    }

    bool ProjectileManager::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type == ESM::REC_PROJ)
        {
            ESM::ProjectileState esm;
            esm.load(reader);

            ProjectileState state;
            state.mCaster = esm.mCaster;
            state.mBowId = esm.mBowId;
            state.mVelocity = vsg::vec3(esm.mVelocity);
            state.mIdArrow = esm.mId;
            state.mAttackStrength = esm.mAttackStrength;
            state.mToDelete = false;

            VFS::Path::Normalized model;
            std::optional<vsg::vec4> glowColor;
            try
            {
                MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), esm.mId);
                MWWorld::Ptr ptr = ref.getPtr();
                model = ptr.getClass().getCorrectedModel(ptr);

                state.mProjectileId
                    = mPhysics->addProjectile(state.getCaster(), osg::Vec3f(vsg::vec3(esm.mPosition)), model, false);
                glowColor = MWRender::getGlowColor(ptr);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to add projectile for " << esm.mId
                                    << " while reading projectile record: " << e.what();
                return true;
            }

            createModel(state, model.value(), toVsg(osg::Vec3f(vsg::vec3(esm.mPosition))), toVsg(osg::Quat(vsg::dquat(esm.mOrientation))), false,
                {}, glowColor);

            mProjectiles.push_back(std::move(state));
            return true;
        }
        if (type == ESM::REC_MPRJ)
        {
            ESM::MagicBoltState esm;
            esm.load(reader);

            MagicBoltState state;
            state.mIdMagic.push_back(esm.mId);
            state.mSpellId = esm.mSpellId;
            state.mCaster = esm.mCaster;
            state.mToDelete = false;
            state.mItem = esm.mItem;
            std::string texture;

            try
            {
                state.mEffects = getMagicBoltData(
                    state.mIdMagic, state.mSoundIds, state.mSpeed, texture, state.mSourceName, state.mSpellId);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to recreate magic projectile for " << esm.mId << " and spell "
                                    << state.mSpellId << " while reading projectile record: " << e.what();
                return true;
            }

            state.mSpeed = esm.mSpeed; // speed is derived from non-projectile effects as well as
                                       // projectile effects, so we can't calculate it from the save
                                       // file's effect list, which is already trimmed of non-projectile
                                       // effects. We need to use the stored value.

            VFS::Path::Normalized model;
            try
            {
                MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), state.mIdMagic.at(0));
                MWWorld::Ptr ptr = ref.getPtr();
                model = ptr.getClass().getCorrectedModel(ptr);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to get model for " << state.mIdMagic.at(0)
                                    << " while reading projectile record: " << e.what();
                return true;
            }

            auto lightDiffuseColor = getMagicBoltLightDiffuseColor(state.mEffects);
            createModel(state, model.value(), toVsg(osg::Vec3f(vsg::vec3(esm.mPosition))), toVsg(osg::Quat(vsg::dquat(esm.mOrientation))), true,
                lightDiffuseColor, {}, texture);
            state.mProjectileId = mPhysics->addProjectile(state.getCaster(), osg::Vec3f(vsg::vec3(esm.mPosition)), model, true);

            MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
            for (const auto& soundid : state.mSoundIds)
            {
                MWBase::Sound* sound = sndMgr->playSound3D(
                    vsg::vec3(esm.mPosition), soundid, 1.0f, 1.0f, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
                if (sound)
                    state.mSounds.push_back(sound);
            }

            mMagicBolts.push_back(std::move(state));
            return true;
        }

        return false;
    }

    size_t ProjectileManager::countSavedGameRecords() const
    {
        return mMagicBolts.size() + mProjectiles.size();
    }

    void ProjectileManager::saveLoaded(const ESM::ESMReader& reader)
    {
        // Can't do this in readRecord because the vectors might get reallocated as they grow
        if (reader.mActorIdConverter)
        {
            for (ProjectileState& projectile : mProjectiles)
                reader.mActorIdConverter->convert(projectile.mCaster, projectile.mCaster.mIndex);
            for (MagicBoltState& bolt : mMagicBolts)
                reader.mActorIdConverter->convert(bolt.mCaster, bolt.mCaster.mIndex);
        }
    }

    MWWorld::Ptr ProjectileManager::State::getCaster()
    {
        if (!mCasterHandle.isEmpty())
            return mCasterHandle;

        return MWBase::Environment::get().getWorldModel()->getPtr(mCaster);
    }

}
