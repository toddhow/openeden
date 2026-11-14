#include "game_skyrimse/package_brain.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        namespace sdkgame = openeden::sdk::game;

        /// The longest frame time one update takes into account (a stall must not run a pause out at once).
        constexpr float kMaxStepSeconds = 1.f;
        constexpr double kTwoPi = 6.283185307179586;

        std::uint64_t splitmix(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        double distance2d(double ax, double ay, double bx, double by)
        {
            return std::hypot(ax - bx, ay - by);
        }

        /// Where an enter clip's motion starts, and the heading it starts with, for a seat at (x, y) facing `heading`:
        /// the motion ends on the seat with the seat's heading. The motion is in the actor's frame at the start of the
        /// clip (x to its right, y forward); a counter-clockwise yaw (Havok) is a clockwise change of the engine's heading.
        void enterStart(const ClipMotion& motion, double seatX, double seatY, float seatHeading, double& startX,
            double& startY, float& startHeading)
        {
            double dx = 0, dy = 0, yaw = 0;
            motion.total(dx, dy, yaw);
            const double hs = static_cast<double>(seatHeading) + yaw;
            startX = seatX - (std::cos(hs) * dx + std::sin(hs) * dy);
            startY = seatY - (-std::sin(hs) * dx + std::cos(hs) * dy);
            startHeading = static_cast<float>(hs);
        }

        /// The position and heading `time` seconds into a motion that starts at (x, y) facing `heading`.
        void motionAt(const ClipMotion& motion, float time, double x, double y, double heading, double& outX,
            double& outY, float& outHeading)
        {
            double dx = 0, dy = 0, yaw = 0;
            motion.sample(time, dx, dy, yaw);
            outX = x + std::cos(heading) * dx + std::sin(heading) * dy;
            outY = y - std::sin(heading) * dx + std::cos(heading) * dy;
            outHeading = static_cast<float>(heading - yaw);
        }
    }

    sdkgame::ActorSpeed packageSpeed(const PackageRecord& package)
    {
        if ((package.mFlags & kPreferredSpeedFlag) != 0 && (package.mPreferredSpeed == 1 || package.mPreferredSpeed == 2))
            return sdkgame::ActorSpeed::Run;
        return sdkgame::ActorSpeed::Walk;
    }

    void PackageBrain::onConversation(sdkgame::WorldId reference, bool begins)
    {
        const auto it = mActors.find(reference);
        if (it == mActors.end())
            return;
        Actor& actor = it->second;
        // The game's simulation (and so update()) does not run while the conversation box is open, so the begin and the
        // end of one conversation can arrive before either was acted on: keep their order.
        if (begins)
        {
            actor.mTalkRequest = 1;
            actor.mTalkEndPending = false;
        }
        else if (actor.mTalkRequest == 1)
            actor.mTalkEndPending = true;
        else
            actor.mTalkRequest = 2;
    }

    int weekdayOfDay(std::int32_t day)
    {
        const std::int64_t d = static_cast<std::int64_t>(day);
        return static_cast<int>((((d + 1) % 7) + 7) % 7);
    }

    bool scheduleActive(const PackageSchedule& schedule, float hour, std::int32_t day, bool clockValid)
    {
        if (!clockValid)
            return true;
        if (schedule.mDayOfWeek != -1)
        {
            const int wd = weekdayOfDay(day);
            const int filter = schedule.mDayOfWeek;
            bool pass = false;
            if (filter >= 0 && filter <= 6)
                pass = wd == filter;
            else if (filter == 7)
                pass = wd >= 1 && wd <= 5;
            else if (filter == 8)
                pass = wd == 0 || wd == 6;
            else if (filter == 9)
                pass = wd == 1 || wd == 3 || wd == 5;
            else if (filter == 10)
                pass = wd == 2 || wd == 4;
            if (!pass)
                return false;
        }
        if (schedule.mHour == -1)
            return true;
        if (schedule.mHour < 0 || schedule.mHour > 23 || schedule.mMinute > 59)
            return false;
        if (!std::isfinite(hour))
            return false;
        const int start = schedule.mHour * 60 + std::max<int>(schedule.mMinute, 0);
        const int duration = schedule.mDurationMinutes <= 0 ? 60 : schedule.mDurationMinutes;
        if (duration >= 1440)
            return true;
        const double now = static_cast<double>(hour) * 60.0;
        double delta = std::fmod(now - start, 1440.0);
        if (delta < 0.0)
            delta += 1440.0;
        return delta < duration;
    }

    const PackageRecord* selectPackage(const std::vector<const PackageRecord*>& packages, float hour,
        std::int32_t day, bool clockValid, PackageSource& source, FormId actorBase, FormId actorReference)
    {
        for (const PackageRecord* p : packages)
        {
            if (p == nullptr || !scheduleActive(p->mSchedule, hour, day, clockValid))
                continue;
            if (source.conditionsPass(*p, actorBase, actorReference))
                return p;
        }
        return nullptr;
    }

    PackageBrain::PackageBrain(PackageSource& source)
        : PackageBrain(source, Options{})
    {
    }

    PackageBrain::PackageBrain(PackageSource& source, Options options)
        : mSource(source)
        , mOptions(options)
    {
    }

    float PackageBrain::nextUnit(Actor& actor)
    {
        return static_cast<float>(static_cast<double>(splitmix(actor.mRng) >> 11) / 9007199254740992.0);
    }

    bool PackageBrain::wanders(const Actor& actor) const
    {
        return (actor.mBehavior == PackageBehavior::Sandbox || actor.mBehavior == PackageBehavior::Wander)
            && actor.mRadius >= kMinWanderRadius;
    }

    void PackageBrain::onActorAdded(const sdkgame::ActorInfo& info)
    {
        if (!info.handle.valid())
            return;
        Actor actor;
        actor.mHandle = info.handle;
        actor.mBase = static_cast<FormId>(info.base);
        actor.mHomeX = actor.mX = info.x;
        actor.mHomeY = actor.mY = info.y;
        actor.mHomeZ = actor.mZ = info.z;
        actor.mHeading = info.heading;
        actor.mRng = mOptions.mSeed ^ (static_cast<std::uint64_t>(info.handle.reference) * 0x9E3779B97F4A7C15ull);
        (void)splitmix(actor.mRng); // scramble the seed once
        mActors[info.handle.reference] = std::move(actor);
        ++mStats.mActorsAdded;
    }

    void PackageBrain::onActorRemoved(sdkgame::ActorHandle handle)
    {
        const auto it = mActors.find(handle.reference);
        if (it != mActors.end() && it->second.mHandle == handle)
        {
            releaseFurniture(it->second); // the core already forgot the actor: nothing to write
            mActors.erase(it);
        }
    }

    void PackageBrain::onGoalFinished(sdkgame::ActorHandle handle, sdkgame::GoalResult result)
    {
        const auto it = mActors.find(handle.reference);
        if (it == mActors.end() || it->second.mHandle != handle)
            return;
        Actor& actor = it->second;
        if (!actor.mGoalOut)
            return; // a result for a goal that was replaced
        actor.mGoalOut = false;
        if (actor.mToFurniture && actor.mHasSlot)
        {
            if (result == sdkgame::GoalResult::Arrived)
            {
                ++mStats.mArrived;
                actor.mX = actor.mSlot.mEntryX;
                actor.mY = actor.mSlot.mEntryY;
                actor.mZ = actor.mSlot.mEntryZ;
                actor.mUseNext = true; // the writer is not open here: written at the next update
                return;
            }
            if (result == sdkgame::GoalResult::NoPath)
                ++mStats.mNoPath;
            else
                ++mStats.mBlocked;
            if (mOptions.mSnapSleepers && actor.mBehavior == PackageBehavior::Sleep
                && actor.mSlot.mPose == sdkgame::FurniturePose::Sleep)
            {
                ++mStats.mSnapped;
                actor.mSkipEnter = true;
                actor.mUseNext = true; // the bed stays reserved; the use puts the actor on it
                return;
            }
            ++mStats.mFurnitureFailed;
            if (actor.mBadSlots.size() < kMaxBadSlots)
                actor.mBadSlots.push_back(actor.mSlot.mKey);
            releaseFurniture(actor);
            actor.mPhase = Phase::Failed;
            actor.mTimer = kFurnitureRetrySeconds;
            return;
        }
        if (actor.mToPatrol)
        {
            if (result == sdkgame::GoalResult::Arrived)
            {
                ++mStats.mArrived;
                actor.mX = actor.mGoalX;
                actor.mY = actor.mGoalY;
                actor.mZ = actor.mGoalZ;
                const float idle = mSource.patrolIdle(actor.mPatrolMarker);
                actor.mPhase = Phase::Patrolling;
                actor.mTimer = idle >= 0.f ? idle : kPatrolDefaultIdleSeconds;
                return;
            }
            if (result == sdkgame::GoalResult::NoPath)
                ++mStats.mNoPath;
            else
                ++mStats.mBlocked;
            actor.mPhase = Phase::Failed;
            actor.mTimer = kRetrySeconds;
            return;
        }
        if (actor.mToFollow)
        {
            if (result == sdkgame::GoalResult::Arrived)
                ++mStats.mArrived;
            else if (result == sdkgame::GoalResult::NoPath)
                ++mStats.mNoPath;
            else
                ++mStats.mBlocked;
            actor.mPhase = Phase::Following;
            return;
        }
        const bool wasLeg = !actor.mToAnchor;
        if (result == sdkgame::GoalResult::Arrived)
        {
            ++mStats.mArrived;
            actor.mX = actor.mGoalX;
            actor.mY = actor.mGoalY;
            actor.mZ = actor.mGoalZ;
            if (wanders(actor))
            {
                actor.mPhase = Phase::Pausing;
                actor.mTimer = kPauseMinSeconds + nextUnit(actor) * (kPauseMaxSeconds - kPauseMinSeconds);
                actor.mFaceHeading = static_cast<float>((nextUnit(actor) - 0.5) * kTwoPi);
                actor.mFaceNext = true;
            }
            else
                actor.mPhase = Phase::Done;
            return;
        }
        if (result == sdkgame::GoalResult::NoPath)
            ++mStats.mNoPath;
        else
            ++mStats.mBlocked;
        if (wanders(actor) && wasLeg)
        {
            actor.mPhase = Phase::Pausing;
            actor.mTimer = kFailurePauseSeconds;
        }
        else
        {
            actor.mPhase = Phase::Failed;
            actor.mTimer = kRetrySeconds;
        }
    }

    void PackageBrain::goTo(
        Actor& actor, double x, double y, double z, sdkgame::ActorRequestWriter& writer, sdkgame::ActorSpeed speed)
    {
        const sdkgame::ActorGoal goal{ x, y, z, speed };
        if (!writer.goTo(actor.mHandle, goal))
        {
            ++mStats.mWritesRefused;
            actor.mGoalOut = false;
            actor.mPhase = Phase::Failed;
            actor.mTimer = kRetrySeconds;
            return;
        }
        if (speed == sdkgame::ActorSpeed::Run)
            ++mStats.mRuns;
        actor.mGoalOut = true;
        actor.mGoalX = x;
        actor.mGoalY = y;
        actor.mGoalZ = z;
        actor.mPhase = Phase::Walking;
        ++mStats.mGoalsIssued;
    }

    bool PackageBrain::sitAllowed(const Actor& actor) const
    {
        if (actor.mActive == nullptr || mFurniture == nullptr)
            return false;
        if (actor.mBehavior != PackageBehavior::Sandbox && actor.mBehavior != PackageBehavior::Wander)
            return false;
        const std::vector<std::uint8_t>& bools = actor.mActive->mBools;
        return bools.size() > SandboxBool::Sitting && bools[SandboxBool::Sitting] != 0;
    }

    void PackageBrain::releaseFurniture(Actor& actor)
    {
        if (actor.mHasSlot)
        {
            const auto it = mReserved.find(actor.mSlot.mKey);
            if (it != mReserved.end() && it->second == actor.mHandle.reference)
                mReserved.erase(it);
        }
        actor.mHasSlot = false;
        actor.mToFurniture = false;
        actor.mUseNext = false;
        actor.mSeated = false;
        actor.mSeatTimer = -1.f;
        actor.mEntering = false;
        actor.mExiting = false;
        actor.mClipClock = 0.f;
        actor.mSkipEnter = false;
    }

    bool PackageBrain::standUp(Actor& actor, sdkgame::ActorRequestWriter& writer, bool quick)
    {
        if (actor.mSeated && actor.mHasSlot)
        {
            const FurnitureSlot& slot = actor.mSlot;
            const bool useQuick = quick && slot.mQuickExitMotion != nullptr && !slot.mQuickExitClip.empty();
            const ClipMotion* motion = useQuick ? slot.mQuickExitMotion : slot.mExitMotion;
            const std::string& clipName = useQuick ? slot.mQuickExitClip : slot.mExitClip;
            if (motion != nullptr && !clipName.empty() && !actor.mEntering && !actor.mExiting)
            {
                const sdkgame::FurnitureUse use{ slot.mX, slot.mY, slot.mZ, slot.mHeading, slot.mPose, clipName,
                    slot.mClipPerSex };
                if (writer.useFurniture(actor.mHandle, use))
                {
                    actor.mExiting = true;
                    actor.mExitMotion = motion;
                    actor.mClipClock = 0.f;
                    float heading = 0.f;
                    motionAt(*motion, motion->mDuration, slot.mX, slot.mY, slot.mHeading,
                        actor.mExitX, actor.mExitY, heading);
                    actor.mExitHeading = heading;
                    actor.mExitZ = slot.mZ;
                    return false;
                }
                ++mStats.mWritesRefused;
            }
            if (!writer.leaveFurniture(actor.mHandle, actor.mSlot.mEntryX, actor.mSlot.mEntryY, actor.mSlot.mEntryZ))
                ++mStats.mWritesRefused;
            actor.mX = actor.mSlot.mEntryX;
            actor.mY = actor.mSlot.mEntryY;
            actor.mZ = actor.mSlot.mEntryZ;
            ++mStats.mStandUps;
        }
        releaseFurniture(actor);
        return true;
    }

    void PackageBrain::finishExit(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        if (!writer.leaveFurniture(actor.mHandle, actor.mExitX, actor.mExitY, actor.mExitZ))
            ++mStats.mWritesRefused;
        actor.mX = actor.mExitX;
        actor.mY = actor.mExitY;
        actor.mZ = actor.mExitZ;
        ++mStats.mStandUps;
        ++mStats.mExits;
        releaseFurniture(actor);
        if (actor.mHasDeferred)
        {
            const PackageRecord* next = actor.mDeferred;
            actor.mDeferred = nullptr;
            actor.mHasDeferred = false;
            start(actor, next, writer);
            return;
        }
        if (actor.mWoken && !actor.mTalking && actor.mActive != nullptr)
        {
            // The conversation ended while the sleeper was getting up: back to its package.
            actor.mWoken = false;
            ++mStats.mResumes;
            begin(actor, writer);
        }
        else if (actor.mTalking)
            actor.mPhase = Phase::Done;
        else if (wanders(actor))
        {
            actor.mPhase = Phase::Pausing;
            actor.mTimer = 2.f;
        }
        else
            actor.mPhase = Phase::Done;
    }

    bool PackageBrain::tryFurniture(
        Actor& actor, sdkgame::FurniturePose pose, double radius, sdkgame::ActorRequestWriter& writer)
    {
        if (mFurniture == nullptr)
            return false;
        mFurniture->slotsNear(actor.mHandle.reference, actor.mAnchorX, actor.mAnchorY, actor.mAnchorZ, radius, pose,
            actor.mX, actor.mY, actor.mZ, mScratch);
        for (const FurnitureSlot& slot : mScratch)
        {
            const auto held = mReserved.find(slot.mKey);
            if (held != mReserved.end() && held->second != actor.mHandle.reference)
                continue;
            if (std::find(actor.mBadSlots.begin(), actor.mBadSlots.end(), slot.mKey) != actor.mBadSlots.end())
                continue;
            actor.mSlot = slot;
            actor.mHasSlot = true;
            actor.mToFurniture = true;
            actor.mToAnchor = false;
            actor.mToPatrol = false;
            actor.mToFollow = false;
            mReserved[slot.mKey] = actor.mHandle.reference;
            ++mStats.mFurnitureTrips;
            if (slot.mEnterMotion != nullptr && !slot.mEnterClip.empty())
            {
                enterStart(*slot.mEnterMotion, slot.mX, slot.mY, slot.mHeading, actor.mStartX, actor.mStartY,
                    actor.mStartHeading);
                actor.mStartZ = slot.mZ;
                goTo(actor, actor.mStartX, actor.mStartY, actor.mStartZ, writer);
            }
            else
                goTo(actor, slot.mEntryX, slot.mEntryY, slot.mEntryZ, writer);
            if (actor.mPhase == Phase::Walking)
                actor.mPhase = Phase::Approaching;
            else
            {
                ++mStats.mFurnitureFailed;
                releaseFurniture(actor); // the core refused the goal; the phase is Failed
            }
            return true;
        }
        ++mStats.mNoFurniture;
        return false;
    }

    void PackageBrain::beginPatrol(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        FormId marker = actor.mPatrolMarker;
        if (marker == kNullFormId)
        {
            PackageTarget target;
            if (actor.mActive != nullptr && mSource.targetOf(*actor.mActive, target))
            {
                if (target.mType == PackageTargetType::SpecificReference)
                    marker = static_cast<FormId>(target.mValue);
                else if (target.mType == PackageTargetType::LinkedReference)
                {
                    FormId link = kNullFormId;
                    if (mSource.linkedRef(actor.mHandle.reference, static_cast<FormId>(target.mValue), link))
                        marker = link;
                }
            }
        }
        double x = 0, y = 0, z = 0;
        if (marker != kNullFormId && marker != kUnresolvedFormId && mSource.locate(marker, x, y, z))
        {
            if (actor.mPatrolFirst == kNullFormId)
                actor.mPatrolFirst = marker;
            actor.mPatrolMarker = marker;
            actor.mToAnchor = false;
            actor.mToPatrol = true;
            goTo(actor, x, y, z, writer, actor.mSpeed);
            if (actor.mPhase == Phase::Walking)
                ++mStats.mPatrolLegs;
            return;
        }
        ++mStats.mPatrolUnresolved;
        actor.mPhase = Phase::Failed;
        actor.mTimer = kRetrySeconds;
    }

    void PackageBrain::patrolNext(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        FormId next = kNullFormId;
        if (!mSource.linkedRef(actor.mPatrolMarker, kNullFormId, next) || next == kNullFormId || next == kUnresolvedFormId)
        {
            if (actor.mPatrolMarker == actor.mPatrolFirst)
            {
                // A chain of one marker (or one that cannot be followed from its first): wait here again.
                const float idle = mSource.patrolIdle(actor.mPatrolMarker);
                actor.mTimer = idle >= 0.f ? idle : kPatrolDefaultIdleSeconds;
                return;
            }
            next = actor.mPatrolFirst; // the end of a chain: back to its start
        }
        actor.mPatrolMarker = next;
        double x = 0, y = 0, z = 0;
        if (!mSource.locate(next, x, y, z))
        {
            actor.mPhase = Phase::Failed;
            actor.mTimer = kRetrySeconds;
            return;
        }
        actor.mToPatrol = true;
        goTo(actor, x, y, z, writer, actor.mSpeed);
        if (actor.mPhase == Phase::Walking)
            ++mStats.mPatrolLegs;
    }

    void PackageBrain::beginFollow(Actor& actor, sdkgame::ActorRequestWriter&)
    {
        PackageTarget target;
        if (actor.mActive != nullptr && mSource.targetOf(*actor.mActive, target)
            && target.mType == PackageTargetType::SpecificReference && target.mValue != kNullFormId
            && target.mValue != kUnresolvedFormId && target.mValue != actor.mHandle.reference
            && mActors.count(static_cast<sdkgame::WorldId>(target.mValue)) != 0)
        {
            actor.mFollowTarget = static_cast<FormId>(target.mValue);
            actor.mFollowTimer = 0.f;
            actor.mToFollow = false;
            actor.mPhase = Phase::Following;
            return;
        }
        ++mStats.mFollowUnresolved;
        actor.mPhase = Phase::Done;
    }

    void PackageBrain::runFollow(Actor& actor, float seconds, sdkgame::ActorRequestWriter& writer)
    {
        actor.mFollowTimer -= seconds;
        if (actor.mFollowTimer > 0.f || actor.mGoalOut)
            return;
        actor.mFollowTimer = kFollowRepathSeconds;
        const auto found = mActors.find(static_cast<sdkgame::WorldId>(actor.mFollowTarget));
        if (found == mActors.end())
            return;
        const Actor& target = found->second;
        double tx = target.mX, ty = target.mY, tz = target.mZ;
        (void)writer.actorPosition(target.mHandle, tx, ty, tz);
        double sx = actor.mX, sy = actor.mY, sz = actor.mZ;
        (void)writer.actorPosition(actor.mHandle, sx, sy, sz);
        actor.mX = sx;
        actor.mY = sy;
        actor.mZ = sz;
        const double d = distance2d(tx, ty, sx, sy);
        if (d <= kFollowDistance)
            return;
        const double ux = (sx - tx) / d;
        const double uy = (sy - ty) / d;
        actor.mToAnchor = false;
        actor.mToFollow = true;
        goTo(actor, tx + ux * kFollowStandOff, ty + uy * kFollowStandOff, tz, writer, actor.mSpeed);
        if (actor.mPhase == Phase::Walking)
        {
            ++mStats.mFollowLegs;
            actor.mPhase = Phase::Following;
        }
    }

    void PackageBrain::handleTalk(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        const int request = actor.mTalkRequest;
        const bool endToo = actor.mTalkEndPending;
        actor.mTalkRequest = 0;
        actor.mTalkEndPending = false;
        if (request == 1)
        {
            ++mStats.mConversations;
            actor.mTalking = true;
            if (actor.mSeated)
            {
                if (actor.mSlot.mPose == sdkgame::FurniturePose::Sleep)
                {
                    ++mStats.mWakeUps;
                    actor.mWoken = true;
                    if (standUp(actor, writer, true))
                        actor.mPhase = Phase::Done;
                }
                // A sitter stays where it is.
                if (endToo)
                {
                    actor.mTalkRequest = 2;
                    handleTalk(actor, writer);
                }
                return;
            }
            if (actor.mGoalOut)
            {
                if (!writer.cancelGoal(actor.mHandle))
                    ++mStats.mWritesRefused;
                actor.mGoalOut = false;
            }
            if (actor.mHasSlot)
                releaseFurniture(actor);
            actor.mWoken = true; // takes its package up again afterwards
            if (endToo)
            {
                actor.mTalkRequest = 2;
                handleTalk(actor, writer);
            }
            return;
        }
        if (request == 2 && actor.mTalking)
        {
            actor.mTalking = false;
            if (actor.mWoken && !actor.mSeated && !actor.mExiting)
            {
                actor.mWoken = false;
                if (actor.mActive != nullptr)
                {
                    ++mStats.mResumes;
                    begin(actor, writer);
                }
            }
            // A sleeper still getting up resumes when the get-up ends (finishExit looks at mWoken).
        }
    }

    void PackageBrain::runClip(Actor& actor, float seconds, sdkgame::ActorRequestWriter& writer)
    {
        const FurnitureSlot& slot = actor.mSlot;
        actor.mClipClock += seconds;
        if (actor.mEntering)
        {
            const ClipMotion& motion = *slot.mEnterMotion;
            double x = 0, y = 0;
            float heading = 0.f;
            motionAt(motion, actor.mClipClock, actor.mStartX, actor.mStartY, actor.mStartHeading, x, y, heading);
            if (actor.mClipClock < motion.mDuration)
            {
                if (!writer.placeFurniture(actor.mHandle, x, y, actor.mStartZ, heading))
                    ++mStats.mWritesRefused;
                return;
            }
            const sdkgame::FurnitureUse use{ slot.mX, slot.mY, slot.mZ, slot.mHeading, slot.mPose, slot.mClip,
                slot.mClipPerSex };
            if (!writer.useFurniture(actor.mHandle, use))
                ++mStats.mWritesRefused;
            actor.mEntering = false;
            actor.mX = slot.mX;
            actor.mY = slot.mY;
            actor.mZ = slot.mZ;
            return;
        }
        if (actor.mExiting)
        {
            const ClipMotion& motion = *actor.mExitMotion;
            double x = 0, y = 0;
            float heading = 0.f;
            motionAt(motion, actor.mClipClock, slot.mX, slot.mY, slot.mHeading, x, y, heading);
            if (actor.mClipClock < motion.mDuration)
            {
                if (!writer.placeFurniture(actor.mHandle, x, y, slot.mZ, heading))
                    ++mStats.mWritesRefused;
                return;
            }
            if (!writer.placeFurniture(actor.mHandle, actor.mExitX, actor.mExitY, actor.mExitZ, actor.mExitHeading))
                ++mStats.mWritesRefused;
            finishExit(actor, writer);
        }
    }

    void PackageBrain::begin(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        if (actor.mActivity == PackageActivity::Patrol)
        {
            beginPatrol(actor, writer);
            return;
        }
        if (actor.mActivity == PackageActivity::Follow)
        {
            beginFollow(actor, writer);
            return;
        }
        if (mFurniture != nullptr && (actor.mBehavior == PackageBehavior::Sleep || actor.mBehavior == PackageBehavior::Eat))
        {
            const sdkgame::FurniturePose pose
                = actor.mBehavior == PackageBehavior::Sleep ? sdkgame::FurniturePose::Sleep : sdkgame::FurniturePose::Sit;
            if (tryFurniture(actor, pose, std::max(actor.mRadius, kMinFurnitureRadius), writer))
                return;
            // A sleeper with no bed in range looks in the whole neighbourhood before it gives up (a package with a small
            // radius, such as a squatter's, still means "go to bed").
            if (actor.mBehavior == PackageBehavior::Sleep && std::max(actor.mRadius, kMinFurnitureRadius) < kMaxPackageRadius
                && tryFurniture(actor, pose, kMaxPackageRadius, writer))
                return;
        }
        const double away = distance2d(actor.mX, actor.mY, actor.mAnchorX, actor.mAnchorY);
        if (wanders(actor))
        {
            if (away > actor.mRadius + kArriveTolerance)
            {
                actor.mToAnchor = true;
                goTo(actor, actor.mAnchorX, actor.mAnchorY, actor.mAnchorZ, writer, actor.mSpeed);
            }
            else
            {
                actor.mPhase = Phase::Pausing;
                actor.mTimer = kPauseMinSeconds + nextUnit(actor) * (kPauseMaxSeconds - kPauseMinSeconds);
            }
            return;
        }
        const double tolerance
            = actor.mBehavior == PackageBehavior::Travel ? kArriveTolerance : std::max<double>(actor.mRadius, kArriveTolerance);
        if (away > tolerance)
        {
            actor.mToAnchor = true;
            goTo(actor, actor.mAnchorX, actor.mAnchorY, actor.mAnchorZ, writer, actor.mSpeed);
        }
        else
            actor.mPhase = Phase::Done;
    }

    void PackageBrain::start(Actor& actor, const PackageRecord* package, sdkgame::ActorRequestWriter& writer)
    {
        if (actor.mSeated && !actor.mExiting)
        {
            if (!standUp(actor, writer))
            {
                // The get-up clip plays first: the next package starts when it ends (finishExit).
                actor.mDeferred = package;
                actor.mHasDeferred = true;
                if (actor.mGoalOut)
                {
                    if (!writer.cancelGoal(actor.mHandle))
                        ++mStats.mWritesRefused;
                    actor.mGoalOut = false;
                }
                return;
            }
        }
        else if (actor.mHasSlot)
            releaseFurniture(actor);
        ++mStats.mSwitches;
        actor.mBadSlots.clear();
        if (actor.mGoalOut)
        {
            if (!writer.cancelGoal(actor.mHandle))
                ++mStats.mWritesRefused;
            actor.mGoalOut = false;
        }
        actor.mActive = package;
        actor.mToAnchor = false;
        if (package == nullptr)
        {
            actor.mBehavior = PackageBehavior::Stand;
            actor.mPhase = Phase::Idle;
            return;
        }
        actor.mBehavior = mSource.behaviorOf(*package);
        actor.mActivity = PackageActivity::None;
        actor.mSpeed = packageSpeed(*package);
        actor.mToPatrol = false;
        actor.mToFollow = false;
        actor.mPatrolFirst = kNullFormId;
        actor.mPatrolMarker = kNullFormId;
        actor.mFollowTarget = kNullFormId;
        if (actor.mBehavior == PackageBehavior::Stand || actor.mBehavior == PackageBehavior::Unsupported)
            actor.mActivity = mSource.activityOf(*package);
        if (actor.mActivity == PackageActivity::Escort)
            ++mStats.mEscortsSkipped;
        if (actor.mBehavior == PackageBehavior::Unsupported && actor.mActivity == PackageActivity::None)
            ++mStats.mUnsupported;
        const bool sandboxLike = actor.mBehavior == PackageBehavior::Sandbox || actor.mBehavior == PackageBehavior::Wander;
        actor.mAnchorX = actor.mHomeX;
        actor.mAnchorY = actor.mHomeY;
        actor.mAnchorZ = actor.mHomeZ;
        actor.mRadius = sandboxLike ? kDefaultSandboxRadius : 0.f;
        if (!package->mLocations.empty())
        {
            const PackageLocation& loc = package->mLocations.front();
            bool placed = true;
            switch (loc.mType)
            {
                case PackageLocationType::NearReference:
                {
                    double x = 0, y = 0, z = 0;
                    if (loc.mValue != kNullFormId && loc.mValue != kUnresolvedFormId
                        && mSource.locate(static_cast<FormId>(loc.mValue), x, y, z))
                    {
                        actor.mAnchorX = x;
                        actor.mAnchorY = y;
                        actor.mAnchorZ = z;
                    }
                    else
                        placed = false;
                    break;
                }
                case PackageLocationType::NearEditorLocation:
                    break;
                case PackageLocationType::NearLinkedReference:
                {
                    // Stage S6f: the reference the ACTOR is linked to with this keyword.
                    FormId link = kNullFormId;
                    double x = 0, y = 0, z = 0;
                    if (loc.mValue != kUnresolvedFormId
                        && mSource.linkedRef(actor.mHandle.reference, static_cast<FormId>(loc.mValue), link)
                        && link != kNullFormId && link != kUnresolvedFormId && mSource.locate(link, x, y, z))
                    {
                        actor.mAnchorX = x;
                        actor.mAnchorY = y;
                        actor.mAnchorZ = z;
                    }
                    else
                        placed = false;
                    break;
                }
                case PackageLocationType::NearPackageStart:
                case PackageLocationType::NearSelf:
                    actor.mAnchorX = actor.mX;
                    actor.mAnchorY = actor.mY;
                    actor.mAnchorZ = actor.mZ;
                    break;
                default:
                    placed = false; // in cell, linked reference, alias, object id/type, unknown: not placeable yet
                    break;
            }
            if (!placed)
                ++mStats.mUnresolvedLocations;
            const auto radius = static_cast<double>(loc.mRadius);
            actor.mRadius = static_cast<float>(std::clamp(radius, 0.0, static_cast<double>(kMaxPackageRadius)));
        }
        begin(actor, writer);
    }

    void PackageBrain::evaluate(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        ++mStats.mEvaluations;
        const sdkgame::ActorClock clock = writer.clock();
        const PackageRecord* chosen = selectPackage(actor.mPackages, clock.hour, clock.day, clock.valid, mSource,
            actor.mBase, static_cast<FormId>(actor.mHandle.reference));
        if (chosen == nullptr)
        {
            ++mStats.mNoPackage;
            if (actor.mPhase == Phase::Waiting)
                actor.mPhase = Phase::Idle;
        }
        if (chosen != actor.mActive)
            start(actor, chosen, writer);
    }

    void PackageBrain::run(Actor& actor, float seconds, sdkgame::ActorRequestWriter& writer)
    {
        if (actor.mUseNext)
        {
            actor.mUseNext = false;
            const FurnitureSlot& slot = actor.mSlot;
            const bool clip = !actor.mSkipEnter && slot.mEnterMotion != nullptr && !slot.mEnterClip.empty();
            actor.mSkipEnter = false;
            const sdkgame::FurnitureUse use = clip
                ? sdkgame::FurnitureUse{ actor.mStartX, actor.mStartY, actor.mStartZ, actor.mStartHeading, slot.mPose,
                    slot.mEnterClip, slot.mClipPerSex }
                : sdkgame::FurnitureUse{ slot.mX, slot.mY, slot.mZ, slot.mHeading, slot.mPose, slot.mClip,
                    slot.mClipPerSex };
            if (actor.mHasSlot && writer.useFurniture(actor.mHandle, use))
            {
                actor.mToFurniture = false;
                actor.mSeated = true;
                actor.mPhase = Phase::Seated;
                actor.mX = use.x;
                actor.mY = use.y;
                actor.mZ = use.z;
                if (clip)
                {
                    actor.mEntering = true;
                    actor.mClipClock = 0.f;
                    ++mStats.mEnters;
                }
                ++mStats.mSeatings;
                const bool forever = actor.mBehavior == PackageBehavior::Sleep || actor.mBehavior == PackageBehavior::Eat;
                actor.mSeatTimer = forever ? -1.f : kSitMinSeconds + nextUnit(actor) * (kSitMaxSeconds - kSitMinSeconds);
            }
            else
            {
                ++mStats.mWritesRefused;
                ++mStats.mFurnitureFailed;
                if (actor.mHasSlot && actor.mBadSlots.size() < kMaxBadSlots)
                    actor.mBadSlots.push_back(actor.mSlot.mKey);
                releaseFurniture(actor);
                actor.mPhase = Phase::Failed;
                actor.mTimer = kFurnitureRetrySeconds;
            }
            return;
        }
        if (actor.mEntering || actor.mExiting)
        {
            runClip(actor, seconds, writer);
            return;
        }
        if (actor.mTalking)
            return; // the actor waits while the player talks to it
        if (actor.mFaceNext)
        {
            actor.mFaceNext = false;
            if (!writer.setHeading(actor.mHandle, actor.mFaceHeading))
                ++mStats.mWritesRefused;
        }
        switch (actor.mPhase)
        {
            case Phase::Pausing:
            {
                actor.mTimer -= seconds;
                if (actor.mTimer > 0.f)
                    return;
                if (sitAllowed(actor) && nextUnit(actor) < kSitChance
                    && tryFurniture(actor, sdkgame::FurniturePose::Sit, std::max(actor.mRadius, kMinFurnitureRadius), writer))
                    return;
                const double angle = static_cast<double>(nextUnit(actor)) * kTwoPi;
                const double reach = static_cast<double>(actor.mRadius) * std::sqrt(static_cast<double>(nextUnit(actor)));
                actor.mToAnchor = false;
                goTo(actor, actor.mAnchorX + std::cos(angle) * reach, actor.mAnchorY + std::sin(angle) * reach,
                    actor.mAnchorZ, writer, actor.mSpeed);
                return;
            }
            case Phase::Patrolling:
            {
                actor.mTimer -= seconds;
                if (actor.mTimer > 0.f)
                    return;
                patrolNext(actor, writer);
                return;
            }
            case Phase::Following:
                runFollow(actor, seconds, writer);
                return;
            case Phase::Seated:
            {
                if (actor.mSeatTimer < 0.f)
                    return; // sleeping or eating: until the package ends
                actor.mSeatTimer -= seconds;
                if (actor.mSeatTimer > 0.f)
                    return;
                if (!standUp(actor, writer))
                    return; // the get-up clip is playing
                if (wanders(actor))
                {
                    actor.mPhase = Phase::Pausing;
                    actor.mTimer = 2.f;
                }
                else
                    actor.mPhase = Phase::Done;
                return;
            }
            case Phase::Failed:
            {
                actor.mTimer -= seconds;
                if (actor.mTimer > 0.f)
                    return;
                if (actor.mActive != nullptr)
                    begin(actor, writer);
                else
                    actor.mPhase = Phase::Idle;
                return;
            }
            default:
                return;
        }
    }

    void PackageBrain::update(float seconds, sdkgame::ActorRequestWriter& writer)
    {
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;
        seconds = std::min(seconds, kMaxStepSeconds);
        for (auto& [key, actor] : mActors)
        {
            if (!actor.mResolved)
            {
                if (!mSource.ready())
                    continue;
                actor.mResolved = true;
                if (!mSource.packagesFor(actor.mBase, actor.mPackages))
                    actor.mPackages.clear();
                actor.mEvaluatedOnce = false;
            }
            if (actor.mTalkRequest != 0)
                handleTalk(actor, writer);
            actor.mSinceEval += seconds;
            if (!actor.mTalking && !actor.mExiting
                && (!actor.mEvaluatedOnce || actor.mSinceEval >= mOptions.mReevaluateSeconds))
            {
                const bool first = !actor.mEvaluatedOnce;
                actor.mEvaluatedOnce = true;
                actor.mSinceEval = 0.f;
                if (first)
                    actor.mSinceEval = -nextUnit(actor) * mOptions.mReevaluateSeconds; // spread the actors over frames
                evaluate(actor, writer);
            }
            run(actor, seconds, writer);
        }
    }

    std::optional<PackageBrain::ActorView> PackageBrain::view(sdkgame::WorldId reference) const
    {
        const auto it = mActors.find(reference);
        if (it == mActors.end())
            return std::nullopt;
        const Actor& a = it->second;
        ActorView v;
        v.mActive = a.mActive;
        v.mBehavior = a.mBehavior;
        v.mPhase = a.mPhase;
        v.mX = a.mX;
        v.mY = a.mY;
        v.mZ = a.mZ;
        v.mAnchorX = a.mAnchorX;
        v.mAnchorY = a.mAnchorY;
        v.mAnchorZ = a.mAnchorZ;
        v.mRadius = a.mRadius;
        v.mPackages = a.mPackages.size();
        v.mSeated = a.mSeated;
        v.mSlotKey = a.mHasSlot ? a.mSlot.mKey : 0;
        v.mActivity = a.mActivity;
        v.mSpeed = a.mSpeed;
        v.mTalking = a.mTalking;
        v.mEntering = a.mEntering;
        v.mExiting = a.mExiting;
        v.mPatrolMarker = a.mPatrolMarker;
        return v;
    }
}
