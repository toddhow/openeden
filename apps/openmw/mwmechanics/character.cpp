#include "character.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <unordered_set>

#include <iostream>
#include <sstream>

#include <components/animation/transform.hpp>
#include <components/esmbridge/records.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/misc/mathutil.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/mwanimation/effects.hpp>
#include <components/mwanimation/groups.hpp>
#include <components/mwanimation/lighteffect.hpp>
#include <components/mwanimation/play.hpp>
#include <components/mwanimation/position.hpp>
#include <components/vsgutil/computetransform.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include <span>

#include "ragdolltrigger.hpp"

#include "../mwrender/actorikcontroller.hpp"
#include "../mwrender/actorragdolldriver.hpp"
#include "../mwrender/creativemotiondriver.hpp"
#include "../mwrender/creativemotionstyle.hpp"
#include "../mwrender/effect.hpp"
#include "../mwrender/itemlight.hpp"
#include "../mwrender/player.hpp"
#include "../mwrender/transparency.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/physicssystem.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/spellcaststate.hpp"

#include "actorutil.hpp"
#include "aicombataction.hpp"
#include "combat.hpp"
#include "creaturestats.hpp"
#include "movement.hpp"
#include "npcstats.hpp"
#include "security.hpp"
#include "spellcasting.hpp"
#include "weapontype.hpp"

namespace
{
    // The styles of task 0053, read once each from the VFS ("styles/<name>.style.json"). Reads happen on the main
    // thread (createPoseControllers and the console); the profiles are then only read, by animation threads.
    MWRender::StyleLibrary& styleLibrary()
    {
        static MWRender::StyleLibrary library([](const std::string& path) -> std::optional<std::string> {
            const VFS::Manager* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
            const VFS::Path::Normalized normalized(path);
            if (!vfs->exists(normalized))
                return std::nullopt;
            const Files::IStreamPtr stream = vfs->get(normalized);
            std::ostringstream text;
            text << stream->rdbuf();
            return text.str();
        });
        return library;
    }

    struct UpdateEquipment : public MWWorld::InventoryStoreListener
    {
        UpdateEquipment(MWAnim::Wielding* w)
            : actor(w)
        {
        }
        MWAnim::Wielding* actor;
        void equipmentChanged() override { actor->updateEquipment(); }
    };

    std::string_view getBestAttack(const ESM::Weapon* weapon)
    {
        int slash = weapon->mData.mSlash[0] + weapon->mData.mSlash[1];
        int chop = weapon->mData.mChop[0] + weapon->mData.mChop[1];
        int thrust = weapon->mData.mThrust[0] + weapon->mData.mThrust[1];
        if (slash == chop && slash == thrust)
            return "slash";
        else if (thrust >= chop && thrust >= slash)
            return "thrust";
        else if (slash >= chop && slash >= thrust)
            return "slash";
        else
            return "chop";
    }

    // Converts a movement Run state to its equivalent Walk state, if there is one.
    MWMechanics::CharacterState runStateToWalkState(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_RunForward:
                return CharState_WalkForward;
            case CharState_RunBack:
                return CharState_WalkBack;
            case CharState_RunLeft:
                return CharState_WalkLeft;
            case CharState_RunRight:
                return CharState_WalkRight;
            case CharState_SwimRunForward:
                return CharState_SwimWalkForward;
            case CharState_SwimRunBack:
                return CharState_SwimWalkBack;
            case CharState_SwimRunLeft:
                return CharState_SwimWalkLeft;
            case CharState_SwimRunRight:
                return CharState_SwimWalkRight;
            case CharState_RunStart:
                return CharState_WalkStart;
            case CharState_RunStop:
                return CharState_WalkStop;
            default:
                return state;
        }
    }

    // Converts a Hit state to its equivalent Death state.
    MWMechanics::CharacterState hitStateToDeathState(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_SwimKnockDown:
                return CharState_SwimDeathKnockDown;
            case CharState_SwimKnockOut:
                return CharState_SwimDeathKnockOut;
            case CharState_KnockDown:
                return CharState_DeathKnockDown;
            case CharState_KnockOut:
                return CharState_DeathKnockOut;
            default:
                return CharState_None;
        }
    }

    // Converts a movement state to its equivalent base animation group as long as it is a movement state.
    std::string_view movementStateToAnimGroup(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_WalkForward:
                return "walkforward";
            case CharState_WalkBack:
                return "walkback";
            case CharState_WalkLeft:
                return "walkleft";
            case CharState_WalkRight:
                return "walkright";

            case CharState_SwimWalkForward:
                return "swimwalkforward";
            case CharState_SwimWalkBack:
                return "swimwalkback";
            case CharState_SwimWalkLeft:
                return "swimwalkleft";
            case CharState_SwimWalkRight:
                return "swimwalkright";

            case CharState_RunForward:
                return "runforward";
            case CharState_RunBack:
                return "runback";
            case CharState_RunLeft:
                return "runleft";
            case CharState_RunRight:
                return "runright";

            case CharState_SwimRunForward:
                return "swimrunforward";
            case CharState_SwimRunBack:
                return "swimrunback";
            case CharState_SwimRunLeft:
                return "swimrunleft";
            case CharState_SwimRunRight:
                return "swimrunright";

            case CharState_SneakForward:
                return "sneakforward";
            case CharState_SneakBack:
                return "sneakback";
            case CharState_SneakLeft:
                return "sneakleft";
            case CharState_SneakRight:
                return "sneakright";

            case CharState_TurnLeft:
                return "turnleft";
            case CharState_TurnRight:
                return "turnright";
            case CharState_SwimTurnLeft:
                return "swimturnleft";
            case CharState_SwimTurnRight:
                return "swimturnright";
            case CharState_WalkStart:
                return "walkstart";
            case CharState_RunStart:
                return "runstart";
            case CharState_WalkStop:
                return "walkstop";
            case CharState_RunStop:
                return "runstop";
            case CharState_RunSkid:
                return "runskid";
            case CharState_TurnLeft90:
                return "turnleft90";
            case CharState_TurnRight90:
                return "turnright90";
            default:
                return {};
        }
    }

    // Converts a death state to its equivalent animation group as long as it is a death state.
    std::string_view deathStateToAnimGroup(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_SwimDeath:
                return "swimdeath";
            case CharState_SwimDeathKnockDown:
                return "swimdeathknockdown";
            case CharState_SwimDeathKnockOut:
                return "swimdeathknockout";
            case CharState_DeathKnockDown:
                return "deathknockdown";
            case CharState_DeathKnockOut:
                return "deathknockout";
            case CharState_Death1:
                return "death1";
            case CharState_Death2:
                return "death2";
            case CharState_Death3:
                return "death3";
            case CharState_Death4:
                return "death4";
            case CharState_Death5:
                return "death5";
            default:
                return {};
        }
    }

    // Converts a hit state to its equivalent animation group as long as it is a hit state.
    std::string hitStateToAnimGroup(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_SwimHit:
                return "swimhit";
            case CharState_SwimKnockDown:
                return "swimknockdown";
            case CharState_SwimKnockOut:
                return "swimknockout";

            case CharState_Hit:
                return "hit";
            case CharState_KnockDown:
                return "knockdown";
            case CharState_KnockOut:
                return "knockout";

            case CharState_Block:
                return "shield";

            default:
                return {};
        }
    }

    // Converts an idle state to its equivalent animation group.
    std::string idleStateToAnimGroup(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        switch (state)
        {
            case CharState_IdleSwim:
                return "idleswim";
            case CharState_IdleSneak:
                return "idlesneak";
            case CharState_Idle:
            case CharState_SpecialIdle:
                return "idle";
            default:
                return {};
        }
    }

    MWAnim::Priority getIdlePriority(MWMechanics::CharacterState state)
    {
        using namespace MWMechanics;
        MWAnim::Priority priority(Priority_Default);
        switch (state)
        {
            case CharState_IdleSwim:
                return Priority_SwimIdle;
            case CharState_IdleSneak:
                priority[MWAnim::BoneGroup_LowerBody] = Priority_SneakIdleLowerBody;
                [[fallthrough]];
            default:
                return priority;
        }
    }

    float getFallDamage(const MWWorld::Ptr& ptr, float fallHeight)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Store<ESM::GameSetting>& store = world->getStore().get<ESM::GameSetting>();

        const float fallDistanceMin = store.find("fFallDamageDistanceMin")->mValue.getFloat();

        if (fallHeight >= fallDistanceMin)
        {
            const float acrobaticsSkill = static_cast<float>(ptr.getClass().getSkill(ptr, ESM::Skill::Acrobatics));
            const float jumpSpellBonus = ptr.getClass()
                                             .getCreatureStats(ptr)
                                             .getMagicEffects()
                                             .getOrDefault(ESM::MagicEffect::Jump)
                                             .getMagnitude();
            const float fallAcroBase = store.find("fFallAcroBase")->mValue.getFloat();
            const float fallAcroMult = store.find("fFallAcroMult")->mValue.getFloat();
            const float fallDistanceBase = store.find("fFallDistanceBase")->mValue.getFloat();
            const float fallDistanceMult = store.find("fFallDistanceMult")->mValue.getFloat();

            float x = fallHeight - fallDistanceMin;
            x -= (1.5f * acrobaticsSkill) + jumpSpellBonus;
            x = std::max(0.0f, x);

            float a = fallAcroBase + fallAcroMult * (100 - acrobaticsSkill);
            x = fallDistanceBase + fallDistanceMult * x;
            x *= a;

            return x;
        }
        return 0.f;
    }

    bool isRealWeapon(int weaponType)
    {
        return weaponType != ESM::Weapon::HandToHand && weaponType != ESM::Weapon::Spell
            && weaponType != ESM::Weapon::None;
    }

}

namespace MWMechanics
{

    std::string CharacterController::chooseRandomGroup(const std::string& prefix, int* num) const
    {
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();

        int numAnims = 0;
        while (mAnimation->hasAnimation(prefix + std::to_string(numAnims + 1)))
            ++numAnims;

        int roll = Misc::Rng::rollDice(numAnims, prng) + 1; // [1, numAnims]
        if (num)
            *num = roll;
        return prefix + std::to_string(roll);
    }

    void CharacterController::clearStateAnimation(std::string& anim) const
    {
        if (anim.empty())
            return;
        mAnimation->disable(anim);
        anim.clear();
    }

    void CharacterController::resetCurrentJumpState()
    {
        clearStateAnimation(mCurrentJump);
        mJumpState = JumpState_None;
    }

    void CharacterController::resetCurrentMovementState()
    {
        clearStateAnimation(mCurrentMovement);
        mMovementState = CharState_None;
        mMovementAnimationHasMovement = false;
    }

    void CharacterController::resetCurrentIdleState()
    {
        clearStateAnimation(mCurrentIdle);
        mIdleState = CharState_None;
    }

    void CharacterController::resetCurrentHitState()
    {
        clearStateAnimation(mCurrentHit);
        mHitState = CharState_None;
    }

    void CharacterController::resetCurrentWeaponState()
    {
        clearStateAnimation(mCurrentWeapon);
        mUpperBodyState = UpperBodyState::None;
    }

    void CharacterController::resetCurrentDeathState()
    {
        clearStateAnimation(mCurrentDeath);
        mDeathState = CharState_None;
        // Task 0106: an actor that is alive again is not a ragdoll (resurrect is the caller). The bones are written
        // by the animation from the next frame on. mRagdollTried stays: a second death gets a ragdoll again, through
        // mSawAlive, only if the setting allows and nothing is left of the first.
        if (mRagdollDriver || mRagdollTried)
        {
            mRagdollDriver.reset();
            mRagdollTried = false;
            mSawAlive = true;
            if (MWBase::World* world = MWBase::Environment::get().getWorld())
            {
                if (MWPhysics::PhysicsSystem* physics = world->getPhysicsSystem())
                    physics->stopActorRagdoll(mPtr);
            }
        }
    }

    void CharacterController::refreshHitRecoilAnims()
    {
        auto& charClass = mPtr.getClass();
        if (!charClass.isActor())
            return;
        const auto world = MWBase::Environment::get().getWorld();
        auto& stats = charClass.getCreatureStats(mPtr);
        bool knockout = stats.getFatigue().getCurrent() < 0 || stats.getFatigue().getBase() == 0;
        bool recovery = stats.getHitRecovery();
        bool knockdown = stats.getKnockedDown();
        bool block = stats.getBlock() && !knockout && !recovery && !knockdown;
        bool isSwimming = world->isSwimming(mPtr);

        stats.setBlock(false);

        if (mPtr == getPlayer() && mHitState == CharState_Block && block)
        {
            mHitState = CharState_None;
            resetCurrentIdleState();
        }

        if (!mPtr.getClass().isNpc() && mUpperBodyState > UpperBodyState::WeaponEquipped)
        {
            recovery = false;
            stats.setHitRecovery(false);
        }

        if (mHitState != CharState_None)
        {
            if (!mAnimation->isPlaying(mCurrentHit))
            {
                if (isKnockedOut() && mCurrentHit.empty() && knockout)
                    return;

                mHitState = CharState_None;
                mCurrentHit.clear();
                stats.setKnockedDown(false);
                stats.setHitRecovery(false);
                resetCurrentIdleState();
            }
            else if (isKnockedOut())
                mAnimation->setLoopingEnabled(mCurrentHit, knockout);
            return;
        }

        if (!knockout && !knockdown && !recovery && !block)
            return;

        MWAnim::Priority priority(Priority_Knockdown);
        std::string_view startKey = "start";
        std::string_view stopKey = "stop";
        if (knockout)
        {
            mHitState = isSwimming ? CharState_SwimKnockOut : CharState_KnockOut;
            stats.setKnockedDown(true);
        }
        else if (knockdown)
        {
            mHitState = isSwimming ? CharState_SwimKnockDown : CharState_KnockDown;
        }
        else if (recovery)
        {
            mHitState = isSwimming ? CharState_SwimHit : CharState_Hit;
            priority = Priority_Hit;
        }
        else if (block)
        {
            mHitState = CharState_Block;
            priority = Priority_Hit;
            priority[MWAnim::BoneGroup_LeftArm] = Priority_Block;
            priority[MWAnim::BoneGroup_LowerBody] = Priority_WeaponLowerBody;
            startKey = "block start";
            stopKey = "block stop";
        }

        mCurrentHit = hitStateToAnimGroup(mHitState);

        if (isRecovery())
        {
            mCurrentHit = chooseRandomGroup(mCurrentHit);
            if (mHitState == CharState_SwimHit && !mAnimation->hasAnimation(mCurrentHit))
                mCurrentHit = chooseRandomGroup(hitStateToAnimGroup(CharState_Hit));
        }

        // Cancel upper body animations
        if (isKnockedOut() || isKnockedDown())
        {
            if (!mCurrentWeapon.empty())
                mAnimation->disable(mCurrentWeapon);
            if (mUpperBodyState > UpperBodyState::WeaponEquipped)
            {
                mUpperBodyState = UpperBodyState::WeaponEquipped;
                if (mWeaponType > ESM::Weapon::None)
                    showWeapons(true);
            }
            else if (mUpperBodyState < UpperBodyState::WeaponEquipped)
            {
                mUpperBodyState = UpperBodyState::None;
            }
        }

        if (!mAnimation->hasAnimation(mCurrentHit))
        {
            mCurrentHit.clear();
            return;
        }

        playBlendedAnimation(mCurrentHit, priority, MWAnim::BlendMask_All, true, 1, startKey, stopKey, 0.0f,
            std::numeric_limits<uint32_t>::max());
    }

    void CharacterController::refreshJumpAnims(JumpingState jump, bool force)
    {
        if (!force && jump == mJumpState)
            return;

        if (jump == JumpState_None)
        {
            if (!mCurrentJump.empty())
                resetCurrentIdleState();
            resetCurrentJumpState();
            return;
        }

        std::string_view weapShortGroup = getWeaponShortGroup(mWeaponType);
        std::string jumpAnimName = "jump";
        jumpAnimName += weapShortGroup;
        MWAnim::BlendMask jumpmask = MWAnim::BlendMask_All;
        if (!weapShortGroup.empty() && !mAnimation->hasAnimation(jumpAnimName))
            jumpAnimName = fallbackShortWeaponGroup("jump", &jumpmask);

        if (!mAnimation->hasAnimation(jumpAnimName))
        {
            if (!mCurrentJump.empty())
                resetCurrentIdleState();
            resetCurrentJumpState();
            return;
        }

        bool startAtLoop = (jump == mJumpState);
        mJumpState = jump;
        clearStateAnimation(mCurrentJump);

        mCurrentJump = jumpAnimName;
        if (mJumpState == JumpState_InAir)
            playBlendedAnimation(jumpAnimName, Priority_Jump, jumpmask, false, 1.0f,
                startAtLoop ? "loop start" : "start", "stop", 0.f, std::numeric_limits<uint32_t>::max());
        else if (mJumpState == JumpState_Landing)
            playBlendedAnimation(jumpAnimName, Priority_Jump, jumpmask, true, 1.0f, "loop stop", "stop", 0.0f, 0);
    }

    bool CharacterController::onOpen() const
    {
        if (mPtr.getType() == ESM::Container::sRecordId && mAnimation)
        {
            if (!mAnimation->hasAnimation("containeropen"))
                return true;

            if (mAnimation->isPlaying("containeropen"))
                return false;

            if (mAnimation->isPlaying("containerclose"))
                return false;

            mAnimation->play(
                "containeropen", Priority_Scripted, MWAnim::BlendMask_All, false, 1.0f, "start", "stop", 0.f, 0);
            if (mAnimation->isPlaying("containeropen"))
                return false;
        }

        return true;
    }

    void CharacterController::onClose() const
    {
        if (mPtr.getType() == ESM::Container::sRecordId)
        {
            if (!mAnimation || !mAnimation->hasAnimation("containerclose"))
                return;

            float complete, startPoint = 0.f;
            bool animPlaying = mAnimation->getInfo("containeropen", &complete);
            if (animPlaying)
                startPoint = 1.f - complete;

            mAnimation->play("containerclose", Priority_Scripted, MWAnim::BlendMask_All, false, 1.0f, "start", "stop",
                startPoint, 0);
        }
    }

    std::string_view CharacterController::getWeaponAnimation(int weaponType) const
    {
        std::string_view weaponGroup = getWeaponType(weaponType)->mLongGroup;
        if (isRealWeapon(weaponType) && !mAnimation->hasAnimation(weaponGroup))
        {
            static const std::string_view oneHandFallback = getWeaponType(ESM::Weapon::LongBladeOneHand)->mLongGroup;
            static const std::string_view twoHandFallback = getWeaponType(ESM::Weapon::LongBladeTwoHand)->mLongGroup;

            const ESM::WeaponType* weapInfo = getWeaponType(weaponType);

            // For real two-handed melee weapons use 2h swords animations as fallback, otherwise use the 1h ones
            if (weapInfo->mFlags & ESM::WeaponType::TwoHanded && weapInfo->mWeaponClass == ESM::WeaponType::Melee)
                weaponGroup = twoHandFallback;
            else
                weaponGroup = oneHandFallback;
        }
        else if (weaponType == ESM::Weapon::HandToHand && !mPtr.getClass().isBipedal(mPtr))
            return "attack1";

        return weaponGroup;
    }

    std::string_view CharacterController::getWeaponShortGroup(int weaponType) const
    {
        if (weaponType == ESM::Weapon::HandToHand && !mPtr.getClass().isBipedal(mPtr))
            return {};
        return getWeaponType(weaponType)->mShortGroup;
    }

    std::string CharacterController::fallbackShortWeaponGroup(
        const std::string& baseGroupName, MWAnim::BlendMask* blendMask) const
    {
        if (!isRealWeapon(mWeaponType))
        {
            if (blendMask != nullptr)
                *blendMask = MWAnim::BlendMask_LowerBody;

            return baseGroupName;
        }

        static const std::string_view oneHandFallback = getWeaponShortGroup(ESM::Weapon::LongBladeOneHand);
        static const std::string_view twoHandFallback = getWeaponShortGroup(ESM::Weapon::LongBladeTwoHand);

        std::string groupName = baseGroupName;
        const ESM::WeaponType* weapInfo = getWeaponType(mWeaponType);

        // For real two-handed melee weapons use 2h swords animations as fallback, otherwise use the 1h ones
        if (weapInfo->mFlags & ESM::WeaponType::TwoHanded && weapInfo->mWeaponClass == ESM::WeaponType::Melee)
            groupName += twoHandFallback;
        else
            groupName += oneHandFallback;

        // Special case for crossbows - we should apply 1h animations a fallback only for lower body
        if (mWeaponType == ESM::Weapon::MarksmanCrossbow && blendMask != nullptr)
            *blendMask = MWAnim::BlendMask_LowerBody;

        if (!mAnimation->hasAnimation(groupName))
        {
            groupName = baseGroupName;
            if (blendMask != nullptr)
                *blendMask = MWAnim::BlendMask_LowerBody;
        }

        return groupName;
    }

    void CharacterController::refreshMovementAnims(CharacterState movementState, bool force)
    {
        if (movementState == mMovementState && !force)
            return;

        std::string_view movementAnimGroup = movementStateToAnimGroup(movementState);

        if (movementAnimGroup.empty())
        {
            if (!mCurrentMovement.empty())
                resetCurrentIdleState();
            resetCurrentMovementState();
            return;
        }
        std::string movementAnimName{ movementAnimGroup };

        CharacterState prevMovementState = mMovementState;
        mMovementState = movementState;
        std::string::size_type swimpos = movementAnimName.find("swim");
        if (!mAnimation->hasAnimation(movementAnimName))
        {
            if (swimpos != std::string::npos)
            {
                movementAnimName.erase(swimpos, 4);
                swimpos = std::string::npos;
            }
        }

        MWAnim::BlendMask movemask = MWAnim::BlendMask_All;

        std::string_view weapShortGroup = getWeaponShortGroup(mWeaponType);

        // Non-biped creatures don't use spellcasting-specific movement animations.
        if (!isRealWeapon(mWeaponType) && !mPtr.getClass().isBipedal(mPtr))
            weapShortGroup = {};

        if (swimpos == std::string::npos && !weapShortGroup.empty())
        {
            std::string weapMovementAnimName;
            // Spellcasting stance turning is a special case
            if (mWeaponType == ESM::Weapon::Spell && isTurning())
            {
                weapMovementAnimName = weapShortGroup;
                weapMovementAnimName += movementAnimName;
            }
            else
            {
                weapMovementAnimName = movementAnimName;
                weapMovementAnimName += weapShortGroup;
            }

            if (!mAnimation->hasAnimation(weapMovementAnimName))
                weapMovementAnimName = fallbackShortWeaponGroup(movementAnimName, &movemask);

            movementAnimName = std::move(weapMovementAnimName);
        }

        if (!mAnimation->hasAnimation(movementAnimName))
        {
            std::string::size_type runpos = movementAnimName.find("run");
            if (runpos != std::string::npos)
                movementAnimName.replace(runpos, 3, "walk");
            if (!mAnimation->hasAnimation(movementAnimName))
            {
                if (!mCurrentMovement.empty())
                    resetCurrentIdleState();
                resetCurrentMovementState();
                return;
            }
        }

        // If we're playing the same animation, or transitioning between equivalent walk/run cycles,
        // synchronize phase to prevent foot sliding or sudden pose discontinuities.
        float startpoint = 0.f;
        if (!mCurrentMovement.empty())
        {
            if (movementAnimName == mCurrentMovement)
                mAnimation->getInfo(mCurrentMovement, &startpoint);
            else if (runStateToWalkState(prevMovementState) == runStateToWalkState(movementState))
                mAnimation->getInfo(mCurrentMovement, &startpoint);
        }

        mMovementAnimationHasMovement = true;

        clearStateAnimation(mCurrentMovement);
        mCurrentMovement = std::move(movementAnimName);

        // For non-flying creatures, MW uses the Walk animation to calculate the animation velocity
        // even if we are running. This must be replicated, otherwise the observed speed would differ drastically.
        mAdjustMovementAnimSpeed = true;
        if (mPtr.getClass().getType() == ESM::Creature::sRecordId
            && !(mPtr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Flies))
        {
            CharacterState walkState = runStateToWalkState(mMovementState);
            std::string_view anim = movementStateToAnimGroup(walkState);

            mMovementAnimSpeed = mAnimation->getVelocity(anim);
            if (mMovementAnimSpeed <= 1.0f)
            {
                // Another bug: when using a fallback animation (e.g. RunForward as fallback to SwimRunForward),
                // then the equivalent Walk animation will not use a fallback, and if that animation doesn't exist
                // we will play without any scaling.
                // Makes the speed attribute of most water creatures totally useless.
                // And again, this can not be fixed without patching game data.
                mAdjustMovementAnimSpeed = false;
                mMovementAnimSpeed = 1.f;
            }
        }
        else
        {
            mMovementAnimSpeed = mAnimation->getVelocity(mCurrentMovement);

            if (mMovementAnimSpeed <= 1.0f)
            {
                // The first person anims don't have any velocity to calculate a speed multiplier from.
                // We use the third person velocities instead.
                // FIXME: should be pulled from the actual animation, but it is not presently loaded.
                bool sneaking = mMovementState == CharState_SneakForward || mMovementState == CharState_SneakBack
                    || mMovementState == CharState_SneakLeft || mMovementState == CharState_SneakRight;
                mMovementAnimSpeed = (sneaking ? 33.5452f : (isRunning() ? 222.857f : 154.064f));
                mMovementAnimationHasMovement = false;
            }
        }

        bool isTransition = isTransitionMovementState(mMovementState);
        uint32_t numLoops = isTransition ? 0 : std::numeric_limits<uint32_t>::max();
        bool autodisable = isTransition;

        playBlendedAnimation(mCurrentMovement, Priority_Movement, movemask, autodisable, 1.f, "start", "stop",
            startpoint, numLoops, true);
    }

    void CharacterController::refreshIdleAnims(CharacterState idle, bool force)
    {
        // FIXME: if one of the below states is close to their last animation frame (i.e. will be disabled in the coming
        // update), the idle animation should be displayed
        if (((mUpperBodyState != UpperBodyState::None && mUpperBodyState != UpperBodyState::WeaponEquipped)
                || mMovementState != CharState_None || !mCurrentHit.empty())
            && !mPtr.getClass().isBipedal(mPtr))
        {
            resetCurrentIdleState();
            return;
        }

        if (!force && idle == mIdleState && (mAnimation->isPlaying(mCurrentIdle) || !mAnimQueue.empty()))
            return;

        mIdleState = idle;

        std::string idleGroup = idleStateToAnimGroup(mIdleState);
        if (idleGroup.empty())
        {
            resetCurrentIdleState();
            return;
        }

        MWAnim::Priority priority = getIdlePriority(mIdleState);
        size_t numLoops = std::numeric_limits<uint32_t>::max();

        // Only play "idleswim" or "idlesneak" if they exist. Otherwise, fallback to
        // "idle"+weapon or "idle".
        bool fallback = mIdleState != CharState_Idle && !mAnimation->hasAnimation(idleGroup);
        if (fallback)
        {
            priority = getIdlePriority(CharState_Idle);
            idleGroup = idleStateToAnimGroup(CharState_Idle);
        }

        if (fallback || mIdleState == CharState_Idle || mIdleState == CharState_SpecialIdle)
        {
            std::string_view weapShortGroup = getWeaponShortGroup(mWeaponType);
            if (!weapShortGroup.empty())
            {
                std::string weapIdleGroup = idleGroup;
                weapIdleGroup += weapShortGroup;
                if (!mAnimation->hasAnimation(weapIdleGroup))
                    weapIdleGroup = fallbackShortWeaponGroup(idleGroup);
                idleGroup = std::move(weapIdleGroup);

                // play until the Loop Stop key 2 to 5 times, then play until the Stop key
                // this replicates original engine behavior for the "Idle1h" 1st-person animation
                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                numLoops = 1 + Misc::Rng::rollDice(4, prng);
            }
        }

        if (!mAnimation->hasAnimation(idleGroup))
        {
            resetCurrentIdleState();
            return;
        }

        float startPoint = 0.f;
        // There is no need to restart anim if the new and old anims are the same.
        // Just update the number of loops.
        if (mCurrentIdle == idleGroup)
            mAnimation->getInfo(mCurrentIdle, &startPoint);

        clearStateAnimation(mCurrentIdle);
        mCurrentIdle = std::move(idleGroup);
        playBlendedAnimation(mCurrentIdle, priority, MWAnim::BlendMask_All, false, 1.0f, "start", "stop", startPoint,
            static_cast<uint32_t>(numLoops), true);
    }

    void CharacterController::refreshCurrentAnims(
        CharacterState idle, CharacterState movementState, JumpingState jump, bool force)
    {
        // If the current animation is scripted, do not touch it
        if (isScriptedAnimPlaying())
            return;

        refreshHitRecoilAnims();
        refreshJumpAnims(jump, force);
        refreshMovementAnims(movementState, force);

        // idle handled last as it can depend on the other states
        refreshIdleAnims(idle, force);
    }

    void CharacterController::playDeath(float startpoint, CharacterState death)
    {
        mDeathState = death;
        mCurrentDeath = deathStateToAnimGroup(mDeathState);
        mPtr.getClass().getCreatureStats(mPtr).setDeathAnimation(
            static_cast<signed char>(mDeathState - CharState_Death1));

        // For dead actors, refreshCurrentAnims is no longer called, so we need to disable the movement state manually.
        // Note that these animations wouldn't actually be visible (due to the Death animation's priority being higher).
        // However, they could still trigger text keys, such as Hit events, or sounds.
        resetCurrentMovementState();
        resetCurrentWeaponState();
        resetCurrentHitState();
        resetCurrentIdleState();
        resetCurrentJumpState();

        playBlendedAnimation(
            mCurrentDeath, Priority_Death, MWAnim::BlendMask_All, false, 1.0f, "start", "stop", startpoint, 0);
    }

    CharacterState CharacterController::chooseRandomDeathState() const
    {
        int selected = 0;
        chooseRandomGroup("death", &selected);
        return static_cast<CharacterState>(CharState_Death1 + (selected - 1));
    }

    void CharacterController::playRandomDeath(float startpoint)
    {
        if (mPtr == getPlayer())
        {
            // The first-person animations do not include death, so we need to
            // force-switch to third person before playing the death animation.
            MWBase::Environment::get().getWorld()->useDeathCamera();
        }

        mDeathState = hitStateToDeathState(mHitState);
        if (mDeathState == CharState_None && MWBase::Environment::get().getWorld()->isSwimming(mPtr))
            mDeathState = CharState_SwimDeath;

        if (mDeathState == CharState_None
            || (mAnimation && !mAnimation->hasAnimation(deathStateToAnimGroup(mDeathState))))
            mDeathState = chooseRandomDeathState();

        // Do not interrupt scripted animation by death
        if (!mAnimation || isScriptedAnimPlaying())
            return;

        playDeath(startpoint, mDeathState);
    }

    std::string CharacterController::chooseRandomAttackAnimation() const
    {
        std::string result;
        bool isSwimming = MWBase::Environment::get().getWorld()->isSwimming(mPtr);

        if (isSwimming)
            result = chooseRandomGroup("swimattack");

        if (!isSwimming || !mAnimation->hasAnimation(result))
            result = chooseRandomGroup("attack");

        return result;
    }

    CharacterController::CharacterController(const MWWorld::Ptr& ptr, MWAnim::Object* anim)
        : mPtr(ptr)
        , mWeapon(MWWorld::Ptr())
        , mAnimation(anim->animation.get())
        , mObject(anim)
        , mActor(dynamic_cast<MWAnim::Actor*>(anim))
        , mWielding(dynamic_cast<MWAnim::Wielding*>(mActor))
        , mNpc(dynamic_cast<MWRender::Npc*>(mWielding))
        , mPlayer(dynamic_cast<MWRender::Player*>(mNpc))
    {
        const MWWorld::Class& cls = mPtr.getClass();
        if (mActor)
        {
            /* Accumulate along X/Y only for now, until we can figure out how we should
             * handle knockout and death which moves the character down. */
            mAnimation->setAccumulation({ 1.0f, 1.0f, 0.0f });

            createPoseControllers();

            MWRender::addItemLightsAndListener(*mObject, cls.getContainerStore(mPtr));

            if (cls.hasInventoryStore(mPtr))
            {
                getActiveWeapon(mPtr, &mWeaponType);
                if (mWeaponType != ESM::Weapon::None)
                {
                    mUpperBodyState = UpperBodyState::WeaponEquipped;
                    mCurrentWeapon = getWeaponAnimation(mWeaponType);
                }

                if (mWeaponType != ESM::Weapon::None && mWeaponType != ESM::Weapon::Spell
                    && mWeaponType != ESM::Weapon::HandToHand)
                    showWeapons(true);

                showCarriedLeft(updateCarriedLeftVisible(mWeaponType));
            }

            if (!cls.getCreatureStats(mPtr).isDead())
            {
                mIdleState = CharState_Idle;
                if (cls.getCreatureStats(mPtr).getFallHeight() > 0)
                    mJumpState = JumpState_InAir;
            }
            else
            {
                const MWMechanics::CreatureStats& cStats = mPtr.getClass().getCreatureStats(mPtr);
                if (cStats.isDeathAnimationFinished())
                {
                    // Set the death state, but don't play it yet
                    // We will play it in the first frame, but only if no script set the skipAnim flag
                    signed char deathanim = cStats.getDeathAnimation();
                    if (deathanim == -1)
                        mDeathState = chooseRandomDeathState();
                    else
                        mDeathState = static_cast<CharacterState>(CharState_Death1 + deathanim);

                    mFloatToSurface = cStats.getHealth().getBase() != 0;
                }
                // else: nothing to do, will detect death in the next frame and start playing death animation
            }
        }
        else
        {
            /* Don't accumulate with non-actors. */
            mAnimation->setAccumulation({ 0.f, 0.f, 0.f });

            mIdleState = CharState_Idle;
        }

        if (mWielding)
            mPtr.getClass().getInventoryStore(mPtr).setInvListener(new UpdateEquipment(mWielding));

        // Do not update animation status for dead actors
        if (mDeathState == CharState_None && (!cls.isActor() || !cls.getCreatureStats(mPtr).isDead()))
            refreshCurrentAnims(mIdleState, mMovementState, mJumpState, true);

        mAnimation->runAnimation(0.f);
        unpersistAnimationState();
    }

    CharacterController::~CharacterController()
    {
        detachAnimation();
    }

    void CharacterController::detachAnimation()
    {
        if (!mAnimation)
            return;

        mIkController.reset();
        mRagdollDriver.reset();
        if (mPoseDriver)
        {
            mPoseDriver.reset();
            mAnimation->updateBones = true;
        }
        persistAnimationState();

        // OpenEden: loading a game while one is running clears the world first and the mechanics after, so the actor's
        // custom data (its inventory) may be gone by now. Asking for the store would make a new one, which fills itself
        // from the ESM data and throws (Class::getServices) inside this destructor: the game aborted on quickload. An
        // actor with no custom data has no store and so no listener to remove.
        const bool hasStore = mPtr.getRefData().getCustomData() != nullptr;
        if (mActor && hasStore)
            MWRender::removeListener(mPtr.getClass().getContainerStore(mPtr));
        if (mWielding && hasStore)
        {
            delete mPtr.getClass().getInventoryStore(mPtr).getInvListener();
            mPtr.getClass().getInventoryStore(mPtr).setInvListener(nullptr);
        }

        mAnimation = nullptr;
        mObject = nullptr;
        mActor = nullptr;
        mWielding = nullptr;
        mNpc = nullptr;
        mPlayer = nullptr;
    }

    void CharacterController::handleTextKeys()
    {
        if (!mAnimation)
            return;
        for (auto& evt : mAnimation->encounteredTags)
        {
            try
            {
                handleTextKey(evt.groupname, evt.key, evt.map);
            }
            catch (std::exception& e)
            {
                std::cerr << "Error handling text key: " << e.what() << std::endl;
            }
        }
        mAnimation->encounteredTags.clear();

        // Animations end on update threads too; report them to Lua here, on the main thread, after their text keys.
        for (const MWAnim::Play::EndedEvent& ended : mAnimation->endedAnimations)
            MWBase::Environment::get().getLuaManager()->animationEnded(
                mPtr, ended.groupname, ended.time, ended.completion, ended.startKey, ended.stopKey);
        mAnimation->endedAnimations.clear();
    }

    void CharacterController::handleTextKey(
        std::string_view groupname, Anim::Tags::ConstIterator key, const Anim::Tags& map)
    {
        if (!mAnimation)
            return;
        std::string_view evt = key->second;

        MWBase::Environment::get().getLuaManager()->animationTextKey(mPtr, key->second);

        if (evt.substr(0, 7) == "sound: ")
        {
            MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
            sndMgr->playSound3D(mPtr, ESM::RefId::stringRefId(evt.substr(7)), 1.0f, 1.0f);
            return;
        }

        auto& charClass = mPtr.getClass();
        if (evt.substr(0, 10) == "soundgen: ")
        {
            std::string_view soundgen = evt.substr(10);

            // The event can optionally contain volume and pitch modifiers
            float volume = 1.0f;
            float pitch = 1.0f;

            if (soundgen.find(' ') != std::string::npos)
            {
                std::vector<std::string_view> tokens;
                Misc::StringUtils::split(soundgen, tokens);
                soundgen = tokens[0];

                if (tokens.size() >= 2)
                {
                    volume = Misc::StringUtils::toNumeric<float>(tokens[1], volume);
                }

                if (tokens.size() >= 3)
                {
                    pitch = Misc::StringUtils::toNumeric<float>(tokens[2], pitch);
                }
            }

            const ESM::RefId sound = charClass.getSoundIdFromSndGen(mPtr, soundgen);
            if (!sound.empty())
            {
                MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
                if (soundgen == "left" || soundgen == "right")
                {
                    sndMgr->playSound3D(
                        mPtr, sound, volume, pitch, MWSound::Type::Foot, MWSound::PlayMode::NoPlayerLocal);
                }
                else
                {
                    sndMgr->playSound3D(mPtr, sound, volume, pitch);
                }
            }
            return;
        }

        if (evt.substr(0, groupname.size()) != groupname || evt.substr(groupname.size(), 2) != ": ")
        {
            // Not ours, skip it
            return;
        }

        std::string_view action = evt.substr(groupname.size() + 2);
        if (action == "equip attach")
        {
            if (groupname == "shield")
                showCarriedLeft(true);
            else if (mUpperBodyState == UpperBodyState::Equipping)
                showWeapons(true);
        }
        else if (action == "unequip detach")
        {
            if (groupname == "shield")
                showCarriedLeft(false);
            else if (mUpperBodyState == UpperBodyState::Unequipping)
                showWeapons(false);
        }
        else if (action == "chop hit" || action == "slash hit" || action == "thrust hit" || action == "hit")
        {
            int attackType = -1;
            if (action == "hit")
            {
                if (groupname == "attack1" || groupname == "swimattack1")
                    attackType = ESM::Weapon::AT_Chop;
                else if (groupname == "attack2" || groupname == "swimattack2")
                    attackType = ESM::Weapon::AT_Slash;
                else if (groupname == "attack3" || groupname == "swimattack3")
                    attackType = ESM::Weapon::AT_Thrust;
            }
            else if (action == "chop hit")
                attackType = ESM::Weapon::AT_Chop;
            else if (action == "slash hit")
                attackType = ESM::Weapon::AT_Slash;
            else if (action == "thrust hit")
                attackType = ESM::Weapon::AT_Thrust;
            // We want to avoid hit keys that come out of nowhere (e.g. in the follow animation)
            // and processing multiple hit keys for a single attack
            if (mReadyToHit)
            {
                charClass.hit(mPtr, mAttackStrength, attackType, mAttackVictim, mAttackHitPos, mAttackSuccess);
                mReadyToHit = false;
            }
        }
        else if (isRandomAttackAnimation(groupname) && action == "start")
        {
            std::multimap<float, std::string>::const_iterator hitKey = key;

            // Not all animations have a hit key defined. If there is none, the hit happens with the start key.
            bool hasHitKey = false;
            while (hitKey != map.end())
            {
                if (hitKey->second.starts_with(groupname))
                {
                    std::string_view suffix = std::string_view(hitKey->second).substr(groupname.size());
                    if (suffix == ": hit")
                    {
                        hasHitKey = true;
                        break;
                    }
                    if (suffix == ": stop")
                        break;
                }
                ++hitKey;
            }
            if (!hasHitKey)
            {
                // State update doesn't expect the start key to be the hit key,
                // so we have to do this early.
                prepareHit();

                if (groupname == "attack1" || groupname == "swimattack1")
                    charClass.hit(
                        mPtr, mAttackStrength, ESM::Weapon::AT_Chop, mAttackVictim, mAttackHitPos, mAttackSuccess);
                else if (groupname == "attack2" || groupname == "swimattack2")
                    charClass.hit(
                        mPtr, mAttackStrength, ESM::Weapon::AT_Slash, mAttackVictim, mAttackHitPos, mAttackSuccess);
                else if (groupname == "attack3" || groupname == "swimattack3")
                    charClass.hit(
                        mPtr, mAttackStrength, ESM::Weapon::AT_Thrust, mAttackVictim, mAttackHitPos, mAttackSuccess);
            }
        }
        else if (action == "shoot attach")
            showAmmo(true);
        else if (action == "shoot release")
        {
            // See notes for melee release above
            if (mReadyToHit)
            {
                releaseShot();
                mReadyToHit = false;
            }
        }
        else if (action == "shoot follow attach")
            showAmmo(true);
        // Make sure this key is actually for the RangeType we are casting. The flame atronach has
        // the same animation for all range types, so there are 3 "release" keys on the same time, one for each range
        // type.
        else if (groupname == "spellcast" && action == mAttackType + " release")
        {
            if (mCanCast)
                MWBase::Environment::get().getWorld()->castSpell(mPtr, mCastingScriptedSpell);
            mCastingScriptedSpell = false;
            mCanCast = false;
        }
        else if (groupname == "containeropen" && action == "loot")
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Container, mPtr);
    }

    void CharacterController::updatePtr(const MWWorld::Ptr& ptr)
    {
        mPtr = ptr;
        if (mIkController)
            mIkController->updatePtr(ptr);
    }

    void CharacterController::updateIdleStormState(bool inwater) const
    {
        if (!mAnimation->hasAnimation("idlestorm"))
            return;

        bool animPlaying = mAnimation->isPlaying("idlestorm");
        if (mUpperBodyState != UpperBodyState::None || inwater)
        {
            if (animPlaying)
                mAnimation->disable("idlestorm");
            return;
        }

        const auto world = MWBase::Environment::get().getWorld();
        if (world->isInStorm())
        {
            vsg::vec3 stormDirection = world->getStormDirection();
            Misc::VsgMath::normalizeInPlace(stormDirection);
            auto characterDirection = MWAnim::forward(mPtr.getRefData().getPosition());
            if (vsg::dot(stormDirection, characterDirection) < -0.5f)
            {
                if (!animPlaying)
                {
                    int mask = MWAnim::BlendMask_Torso | MWAnim::BlendMask_RightArm;
                    playBlendedAnimation("idlestorm", Priority_Storm, mask, true, 1.0f, "start", "stop", 0.0f,
                        std::numeric_limits<uint32_t>::max(), true);
                }
                else
                {
                    mAnimation->setLoopingEnabled("idlestorm", true);
                }
                return;
            }
        }

        if (animPlaying)
        {
            mAnimation->setLoopingEnabled("idlestorm", false);
        }
    }

    bool CharacterController::useShieldAnimations() const
    {
        return mWielding && mWielding->canAnimateShield() && mAnimation->getTextKeyTime("shield: equip attach") >= 0
            && mAnimation->getTextKeyTime("shield: unequip detach") >= 0;
    }

    bool CharacterController::updateCarriedLeftVisible(const int weaptype) const
    {
        if (mWielding && mWielding->hasSheathedShield())
            return false;

        // Shields/torches shouldn't be visible during any operation involving two hands
        // There seems to be no text keys for this purpose, except maybe for "[un]equip start/stop",
        // but they are also present in weapon drawing animation.
        return !(getWeaponType(weaptype)->mFlags & ESM::WeaponType::TwoHanded);
    }

    float CharacterController::calculateWindUp() const
    {
        if (!mAnimation || mCurrentWeapon.empty() || mWeaponType == ESM::Weapon::PickProbe
            || isRandomAttackAnimation(mCurrentWeapon))
            return -1.f;

        float minAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " min attack");
        float maxAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " max attack");
        if (minAttackTime == -1.f || minAttackTime >= maxAttackTime)
            return -1.f;

        return std::clamp(
            (mAnimation->getCurrentTime(mCurrentWeapon) - minAttackTime) / (maxAttackTime - minAttackTime), 0.f, 1.f);
    }

    void CharacterController::prepareHit()
    {
        if (mReadyToHit)
            return;

        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        mAttackStrength = calculateWindUp();
        if (mAttackStrength == -1.f)
            mAttackStrength = std::min(1.f, 0.1f + Misc::Rng::rollClosedProbability(prng));
        ESM::WeaponType::Class weapclass = getWeaponType(mWeaponType)->mWeaponClass;
        if (weapclass != ESM::WeaponType::Ranged)
        {
            if (weapclass != ESM::WeaponType::Thrown)
            {
                mAttackSuccess = mPtr.getClass().evaluateHit(mPtr, mAttackVictim, mAttackHitPos);
                if (!mAttackSuccess)
                    mAttackStrength = 0.f;
            }
            playSwishSound();
        }

        mReadyToHit = true;
    }

    bool CharacterController::updateWeaponState()
    {
        // If the current animation is scripted, we can't do anything here.
        if (isScriptedAnimPlaying())
            return false;

        const auto world = MWBase::Environment::get().getWorld();
        auto& prng = world->getPrng();
        MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();

        const MWWorld::Class& cls = mPtr.getClass();
        CreatureStats& stats = cls.getCreatureStats(mPtr);
        int weaptype = ESM::Weapon::None;
        if (stats.getDrawState() == DrawState::Weapon)
            weaptype = ESM::Weapon::HandToHand;
        else if (stats.getDrawState() == DrawState::Spell)
            weaptype = ESM::Weapon::Spell;

        const bool isWerewolf = cls.isNpc() && cls.getNpcStats(mPtr).isWerewolf();

        const ESM::RefId* downSoundId = nullptr;
        bool weaponChanged = false;
        bool ammunition = true;
        float weapSpeed = 1.f;
        if (cls.hasInventoryStore(mPtr))
        {
            MWWorld::InventoryStore& inv = cls.getInventoryStore(mPtr);
            MWWorld::ContainerStoreIterator weapon = getActiveWeapon(mPtr, &weaptype);
            if (stats.getDrawState() == DrawState::Spell)
                weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);

            MWWorld::Ptr newWeapon;
            if (weapon != inv.end())
            {
                newWeapon = *weapon;
                if (isRealWeapon(mWeaponType))
                    downSoundId = &newWeapon.getClass().getDownSoundId(newWeapon);
            }
            // weapon->HtH switch: weapon is empty already, so we need to take sound from previous weapon
            else if (!mWeapon.isEmpty() && weaptype == ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell)
                downSoundId = &mWeapon.getClass().getDownSoundId(mWeapon);

            if (mWeapon != newWeapon)
            {
                mWeapon = newWeapon;
                weaponChanged = true;
            }

            if (stats.getDrawState() == DrawState::Weapon && !mWeapon.isEmpty()
                && mWeapon.getType() == ESM::Weapon::sRecordId)
            {
                weapSpeed = mWeapon.get<ESM::Weapon>()->mBase->mData.mSpeed;
                MWWorld::ConstContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
                int ammotype = getWeaponType(mWeapon.get<ESM::Weapon>()->mBase->mData.mType)->mAmmoType;
                if (ammotype != ESM::Weapon::None)
                    ammunition = ammo != inv.end() && ammo->get<ESM::Weapon>()->mBase->mData.mType == ammotype;
                // Cancel attack if we no longer have ammunition
                if (!ammunition)
                    setAttackingOrSpell(false);
            }

            MWWorld::ConstContainerStoreIterator torch = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            if (torch != inv.end() && torch->getType() == ESM::Light::sRecordId
                && updateCarriedLeftVisible(mWeaponType))
            {
                if (mAnimation->isPlaying("shield"))
                    mAnimation->disable("shield");

                playBlendedAnimation("torch", Priority_Torch, MWAnim::BlendMask_LeftArm, false, 1.0f, "start", "stop",
                    0.0f, std::numeric_limits<uint32_t>::max(), true);
            }
            else if (mAnimation->isPlaying("torch"))
            {
                mAnimation->disable("torch");
            }
        }

        MWAnim::Priority priorityWeapon(Priority_Default);
        if (cls.isBipedal(mPtr))
        {
            // For bipeds, blend weapon animations with lower body animations with higher priority
            // For non-bipeds, movement takes priority
            priorityWeapon = Priority_Weapon;
            priorityWeapon[MWAnim::BoneGroup_LowerBody] = Priority_WeaponLowerBody;
        }

        bool forcestateupdate = false;

        // We should not play equipping animation and sound during weapon->weapon transition
        const bool isStillWeapon = isRealWeapon(mWeaponType) && isRealWeapon(weaptype);

        // If the current weapon type was changed in the middle of attack (e.g. by Equip console command or when bound
        // spell expires), we should force actor to the "weapon equipped" state, interrupt attack and update animations.
        // Morrowind does this at the end of the attack (see #4646 and PR 1972).
        // If we decide to cope with the resulting problems, the thrown weapon->H2H case below should be extended.
        if (isStillWeapon && mWeaponType != weaptype && mUpperBodyState > UpperBodyState::WeaponEquipped)
        {
            forcestateupdate = true;
            if (!mCurrentWeapon.empty())
                mAnimation->disable(mCurrentWeapon);
            mUpperBodyState = UpperBodyState::WeaponEquipped;
            setAttackingOrSpell(false);
            showWeapons(true);
        }

        // openeden-vsg: vsgopenmw's renderer does not attach a bolt when a crossbow is equipped, so show it here.
        if (ammunition && weaptype == ESM::Weapon::MarksmanCrossbow && mWeaponType != ESM::Weapon::MarksmanCrossbow
            && mUpperBodyState >= UpperBodyState::Equipping)
            showAmmo(true);

        if (!isKnockedOut() && !isKnockedDown() && !isRecovery())
        {
            std::string weapgroup;
            if (((!isWerewolf && cls.isBipedal(mPtr)) || mWeaponType != ESM::Weapon::Spell) && weaptype != mWeaponType
                && mUpperBodyState <= UpperBodyState::AttackWindUp && mUpperBodyState != UpperBodyState::Unequipping
                && !isStillWeapon)
            {
                // We can not play un-equip animation if weapon changed since last update
                if (!weaponChanged)
                {
                    // Note: we do not disable the weapon unequipping animation automatically to avoid body desync
                    weapgroup = getWeaponAnimation(mWeaponType);
                    int unequipMask = MWAnim::BlendMask_All;
                    mUpperBodyState = UpperBodyState::Unequipping;
                    const bool useShieldAnims = useShieldAnimations();
                    if (useShieldAnims && mWeaponType != ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell
                        && !(mWeaponType == ESM::Weapon::None && weaptype == ESM::Weapon::Spell))
                    {
                        unequipMask = unequipMask | ~MWAnim::BlendMask_LeftArm;
                        mAnimation->disable("shield");
                        playBlendedAnimation("shield", Priority_Block, MWAnim::BlendMask_LeftArm, true, 1.0f,
                            "unequip start", "unequip stop", 0.0f, 0);
                    }
                    else if (mWeaponType == ESM::Weapon::HandToHand)
                        showCarriedLeft(false);

                    mAnimation->disable(weapgroup);
                    playBlendedAnimation(
                        weapgroup, priorityWeapon, unequipMask, false, 1.0f, "unequip start", "unequip stop", 0.0f, 0);

                    showAmmo(false);

                    // If we do not have the "unequip detach" key, hide weapon manually.
                    if (mAnimation->getTextKeyTime(weapgroup + ": unequip detach") < 0)
                        showWeapons(false);
                }

                if (downSoundId && !downSoundId->empty())
                {
                    sndMgr->playSound3D(mPtr, *downSoundId, 1.0f, 1.0f);
                }
            }

            float complete;
            bool animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);
            if (!animPlaying || complete >= 1.0f)
            {
                // Weapon is changed, no current animation (e.g. unequipping or attack).
                // Start equipping animation now.
                if (weaptype != mWeaponType && mUpperBodyState <= UpperBodyState::WeaponEquipped)
                {
                    forcestateupdate = true;
                    const bool useShieldAnims = useShieldAnimations();
                    if (!useShieldAnims)
                        showCarriedLeft(updateCarriedLeftVisible(weaptype));

                    weapgroup = getWeaponAnimation(weaptype);

                    if (!isStillWeapon)
                    {
                        if (animPlaying)
                            mAnimation->disable(mCurrentWeapon);
                        if (weaptype != ESM::Weapon::None)
                        {
                            showWeapons(false);
                            int equipMask = MWAnim::BlendMask_All;
                            mUpperBodyState = UpperBodyState::Equipping;
                            mResetIdleOnAttackEnd = true;
                            if (useShieldAnims && weaptype != ESM::Weapon::Spell)
                            {
                                equipMask = equipMask | ~MWAnim::BlendMask_LeftArm;
                                mAnimation->disable("shield");
                                playBlendedAnimation("shield", Priority_Block, MWAnim::BlendMask_LeftArm, true, 1.0f,
                                    "equip start", "equip stop", 0.0f, 0);
                            }

                            if (weaptype != ESM::Weapon::Spell || cls.isBipedal(mPtr))
                            {
                                playBlendedAnimation(weapgroup, priorityWeapon, equipMask, false, 1.0f, "equip start",
                                    "equip stop", 0.0f, 0);
                            }

                            // If we do not have the "equip attach" key, show weapon manually.
                            if (weaptype != ESM::Weapon::Spell
                                && mAnimation->getTextKeyTime(weapgroup + ": equip attach") < 0)
                            {
                                showWeapons(true);
                            }

                            if (!mWeapon.isEmpty() && mWeaponType != ESM::Weapon::HandToHand && isRealWeapon(weaptype))
                            {
                                const ESM::RefId& upSoundId = mWeapon.getClass().getUpSoundId(mWeapon);
                                if (!upSoundId.empty())
                                    sndMgr->playSound3D(mPtr, upSoundId, 1.0f, 1.0f);
                            }
                        }
                    }

                    if (isWerewolf)
                    {
                        const MWWorld::ESMStore& store = world->getStore();
                        const ESM::Sound* sound = store.get<ESM::Sound>().searchRandom("WolfEquip", prng);
                        if (sound)
                        {
                            sndMgr->playSound3D(mPtr, sound->mId, 1.0f, 1.0f);
                        }
                    }

                    mWeaponType = weaptype;
                    mCurrentWeapon = weapgroup;
                }

                // Make sure that we disabled unequipping animation
                if (mUpperBodyState == UpperBodyState::Unequipping)
                {
                    resetCurrentWeaponState();
                    mWeaponType = ESM::Weapon::None;
                }
            }
        }

        if (isWerewolf)
        {
            const ESM::RefId wolfRun = ESM::RefId::stringRefId("WolfRun");
            if (isRunning() && !world->isSwimming(mPtr) && mWeaponType == ESM::Weapon::None)
            {
                if (!sndMgr->getSoundPlaying(mPtr, wolfRun))
                    sndMgr->playSound3D(mPtr, wolfRun, 1.0f, 1.0f, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
            }
            else
                sndMgr->stopSound3D(mPtr, wolfRun);
        }

        float complete = 0.f;
        bool animPlaying = false;
        ESM::WeaponType::Class weapclass = getWeaponType(mWeaponType)->mWeaponClass;
        if (getAttackingOrSpell())
        {
            mResetIdleOnAttackEnd = true;
            if (mUpperBodyState == UpperBodyState::WeaponEquipped
                && (mHitState == CharState_None || mHitState == CharState_Block))
            {
                mAttackStrength = -1.f;
                mReadyToHit = false;

                // Randomize attacks for non-bipedal creatures
                if (!cls.isBipedal(mPtr)
                    && (!mAnimation->hasAnimation(mCurrentWeapon) || isRandomAttackAnimation(mCurrentWeapon)))
                {
                    mCurrentWeapon = chooseRandomAttackAnimation();
                }

                if (mWeaponType == ESM::Weapon::Spell)
                {
                    // Unset casting flag, otherwise pressing the mouse button down would
                    // continue casting every frame if there is no animation
                    setAttackingOrSpell(false);
                    if (mPtr == getPlayer())
                    {
                        // For the player, set the spell we want to cast
                        // This has to be done at the start of the casting animation,
                        // *not* when selecting a spell in the GUI (otherwise you could change the spell mid-animation)
                        const ESM::RefId& selectedSpell
                            = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
                        stats.getSpells().setSelectedSpell(selectedSpell);
                    }
                    ESM::RefId spellid = stats.getSpells().getSelectedSpell();
                    bool isMagicItem = false;

                    // Play hand VFX and allow castSpell use (assuming an animation is going to be played) if
                    // spellcasting is successful. Scripted spellcasting bypasses restrictions.
                    MWWorld::SpellCastState spellCastResult = MWWorld::SpellCastState::Success;
                    if (!mCastingScriptedSpell)
                        spellCastResult = world->startSpellCast(mPtr);
                    mCanCast = spellCastResult == MWWorld::SpellCastState::Success;

                    if (spellid.empty())
                    {
                        const MWWorld::ContainerStore& inv = cls.getContainerStore(mPtr);
                        if (inv.getSelectedEnchantItem() != inv.end())
                        {
                            const MWWorld::ConstPtr& enchantItem = *inv.getSelectedEnchantItem();
                            spellid = enchantItem.getClass().getEnchantment(enchantItem);
                            isMagicItem = true;
                        }
                    }

                    if (isMagicItem && !Settings::game().mUseMagicItemAnimations)
                    {
                        world->breakInvisibility(mPtr);
                        // Enchanted items by default do not use casting animations
                        world->castSpell(mPtr);
                        // Spellcasting animation needs to "play" for at least one frame to reset the aiming factor
                        animPlaying = true;
                        mUpperBodyState = UpperBodyState::Casting;
                        // But idle should not be reset
                        mResetIdleOnAttackEnd = false;
                    }
                    // Play the spellcasting animation/VFX if the spellcasting was successful or failed due to
                    // insufficient magicka. Used up powers are exempt from this from some reason.
                    else if (!spellid.empty() && spellCastResult != MWWorld::SpellCastState::PowerAlreadyUsed)
                    {
                        world->breakInvisibility(mPtr);
                        MWMechanics::CastSpell cast(mPtr, {}, false, mCastingScriptedSpell);

                        const std::vector<ESM::IndexedENAMstruct>* effects{ nullptr };
                        const MWWorld::ESMStore& store = world->getStore();
                        if (isMagicItem)
                        {
                            const ESM::Enchantment* enchantment = store.get<ESM::Enchantment>().find(spellid);
                            effects = &enchantment->mEffects.mList;
                            cast.playSpellCastingEffects(enchantment);
                        }
                        else
                        {
                            const ESM::Spell* spell = store.get<ESM::Spell>().find(spellid);
                            effects = &spell->mEffects.mList;
                            cast.playSpellCastingEffects(spell);
                        }
                        if (!effects->empty())
                        {
                            if (mCanCast)
                            {
                                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().find(
                                    effects->back().mData.mEffectID); // use last effect of list for color of VFX_Hands

                                // A hand without a bone gets no effect, as in OpenMW 0.51.
                                const ESM::RefId castStatic = ESM::RefId::stringRefId("VFX_Hands");
                                MWRender::addEffect(mPtr, castStatic, {}, false, "Bip01 L Hand", effect->mParticle);
                                MWRender::addEffect(mPtr, castStatic, {}, false, "Bip01 R Hand", effect->mParticle);
                            }
                            // first effect used for casting animation
                            const ESM::ENAMstruct& firstEffect = effects->front().mData;

                            std::string startKey;
                            std::string stopKey;
                            if (isRandomAttackAnimation(mCurrentWeapon))
                            {
                                startKey = "start";
                                stopKey = "stop";
                                if (mCanCast)
                                    world->castSpell(mPtr,
                                        mCastingScriptedSpell); // No "release" text key to use, so cast immediately
                                mCastingScriptedSpell = false;
                                mCanCast = false;
                            }
                            else
                            {
                                switch (firstEffect.mRange)
                                {
                                    case 0:
                                        mAttackType = "self";
                                        break;
                                    case 1:
                                        mAttackType = "touch";
                                        break;
                                    case 2:
                                        mAttackType = "target";
                                        break;
                                }

                                startKey = mAttackType + " start";
                                stopKey = mAttackType + " stop";
                            }
                            playBlendedAnimation(mCurrentWeapon, priorityWeapon, MWAnim::BlendMask_All, false, 1,
                                startKey, stopKey, 0.0f, 0);
                            mUpperBodyState = UpperBodyState::Casting;
                        }
                    }
                }
                else
                {
                    std::string startKey = "start";
                    std::string stopKey = "stop";

                    MWBase::LuaManager::ActorControls* actorControls
                        = MWBase::Environment::get().getLuaManager()->getActorControls(mPtr);
                    const bool aiInactive
                        = actorControls->mDisableAI || !MWBase::Environment::get().getMechanicsManager()->isAIActive();
                    if (mWeaponType != ESM::Weapon::PickProbe && !isRandomAttackAnimation(mCurrentWeapon))
                    {
                        if (weapclass == ESM::WeaponType::Ranged || weapclass == ESM::WeaponType::Thrown)
                            mAttackType = "shoot";
                        else if (mPtr == getPlayer())
                        {
                            if (Settings::game().mBestAttack)
                            {
                                if (!mWeapon.isEmpty() && mWeapon.getType() == ESM::Weapon::sRecordId)
                                {
                                    mAttackType = getBestAttack(mWeapon.get<ESM::Weapon>()->mBase);
                                }
                                else
                                {
                                    // There is no "best attack" for Hand-to-Hand
                                    mAttackType = getRandomAttackType();
                                }
                            }
                            else
                            {
                                mAttackType = getMovementBasedAttackType();
                            }
                        }
                        else if (aiInactive)
                        {
                            mAttackType = getDesiredAttackType();
                            if (mAttackType == "")
                                mAttackType = getRandomAttackType();
                        }

                        // else if (mPtr != getPlayer()) use mAttackType set by AiCombat
                        startKey = mAttackType + ' ' + startKey;
                        stopKey = mAttackType + " max attack";
                    }

                    mUpperBodyState = UpperBodyState::AttackWindUp;

                    // Reset the attack results when the attack starts.
                    // Strictly speaking this should probably be done when the attack ends,
                    // but the attack animation might be cancelled in a myriad different ways.
                    mAttackSuccess = false;
                    mAttackVictim = MWWorld::Ptr();
                    mAttackHitPos = vsg::vec3();

                    playBlendedAnimation(mCurrentWeapon, priorityWeapon, MWAnim::BlendMask_All, false, weapSpeed,
                        startKey, stopKey, 0.0f, 0);
                }
            }
        }

        // Random attack and pick/probe animations never have wind up and are played to their end.
        // Other animations must be released when the attack state is unset.
        if (mUpperBodyState == UpperBodyState::AttackWindUp
            && (mWeaponType == ESM::Weapon::PickProbe || isRandomAttackAnimation(mCurrentWeapon)
                || !getAttackingOrSpell()))
        {
            mUpperBodyState = UpperBodyState::AttackRelease;
            world->breakInvisibility(mPtr);
            if (mWeaponType == ESM::Weapon::PickProbe)
            {
                // TODO: this will only work for the player, and needs to be fixed if NPCs should ever use
                // lockpicks/probes.
                MWWorld::Ptr target = world->getFocusObject();

                if (!target.isEmpty())
                {
                    std::string_view resultMessage, resultSound;
                    if (mWeapon.getType() == ESM::Lockpick::sRecordId)
                        Security(mPtr).pickLock(target, mWeapon, resultMessage, resultSound);
                    else if (mWeapon.getType() == ESM::Probe::sRecordId)
                        Security(mPtr).probeTrap(target, mWeapon, resultMessage, resultSound);
                    if (!resultMessage.empty())
                        MWBase::Environment::get().getWindowManager()->messageBox(resultMessage);
                    if (!resultSound.empty())
                        sndMgr->playSound3D(target, ESM::RefId::stringRefId(resultSound), 1.0f, 1.0f);
                }
            }
            // Evaluate the attack results and play the swish sound.
            // Attack animations with no hit key do this earlier.
            else
            {
                prepareHit();
            }

            if (mWeaponType == ESM::Weapon::PickProbe || isRandomAttackAnimation(mCurrentWeapon))
                mUpperBodyState = UpperBodyState::AttackEnd;
        }

        if (mUpperBodyState == UpperBodyState::AttackRelease)
        {
            // The release state might have been reached before reaching the wind-up section. We'll play the new section
            // only when the wind-up section is reached.
            float currentTime = mAnimation->getCurrentTime(mCurrentWeapon);
            float minAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " min attack");
            float maxAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " max attack");
            if (minAttackTime <= currentTime && currentTime <= maxAttackTime)
            {
                std::string hit = mAttackType != "shoot" ? "hit" : "release";

                float startPoint = 0.f;

                // Skip a bit of the pre-hit section based on the attack strength
                if (minAttackTime != -1.f && minAttackTime < maxAttackTime)
                {
                    startPoint = 1.f - mAttackStrength;
                    float minHitTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " min hit");
                    float hitTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + ' ' + hit);
                    if (maxAttackTime <= minHitTime && minHitTime < hitTime)
                        startPoint *= (minHitTime - maxAttackTime) / (hitTime - maxAttackTime);
                }

                mAnimation->disable(mCurrentWeapon);
                playBlendedAnimation(mCurrentWeapon, priorityWeapon, MWAnim::BlendMask_All, false, weapSpeed,
                    mAttackType + " max attack", mAttackType + ' ' + hit, startPoint, 0);
            }

            animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);

            // Try playing the "follow" section if the attack animation ended naturally or didn't play at all.
            if (!animPlaying || (currentTime >= maxAttackTime && complete >= 1.f))
            {
                std::string start = "follow start";
                std::string stop = "follow stop";

                if (mAttackType != "shoot")
                {
                    std::string strength = mAttackStrength < 0.33f ? "small"
                        : mAttackStrength < 0.66f                  ? "medium"
                                                                   : "large";
                    start = strength + ' ' + start;
                    stop = strength + ' ' + stop;
                }

                mReadyToHit = false;

                if (animPlaying)
                    mAnimation->disable(mCurrentWeapon);
                playBlendedAnimation(mCurrentWeapon, priorityWeapon, MWAnim::BlendMask_All, false, weapSpeed,
                    mAttackType + ' ' + start, mAttackType + ' ' + stop, 0.0f, 0);
                mUpperBodyState = UpperBodyState::AttackEnd;

                animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);
            }
        }

        if (!animPlaying)
            animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);

        if (!animPlaying || complete >= 1.f)
        {
            if (mUpperBodyState == UpperBodyState::Equipping || mUpperBodyState == UpperBodyState::AttackEnd
                || mUpperBodyState == UpperBodyState::Casting)
            {
                if (ammunition && mWeaponType == ESM::Weapon::MarksmanCrossbow)
                    showAmmo(true);

                // Cancel stagger animation at the end of an attack to avoid abrupt transitions
                // in favor of a different abrupt transition, like Morrowind
                if (mUpperBodyState != UpperBodyState::Equipping && isRecovery())
                    mAnimation->disable(mCurrentHit);

                if (animPlaying)
                    mAnimation->disable(mCurrentWeapon);

                // Skip Thrown->H2H idle transition (e.g., if we've run out of ammo)
                // In Morrowind this isn't actually specific to this transition
                // See the weapon->weapon mid-attack skip logic above
                if (mUpperBodyState == UpperBodyState::AttackEnd)
                {
                    if (weapclass == ESM::WeaponType::Thrown && weaptype == ESM::Weapon::HandToHand)
                    {
                        forcestateupdate = true;
                        mWeaponType = weaptype;
                        mCurrentWeapon = getWeaponAnimation(mWeaponType);
                        showWeapons(false);
                    }
                }
                // We should not break swim and sneak animations
                if (mResetIdleOnAttackEnd && mIdleState != CharState_IdleSneak && mIdleState != CharState_IdleSwim)
                {
                    resetCurrentIdleState();
                }

                mUpperBodyState = UpperBodyState::WeaponEquipped;
            }
            else if (mUpperBodyState == UpperBodyState::Unequipping)
            {
                if (animPlaying)
                    mAnimation->disable(mCurrentWeapon);
                mUpperBodyState = UpperBodyState::None;
            }
        }

        mActor->spinePitchFactor = 0;
        if (mUpperBodyState > UpperBodyState::WeaponEquipped
            && (weapclass == ESM::WeaponType::Ranged || weapclass == ESM::WeaponType::Thrown))
        {
            mActor->spinePitchFactor = 1;

            // A smooth transition can be provided if a pre-wind-up section is defined. Random attack animations never
            // have one.
            if (mUpperBodyState == UpperBodyState::AttackWindUp && !isRandomAttackAnimation(mCurrentWeapon))
            {
                float currentTime = mAnimation->getCurrentTime(mCurrentWeapon);
                float minAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " min attack");
                float startTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": " + mAttackType + " start");
                if (startTime <= currentTime && currentTime < minAttackTime)
                    mActor->spinePitchFactor = (currentTime - startTime) / (minAttackTime - startTime);
            }
            else if (mUpperBodyState == UpperBodyState::AttackEnd)
            {
                // technically we do not need a pitch for crossbow reload animation,
                // but we should avoid abrupt repositioning
                if (mWeaponType == ESM::Weapon::MarksmanCrossbow)
                    mActor->spinePitchFactor = std::max(0.f, 1.f - complete * 10.f);
                else
                    mActor->spinePitchFactor = 1.f - complete;
            }
        }

        if (mPlayer)
            mPlayer->accurateAiming = mUpperBodyState > UpperBodyState::WeaponEquipped;

        return forcestateupdate;
    }

    void CharacterController::updateAnimQueue()
    {
        if (mAnimQueue.empty())
            return;

        if (!mAnimation->isPlaying(mAnimQueue.front().mGroup))
        {
            // Playing animations through mwscript is weird. If an animation is
            // a looping animation (idle or other cyclical animations), then they
            // will end as expected. However, if they are non-looping animations, they
            // will stick around forever or until another animation appears in the queue.
            bool shouldPlayOrRestart = mAnimQueue.size() > 1;
            if (shouldPlayOrRestart || !mAnimQueue.front().mScripted
                || (mAnimQueue.front().mLoopCount == 0 && mAnimQueue.front().mLooping))
            {
                setPlayScriptedOnly(false);
                mAnimation->disable(mAnimQueue.front().mGroup);
                mAnimQueue.pop_front();
                shouldPlayOrRestart = true;
            }
            else
                // A non-looping animation will stick around forever, so only restart if the animation
                // actually was removed for some reason.
                shouldPlayOrRestart = !mAnimation->getInfo(mAnimQueue.front().mGroup)
                    && mAnimation->hasAnimation(mAnimQueue.front().mGroup);

            if (shouldPlayOrRestart)
            {
                // Move on to the remaining items of the queue
                playAnimQueue();
            }
        }
        else
        {
            float complete;
            uint32_t loopcount;
            mAnimation->getInfo(mAnimQueue.front().mGroup, &complete, nullptr, &loopcount);
            mAnimQueue.front().mLoopCount = loopcount;
            mAnimQueue.front().mTime = complete;
        }

        if (!mAnimQueue.empty())
            mAnimation->setLoopingEnabled(mAnimQueue.front().mGroup, mAnimQueue.size() <= 1);
    }

    void CharacterController::playAnimQueue(bool loopStart)
    {
        if (!mAnimQueue.empty())
        {
            clearStateAnimation(mCurrentIdle);
            mIdleState = CharState_SpecialIdle;
            auto priority = mAnimQueue.front().mScripted ? Priority_Scripted : Priority_Default;
            setPlayScriptedOnly(mAnimQueue.front().mScripted);
            if (mAnimQueue.front().mScripted)
                mAnimation->play(mAnimQueue.front().mGroup, priority, MWAnim::BlendMask_All, false,
                    mAnimQueue.front().mSpeed, (loopStart ? "loop start" : mAnimQueue.front().mStartKey),
                    mAnimQueue.front().mStopKey, mAnimQueue.front().mTime, mAnimQueue.front().mLoopCount,
                    mAnimQueue.front().mLooping);
            else
                playBlendedAnimation(mAnimQueue.front().mGroup, priority, MWAnim::BlendMask_All, false,
                    mAnimQueue.front().mSpeed, (loopStart ? "loop start" : mAnimQueue.front().mStartKey),
                    mAnimQueue.front().mStopKey, mAnimQueue.front().mTime, mAnimQueue.front().mLoopCount,
                    mAnimQueue.front().mLooping);
        }
    }

    void CharacterController::setPlayScriptedOnly(bool playScriptedOnly) const
    {
        // MWAnim::Play then runs only the states that carry this priority, as OpenMW 0.51 does for scripted ones.
        mAnimation->persistentPriority = playScriptedOnly ? static_cast<int>(Priority_Scripted) : -1;
    }

    void CharacterController::update(float dt)
    {
        preAnimation(dt);
        runAnimation(dt);
        postAnimation(dt);
    }

    void CharacterController::preAnimation(float duration)
    {
        if (!mAnimation)
            return;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
        const MWWorld::Class& cls = mPtr.getClass();
        movement = {};
        speed = 0.f;
        inertialActive = false;
        smoothedSpeed = 0.f;
        mTraceLocomotionPhase = 0;
        mIntentWantsToMove = false;
        mIntentSpeedMps = 0.f;
        mIntentMeasuredMps = -1.f;
        mIntentStanceChanged = false;

        updateMagicEffects();
        MWAnim::updateEffects(*mObject->node(), duration);

        bool isPlayer = mPtr == MWMechanics::getPlayer();
        bool isFirstPersonPlayer = isPlayer && MWBase::Environment::get().getWorld()->isFirstPerson();
        bool godmode = isPlayer && MWBase::Environment::get().getWorld()->getGodModeState();

        scale = mPtr.getCellRef().getScale();

        if (!Settings::game().mNormaliseRaceSpeed && cls.isNpc())
        {
            const ESM::NPC* npc = mPtr.get<ESM::NPC>()->mBase;
            const ESM::Race* race = world->getStore().get<ESM::Race>().find(npc->mRace);
            float weight = npc->isMale() ? race->mData.mMaleWeight : race->mData.mFemaleWeight;
            scale *= weight;
        }

        if (cls.isActor() && cls.getCreatureStats(mPtr).wasTeleported())
        {
            mSmoothedSpeed = vsg::vec2();
            cls.getCreatureStats(mPtr).setTeleported(false);
        }

        if (!cls.isActor())
            updateAnimQueue();
        else if (!cls.getCreatureStats(mPtr).isDead())
        {
            bool onground = world->isOnGround(mPtr);
            bool inwater = world->isSwimming(mPtr);
            bool flying = world->isFlying(mPtr);
            bool solid = world->isActorCollisionEnabled(mPtr);
            // Can't run and sneak while flying (see speed formula in Npc/Creature::getSpeed)
            bool sneak
                = cls.getCreatureStats(mPtr).getStance(MWMechanics::CreatureStats::Stance_Sneak) && !flying && !inwater;
            bool isrunning = cls.getCreatureStats(mPtr).getStance(MWMechanics::CreatureStats::Stance_Run) && !flying;
            CreatureStats& stats = cls.getCreatureStats(mPtr);
            Movement& movementSettings = cls.getMovementSettings(mPtr);

            const std::string& inertialSetting = Settings::values().mGame.mInertialLocomotion.get();
            const bool useInertial = (inertialSetting != "off" && !inertialSetting.empty())
                && (isPlayer || Settings::values().mGame.mInertialLocomotionNpcs.get());
            MWPhysics::Actor* physActor = nullptr;
            if (useInertial && world->getPhysicsSystem())
                physActor = world->getPhysicsSystem()->getActor(mPtr);

            const bool enableAuthored = Settings::values().mGame.mEnableAuthoredTransitions.get();
            inertialActive = useInertial && physActor && onground && !inwater && !flying;
            smoothedSpeed = inertialActive ? physActor->getLocomotionState().getSmoothedSpeed() : 0.0f;
            const InertialMotion::LocomotionPhase locPhase
                = inertialActive ? physActor->getLocomotionState().getPhase() : InertialMotion::LocomotionPhase::Idle;
            mTraceLocomotionPhase = static_cast<int>(locPhase);

            // Force Jump Logic

            bool isMoving
                = (std::abs(movementSettings.mPosition[0]) > .5 || std::abs(movementSettings.mPosition[1]) > .5);
            if (mIntentTracker != nullptr)
            {
                // Task 0067, stage 1: the intent's inputs, read here because the movement settings are cleared
                // before the animation thread runs.
                mIntentWantsToMove = isMoving;
                if (inertialActive)
                    mIntentMeasuredMps = smoothedSpeed / Misc::gameConventions().mUnitsPerMeter;
                mIntentStanceChanged = mIntentStanceKnown && (sneak != mIntentLastSneak || isrunning != mIntentLastRun);
                mIntentLastSneak = sneak;
                mIntentLastRun = isrunning;
                mIntentStanceKnown = true;
            }
            if (!inwater && !flying)
            {
                // Force Jump
                if (stats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceJump))
                    movementSettings.mPosition[2] = onground ? 1.f : 0.f;
                // Force Move Jump, only jump if they're otherwise moving
                if (stats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceMoveJump) && isMoving)
                    movementSettings.mPosition[2] = onground ? 1.f : 0.f;
            }

            vsg::vec3 rot = cls.getRotationVector(mPtr);
            vsg::vec3 vec(movementSettings.asVec3());
            movementSettings.mSpeedFactor = std::min(vsg::length(vec), 1.f);
            Misc::VsgMath::normalizeInPlace(vec);

            const bool smoothMovement = Settings::game().mSmoothMovement;
            if (smoothMovement)
            {
                float angle = mPtr.getRefData().getPosition().rot[2];
                vsg::vec2 targetSpeed
                    = Misc::rotateVec2f(vsg::vec2(vec.x, vec.y), -angle) * movementSettings.mSpeedFactor;
                vsg::vec2 delta = targetSpeed - mSmoothedSpeed;
                float speedDelta = movementSettings.mSpeedFactor - vsg::length(mSmoothedSpeed);
                float deltaLen = vsg::length(delta);

                float maxDelta;
                if (isFirstPersonPlayer)
                    maxDelta = 1;
                else if (std::abs(speedDelta) < deltaLen / 2)
                    // Turning is smooth for player and less smooth for NPCs (otherwise NPC can miss a path point).
                    maxDelta = duration * (isPlayer ? 1.0f / Settings::game().mSmoothMovementPlayerTurningDelay : 6.f);
                else if (isPlayer && speedDelta < -deltaLen / 2)
                    // As soon as controls are released, mwinput switches player from running to walking.
                    // So stopping should be instant for player, otherwise it causes a small twitch.
                    maxDelta = 1;
                else // In all other cases speeding up and stopping are smooth.
                    maxDelta = duration * 3.f;

                if (deltaLen > maxDelta)
                    delta *= maxDelta / deltaLen;
                mSmoothedSpeed += delta;

                vsg::vec2 newSpeed = Misc::rotateVec2f(mSmoothedSpeed, angle);
                movementSettings.mSpeedFactor = Misc::VsgMath::normalizeInPlace(newSpeed);
                vec.x = newSpeed.x;
                vec.y = newSpeed.y;

                const float eps = 0.001f;
                if (movementSettings.mSpeedFactor < eps)
                {
                    movementSettings.mSpeedFactor = 0;
                    vec.x = 0;
                    vec.y = 1;
                }
                else if ((vec.y < 0) != mIsMovingBackward)
                {
                    if (vsg::length(targetSpeed) < eps || (movementSettings.mPosition[1] < 0) == mIsMovingBackward)
                        vec.y = mIsMovingBackward ? -eps : eps;
                }
                Misc::VsgMath::normalizeInPlace(vec);
            }

            float effectiveRotation = rot.z;
            bool canMove = cls.getMaxSpeed(mPtr) > 0;
            const bool turnToMovementDirection = Settings::game().mTurnToMovementDirection;
            const bool isBiped = mPtr.getClass().isBipedal(mPtr);
            if (!isBiped || !turnToMovementDirection || isFirstPersonPlayer)
            {
                movementSettings.mIsStrafing = std::abs(vec.x) > std::abs(vec.y) * 2;
                stats.setSideMovementAngle(0);
            }
            else if (canMove)
            {
                float targetMovementAngle = vec.y >= 0 ? std::atan2(-vec.x, vec.y) : std::atan2(vec.x, -vec.y);
                movementSettings.mIsStrafing = (stats.getDrawState() != MWMechanics::DrawState::Nothing || inwater)
                    && std::abs(targetMovementAngle) > Misc::VsgMath::inDegrees(60.0f);
                if (movementSettings.mIsStrafing)
                    targetMovementAngle = 0;
                float delta = targetMovementAngle - stats.getSideMovementAngle();
                float cosDelta = cosf(delta);

                if ((vec.y < 0) == mIsMovingBackward)
                    movementSettings.mSpeedFactor
                        *= std::min(std::max(cosDelta, 0.f) + 0.3f, 1.f); // slow down when turn
                if (std::abs(delta) < Misc::VsgMath::inDegrees(20.0f))
                    mIsMovingBackward = vec.y < 0;

                float maxDelta = Misc::VsgMath::PIf * duration * (2.5f - cosDelta);
                delta = std::clamp(delta, -maxDelta, maxDelta);
                stats.setSideMovementAngle(stats.getSideMovementAngle() + delta);
                effectiveRotation += delta;
            }

            mActor->legsYawRadians = stats.getSideMovementAngle();
            if (stats.getDrawState() == MWMechanics::DrawState::Nothing || inwater)
                mActor->upperBodyYawRadians = stats.getSideMovementAngle() / 2;
            else
                mActor->upperBodyYawRadians = stats.getSideMovementAngle() / 4;
            if (smoothMovement && !isPlayer && !inwater)
                mActor->upperBodyYawRadians += mActor->headYawRadians / 2;

            speed = cls.getCurrentSpeed(mPtr);
            if (mIntentTracker != nullptr)
                mIntentSpeedMps = speed / Misc::gameConventions().mUnitsPerMeter;
            vec.x *= speed;
            vec.y *= speed;

            if (isKnockedOut() || isKnockedDown() || isRecovery() || isScriptedAnimPlaying())
                vec = vsg::vec3();

            CharacterState movestate = CharState_None;
            CharacterState idlestate = CharState_None;
            JumpingState jumpstate = JumpState_None;

            const MWWorld::Store<ESM::GameSetting>& gmst = world->getStore().get<ESM::GameSetting>();
            if (vec.x != 0.f || vec.y != 0.f)
            {
                // advance athletics
                if (isPlayer)
                {
                    if (inwater)
                    {
                        mSecondsOfSwimming += duration;
                        while (mSecondsOfSwimming > 1)
                        {
                            cls.skillUsageSucceeded(mPtr, ESM::Skill::Athletics, ESM::Skill::Athletics_SwimOneSecond);
                            mSecondsOfSwimming -= 1;
                        }
                    }
                    else if (isrunning && !sneak)
                    {
                        mSecondsOfRunning += duration;
                        while (mSecondsOfRunning > 1)
                        {
                            cls.skillUsageSucceeded(mPtr, ESM::Skill::Athletics, ESM::Skill::Athletics_RunOneSecond);
                            mSecondsOfRunning -= 1;
                        }
                    }
                }

                if (!godmode)
                {
                    // reduce fatigue
                    float fatigueLoss = 0.f;
                    static const float fFatigueRunBase = gmst.find("fFatigueRunBase")->mValue.getFloat();
                    static const float fFatigueRunMult = gmst.find("fFatigueRunMult")->mValue.getFloat();
                    static const float fFatigueSwimWalkBase = gmst.find("fFatigueSwimWalkBase")->mValue.getFloat();
                    static const float fFatigueSwimRunBase = gmst.find("fFatigueSwimRunBase")->mValue.getFloat();
                    static const float fFatigueSwimWalkMult = gmst.find("fFatigueSwimWalkMult")->mValue.getFloat();
                    static const float fFatigueSwimRunMult = gmst.find("fFatigueSwimRunMult")->mValue.getFloat();
                    static const float fFatigueSneakBase = gmst.find("fFatigueSneakBase")->mValue.getFloat();
                    static const float fFatigueSneakMult = gmst.find("fFatigueSneakMult")->mValue.getFloat();

                    if (cls.getEncumbrance(mPtr) <= cls.getCapacity(mPtr))
                    {
                        const float encumbrance = cls.getNormalizedEncumbrance(mPtr);
                        if (sneak)
                            fatigueLoss = fFatigueSneakBase + encumbrance * fFatigueSneakMult;
                        else
                        {
                            if (inwater)
                            {
                                if (!isrunning)
                                    fatigueLoss = fFatigueSwimWalkBase + encumbrance * fFatigueSwimWalkMult;
                                else
                                    fatigueLoss = fFatigueSwimRunBase + encumbrance * fFatigueSwimRunMult;
                            }
                            else if (isrunning)
                                fatigueLoss = fFatigueRunBase + encumbrance * fFatigueRunMult;
                        }
                    }
                    fatigueLoss *= duration;
                    fatigueLoss *= movementSettings.mSpeedFactor;
                    DynamicStat<float> fatigue = cls.getCreatureStats(mPtr).getFatigue();
                    fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss, fatigue.getCurrent() < 0);
                    cls.getCreatureStats(mPtr).setFatigue(fatigue);
                }
            }

            bool wasInJump = mInJump;
            mInJump = false;
            const float jumpHeight = cls.getJump(mPtr);
            if (jumpHeight <= 0.f || sneak || inwater || flying || !solid)
            {
                vec.z = 0.f;
                // Following code might assign some vertical movement regardless, need to reset this manually
                // This is used for jumping detection
                movementSettings.mPosition[2] = 0;
            }

            if (!inwater && !flying && solid)
            {
                // In the air (either getting up —ascending part of jump— or falling).
                if (!onground)
                {
                    mInJump = true;
                    jumpstate = JumpState_InAir;

                    static const float fJumpMoveBase = gmst.find("fJumpMoveBase")->mValue.getFloat();
                    static const float fJumpMoveMult = gmst.find("fJumpMoveMult")->mValue.getFloat();
                    float factor = fJumpMoveBase
                        + fJumpMoveMult * mPtr.getClass().getSkill(mPtr, ESM::Skill::Acrobatics) / 100.f;
                    factor = std::min(1.f, factor);
                    vec.x *= factor;
                    vec.y *= factor;
                    vec.z = 0.0f;
                }
                // Started a jump.
                else if (mJumpState != JumpState_InAir && vec.z > 0.f)
                {
                    mInJump = true;
                    if (vec.x == 0 && vec.y == 0)
                        vec.z = jumpHeight;
                    else
                    {
                        vsg::vec3 lat(vec.x, vec.y, 0.0f);
                        Misc::VsgMath::normalizeInPlace(lat);
                        vec = vsg::vec3(lat.x, lat.y, 1.0f) * jumpHeight * 0.707f;
                    }
                }
            }

            if (!mInJump)
            {
                if (mJumpState == JumpState_InAir && !flying && solid && wasInJump)
                {
                    float height = cls.getCreatureStats(mPtr).land(isPlayer);
                    float healthLost = 0.f;
                    if (!inwater)
                        healthLost = getFallDamage(mPtr, height);

                    if (healthLost > 0.0f)
                    {
                        const float fatigueTerm = cls.getCreatureStats(mPtr).getFatigueTerm();

                        // inflict fall damages
                        if (!godmode)
                        {
                            DynamicStat<float> health = cls.getCreatureStats(mPtr).getHealth();
                            float realHealthLost = healthLost * (1.0f - 0.25f * fatigueTerm);
                            health.setCurrent(health.getCurrent() - realHealthLost);
                            cls.getCreatureStats(mPtr).setHealth(health);
                            sndMgr->playSound3D(mPtr, ESM::RefId::stringRefId("Health Damage"), 1.0f, 1.0f);
                            if (isPlayer)
                                MWBase::Environment::get().getWindowManager()->activateHitOverlay();
                        }

                        const float acrobaticsSkill = cls.getSkill(mPtr, ESM::Skill::Acrobatics);
                        if (healthLost > (acrobaticsSkill * fatigueTerm))
                        {
                            if (!godmode)
                                cls.getCreatureStats(mPtr).setKnockedDown(true);
                        }
                        else
                        {
                            // report acrobatics progression
                            if (isPlayer)
                                cls.skillUsageSucceeded(mPtr, ESM::Skill::Acrobatics, ESM::Skill::Acrobatics_Fall);
                        }
                    }

                    if (mPtr.getClass().isNpc())
                    {
                        std::string_view sound;
                        vsg::vec3 pos(mPtr.getRefData().getPosition().asVec3());
                        if (world->isUnderwater(mPtr.getCell(), pos) || world->isWalkingOnWater(mPtr))
                            sound = "DefaultLandWater";
                        else if (onground)
                            sound = "DefaultLand";

                        if (!sound.empty())
                            sndMgr->playSound3D(mPtr, ESM::RefId::stringRefId(sound), 1.f, 1.f, MWSound::Type::Foot,
                                MWSound::PlayMode::NoPlayerLocal);
                    }
                }

                if (mAnimation->isPlaying(mCurrentJump))
                    jumpstate = JumpState_Landing;

                vec.z = 0.0f;

                // Check if an authored transition is actively playing
                if (inertialActive && isTransitionMovementState(mMovementState)
                    && mAnimation->isPlaying(mCurrentMovement))
                {
                    if (mMovementState == CharState_RunStop || mMovementState == CharState_WalkStop)
                    {
                        if (vsg::length2(vec) > 0.0f)
                            movestate = CharState_None; // interrupt stop to move
                        else if (smoothedSpeed <= 5.0f)
                            movestate = CharState_None; // fully stopped
                        else
                            movestate = mMovementState; // continue playing stop
                    }
                    else if (mMovementState == CharState_RunStart || mMovementState == CharState_WalkStart)
                    {
                        if (vsg::length2(vec) == 0.0f)
                            movestate = CharState_None; // input released during startup
                        else
                            movestate = mMovementState; // continue playing start
                    }
                    else if (mMovementState == CharState_RunSkid)
                    {
                        movestate = mMovementState; // continue playing skid
                    }
                    else if (mMovementState == CharState_TurnLeft90 || mMovementState == CharState_TurnRight90)
                    {
                        if (vsg::length2(vec) > 0.0f)
                            movestate = CharState_None; // interrupt turn for movement
                        else
                            movestate = mMovementState; // continue playing 90-degree turn
                    }
                }

                if (movestate == CharState_None)
                {
                    bool effectiveIsRunning = isrunning;
                    if (inertialActive && isrunning && !inwater && !sneak && solid)
                    {
                        const float walkSpeed = cls.getWalkSpeed(mPtr);
                        if (walkSpeed > 10.0f)
                        {
                            if (isRunning())
                            {
                                // In a run state: drop down to walk if speed falls below walk threshold with hysteresis
                                if (smoothedSpeed < walkSpeed * 0.75f)
                                    effectiveIsRunning = false;
                            }
                            else
                            {
                                // In walk, idle, or transition state: only enter run once speed reaches walk threshold
                                if (smoothedSpeed < walkSpeed * 0.90f)
                                    effectiveIsRunning = false;
                            }
                        }
                    }

                    if (movementSettings.mIsStrafing)
                    {
                        if (vec.x > 0.0f)
                            movestate
                                = (inwater ? (isrunning ? CharState_SwimRunRight : CharState_SwimWalkRight)
                                           : (sneak ? CharState_SneakRight
                                                    : (effectiveIsRunning ? CharState_RunRight : CharState_WalkRight)));
                        else if (vec.x < 0.0f)
                            movestate
                                = (inwater ? (isrunning ? CharState_SwimRunLeft : CharState_SwimWalkLeft)
                                           : (sneak ? CharState_SneakLeft
                                                    : (effectiveIsRunning ? CharState_RunLeft : CharState_WalkLeft)));
                    }
                    else if (vsg::length2(vec) > 0.0f)
                    {
                        if (inertialActive && enableAuthored && locPhase == InertialMotion::LocomotionPhase::Skidding
                            && mAnimation->hasAnimation("runskid"))
                        {
                            movestate = CharState_RunSkid;
                        }
                        else if (inertialActive && enableAuthored
                            && locPhase == InertialMotion::LocomotionPhase::Starting
                            && (mMovementState == CharState_None || isTurning()) && smoothedSpeed < 20.0f)
                        {
                            if (effectiveIsRunning && mAnimation->hasAnimation("runstart"))
                                movestate = CharState_RunStart;
                            else if (!effectiveIsRunning && mAnimation->hasAnimation("walkstart"))
                                movestate = CharState_WalkStart;
                        }

                        if (movestate == CharState_None)
                        {
                            if (vec.y >= 0.0f)
                                movestate = (inwater
                                        ? (isrunning ? CharState_SwimRunForward : CharState_SwimWalkForward)
                                        : (sneak
                                                ? CharState_SneakForward
                                                : (effectiveIsRunning ? CharState_RunForward : CharState_WalkForward)));
                            else
                                movestate = (inwater
                                        ? (isrunning ? CharState_SwimRunBack : CharState_SwimWalkBack)
                                        : (sneak ? CharState_SneakBack
                                                 : (effectiveIsRunning ? CharState_RunBack : CharState_WalkBack)));
                        }
                    }
                    else
                    {
                        // Movement input is 0 (vsg::length2(vec) == 0.0f)
                        if (inertialActive && smoothedSpeed > 5.0f)
                        {
                            // Decelerating with physical inertia
                            if (enableAuthored && effectiveIsRunning && mAnimation->hasAnimation("runstop"))
                                movestate = CharState_RunStop;
                            else if (enableAuthored && !effectiveIsRunning && mAnimation->hasAnimation("walkstop"))
                                movestate = CharState_WalkStop;
                            else
                            {
                                // Procedural fallback: maintain previous walk/run cycle while slowing
                                if (mMovementState != CharState_None && !isTransitionMovementState(mMovementState))
                                {
                                    if (!effectiveIsRunning && isRunning())
                                        movestate = runStateToWalkState(mMovementState);
                                    else
                                        movestate = mMovementState;
                                }
                                else
                                    movestate = effectiveIsRunning ? CharState_RunForward : CharState_WalkForward;
                            }
                        }
                        else
                        {
                            // Stationary Turning
                            if (!sneak && !isFirstPersonPlayer && isBiped)
                            {
                                if (effectiveRotation > 0.f)
                                {
                                    if (inertialActive && enableAuthored && mAnimation->hasAnimation("turnright90"))
                                        movestate = CharState_TurnRight90;
                                    else
                                        movestate = inwater ? CharState_SwimTurnRight : CharState_TurnRight;
                                }
                                else if (effectiveRotation < 0.f)
                                {
                                    if (inertialActive && enableAuthored && mAnimation->hasAnimation("turnleft90"))
                                        movestate = CharState_TurnLeft90;
                                    else
                                        movestate = inwater ? CharState_SwimTurnLeft : CharState_TurnLeft;
                                }
                            }
                        }
                    }
                }
            }

            if (turnToMovementDirection && !isFirstPersonPlayer && isBiped
                && (movestate == CharState_SwimRunForward || movestate == CharState_SwimWalkForward
                    || movestate == CharState_SwimRunBack || movestate == CharState_SwimWalkBack))
            {
                float swimmingPitch = mActor->bodyPitchRadians;
                float targetSwimmingPitch = -mPtr.getRefData().getPosition().rot[0];
                float maxSwimPitchDelta = 3.0f * duration;
                swimmingPitch += std::clamp(targetSwimmingPitch - swimmingPitch, -maxSwimPitchDelta, maxSwimPitchDelta);
                mActor->bodyPitchRadians = swimmingPitch;
            }
            else
                mActor->bodyPitchRadians = 0;

            if (inwater && isPlayer && !isFirstPersonPlayer && Settings::game().mSwimUpwardCorrection)
            {
                const float swimUpwardCoef = Settings::game().mSwimUpwardCoef;
                vec.z = std::abs(vec.y) * swimUpwardCoef;
                vec.y *= std::sqrt(1.0f - swimUpwardCoef * swimUpwardCoef);
            }

            if (isBiped)
            {
                if (mTurnAnimationThreshold > 0)
                    mTurnAnimationThreshold -= duration;

                if (movestate == CharState_TurnRight || movestate == CharState_TurnLeft
                    || movestate == CharState_SwimTurnRight || movestate == CharState_SwimTurnLeft
                    || movestate == CharState_TurnLeft90 || movestate == CharState_TurnRight90)
                {
                    mTurnAnimationThreshold = 0.05f;
                }
                else if (movestate == CharState_None && isTurning() && mTurnAnimationThreshold > 0)
                {
                    movestate = mMovementState;
                }
            }

            if (movestate != CharState_None)
            {
                clearAnimQueue();
                jumpstate = JumpState_None;
            }

            updateAnimQueue();
            if (!mAnimQueue.empty())
                idlestate = CharState_SpecialIdle;
            else if (sneak && !mInJump)
                idlestate = CharState_IdleSneak;
            else
                idlestate = CharState_Idle;

            if (inwater)
                idlestate = CharState_IdleSwim;

            if (!mSkipAnim)
            {
                refreshCurrentAnims(idlestate, movestate, jumpstate, updateWeaponState());
                updateIdleStormState(inwater);
            }

            if (isTurning() && !isTransitionMovementState(mMovementState))
            {
                if (duration > 0)
                {
                    float turnSpeed
                        = std::min(1.5f, std::abs(rot.z) / duration / static_cast<float>(Misc::VsgMath::PI));
                    mAnimation->adjustSpeedMult(mCurrentMovement, turnSpeed);
                }
            }
            else if (mMovementState != CharState_None && mAdjustMovementAnimSpeed
                && !isTransitionMovementState(mMovementState))
            {
                // Vanilla caps the played animation speed.
                const float maxSpeedMult = 10.f;
                float currentSpeed = speed;
                if (inertialActive)
                {
                    // Layer 3.B: Dynamic Playback Scaling
                    currentSpeed = InertialMotion::animationGroundSpeed(smoothedSpeed, speed, vsg::length2(vec) > 0.0f);
                }
                // The style layer's cadence (1 without it), set by runPoseDriver (task 0053).
                const float speedMult = currentSpeed / mMovementAnimSpeed * mStyleCadence;
                mAnimation->adjustSpeedMult(mCurrentMovement, std::min(maxSpeedMult, speedMult));
                // Make sure the actual speed is the "expected" speed even though the animation is slower
                if (isMovementAnimationControlled())
                    scale *= std::max(1.f, speedMult / maxSpeedMult);
            }

            if (!mSkipAnim)
            {
                if (!isKnockedDown() && !isKnockedOut())
                {
                    if (rot != vsg::vec3())
                        world->rotateObject(mPtr, rot, true);
                }
                else // avoid z-rotating for knockdown
                {
                    if (rot.x != 0 && rot.y != 0)
                    {
                        rot.z = 0.0f;
                        world->rotateObject(mPtr, rot, true);
                    }
                }

                updateHeadTracking(duration);
            }

            movement = vec;
            movementSettings.mPosition[0] = movementSettings.mPosition[1] = 0;

            // Can't reset jump state (mPosition[2]) here in full; we don't know for sure whether the PhysicsSystem will
            // actually handle it in this frame due to the fixed minimum timestep used for the physics update. It will
            // be reset in PhysicsSystem::move once the jump is handled.
            if (movement.z == 0.f)
                movementSettings.mPosition[2] = 0;
        }
        else if (cls.getCreatureStats(mPtr).isDead())
        {
            // initial start of death animation for actors that started the game as dead
            // not done in constructor since we need to give scripts a chance to set the mSkipAnim flag
            if (!mSkipAnim && mDeathState != CharState_None && mCurrentDeath.empty())
            {
                // Fast-forward death animation to end for persisting corpses or corpses after end of death animation
                if (cls.isPersistent(mPtr) || cls.getCreatureStats(mPtr).isDeathAnimationFinished())
                    playDeath(1.f, mDeathState);
            }
        }
    }

    void CharacterController::runAnimation(float duration)
    {
        // May run on an update thread: only advance this actor's animation here.
        if (!mAnimation)
        {
            moved = {};
            return;
        }
        const float animationDuration = mSkipAnim && !isScriptedAnimPlaying() ? 0.f : duration;
        moved = mAnimation->runAnimation(animationDuration);
        if (mPoseDriver)
            runPoseDriver(animationDuration);
        if (Settings::game().mCreativeMotionAnimationDebug && (mPoseDriver || mPtr.getClass().isActor()))
            traceAnimation(animationDuration);
    }

    void CharacterController::traceAnimation(float duration)
    {
        // Developer trace of "creativemotion animation debug": one line per actor per frame. May run on an update
        // thread; the logger is thread safe and this reads only this actor's animation and members that update set.
        constexpr std::size_t sTraceTextCapacity = 160;
        static const std::vector<std::string_view> traceBones
            = { "bip01 pelvis", "bip01 l thigh", "bip01 r thigh", "bip01 spine1", "bip01 l upperarm", "bip01 head" };
        std::string line
            = "CMTRACE ref=" + mPtr.getCellRef().getRefId().toDebugString() + " dt=" + std::to_string(duration);
        for (std::size_t group = 0; group < MWRender::sNumBlendMasks; ++group)
        {
            const MWAnim::Play::ActiveState state = mAnimation->getActiveState(group);
            line += " g" + std::to_string(group) + "=" + std::string(state.groupname);
            if (mPoseDriver)
            {
                const auto status = mPoseDriver->getTransitionStatus(group);
                line += "(prev=" + std::string(status.mPreviousGroup) + ",trans="
                    + (status.mActive ? std::to_string(status.mElapsed) + "/" + std::to_string(status.mDuration)
                                      : std::string("none"))
                    + ")";
            }
        }
        const auto& position = mPtr.getRefData().getPosition();
        char positionText[sTraceTextCapacity];
        std::snprintf(positionText, sizeof(positionText), " pos=(%.3f %.3f %.3f) moved=(%.3f %.3f %.3f)",
            position.pos[0], position.pos[1], position.pos[2], moved.x, moved.y, moved.z);
        line += positionText;
        // The locomotion values were read on the main thread in update; this may run on an animation thread.
        float animationSpeed = 0.f;
        mAnimation->getInfo(mCurrentMovement, nullptr, &animationSpeed);
        char locomotionText[sTraceTextCapacity];
        std::snprintf(locomotionText, sizeof(locomotionText), " loco=(speed %.1f phase %d) animspeed=%.3f",
            smoothedSpeed, mTraceLocomotionPhase, animationSpeed);
        line += locomotionText;
        line += std::string(" driver=") + (mPoseDriver ? (mPoseDriverApplies ? "applies" : "compare") : "off");
        if (mIntentTracker != nullptr && mTraceIntent.mValid)
        {
            char intentText[sTraceTextCapacity];
            std::snprintf(intentText, sizeof(intentText), " intent=(wants %d start %d stop %d stance %d turn %.3f)",
                mTraceIntent.mWants ? 1 : 0, mTraceIntent.mStarting ? 1 : 0, mTraceIntent.mStopping ? 1 : 0,
                mTraceIntent.mStance ? 1 : 0, mTraceIntent.mTurnRate);
            line += intentText;
        }
        if (mPoseDriver)
        {
            const auto style = mPoseDriver->getStyleStatus();
            if (style.mProfile != nullptr)
            {
                char styleText[sTraceTextCapacity];
                std::snprintf(styleText, sizeof(styleText),
                    " style=%s(%s) w=(move %.2f idle %.2f posture %.2f) cadence=%.2f",
                    style.mProfile != nullptr ? style.mProfile->name.c_str() : "none", style.mEnabled ? "on" : "off",
                    style.mWeights.mMoving, style.mWeights.mIdle, style.mWeights.mPosture, style.mCadence);
                line += styleText;
            }
        }
        line += MWRender::describeBoneRotations(mAnimation->bones, traceBones);
        if (mPoseDriver && mPoseDriverApplies)
            line += mPoseDriver->describeTargetRotations(traceBones);
        Log(Debug::Info) << line;
    }

    namespace
    {
        // The CreativeMotion pieces that are on once [Game] creativemotion preset is combined with the individual
        // settings (task 0067, stage 1): a setting the user names wins over the preset. An unknown preset name counts
        // as "off" and is reported once.
        MWRender::CreativeMotionFeatures creativeMotionFeatures()
        {
            static std::atomic<bool> reported{ false };
            bool recognised = false;
            const MWRender::CreativeMotionFeatures features
                = MWRender::loadCreativeMotionFeaturesFromSettings(&recognised);
            if (!recognised && !reported.exchange(true))
                Log(Debug::Warning) << "[Game] creativemotion preset = '"
                                    << Settings::game().mCreativeMotionPreset.get()
                                    << "' is not known (off, natural): using off";
            return features;
        }

        // The [CreativeMotion Transitions] category, read once for the session (a change needs a restart); each bad
        // key or value is logged once, with the category, key and value.
        const MWRender::TransitionTimes& transitionTimes()
        {
            static const MWRender::TransitionTimes times = [] {
                std::vector<std::string> problems;
                const MWRender::TransitionTimes loaded = MWRender::loadTransitionTimesFromSettings(&problems);
                for (const std::string& problem : problems)
                    Log(Debug::Warning) << problem;
                return loaded;
            }();
            return times;
        }
    }

    void CharacterController::createPoseControllers()
    {
        // New bones: a ragdoll driver would write the old ones (task 0106), so the ragdoll ends with it and the corpse
        // is drawn by the death clip again. Live game only (a rebuild); the destructor leaves it to PhysicsSystem::remove.
        if (mRagdollDriver)
        {
            mRagdollDriver.reset();
            if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
                physics->stopActorRagdoll(mPtr);
        }
        mIkController = std::make_unique<MWRender::ActorIkController>(mPtr, *mObject);

        mPoseDriver.reset();
        mPoseDriverApplies = false;
        mStyleCadence = 1.f;
        mStyleSpeedMps = 0.f;
        mIntentTracker.reset();
        mHasIntentPreviousYaw = false;
        const MWRender::CreativeMotionFeatures features = creativeMotionFeatures();
        if (features.mAnimation || Settings::game().mCreativeMotionAnimationCompare)
        {
            auto driver = std::make_unique<MWRender::CreativeMotionPoseDriver>(mAnimation->bones);
            if (driver->isValid())
            {
                mPoseDriver = std::move(driver);
                mPoseDriverApplies = features.mAnimation;
                mAnimation->updateBones = !mPoseDriverApplies;
                mPoseDriver->setRecordTargets(Settings::game().mCreativeMotionAnimationDebug);
                mPoseDriver->setTransitionRules(features.mTransitionRules);
                if (features.mIntentTransitions)
                {
                    mPoseDriver->setIntentTransitions(true, transitionTimes());
                    mIntentTracker = std::make_unique<MWRender::IntentTracker>();
                }
                configureStyle();
            }
        }
    }

    const opas::style::StyleProfile* CharacterController::resolveStyleProfile()
    {
        if (!creativeMotionFeatures().mStyleLayer)
            return nullptr;
        std::string name = mStyleChoice;
        if (name.empty())
        {
            std::string raceId;
            if (mPtr.getClass().isNpc())
                raceId = mPtr.get<ESM::NPC>()->mBase->mRace.serializeText();
            name = MWRender::selectStyleName(
                MWRender::loadStyleRulesFromSettings(), mPtr.getCellRef().getRefId().serializeText(), raceId);
        }
        return styleLibrary().find(name);
    }

    void CharacterController::configureStyle()
    {
        mStyleCadence = 1.f;
        if (!mPoseDriver)
            return;
        mPoseDriver->setStyleEnabled(false);
        mPoseDriver->setStyle(nullptr, 0);
        // The style only does anything when the pose driver poses the bones (not in compare-only mode).
        const opas::style::StyleProfile* profile = mPoseDriverApplies ? resolveStyleProfile() : nullptr;
        // The style's noise is seeded from the reference number: the same actor always moves the same way.
        const ESM::RefNum refNum = mPtr.getCellRef().getRefNum();
        const std::uint64_t seed = static_cast<std::uint64_t>(refNum.mIndex)
            | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(refNum.mContentFile)) << 32);
        mPoseDriver->setStyle(profile, seed);
        mPoseDriver->setStyleEnabled(profile != nullptr && !mStyleSwitchedOff);
    }

    MWRender::ActorState CharacterController::currentStyleState() const
    {
        MWRender::ActorState state;
        state.mSpeedMps = mStyleSpeedMps;
        state.mRunning = isRunning();
        state.mSneaking = isSneaking();
        state.mTurning = isTurning();
        state.mWeaponDrawn = mPtr.getClass().getCreatureStats(mPtr).getDrawState() != DrawState::Nothing;
        state.mHit = mHitState != CharState_None;
        return state;
    }

    std::string CharacterController::styleCommand(std::string_view argument)
    {
        if (!mPoseDriver || !mPoseDriverApplies)
            return "CreativeMotion animation is not running for this actor";
        if (argument == "on" || argument == "off")
            mStyleSwitchedOff = argument == "off";
        else if (!argument.empty())
        {
            if (!creativeMotionFeatures().mStyleLayer)
                return "The style layer is off: set [Game] creativemotion style layer = true";
            if (styleLibrary().find(argument) == nullptr)
                return "No style named " + std::string(argument) + " (see the log)";
            mStyleChoice = std::string(argument);
            mStyleSwitchedOff = false;
        }
        // The driver takes the change at its next run; what is described here is what it will then do.
        mStyleDirty = true;
        const opas::style::StyleProfile* profile = resolveStyleProfile();
        if (profile == nullptr)
            return creativeMotionFeatures().mStyleLayer ? "No style for this actor"
                                                        : "The style layer is off ([Game] creativemotion style layer)";
        return MWRender::describeStyle(*profile, !mStyleSwitchedOff, MWRender::styleWeights(currentStyleState()));
    }

    void CharacterController::runPoseDriver(float duration)
    {
        // Differences the comparison mode reports (as the OSG build): 5 mm and about 1.1 degrees.
        constexpr float compareTranslationThreshold = 0.005f;
        constexpr float compareRotationThreshold = 0.02f;

        if (mStyleDirty.exchange(false))
            configureStyle();

        std::array<MWRender::CreativeMotionActiveSource, MWRender::sNumBlendMasks> active;
        for (std::size_t group = 0; group < active.size(); ++group)
        {
            const MWAnim::Play::ActiveState state = mAnimation->getActiveState(group);
            if (state.source != nullptr && !mPoseDriver->registerSource(*state.source))
            {
                // Falls back to vsgopenmw's animation for this actor; its bones catch up on the next frame.
                Log(Debug::Warning) << "CreativeMotion cannot play the animation of " << mPtr.getCellRef().getRefId()
                                    << "; using the engine's animation for this actor";
                mPoseDriver.reset();
                mStyleCadence = 1.f;
                mAnimation->updateBones = true;
                return;
            }
            active[group] = { state.source, state.groupname, state.time };
        }

        // What the actor is doing, for the style layer. The speed is smoothed: root motion varies within a step.
        MWRender::ActorState state;
        float measuredSpeedMps = 0.f;
        if (duration > 0.f)
        {
            // The animation's own movement shrinks with the style's cadence, so divide that out (or the cadence
            // would feed back into the speed it is weighted by).
            const float measured = std::sqrt(moved.x * moved.x + moved.y * moved.y) / duration
                / Misc::gameConventions().mUnitsPerMeter / std::max(mStyleCadence.load(), 0.1f);
            measuredSpeedMps = measured;
            constexpr float speedSmoothingRate = 6.f;
            const float smoothed = mStyleSpeedMps;
            mStyleSpeedMps = smoothed + (measured - smoothed) * std::min(1.f, duration * speedSmoothingRate);
        }
        state = currentStyleState();
        if (mIntentTracker != nullptr && duration > 0.f)
        {
            // The actor's intent (task 0067, stage 1): whether the movement keys (or the AI's request) are held, the
            // speed of its current mode, how fast it turns and whether it switched sneak or run. The request and the
            // speed were stored by preAnimation on the main thread (the movement settings are cleared before this
            // runs); the measured speed is inertial locomotion's smoothed speed when it drives the actor.
            const float yaw = mPtr.getRefData().getPosition().rot[2];
            float yawDelta = 0.f;
            if (mHasIntentPreviousYaw)
                yawDelta = std::remainder(yaw - mIntentPreviousYaw, 2.f * std::numbers::pi_v<float>);
            mIntentPreviousYaw = yaw;
            mHasIntentPreviousYaw = true;

            MWRender::IntentInput input;
            input.mDt = duration;
            input.mMeasuredSpeedMps = mIntentMeasuredMps >= 0.f ? mIntentMeasuredMps : measuredSpeedMps;
            input.mWantsToMove = mIntentWantsToMove;
            input.mIntendedSpeedMps = mIntentWantsToMove ? mIntentSpeedMps : 0.f;
            input.mYawDeltaRad = yawDelta;
            input.mStanceChanged = mIntentStanceChanged;
            const MWRender::ActorIntent intent = mIntentTracker->update(input);
            state.mTargetSpeedMps = intent.mTargetSpeedMps;
            state.mAccelerationMps2 = intent.mAccelerationMps2;
            state.mTurnRate = intent.mTurnRate;
            state.mStarting = intent.mStarting;
            state.mStopping = intent.mStopping;
            state.mStanceChanging = intent.mStanceChanging;
            mTraceIntent = TraceIntent{ true, input.mWantsToMove, intent.mStarting, intent.mStopping,
                intent.mStanceChanging, intent.mTurnRate };
        }
        mPoseDriver->setActorState(state);
        mStyleCadence = mPoseDriver->getStyleStatus().mCadence;

        mPoseDriver->update(duration, active, Settings::game().mCreativeMotionAnimationTransition);
        if (mPoseDriverApplies)
        {
            mPoseDriver->apply();
            return;
        }

        float maxTransDiff = 0.f;
        float maxRotDiff = 0.f;
        std::string maxDiffBone;
        if (mPoseDriver->compareWithCurrentPose(maxTransDiff, maxRotDiff, maxDiffBone)
            && (maxTransDiff > compareTranslationThreshold || maxRotDiff > compareRotationThreshold))
            Log(Debug::Verbose) << "CreativeMotion comparison diff on " << maxDiffBone << ": pos = " << maxTransDiff
                                << "m, rot = " << maxRotDiff << " rad";
    }

    namespace
    {
        // Reads a dead NPC's ragdoll through the physics system (task 0106). Looks the system up on every call: the
        // world may have been cleared since, and a stale handle then simply fails.
        class PhysicsRagdollSource final : public MWRender::RagdollSource
        {
        public:
            explicit PhysicsRagdollSource(MWPhysics::ActorRagdollHandle handle)
                : mHandle(handle)
            {
            }

            std::span<const std::string> boneNames() const override
            {
                const MWPhysics::PhysicsSystem* physics = physicsSystem();
                return physics == nullptr ? std::span<const std::string>() : physics->actorRagdollBones();
            }

            bool read(std::span<MWPhysics::RagdollBoneState> out) const override
            {
                const MWPhysics::PhysicsSystem* physics = physicsSystem();
                return physics != nullptr && physics->readActorRagdoll(mHandle, out);
            }

        private:
            static const MWPhysics::PhysicsSystem* physicsSystem()
            {
                const MWBase::World* world = MWBase::Environment::get().getWorld();
                return world == nullptr ? nullptr : world->getPhysicsSystem();
            }

            MWPhysics::ActorRagdollHandle mHandle;
        };
    }

    void CharacterController::updateRagdoll()
    {
        if (!isDead())
        {
            mSawAlive = true;
            return;
        }
        if (mRagdollDriver)
        {
            if (!mRagdollDriver->apply())
                mRagdollDriver.reset();
            return;
        }
        // One try per controller; a corpse that was loaded dead lies in the clip's last pose and gets none.
        if (mRagdollTried)
            return;
        RagdollTriggerInput input;
        input.mSettingOn = Settings::physics().mCreativeMotionRagdolls;
        input.mDead = true;
        input.mSawAliveHere = mSawAlive;
        input.mIsNpc = mNpc != nullptr;
        input.mIsPlayer = mPlayer != nullptr || mPtr == getPlayer();
        input.mHasBodyAndBones = mObject != nullptr && mAnimation != nullptr;
        input.mScriptedAnimation = isScriptedAnimPlaying();
        if (!input.mSawAliveHere || !input.mSettingOn)
            mRagdollTried = true; // never alive here, or not wanted: not looked at again
        if (!shouldStartActorRagdoll(input))
            return;
        mRagdollTried = true;

        MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem();
        if (physics == nullptr)
            return;
        MWPhysics::ActorRagdollHandle handle;
        std::string message;
        if (!physics->startActorRagdoll(mPtr, handle, message))
        {
            Log(Debug::Verbose) << "No ragdoll for " << mPtr.getCellRef().getRefId() << ": " << message;
            return;
        }
        auto driver = std::make_unique<MWRender::ActorRagdollDriver>(
            mAnimation->bones, mObject->transform(), std::make_unique<PhysicsRagdollSource>(handle));
        if (!driver->isValid())
        {
            Log(Debug::Verbose) << "No ragdoll for " << mPtr.getCellRef().getRefId() << ": " << driver->reason();
            physics->stopActorRagdoll(mPtr);
            return;
        }
        mRagdollDriver = std::move(driver);
        Log(Debug::Info) << "Actor ragdoll started for " << mPtr.getCellRef().getRefId() << " ("
                         << physics->actorRagdollCount() << " in all)";
    }

    void CharacterController::postAnimation(float duration)
    {
        if (!mAnimation)
            return;
        handleTextKeys();
        const MWWorld::Class& cls = mPtr.getClass();
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool isPlayer = mPtr == world->getPlayerPtr();
        const bool godmode = isPlayer && world->getGodModeState();

        vsg::vec3 movementFromAnimation = moved;

        if (cls.isActor() && !isScriptedAnimPlaying())
        {
            if (isMovementAnimationControlled())
            {
                // With inertial locomotion the request is the intended velocity, which the locomotion state then
                // smooths; the animation's root motion would make the smoothed speed (and with it the animation
                // speed) stall at its floor when a walk starts (docs/research/0022).
                const bool intended
                    = InertialMotion::chooseMovementRequestSource(inertialActive, mHitState != CharState_None,
                          mMovementState != CharState_None, isTransitionMovementState(mMovementState))
                    == InertialMotion::MovementRequestSource::Intended;
                // When intended, movement already holds the intended velocity (set in update).
                if (!intended && duration != 0.f && movementFromAnimation != vsg::vec3())
                {
                    movementFromAnimation = Misc::VsgMath::divide(movementFromAnimation, duration);

                    // Ensure we're moving in the right general direction.
                    // In vanilla, all horizontal movement is taken from animations, even when moving diagonally (which
                    // doesn't have a corresponding animation). So to achieve diagonal movement, we have to rotate the
                    // movement taken from the animation to the intended direction.
                    //
                    // Note that while a complete movement animation cycle will have a well defined direction, no
                    // individual frame will, and therefore we have to determine the direction based on the currently
                    // playing cycle instead.
                    if (speed > 0.f && vsg::length2(movement) > 0.0f)
                    {
                        float animMovementAngle = getAnimationMovementDirection();
                        float targetMovementAngle = std::atan2(-movement.x, movement.y);
                        float diff = targetMovementAngle - animMovementAngle;
                        movementFromAnimation = vsg::quat(Misc::VsgMath::makeRotateFromAxis(diff, vsg::vec3(0, 0, 1)))
                            * movementFromAnimation;
                    }

                    if (inertialActive && movement.x == 0.0f && movement.y == 0.0f)
                    {
                        movement.x = 0.0f;
                        movement.y = 0.0f;
                    }
                    else
                    {
                        movement = movementFromAnimation;
                    }
                }
                else if (!intended)
                {
                    movement = vsg::vec3();
                }
            }
            else if (mSkipAnim)
            {
                movement = vsg::vec3();
            }

            if (mFloatToSurface && world->isSwimming(mPtr))
            {
                if (cls.getCreatureStats(mPtr).isDead()
                    || (!godmode
                        && cls.getCreatureStats(mPtr)
                                .getMagicEffects()
                                .getOrDefault(ESM::MagicEffect::Paralyze)
                                .getModifier()
                            > 0))
                {
                    movement.z = 1.0;
                }
            }

            movement.x *= scale;
            movement.y *= scale;
            world->queueMovement(mPtr, movement);
        }

        mSkipAnim = false;

        if (mActor)
        {
            mActor->manualAnimation(mPtr.getRefData().getPosition().rot[0], duration);
            // OpenEden (task 0011): foot IK, procedural lean and compound hitboxes run after the animation and the
            // manual bone rotations, on the main thread, before the scene update skins the actor.
            if (mIkController)
            {
                mIkController->update(duration);
                mIkController->applyPostAnimation(duration);
            }
            // OpenEden (task 0106): a dead NPC's skeleton follows its ragdoll. For a living actor this is one
            // bool test, and with the setting off nothing more happens for a dead one either.
            updateRagdoll();
            if (mWielding)
            {
                if (mWeaponType != 0 && !mCurrentWeapon.empty())
                {
                    ESM::WeaponType::Class weaponClass = getWeaponType(mWeaponType)->mWeaponClass;
                    mWielding->weaponAnimationTime = mAnimation->getCurrentTime(mCurrentWeapon);
                    // Note: controllers for ranged weapon should use time for beginning of animation to play shooting
                    // properly, for other weapons they should use absolute time. Some mods rely on this behaviour (to
                    // rotate throwing projectiles, for example)
                    if (weaponClass == ESM::WeaponType::Ranged)
                        mWielding->weaponAnimationTime -= mAnimation->getStartTime(mCurrentWeapon);
                }
                else
                    mWielding->weaponAnimationTime = 0;

                if (mNpc)
                {
                    auto soundMgr = MWBase::Environment::get().getSoundManager();
                    mNpc->setFacialState(!cls.getCreatureStats(mPtr).isDead(), soundMgr->sayActive(mPtr),
                        soundMgr->getSaySoundLoudness(mPtr));
                    if (mPlayer)
                    {
                        mPlayer->busy = mAnimation->isPriorityActive(Priority_Hit)
                            || mAnimation->isPriorityActive(Priority_Weapon)
                            || mAnimation->isPriorityActive(Priority_Knockdown)
                            || mAnimation->isPriorityActive(Priority_Death);
                    }
                    for (auto& [soundId, sound] : mPlayingSounds)
                    {
                        if (!mNpc->soundsToPlay.count(soundId))
                            soundMgr->stopSound(sound);
                    }
                    for (auto& soundId : mNpc->soundsToPlay)
                    {
                        if (mPlayingSounds.find(soundId) == mPlayingSounds.end())
                            mPlayingSounds[soundId] = soundMgr->playSound3D(
                                mPtr, soundId, 1.0f, 1.0f, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
                    }
                }
            }
        }
    }

    void CharacterController::persistAnimationState() const
    {
        ESM::AnimationState& state = mPtr.getRefData().getAnimationState();

        state.mScriptedAnims.clear();
        for (AnimationQueue::const_iterator iter = mAnimQueue.begin(); iter != mAnimQueue.end(); ++iter)
        {
            // TODO: Probably want to presist lua animations too
            if (!iter->mScripted)
                continue;

            ESM::AnimationState::ScriptedAnimation anim;
            anim.mGroup = iter->mGroup;

            if (iter == mAnimQueue.begin() && mAnimation)
            {
                float complete;
                uint32_t loopcount;
                mAnimation->getInfo(anim.mGroup, &complete, nullptr, &loopcount);
                anim.mTime = complete;
                anim.mLoopCount = loopcount;
            }
            else
            {
                anim.mLoopCount = iter->mLoopCount;
                anim.mTime = 0.f;
            }

            state.mScriptedAnims.push_back(std::move(anim));
        }
    }

    void CharacterController::unpersistAnimationState()
    {
        const ESM::AnimationState& state = mPtr.getRefData().getAnimationState();

        if (!state.mScriptedAnims.empty())
        {
            clearAnimQueue();
            for (const ESM::AnimationState::ScriptedAnimation& animation : state.mScriptedAnims)
            {
                AnimationQueueEntry entry;
                entry.mGroup = animation.mGroup;
                entry.mLoopCount = static_cast<uint32_t>(
                    std::min<uint64_t>(animation.mLoopCount, std::numeric_limits<uint32_t>::max()));
                entry.mLooping = isLoopingAnimation(entry.mGroup);
                entry.mScripted = true;
                entry.mStartKey = "start";
                entry.mStopKey = "stop";
                entry.mSpeed = 1.f;
                entry.mTime = animation.mTime;
                if (animation.mAbsolute)
                {
                    float start = mAnimation->getTextKeyTime(animation.mGroup + ": start");
                    float stop = mAnimation->getTextKeyTime(animation.mGroup + ": stop");
                    float time = std::clamp(animation.mTime, start, stop);
                    entry.mTime = (time - start) / (stop - start);
                }

                mAnimQueue.push_back(std::move(entry));
            }

            playAnimQueue();
        }
    }

    void CharacterController::playBlendedAnimation(const std::string& groupname, const MWAnim::Priority& priority,
        int blendMask, bool autodisable, float speedmult, std::string_view start, std::string_view stop,
        float startpoint, uint32_t loops, bool loopfallback) const
    {
        if (mLuaAnimations)
            MWBase::Environment::get().getLuaManager()->playAnimation(mPtr, groupname, priority, blendMask, autodisable,
                speedmult, start, stop, startpoint, loops, loopfallback);
        else
            mAnimation->play(
                groupname, priority, blendMask, autodisable, speedmult, start, stop, startpoint, loops, loopfallback);
    }

    bool CharacterController::playGroup(std::string_view groupname, int mode, uint32_t count, bool scripted)
    {
        if (!mAnimation || !mAnimation->hasAnimation(groupname))
            return false;

        // We should not interrupt scripted animations with non-scripted ones
        if (isScriptedAnimPlaying() && !scripted)
            return true;

        bool looping = isLoopingAnimation(groupname);

        // If this animation is a looped animation that is already playing
        // and has not yet reached the end of the loop, allow it to continue animating with its existing loop count
        // and remove any other animations that were queued.
        // This emulates observed behavior from the original allows the script "OutsideBanner" to animate banners
        // correctly.
        if (!mAnimQueue.empty() && mAnimQueue.front().mGroup == groupname && looping
            && mAnimation->isPlaying(groupname))
        {
            float endOfLoop = mAnimation->getTextKeyTime(mAnimQueue.front().mGroup + ": loop stop");

            if (endOfLoop < 0) // if no Loop Stop key was found, use the Stop key
                endOfLoop = mAnimation->getTextKeyTime(mAnimQueue.front().mGroup + ": stop");

            if (endOfLoop > 0 && (mAnimation->getCurrentTime(mAnimQueue.front().mGroup) < endOfLoop))
            {
                mAnimQueue.resize(1);
                return true;
            }
        }

        // The loop count in vanilla is weird.
        // if played with a count of 0, all objects play exactly once from start to stop.
        // But if the count is x > 0, actors and non-actors behave differently. actors will loop
        // exactly x times, while non-actors will loop x+1 instead.
        if (mPtr.getClass().isActor() && count > 0)
            count--;

        AnimationQueueEntry entry;
        entry.mGroup = groupname;
        entry.mLoopCount = count;
        entry.mTime = 0.f;
        // "PlayGroup idle" is a special case, used to remove to stop scripted animations playing
        entry.mScripted = (scripted && groupname != "idle");
        entry.mLooping = looping;
        entry.mSpeed = 1.f;
        entry.mStartKey = ((mode == 2) ? "loop start" : "start");
        entry.mStopKey = "stop";

        bool playImmediately = false;

        if (mode != 0 || mAnimQueue.empty() || !isAnimPlaying(mAnimQueue.front().mGroup))
        {
            clearAnimQueue(scripted);

            playImmediately = true;
        }
        else
        {
            mAnimQueue.resize(1);
        }

        mAnimQueue.push_back(std::move(entry));

        if (playImmediately)
            playAnimQueue(mode == 2);

        return true;
    }

    bool CharacterController::playGroupLua(std::string_view groupname, float playSpeed, std::string_view startKey,
        std::string_view stopKey, uint32_t loops, bool forceLoop)
    {
        // Note: In mwscript, "idle" is a special case used to clear the anim queue.
        // In lua we offer an explicit clear method instead so this method does not treat "idle" special.

        if (!mAnimation || !mAnimation->hasAnimation(groupname))
            return false;

        AnimationQueueEntry entry;
        entry.mGroup = groupname;
        // Note: MWScript gives one less loop to actors than non-actors.
        // But this is the Lua version. We don't need to reproduce this weirdness here.
        entry.mLoopCount = loops;
        entry.mStartKey = startKey;
        entry.mStopKey = stopKey;
        entry.mLooping = isLoopingAnimation(groupname) || forceLoop;
        entry.mScripted = true;
        entry.mSpeed = playSpeed;
        entry.mTime = 0;

        if (mAnimQueue.size() > 1)
            mAnimQueue.resize(1);
        mAnimQueue.push_back(std::move(entry));

        if (mAnimQueue.size() == 1)
            playAnimQueue();

        return true;
    }

    void CharacterController::enableLuaAnimations(bool enable)
    {
        mLuaAnimations = enable;
    }

    void CharacterController::skipAnim()
    {
        mSkipAnim = true;
    }

    bool CharacterController::isScriptedAnimPlaying() const
    {
        // If the front of the anim queue is scripted, morrowind treats it as if it's
        // still playing even if it's actually done.
        if (!mAnimQueue.empty())
            return mAnimQueue.front().mScripted;

        return false;
    }

    bool CharacterController::isLoopingAnimation(std::string_view group) const
    {
        return mAnimation && MWMechanics::isLoopingAnimation(*mAnimation, group);
    }

    bool isLoopingAnimation(const MWAnim::Play& animation, std::string_view group)
    {
        // Moved here from OpenMW 0.51's MWRender::Animation::isLoopingAnimation, as MWAnim::Play is a component and
        // the weapon short groups belong to mechanics.
        // In Morrowind, a some animation groups are always considered looping, regardless
        // of loop start/stop keys.
        // To be match vanilla behavior we probably only need to check this list, but we don't
        // want to prevent modded animations with custom group names from looping either.
        static const std::unordered_set<std::string_view> loopingAnimations = { "walkforward", "walkback", "walkleft",
            "walkright", "swimwalkforward", "swimwalkback", "swimwalkleft", "swimwalkright", "runforward", "runback",
            "runleft", "runright", "swimrunforward", "swimrunback", "swimrunleft", "swimrunright", "sneakforward",
            "sneakback", "sneakleft", "sneakright", "turnleft", "turnright", "swimturnleft", "swimturnright",
            "spellturnleft", "spellturnright", "torch", "idle", "idle2", "idle3", "idle4", "idle5", "idle6", "idle7",
            "idle8", "idle9", "idlesneak", "idlestorm", "idleswim", "jump", "inventoryhandtohand",
            "inventoryweapononehand", "inventoryweapontwohand", "inventoryweapontwowide" };
        static const std::vector<std::string_view> shortGroups = getAllWeaponTypeShortGroups();

        if (animation.getTextKeyTime(std::string(group) + ": loop start") >= 0)
            return true;

        // Most looping animations have variants for each weapon type shortgroup.
        // Just remove the shortgroup instead of enumerating all of the possible animation groupnames.
        // Make sure we pick the longest shortgroup so e.g. "bow" doesn't get picked over "crossbow"
        // when the shortgroup is crossbow.
        std::size_t suffixLength = 0;
        for (std::string_view suffix : shortGroups)
        {
            if (suffix.length() > suffixLength && group.ends_with(suffix))
            {
                suffixLength = suffix.length();
            }
        }
        group.remove_suffix(suffixLength);

        return loopingAnimations.count(group) > 0;
    }

    bool CharacterController::isAnimPlaying(std::string_view groupName) const
    {
        if (mAnimation == nullptr)
            return false;
        return mAnimation->isPlaying(groupName);
    }

    bool CharacterController::isMovementAnimationControlled() const
    {
        if (mHitState != CharState_None)
            return true;

        if (Settings::game().mPlayerMovementIgnoresAnimation && mPtr == getPlayer())
            return false;

        if (mInJump)
            return false;

        bool movementAnimationControlled = mIdleState != CharState_None;
        if (mMovementState != CharState_None)
            movementAnimationControlled = mMovementAnimationHasMovement;
        return movementAnimationControlled;
    }

    void CharacterController::clearAnimQueue(bool clearScriptedAnims)
    {
        // Do not interrupt scripted animations, if we want to keep them
        if (mAnimation && (!isScriptedAnimPlaying() || clearScriptedAnims) && !mAnimQueue.empty())
            mAnimation->disable(mAnimQueue.front().mGroup);

        if (clearScriptedAnims)
        {
            if (mAnimation)
                setPlayScriptedOnly(false);
            mAnimQueue.clear();
            return;
        }

        std::erase_if(mAnimQueue, [](const AnimationQueueEntry& entry) { return !entry.mScripted; });
    }

    void CharacterController::forceStateUpdate(MWAnim::Object* obj)
    {
        const bool newAnimation = obj->animation.get() != mAnimation;
        mAnimation = obj->animation.get();
        // A rebuilt model (werewolf, vampire, camera reattachment) brings new bones: the pose driver and the IK
        // controller hold pointers into the old ones, so they are rebuilt too.
        if (newAnimation && mActor)
            createPoseControllers();

        clearAnimQueue();

        // Make sure we canceled the current attack or spellcasting,
        // because we disabled attack animations anyway.
        mCanCast = false;
        mCastingScriptedSpell = false;
        setAttackingOrSpell(false);
        if (mUpperBodyState != UpperBodyState::None)
            mUpperBodyState = UpperBodyState::WeaponEquipped;

        refreshCurrentAnims(mIdleState, mMovementState, mJumpState, true);

        if (mDeathState != CharState_None)
        {
            playRandomDeath();
        }

        updateAnimQueue();

        mAnimation->runAnimation(0.f);
    }

    CharacterController::KillResult CharacterController::kill()
    {
        if (mDeathState == CharState_None)
        {
            playRandomDeath();
            resetCurrentIdleState();
            return Result_DeathAnimStarted;
        }

        MWMechanics::CreatureStats& cStats = mPtr.getClass().getCreatureStats(mPtr);
        if (isAnimPlaying(mCurrentDeath))
            return Result_DeathAnimPlaying;
        if (!cStats.isDeathAnimationFinished())
        {
            cStats.setDeathAnimationFinished(true);
            return Result_DeathAnimJustFinished;
        }
        return Result_DeathAnimFinished;
    }

    void CharacterController::resurrect()
    {
        if (mDeathState == CharState_None)
            return;

        resetCurrentDeathState();
        mWeaponType = ESM::Weapon::None;
    }

    void CharacterController::updateContinuousVfx() const
    {
        // Keeping track of when to stop a continuous VFX seems to be very difficult to do inside the spells code,
        // as it's extremely spread out (ActiveSpells, Spells, InventoryStore effects, etc...) so we do it here.

        if (!mObject)
            return;

        // Stop any effects that are no longer active
        std::vector<std::string> effects;
        MWAnim::getLoopingEffects(*mObject->node(), effects);

        for (std::string_view effectStr : effects)
        {
            auto effectId = ESM::RefId::deserializeText(effectStr);

            if (MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().search(effectId)
                && (mPtr.getClass().getCreatureStats(mPtr).isDeathAnimationFinished()
                    || mPtr.getClass()
                            .getCreatureStats(mPtr)
                            .getMagicEffects()
                            .getOrDefault(MWMechanics::EffectKey(effectId))
                            .getMagnitude()
                        <= 0))
                MWAnim::removeEffect(*mObject->node(), effectStr);
        }
    }

    void CharacterController::updateMagicEffects() const
    {
        if (!mActor)
            return;

        auto& stats = mPtr.getClass().getCreatureStats(mPtr);

        float light = stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Light).getMagnitude();
        light *= std::ceil(Constants::UnitsPerFoot) * /*arbitraryMultiplierToMakeCutoffLessObvious*/ 3;
        MWAnim::setLightEffect(*mObject, light);

        // If you're dead you don't care about whether you've started/stopped being a vampire or not
        if (stats.isDead())
            return;

        if (mNpc)
        {
            bool vampire = mPtr.getClass()
                               .getCreatureStats(mPtr)
                               .getMagicEffects()
                               .getOrDefault(ESM::MagicEffect::Vampirism)
                               .getMagnitude()
                > 0.0f;
            mNpc->setVampire(vampire);
        }
    }

    void CharacterController::setVisibility(float visibility) const
    {
        if (!mAnimation)
            return;
        // We should take actor's invisibility in account
        if (mPtr.getClass().isActor())
        {
            float alpha = 1.f;
            if (mPtr.getClass()
                    .getCreatureStats(mPtr)
                    .getMagicEffects()
                    .getOrDefault(ESM::MagicEffect::Invisibility)
                    .getModifier()) // Ignore base magnitude (see bug #3555).
            {
                if (mPtr == getPlayer())
                    alpha = 0.25f;
                else
                    alpha = 0.05f;
            }
            float chameleon = mPtr.getClass()
                                  .getCreatureStats(mPtr)
                                  .getMagicEffects()
                                  .getOrDefault(ESM::MagicEffect::Chameleon)
                                  .getMagnitude();
            if (chameleon)
            {
                alpha *= std::clamp(1.f - chameleon / 100.f, 0.25f, 0.75f);
            }

            visibility = std::min(visibility, alpha);
        }

        MWRender::setTransparency(*mObject, visibility);
    }

    std::string_view CharacterController::getMovementBasedAttackType() const
    {
        float* move = mPtr.getClass().getMovementSettings(mPtr).mPosition;
        if (std::abs(move[1]) > std::abs(move[0]) + 0.2f) // forward-backward
            return "thrust";
        if (std::abs(move[0]) > std::abs(move[1]) + 0.2f) // sideway
            return "slash";
        return "chop";
    }

    bool CharacterController::isRandomAttackAnimation(std::string_view group)
    {
        return (group == "attack1" || group == "swimattack1" || group == "attack2" || group == "swimattack2"
            || group == "attack3" || group == "swimattack3");
    }

    bool CharacterController::isAttackPreparing() const
    {
        return mUpperBodyState == UpperBodyState::AttackWindUp;
    }

    bool CharacterController::isCastingSpell() const
    {
        return mCastingScriptedSpell || mUpperBodyState == UpperBodyState::Casting;
    }

    bool CharacterController::isReadyToBlock() const
    {
        return mAnimation && updateCarriedLeftVisible(mWeaponType);
    }

    bool CharacterController::isKnockedDown() const
    {
        return mHitState == CharState_KnockDown || mHitState == CharState_SwimKnockDown;
    }

    bool CharacterController::isKnockedOut() const
    {
        return mHitState == CharState_KnockOut || mHitState == CharState_SwimKnockOut;
    }

    bool CharacterController::isTurning() const
    {
        return mMovementState == CharState_TurnLeft || mMovementState == CharState_TurnRight
            || mMovementState == CharState_SwimTurnLeft || mMovementState == CharState_SwimTurnRight
            || mMovementState == CharState_TurnLeft90 || mMovementState == CharState_TurnRight90;
    }

    bool CharacterController::isRecovery() const
    {
        return mHitState == CharState_Hit || mHitState == CharState_SwimHit;
    }

    bool CharacterController::isAttackingOrSpell() const
    {
        return mUpperBodyState != UpperBodyState::None && mUpperBodyState != UpperBodyState::WeaponEquipped;
    }

    bool CharacterController::isSneaking() const
    {
        return mIdleState == CharState_IdleSneak || mMovementState == CharState_SneakForward
            || mMovementState == CharState_SneakBack || mMovementState == CharState_SneakLeft
            || mMovementState == CharState_SneakRight;
    }

    bool CharacterController::isRunning() const
    {
        return mMovementState == CharState_RunForward || mMovementState == CharState_RunBack
            || mMovementState == CharState_RunLeft || mMovementState == CharState_RunRight
            || mMovementState == CharState_SwimRunForward || mMovementState == CharState_SwimRunBack
            || mMovementState == CharState_SwimRunLeft || mMovementState == CharState_SwimRunRight
            || mMovementState == CharState_RunStart || mMovementState == CharState_RunStop
            || mMovementState == CharState_RunSkid;
    }

    void CharacterController::setAttackingOrSpell(bool attackingOrSpell) const
    {
        mPtr.getClass().getCreatureStats(mPtr).setAttackingOrSpell(attackingOrSpell);
    }

    void CharacterController::showWeapons(bool show)
    {
        if (mWielding)
        {
            mWielding->show(MWAnim::Wielding::Wield::Weapon, show);
            // If we remove/hide weapon from player, we should reset attack animation as well
            if (/*mPtr == MWMechanics::getPlayer() &&*/ !show)
                mPtr.getClass().getCreatureStats(mPtr).setAttackingOrSpell(false);
        }
    }

    void CharacterController::showCarriedLeft(bool show)
    {
        if (mWielding)
            mWielding->show(MWAnim::Wielding::Wield::CarriedLeft, show);
    }

    void CharacterController::showAmmo(bool show)
    {
        if (mWielding)
            mWielding->show(MWAnim::Wielding::Wield::Ammo, show);
    }

    void CharacterController::releaseShot()
    {
        auto& inv = mPtr.getClass().getInventoryStore(mPtr);
        auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
            return;

        // The orientation of the launched projectile. Always the same as the actor orientation, even if the ArrowBone's
        // orientation dictates otherwise.
        vsg::quat vsgOrient = vsg::quat(mPtr.getRefData().getPosition().rot[0], vsg::vec3(-1, 0, 0))
            * vsg::quat(mPtr.getRefData().getPosition().rot[2], vsg::vec3(0, 0, -1));
        auto orient = vsg::dquat(vsgOrient.x, vsgOrient.y, vsgOrient.z, vsgOrient.w);

        applyFatigueLoss(mPtr, *weapon, mAttackStrength);

        if (getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass == ESM::WeaponType::Thrown)
        {
            auto path = mWielding->getWieldWorldTransform(MWAnim::Wielding::Wield::Weapon);
            if (path.empty())
                return;
            auto vsgLaunchPos = vsgUtil::computePosition(path);
            auto launchPos = vsg::vec3(vsgLaunchPos.x, vsgLaunchPos.y, vsgLaunchPos.z);

            throwWeapon(mPtr, *weapon, mAttackStrength, launchPos, orient);
            showWeapons(false);
            inv.remove(*weapon, 1);
        }
        else
        {
            auto ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammo == inv.end())
                return;

            auto path = mWielding->getWieldWorldTransform(MWAnim::Wielding::Wield::Ammo);
            if (path.empty())
                return;
            auto vsgLaunchPos = vsgUtil::computePosition(path);
            auto launchPos = vsg::vec3(vsgLaunchPos.x, vsgLaunchPos.y, vsgLaunchPos.z);
            shoot(mPtr, *ammo, *weapon, mAttackStrength, launchPos, orient);
            inv.remove(*ammo, 1);
        }
    }

    void CharacterController::castSpell(const ESM::RefId& spellId, bool scriptedSpell)
    {
        setAttackingOrSpell(true);
        mCastingScriptedSpell = scriptedSpell;
        ActionSpell action = ActionSpell(spellId);
        action.prepare(mPtr);
    }

    void CharacterController::setAIAttackType(std::string_view attackType)
    {
        mAttackType = attackType;
    }

    std::string_view CharacterController::getRandomAttackType()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        float random = Misc::Rng::rollProbability(world->getPrng());
        if (random >= 2 / 3.f)
            return "thrust";
        if (random >= 1 / 3.f)
            return "slash";
        return "chop";
    }

    bool CharacterController::readyToPrepareAttack() const
    {
        return (mHitState == CharState_None || mHitState == CharState_Block)
            && mUpperBodyState <= UpperBodyState::WeaponEquipped;
    }

    bool CharacterController::readyToStartAttack() const
    {
        if (mHitState != CharState_None && mHitState != CharState_Block)
            return false;

        return mUpperBodyState == UpperBodyState::WeaponEquipped;
    }

    float CharacterController::getAttackStrength() const
    {
        return mAttackStrength;
    }

    bool CharacterController::getAttackingOrSpell() const
    {
        return mPtr.getClass().getCreatureStats(mPtr).getAttackingOrSpell();
    }

    std::string_view CharacterController::getDesiredAttackType() const
    {
        return mPtr.getClass().getCreatureStats(mPtr).getAttackType();
    }

    void CharacterController::setHeadTrackTarget(const MWWorld::ConstPtr& target)
    {
        mHeadTrackTarget = target;
    }

    void CharacterController::playSwishSound() const
    {
        static ESM::RefId weaponSwish = ESM::RefId::stringRefId("Weapon Swish");
        const ESM::RefId* soundId = &weaponSwish;
        float volume = 0.98f + mAttackStrength * 0.02f;
        float pitch = 0.75f + mAttackStrength * 0.4f;

        const MWWorld::Class& cls = mPtr.getClass();
        if (cls.isNpc() && cls.getNpcStats(mPtr).isWerewolf())
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const MWWorld::ESMStore& store = world->getStore();
            const ESM::Sound* sound = store.get<ESM::Sound>().searchRandom("WolfSwing", world->getPrng());
            if (sound)
                soundId = &sound->mId;
        }

        if (!soundId->empty())
            MWBase::Environment::get().getSoundManager()->playSound3D(mPtr, *soundId, volume, pitch);
    }

    float CharacterController::getAnimationMovementDirection() const
    {
        if (!mAnimation)
            return 0.f;
        switch (mMovementState)
        {
            case CharState_RunLeft:
            case CharState_SneakLeft:
            case CharState_SwimWalkLeft:
            case CharState_SwimRunLeft:
            case CharState_WalkLeft:
                return Misc::VsgMath::PI_2f;
            case CharState_RunRight:
            case CharState_SneakRight:
            case CharState_SwimWalkRight:
            case CharState_SwimRunRight:
            case CharState_WalkRight:
                return -Misc::VsgMath::PI_2f;
            case CharState_RunForward:
            case CharState_SneakForward:
            case CharState_SwimRunForward:
            case CharState_SwimWalkForward:
            case CharState_WalkForward:
            case CharState_WalkStart:
            case CharState_RunStart:
            case CharState_WalkStop:
            case CharState_RunStop:
            case CharState_RunSkid:
                return mActor->legsYawRadians;
            case CharState_RunBack:
            case CharState_SneakBack:
            case CharState_SwimWalkBack:
            case CharState_SwimRunBack:
            case CharState_WalkBack:
                return mActor->legsYawRadians - Misc::VsgMath::PIf;
            default:
                return 0.0f;
        }
    }

    void CharacterController::updateHeadTracking(float duration)
    {
        if (!mActor)
            return;
        auto head = mActor->getHead();
        if (!head)
            return;

        float zAngleRadians = 0.f;
        float xAngleRadians = 0.f;

        if (!mHeadTrackTarget.isEmpty())
        {
            vsg::vec3 direction;
            if (auto* anim = MWBase::Environment::get().getWorld()->getAnimation(mHeadTrackTarget))
            {
                auto node = anim->headTransform();
                if (!node.empty())
                    direction
                        = vsgUtil::computePosition(node) - vsgUtil::computePosition(mActor->worldTransform(*head));
                else
                {
                    // no head node to look at, fall back to look at center of collision box
                    direction = MWBase::Environment::get().getWorld()->aimToTarget(mPtr, mHeadTrackTarget, false);
                }
            }
            Misc::VsgMath::normalizeInPlace(direction);

            auto actorDirection = MWAnim::forward(mPtr.getRefData().getPosition());

            zAngleRadians = std::atan2(actorDirection.x, actorDirection.y) - std::atan2(direction.x, direction.y);
            zAngleRadians = Misc::normalizeAngle(zAngleRadians - mActor->headYawRadians) + mActor->headYawRadians;
            zAngleRadians *= (1 - direction.z * direction.z);
            xAngleRadians = std::asin(direction.z);
        }

        const float xLimit = Misc::VsgMath::inDegrees(40.f);
        const float zLimit = Misc::VsgMath::inDegrees(30.f);
        const float zLimitOffset = mActor->upperBodyYawRadians;
        xAngleRadians = std::clamp(xAngleRadians, -xLimit, xLimit);
        zAngleRadians = std::clamp(zAngleRadians, -zLimit + zLimitOffset, zLimit + zLimitOffset);

        float factor = duration * 5;
        factor = std::min(factor, 1.f);
        xAngleRadians = (1.f - factor) * mActor->headPitchRadians + factor * xAngleRadians;
        zAngleRadians = (1.f - factor) * mActor->headYawRadians + factor * zAngleRadians;

        mActor->headPitchRadians = xAngleRadians;
        mActor->headYawRadians = zAngleRadians;
    }

    MWWorld::MovementDirectionFlags CharacterController::getSupportedMovementDirections() const
    {
        if (!mAnimation)
            return 0;
        MWWorld::MovementDirectionFlags result = 0;
        std::span<const std::string_view> prefixes;

        using namespace std::string_view_literals;
        // There are fallbacks in the CharacterController::refreshMovementAnims for certain animations. Arrays below
        // represent them.
        constexpr std::array all = { ""sv };
        constexpr std::array walk = { "walk"sv };
        constexpr std::array swimWalk = { "swimwalk"sv, "walk"sv };
        constexpr std::array sneak = { "sneak"sv };
        constexpr std::array run = { "run"sv, "walk"sv };
        constexpr std::array swimRun = { "swimrun"sv, "run"sv, "walk"sv };
        constexpr std::array swim = { "swim"sv };
        switch (mMovementState)
        {
            case CharState_None:
            case CharState_SpecialIdle:
            case CharState_Idle:
            case CharState_IdleSwim:
            case CharState_IdleSneak:
                prefixes = all;
                break;
            case CharState_WalkForward:
            case CharState_WalkBack:
            case CharState_WalkLeft:
            case CharState_WalkRight:
            case CharState_WalkStart:
            case CharState_WalkStop:
                prefixes = walk;
                break;
            case CharState_SwimWalkForward:
            case CharState_SwimWalkBack:
            case CharState_SwimWalkLeft:
            case CharState_SwimWalkRight:
                prefixes = swimWalk;
                break;
            case CharState_RunForward:
            case CharState_RunBack:
            case CharState_RunLeft:
            case CharState_RunRight:
            case CharState_RunStart:
            case CharState_RunStop:
            case CharState_RunSkid:
                prefixes = run;
                break;
            case CharState_TurnLeft90:
            case CharState_TurnRight90:
                prefixes = all;
                break;
            case CharState_SwimRunForward:
            case CharState_SwimRunBack:
            case CharState_SwimRunLeft:
            case CharState_SwimRunRight:
                prefixes = swimRun;
                break;
            case CharState_SneakForward:
            case CharState_SneakBack:
            case CharState_SneakLeft:
            case CharState_SneakRight:
                prefixes = sneak;
                break;
            case CharState_TurnLeft:
            case CharState_TurnRight:
                prefixes = all;
                break;
            case CharState_SwimTurnLeft:
            case CharState_SwimTurnRight:
                prefixes = swim;
                break;
            case CharState_Death1:
            case CharState_Death2:
            case CharState_Death3:
            case CharState_Death4:
            case CharState_Death5:
            case CharState_SwimDeath:
            case CharState_SwimDeathKnockDown:
            case CharState_SwimDeathKnockOut:
            case CharState_DeathKnockDown:
            case CharState_DeathKnockOut:
            case CharState_Hit:
            case CharState_SwimHit:
            case CharState_KnockDown:
            case CharState_KnockOut:
            case CharState_SwimKnockDown:
            case CharState_SwimKnockOut:
            case CharState_Block:
                prefixes = all;
                break;
        }

        auto addMovementDirections = [&result, &prefixes](const std::string& animation) {
            if (std::find_if(
                    prefixes.begin(), prefixes.end(), [&](std::string_view v) { return animation.starts_with(v); })
                == prefixes.end())
                return;
            else if (animation.ends_with("forward"))
                result |= MWWorld::MovementDirectionFlag_Forward;
            else if (animation.ends_with("back"))
                result |= MWWorld::MovementDirectionFlag_Back;
            else if (animation.ends_with("left"))
                result |= MWWorld::MovementDirectionFlag_Left;
            else if (animation.ends_with("right"))
                result |= MWWorld::MovementDirectionFlag_Right;
        };
        mAnimation->listAnimationGroups(addMovementDirections);

        return result;
    }

}
