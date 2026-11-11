#ifndef GAME_MWMECHANICS_CHARACTER_HPP
#define GAME_MWMECHANICS_CHARACTER_HPP

#include <components/misc/vsgmath.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

#include <components/esm3/loadweap.hpp>
#include <vsg/maths/vec3.h>

#include "../mwworld/movementdirection.hpp"
#include "../mwworld/ptr.hpp"

#include <components/animation/tags.hpp>
#include <components/mwanimation/groups.hpp>

namespace MWWorld
{
    class InventoryStore;
}
namespace MWSound
{
    class Sound;
}
namespace opas::style
{
    struct StyleProfile;
}

namespace MWAnim
{
    class Play;
    class Object;
    class Actor;
    class Wielding;
}
namespace MWRender
{
    class Npc;
    class Player;
    class ActorIkController;
    class ActorRagdollDriver;
    class ActorRagdollRest;
    class CreativeMotionPoseDriver;
    class IntentTracker;
    struct ActorState;
}
namespace MWMechanics
{

    struct Movement;
    class CreatureStats;

    enum Priority
    {
        Priority_Default,
        Priority_WeaponLowerBody,
        Priority_SneakIdleLowerBody,
        Priority_SwimIdle,
        Priority_Jump,
        Priority_Movement,
        Priority_Hit,
        Priority_Weapon,
        Priority_Block,
        Priority_Knockdown,
        Priority_Torch,
        Priority_Storm,
        Priority_Death,
        Priority_Scripted,

        Num_Priorities
    };

    enum CharacterState
    {
        CharState_None,

        CharState_SpecialIdle,
        CharState_Idle,
        CharState_IdleSwim,
        CharState_IdleSneak,

        CharState_WalkForward,
        CharState_WalkBack,
        CharState_WalkLeft,
        CharState_WalkRight,

        CharState_SwimWalkForward,
        CharState_SwimWalkBack,
        CharState_SwimWalkLeft,
        CharState_SwimWalkRight,

        CharState_RunForward,
        CharState_RunBack,
        CharState_RunLeft,
        CharState_RunRight,

        CharState_SwimRunForward,
        CharState_SwimRunBack,
        CharState_SwimRunLeft,
        CharState_SwimRunRight,

        CharState_SneakForward,
        CharState_SneakBack,
        CharState_SneakLeft,
        CharState_SneakRight,

        CharState_TurnLeft,
        CharState_TurnRight,
        CharState_SwimTurnLeft,
        CharState_SwimTurnRight,

        CharState_WalkStart,
        CharState_RunStart,
        CharState_WalkStop,
        CharState_RunStop,
        CharState_RunSkid,
        CharState_TurnLeft90,
        CharState_TurnRight90,

        CharState_Death1,
        CharState_Death2,
        CharState_Death3,
        CharState_Death4,
        CharState_Death5,
        CharState_SwimDeath,
        CharState_SwimDeathKnockDown,
        CharState_SwimDeathKnockOut,
        CharState_DeathKnockDown,
        CharState_DeathKnockOut,

        CharState_Hit,
        CharState_SwimHit,
        CharState_KnockDown,
        CharState_KnockOut,
        CharState_SwimKnockDown,
        CharState_SwimKnockOut,
        CharState_Block
    };

    inline bool isTransitionMovementState(CharacterState state)
    {
        return state == CharState_WalkStart || state == CharState_RunStart || state == CharState_WalkStop
            || state == CharState_RunStop || state == CharState_RunSkid || state == CharState_TurnLeft90
            || state == CharState_TurnRight90;
    }

    enum class UpperBodyState
    {
        None,
        Equipping,
        Unequipping,
        WeaponEquipped,
        AttackWindUp,
        AttackRelease,
        AttackEnd,
        Casting
    };

    enum JumpingState
    {
        JumpState_None,
        JumpState_InAir,
        JumpState_Landing
    };

    struct WeaponInfo;

    /// Whether the group loops on this animation: it has a loop start key, or vanilla treats it as looping.
    bool isLoopingAnimation(const MWAnim::Play& animation, std::string_view group);

    /*
     * Controls playing animations.
     */
    class CharacterController
    {
        // struct UpdateData {
        float scale = 1.f;
        float speed = 0.f;
        vsg::vec3 movement;
        vsg::vec3 moved;
        // Inertial locomotion (OpenEden), carried from preAnimation to postAnimation.
        bool inertialActive = false;
        int mTraceLocomotionPhase = 0; // InertialMotion::LocomotionPhase, set in update for the developer trace
        float smoothedSpeed = 0.f;
        // Task 0108: the mean root speed (units per second) of the movement cycle at the animation speed this frame,
        // set by preAnimation, and whether the physics actor was given a stride shape other than one.
        float mExpectedRootSpeed = 0.f;
        bool mStrideShapeSet = false;
        //}
        MWWorld::Ptr mPtr;
        MWWorld::Ptr mWeapon;
        MWAnim::Play* mAnimation;
        MWAnim::Object* mObject;
        MWAnim::Actor* mActor{};
        MWAnim::Wielding* mWielding{};
        MWRender::Npc* mNpc{};
        MWRender::Player* mPlayer{};
        // OpenEden's foot IK, procedural lean and compound hitbox driver (task 0011); actors only.
        std::unique_ptr<MWRender::ActorIkController> mIkController;
        // Task 0106: the dead NPC's skeleton drawn from its ragdoll ([Physics] creativemotion ragdolls, off by
        // default).
        std::unique_ptr<MWRender::ActorRagdollDriver> mRagdollDriver;
        // The 17 mapped bones in their rest pose, read when the pose controllers are made (setting on only); a death
        // starts the ragdoll in the pose the bones have then, relative to this.
        std::unique_ptr<MWRender::ActorRagdollRest> mRagdollRest;
        bool mSawAlive = false; // this controller saw the actor alive: only then does a death make a ragdoll
        bool mRagdollTried = false;
        // CreativeMotion's animation runtime (task 0007, on VSG in task 0011): with "creativemotion animation" it
        // poses the bones in place of MWAnim::Play's controllers; with only "... compare" it is compared with them.
        // Actors only; null when both settings are off.
        std::unique_ptr<MWRender::CreativeMotionPoseDriver> mPoseDriver;
        bool mPoseDriverApplies = false;
        // The style layer's per-actor choices (task 0053), set from the console on the main thread: a style name that
        // replaces the rules, and the layer switched off for this actor. The console only records its choice here and
        // sets mStyleDirty; runPoseDriver (on the thread that runs the driver) then hands it to the driver, so the
        // driver is never changed while it runs. mStyleSpeedMps is the smoothed ground speed that the animation
        // thread writes and the console reads; mStyleCadence is written there and read by update.
        std::string mStyleChoice;
        bool mStyleSwitchedOff = false;
        std::atomic<bool> mStyleDirty{ false };
        std::atomic<float> mStyleSpeedMps{ 0.f };
        // Task 0067, stage 1: with "creativemotion preset = natural" the actor's intent (starting, stopping, turn
        // rate) is worked out each frame and handed to the pose driver with the ActorState. Null otherwise.
        std::unique_ptr<MWRender::IntentTracker> mIntentTracker;
        float mIntentPreviousYaw = 0.f;
        bool mHasIntentPreviousYaw = false;
        // What preAnimation (main thread) knows about the actor's movement request, for runPoseDriver (an animation
        // thread, after preAnimation): preAnimation clears the movement settings before the animation runs, so they
        // cannot be read there (stage 1 review, finding 1).
        bool mIntentWantsToMove = false;
        float mIntentSpeedMps = 0.f; // the speed the actor moves at in its current mode (cls.getCurrentSpeed), m/s
        float mIntentMeasuredMps = -1.f; // inertial locomotion's smoothed speed in m/s; below 0 when not active
        bool mIntentStanceChanged = false;
        bool mIntentStanceKnown = false;
        bool mIntentLastSneak = false;
        bool mIntentLastRun = false;
        // The last intent, for the developer trace.
        struct TraceIntent
        {
            bool mValid = false;
            bool mWants = false;
            bool mStarting = false;
            bool mStopping = false;
            bool mStance = false;
            float mTurnRate = 0.f;
        };
        TraceIntent mTraceIntent;
        std::atomic<float> mStyleCadence{ 1.f };

        std::map<ESM::RefId, MWSound::Sound*> mPlayingSounds;
        struct AnimationQueueEntry
        {
            std::string mGroup;
            uint32_t mLoopCount;
            float mTime;
            bool mLooping;
            bool mScripted;
            std::string mStartKey;
            std::string mStopKey;
            float mSpeed;
        };
        typedef std::deque<AnimationQueueEntry> AnimationQueue;
        AnimationQueue mAnimQueue;
        bool mLuaAnimations{ false };

        CharacterState mIdleState{ CharState_None };
        std::string mCurrentIdle;

        CharacterState mMovementState{ CharState_None };
        std::string mCurrentMovement;
        float mMovementAnimSpeed{ 0.f };
        bool mAdjustMovementAnimSpeed{ false };
        bool mMovementAnimationHasMovement{ false };

        CharacterState mDeathState{ CharState_None };
        std::string mCurrentDeath;
        bool mFloatToSurface{ true };

        CharacterState mHitState{ CharState_None };
        std::string mCurrentHit;

        UpperBodyState mUpperBodyState{ UpperBodyState::None };
        bool mResetIdleOnAttackEnd{ false };

        JumpingState mJumpState{ JumpState_None };
        std::string mCurrentJump;
        bool mInJump{ false };

        int mWeaponType{ ESM::Weapon::None };
        std::string mCurrentWeapon;

        float mAttackStrength{ -1.f };
        bool mReadyToHit{ false };
        MWWorld::Ptr mAttackVictim;
        vsg::vec3 mAttackHitPos;
        bool mAttackSuccess{ false };

        bool mSkipAnim{ false };

        // counted for skill increase
        float mSecondsOfSwimming{ 0.f };
        float mSecondsOfRunning{ 0.f };

        MWWorld::ConstPtr mHeadTrackTarget;

        float mTurnAnimationThreshold{
            0.f
        }; // how long to continue playing turning animation after actor stopped turning

        std::string mAttackType; // slash, chop or thrust

        bool mCanCast{ false };

        bool mCastingScriptedSpell{ false };

        bool mIsMovingBackward{ false };
        vsg::vec2 mSmoothedSpeed;

        std::string_view getMovementBasedAttackType() const;

        void clearStateAnimation(std::string& anim) const;
        void resetCurrentJumpState();
        void resetCurrentMovementState();
        void resetCurrentIdleState();
        void resetCurrentHitState();
        void resetCurrentWeaponState();
        void resetCurrentDeathState();

        void refreshCurrentAnims(
            CharacterState idle, CharacterState movementState, JumpingState jump, bool force = false);
        void refreshHitRecoilAnims();
        void refreshJumpAnims(JumpingState jump, bool force = false);
        void refreshMovementAnims(CharacterState movementState, bool force = false);
        void refreshIdleAnims(CharacterState idle, bool force = false);

        bool updateWeaponState();
        void updateIdleStormState(bool inwater) const;

        std::string chooseRandomAttackAnimation() const;
        static bool isRandomAttackAnimation(std::string_view group);

        bool isMovementAnimationControlled() const;

        void updateAnimQueue();
        void playAnimQueue(bool useLoopStart = false);
        void setPlayScriptedOnly(bool playScriptedOnly) const;

        void updateHeadTracking(float duration);

        void updateMagicEffects() const;

        void playDeath(float startpoint, CharacterState death);
        CharacterState chooseRandomDeathState() const;
        void playRandomDeath(float startpoint = 0.0f);

        /// choose a random animation group with \a prefix and numeric suffix
        /// @param num if non-nullptr, the chosen animation number will be written here
        std::string chooseRandomGroup(const std::string& prefix, int* num = nullptr) const;

        bool updateCarriedLeftVisible(int weaptype) const;
        bool useShieldAnimations() const;

        std::string fallbackShortWeaponGroup(
            const std::string& baseGroupName, MWAnim::BlendMask* blendMask = nullptr) const;

        std::string_view getWeaponAnimation(int weaponType) const;
        std::string_view getWeaponShortGroup(int weaponType) const;

        bool getAttackingOrSpell() const;
        void setAttackingOrSpell(bool attackingOrSpell) const;

        std::string_view getDesiredAttackType() const;

        void prepareHit();
        void showWeapons(bool show);
        void showCarriedLeft(bool show);
        void showAmmo(bool show);
        void releaseShot();

        void unpersistAnimationState();

        void playBlendedAnimation(const std::string& groupname, const MWAnim::Priority& priority, int blendMask,
            bool autodisable, float speedmult, std::string_view start, std::string_view stop, float startpoint,
            uint32_t loops, bool loopfallback = false) const;

    public:
        CharacterController(const MWWorld::Ptr& ptr, MWAnim::Object* anim);
        virtual ~CharacterController();

        CharacterController(const CharacterController&) = delete;
        CharacterController(CharacterController&&) = delete;

        void detachAnimation();

        const MWWorld::Ptr& getPtr() const { return mPtr; }

        void handleTextKeys();
        void handleTextKey(std::string_view groupname, Anim::Tags::ConstIterator key, const Anim::Tags& map);

        // Be careful when to call this, see comment in Actors
        void updateContinuousVfx() const;

        void updatePtr(const MWWorld::Ptr& ptr);

        void update(float duration); // = {
        void preAnimation(float duration);
        void runAnimation(float duration); //.supportsUpdateThreads=true
        void runPoseDriver(float duration); //.supportsUpdateThreads=true
        /// Developer trace of "creativemotion animation debug", one log line per actor per frame.
        void traceAnimation(float duration);
        void updateStrideShape(float duration);
        /// Creates the IK controller and, when a CreativeMotion setting is on, the pose driver for mAnimation's bones.
        void createPoseControllers();
        /// Picks the actor's style (console choice, else the rules of task 0053) and gives it to the pose driver.
        void configureStyle();
        /// The style the rules or the console choice give this actor, or null (main thread: may read the VFS).
        const opas::style::StyleProfile* resolveStyleProfile();
        MWRender::ActorState currentStyleState() const;
        void postAnimation(float duration);
        /// Task 0106. Main thread, in postAnimation: starts the dead NPC's ragdoll once, and draws it every frame.
        void updateRagdoll();
        // }

        bool onOpen() const;
        void onClose() const;

        void persistAnimationState() const;
        bool playGroup(std::string_view groupname, int mode, uint32_t count, bool scripted = false);
        /// The cmStyle console command (task 0053): "" describes the actor's style, "on" and "off" switch the layer
        /// for this actor, anything else is the name of a style to use. Main thread.
        std::string styleCommand(std::string_view argument);
        bool playGroupLua(std::string_view groupname, float playSpeed, std::string_view startKey,
            std::string_view stopKey, uint32_t loops, bool forceLoop);
        void enableLuaAnimations(bool enable);
        void skipAnim();
        bool isAnimPlaying(std::string_view groupName) const;
        bool isScriptedAnimPlaying() const;
        /// Whether the group loops: it has a loop start key, or vanilla treats it as looping.
        bool isLoopingAnimation(std::string_view group) const;
        void clearAnimQueue(bool clearScriptedAnims = false);

        enum KillResult
        {
            Result_DeathAnimStarted,
            Result_DeathAnimPlaying,
            Result_DeathAnimJustFinished,
            Result_DeathAnimFinished
        };
        KillResult kill();

        void resurrect();
        bool isDead() const { return mDeathState != CharState_None; }

        void forceStateUpdate(MWAnim::Object* obj);

        bool isAttackPreparing() const;
        bool isCastingSpell() const;
        bool isReadyToBlock() const;
        bool isKnockedDown() const;
        bool isKnockedOut() const;
        bool isRecovery() const;
        bool isSneaking() const;
        bool isRunning() const;
        bool isTurning() const;
        bool isAttackingOrSpell() const;

        void setVisibility(float visibility) const;
        void castSpell(const ESM::RefId& spellId, bool scriptedSpell = false);
        void setAIAttackType(std::string_view attackType);
        static std::string_view getRandomAttackType();

        bool readyToPrepareAttack() const;
        bool readyToStartAttack() const;

        float calculateWindUp() const;

        float getAttackStrength() const;

        /// Make this character turn its head towards \a target. To turn off head tracking, pass an empty Ptr.
        void setHeadTrackTarget(const MWWorld::ConstPtr& target);

        void playSwishSound() const;

        float getAnimationMovementDirection() const;

        MWWorld::MovementDirectionFlags getSupportedMovementDirections() const;
    };
}

#endif
