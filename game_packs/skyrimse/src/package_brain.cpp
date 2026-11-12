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
            mActors.erase(it);
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

    void PackageBrain::goTo(Actor& actor, double x, double y, double z, sdkgame::ActorRequestWriter& writer)
    {
        const sdkgame::ActorGoal goal{ x, y, z, sdkgame::ActorSpeed::Walk };
        if (!writer.goTo(actor.mHandle, goal))
        {
            ++mStats.mWritesRefused;
            actor.mGoalOut = false;
            actor.mPhase = Phase::Failed;
            actor.mTimer = kRetrySeconds;
            return;
        }
        actor.mGoalOut = true;
        actor.mGoalX = x;
        actor.mGoalY = y;
        actor.mGoalZ = z;
        actor.mPhase = Phase::Walking;
        ++mStats.mGoalsIssued;
    }

    void PackageBrain::begin(Actor& actor, sdkgame::ActorRequestWriter& writer)
    {
        const double away = distance2d(actor.mX, actor.mY, actor.mAnchorX, actor.mAnchorY);
        if (wanders(actor))
        {
            if (away > actor.mRadius + kArriveTolerance)
            {
                actor.mToAnchor = true;
                goTo(actor, actor.mAnchorX, actor.mAnchorY, actor.mAnchorZ, writer);
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
            goTo(actor, actor.mAnchorX, actor.mAnchorY, actor.mAnchorZ, writer);
        }
        else
            actor.mPhase = Phase::Done;
    }

    void PackageBrain::start(Actor& actor, const PackageRecord* package, sdkgame::ActorRequestWriter& writer)
    {
        ++mStats.mSwitches;
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
        if (actor.mBehavior == PackageBehavior::Unsupported)
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
                const double angle = static_cast<double>(nextUnit(actor)) * kTwoPi;
                const double reach = static_cast<double>(actor.mRadius) * std::sqrt(static_cast<double>(nextUnit(actor)));
                actor.mToAnchor = false;
                goTo(actor, actor.mAnchorX + std::cos(angle) * reach, actor.mAnchorY + std::sin(angle) * reach,
                    actor.mAnchorZ, writer);
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
            actor.mSinceEval += seconds;
            if (!actor.mEvaluatedOnce || actor.mSinceEval >= mOptions.mReevaluateSeconds)
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
        return v;
    }
}
