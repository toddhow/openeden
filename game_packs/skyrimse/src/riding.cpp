#include "game_skyrimse/riding.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <span>

namespace game_skyrimse
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        std::string lowered(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
    }

    Gait gaitFor(const openeden::sdk::game::RiderControls& controls)
    {
        if (!controls.valid || !std::isfinite(controls.forward) || controls.forward <= kRidingDeadZone)
            return Gait::Stand;
        return controls.run ? Gait::Run : Gait::Walk;
    }

    float nextRiderHeading(float heading, float turn, float dt)
    {
        if (!std::isfinite(heading))
            return 0.f;
        if (!std::isfinite(turn) || !std::isfinite(dt) || dt <= 0.f)
            return heading;
        const float clamped = std::clamp(turn, -1.f, 1.f);
        float result = std::fmod(heading + clamped * kRidingTurnRate * dt, 2.f * kPi);
        if (result > kPi)
            result -= 2.f * kPi;
        else if (result <= -kPi)
            result += 2.f * kPi;
        return result;
    }

    Velocity velocityFor(Gait gait, float heading, float walkSpeed, float runSpeed)
    {
        if (gait == Gait::Stand || !std::isfinite(heading))
            return {};
        const float speed = gait == Gait::Run ? runSpeed : walkSpeed;
        if (!std::isfinite(speed) || speed <= 0.f)
            return {};
        // Heading 0 faces +Y and positive turns towards +X.
        return { std::sin(heading) * speed, std::cos(heading) * speed };
    }

    Point saddlePoint(double horseX, double horseY, double horseZ, float heading)
    {
        const double s = std::sin(static_cast<double>(heading));
        const double c = std::cos(static_cast<double>(heading));
        // The offset is in the horse's frame: x to its right, y forward.
        const double ox = kSaddleX;
        const double oy = kSaddleY;
        return { horseX + ox * c + oy * s, horseY - ox * s + oy * c,
            horseZ + static_cast<double>(kSaddleZ) + static_cast<double>(kRiderLift) };
    }

    Point dismountPoint(double horseX, double horseY, double horseZ, float heading)
    {
        const double s = std::sin(static_cast<double>(heading));
        const double c = std::cos(static_cast<double>(heading));
        const double side = kDismountSide;
        // To the right of the horse: the heading turned a quarter turn clockwise.
        return { horseX + side * c, horseY - side * s, horseZ };
    }

    Point horseFramePoint(double horseX, double horseY, double horseZ, float heading, double x, double y, double z)
    {
        const double s = std::sin(static_cast<double>(heading));
        const double c = std::cos(static_cast<double>(heading));
        // x is to the horse's right, y forward (as saddlePoint's offset is).
        return { horseX + x * c + y * s, horseY - x * s + y * c, horseZ + z };
    }

    float headingForLeftFlank(double horseX, double horseY, double riderX, double riderY, float fallback)
    {
        if (!std::isfinite(horseX) || !std::isfinite(horseY) || !std::isfinite(riderX) || !std::isfinite(riderY))
            return fallback;
        const double dx = riderX - horseX;
        const double dy = riderY - horseY;
        const double distance = std::hypot(dx, dy);
        if (distance < 1e-3)
            return fallback;
        // The horse's left is (-cos h, sin h); it must point at the rider.
        return static_cast<float>(std::atan2(dy / distance, -dx / distance));
    }

    std::string_view Riding::clip() const
    {
        if (!mMounted)
            return {};
        switch (mPhase)
        {
            case RidePhase::Mounting:
                return kPairedMountClip;
            case RidePhase::Dismounting:
                return kPairedDismountClip;
            case RidePhase::Ride:
                break;
        }
        return riderClipFor(mGait);
    }

    std::string_view riderClipFor(Gait gait)
    {
        switch (gait)
        {
            case Gait::Walk:
                return "horse_rider/walkforward";
            case Gait::Run:
                return "horse_rider/runforward";
            case Gait::Stand:
                break;
        }
        return "horse_rider/idle";
    }

    void Riding::clearHorseClip(openeden::sdk::game::RidingWriter* anyWriter)
    {
        if (auto* paired = dynamic_cast<openeden::sdk::game::PairedWriter*>(anyWriter))
            (void)paired->playPairedClip(mHorse, std::string_view());
    }

    void Riding::beginDismount(openeden::sdk::game::ActorRequestWriter& writer,
        openeden::sdk::game::RidingWriter* player, const openeden::sdk::game::PairedClipInfo& info)
    {
        mPhase = RidePhase::Dismounting;
        mPhaseTime = 0.f;
        mStepActive = false;
        mPairedInfo = info;
        mDismountRequested = false;
        mGait = Gait::Stand;
        (void)writer.setMovement(mHorse, 0.f, 0.f);
        (void)writer.setHeading(mHorse, mHeading);
        if (auto* paired = dynamic_cast<openeden::sdk::game::PairedWriter*>(&writer))
            (void)paired->playPairedClip(mHorse, kPairedDismountHorseClip);
        log("RIDING paired dismount, " + std::to_string(info.duration) + " s");
        if (player != nullptr)
            updatePaired(0.f, writer, *player);
    }

    void Riding::updatePaired(
        float seconds, openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter& player)
    {
        double x = 0.0, y = 0.0, z = 0.0;
        if (!writer.actorPosition(mHorse, x, y, z))
        {
            dismount(writer, &player, "the horse cannot be found");
            return;
        }
        mLastHorse = { x, y, z };
        mDismountRequested = false;
        // The horse stands where it is. Both clips are in its frame (docs/tasks/0267-skyrim-paired-animations.md
        // section 1), so the player's body is drawn at its origin, turned as the horse is; the player himself stays
        // on the saddle, where his camera is not inside the horse's body (the camera's ray to the player must not
        // start in it).
        (void)writer.setMovement(mHorse, 0.f, 0.f);
        (void)writer.setHeading(mHorse, mHeading);
        const Point saddle = saddlePoint(x, y, z, mHeading);
        (void)player.carryPlayer(saddle.mX, saddle.mY, saddle.mZ, mHeading);
        mBodyOffset = { x - saddle.mX, y - saddle.mY, z - saddle.mZ };
        if (mPhase == RidePhase::Dismounting)
        {
            // The clip starts 7 units from where the loop holds the body: blend that step away at the start.
            const Point start
                = horseFramePoint(x, y, z, mHeading, mPairedInfo.start.x, mPairedInfo.start.y, mPairedInfo.start.z);
            mStepDelta = { saddle.mX - start.mX, saddle.mY - start.mY, saddle.mZ - start.mZ };
        }
        mPhaseTime += seconds;
        if (!(mPhaseTime >= mPairedInfo.duration))
            return;
        auto* paired = dynamic_cast<openeden::sdk::game::PairedWriter*>(&writer);
        if (paired != nullptr)
            (void)paired->playPairedClip(mHorse, std::string_view());
        if (mPhase == RidePhase::Mounting)
        {
            // Task 0278: the loop holds the body at the saddle, the clip ended a few units away: blend from there.
            const Point end
                = horseFramePoint(x, y, z, mHeading, mPairedInfo.end.x, mPairedInfo.end.y, mPairedInfo.end.z);
            mStepDelta = { end.mX - saddle.mX, end.mY - saddle.mY, end.mZ - saddle.mZ };
            mStepTime = 0.f;
            mStepActive = true;
            mPhase = RidePhase::Ride;
            mGait = Gait::Stand;
            log("RIDING paired mount finished");
            return;
        }
        // The dismount clip ends with the rider on the ground beside the horse: that is where he stands.
        const openeden::sdk::game::PairedPose& end = mPairedInfo.end;
        const Point down = horseFramePoint(x, y, z, mHeading, end.x, end.y, end.z);
        (void)player.releasePlayer(down.mX, down.mY, down.mZ);
        if (mBrainControl)
            mBrainControl(mHorse.reference, false);
        log("RIDING paired dismount finished");
        mMounted = false;
        mPhase = RidePhase::Ride;
        mGait = Gait::Stand;
        mDismountRequested = false;
        ++mDismountCount;
    }

    void Riding::setTestOwned(std::string editorId)
    {
        mTestOwned = lowered(editorId);
    }

    void Riding::onActorAdded(const openeden::sdk::game::ActorInfo& info)
    {
        if (!mEnabled || !info.handle.valid())
            return;
        Row row;
        row.mHandle = info.handle;
        row.mBase = info.base;
        row.mOwned = !mTestOwned.empty() && (mTestOwned == "*" || mTestOwned == lowered(info.editorId));
        mRows[info.handle.reference] = row;
        // A mounted horse that comes back (a door made the core add it again) keeps its rider.
        if (mMounted && info.handle.reference == mHorse.reference)
        {
            mHorse = info.handle;
            mHorseLost = false;
        }
    }

    void Riding::onActorRemoved(openeden::sdk::game::ActorHandle handle)
    {
        if (!mEnabled)
            return;
        if (mMounted && handle == mHorse)
            mHorseLost = true;
        const auto it = mRows.find(handle.reference);
        if (it != mRows.end() && it->second.mHandle == handle)
            mRows.erase(it);
    }

    Riding::Row* Riding::rowOf(openeden::sdk::game::ActorHandle handle) const
    {
        const auto it = mRows.find(handle.reference);
        if (it == mRows.end() || !(it->second.mHandle == handle))
            return nullptr;
        Row& row = it->second;
        if (row.mHorse < 0)
        {
            HorseInfo info;
            row.mHorse = mLookup && mLookup(row.mBase, static_cast<std::uint32_t>(handle.reference), info) ? 1 : 0;
            if (row.mHorse == 1)
                row.mInfo = info;
        }
        return &row;
    }

    bool Riding::canMount(openeden::sdk::game::ActorHandle handle) const
    {
        if (!mEnabled || !handle.valid())
            return false;
        if (mMounted)
            return handle == mHorse; // the horse he sits on: activating it gets him off
        const Row* row = rowOf(handle);
        // Task 0278: owned by the test setting, or bought (the game's own purchase script put it in
        // PlayerHorseFaction).
        const bool owned = row != nullptr && (row->mOwned || (mOwnedLookup && mOwnedLookup(handle.reference)));
        return row != nullptr && row->mHorse == 1 && owned && row->mInfo.mWalkSpeed > 0.f;
    }

    void Riding::onMountRequested(openeden::sdk::game::ActorHandle handle)
    {
        if (!mEnabled)
            return;
        if (mMounted)
        {
            // Activating the horse again gets off (only the horse he sits on).
            if (handle == mHorse)
                mDismountRequested = true;
            return;
        }
        if (!canMount(handle))
            return;
        mRequest = handle;
        mHasRequest = true;
        mPairedWait = 0.f;
    }

    void Riding::dismountNow(openeden::sdk::game::RidingWriter& player)
    {
        if (!mEnabled || !mMounted)
            return;
        // The horse's own place is the last one the ride saw (no update runs here): he stands beside it.
        const Point down = dismountPoint(mLastHorse.mX, mLastHorse.mY, mLastHorse.mZ, mHeading);
        (void)player.releasePlayer(down.mX, down.mY, down.mZ);
        if (mPhase != RidePhase::Ride && !mHorseLost)
            clearHorseClip(&player); // task 0267: the horse goes back to its own animation
        mPhase = RidePhase::Ride;
        if (mBrainControl)
            mBrainControl(mHorse.reference, false);
        log("RIDING dismounted by a load door");
        mMounted = false;
        mGait = Gait::Stand;
        mStopHorse = !mHorseLost; // its movement request is cleared by the next update (it has a writer)
        mHorseLost = false;
        mDismountRequested = false;
        ++mDismountCount;
    }

    openeden::sdk::game::ActorHandle Riding::mountedHorse() const
    {
        return mMounted ? mHorse : openeden::sdk::game::ActorHandle();
    }

    RidingState Riding::savedState() const
    {
        RidingState state;
        state.mMounted = mMounted;
        state.mHorse = mountReference();
        return state;
    }

    void Riding::restoreState(const RidingState& state)
    {
        // A load replaced the world under a ride in progress: that ride is over (the load put the player where he was
        // saved, the horse stands where its placement does), and the brain takes the horse back.
        if (mMounted)
        {
            if (mBrainControl)
                mBrainControl(mHorse.reference, false);
            log("RIDING ride ended by a load");
            mClearHorseClip = mPhase != RidePhase::Ride && !mHorseLost; // task 0267: cleared at the next update
            mPhase = RidePhase::Ride;
            mMounted = false;
            mGait = Gait::Stand;
            mStopHorse = !mHorseLost;
            ++mDismountCount;
        }
        mHasRequest = false;
        mDismountRequested = false;
        mHorseLost = false;
        mRestorePending = state.mMounted;
    }

    void RidingChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const RidingState state = mGet ? mGet() : RidingState();
        writer.writeValue(static_cast<std::uint8_t>(state.mMounted ? 1 : 0));
        writer.writeValue(state.mHorse);
    }

    bool RidingChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        if (version != kVersion || reader.remaining() != sizeof(std::uint8_t) + sizeof(std::uint32_t))
            return false;
        std::uint8_t flag = 0;
        RidingState state;
        if (!reader.readValue(flag) || !reader.readValue(state.mHorse) || flag > 1)
            return false;
        state.mMounted = flag == 1;
        if (mSet)
            mSet(state);
        return true;
    }

    struct RidingBindings::Impl
    {
        using BindingResult = openeden::sdk::BindingResult;
        using BindingValue = openeden::sdk::BindingValue;
        using BindingType = openeden::sdk::BindingType;

        struct ActiveHandler final : openeden::sdk::BindingHandler
        {
            const Riding& mRiding;
            explicit ActiveHandler(const Riding& riding)
                : mRiding(riding)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::Bool;
                result.value.boolean = mRiding.riding();
                return result;
            }
        };

        struct ScaleHandler final : openeden::sdk::BindingHandler
        {
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::Double;
                result.value.number = static_cast<double>(kRidingCameraScale);
                return result;
            }
        };

        explicit Impl(const Riding& riding)
            : mActive(riding)
        {
        }

        ActiveHandler mActive;
        ScaleHandler mScale;
        std::array<openeden::sdk::FunctionDescriptor, 2> mFunctions;
    };

    RidingBindings::RidingBindings(const Riding& riding)
        : mImpl(std::make_unique<Impl>(riding))
    {
    }

    RidingBindings::~RidingBindings() = default;

    openeden::sdk::DeclareResult RidingBindings::declareTo(openeden::sdk::BindingsService& service)
    {
        using namespace openeden::sdk;
        mImpl->mFunctions[0]
            = FunctionDescriptor{ "active", {}, true, BindingType::Bool, BindingAccess::Read, &mImpl->mActive, 0 };
        mImpl->mFunctions[1] = FunctionDescriptor{ "camera_scale", {}, true, BindingType::Double, BindingAccess::Read,
            &mImpl->mScale, 0 };
        NamespaceDescriptor ns;
        ns.name = kRidingNamespace;
        ns.version = Version{ 1, 0 };
        ns.functions = mImpl->mFunctions;
        ns.menuSafe = false;
        return service.declareNamespace(ns);
    }

    void Riding::dismount(openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter* player,
        const std::string& why)
    {
        Point from = mLastHorse;
        double x = 0.0, y = 0.0, z = 0.0;
        if (!mHorseLost && writer.actorPosition(mHorse, x, y, z))
            from = { x, y, z };
        const Point down = dismountPoint(from.mX, from.mY, from.mZ, mHeading);
        if (player != nullptr)
            (void)player->releasePlayer(down.mX, down.mY, down.mZ);
        if (!mHorseLost)
            (void)writer.setMovement(mHorse, 0.f, 0.f);
        if (mPhase != RidePhase::Ride && !mHorseLost)
            clearHorseClip(player != nullptr ? player : dynamic_cast<openeden::sdk::game::RidingWriter*>(&writer));
        mPhase = RidePhase::Ride;
        if (mBrainControl)
            mBrainControl(mHorse.reference, false);
        log("RIDING dismounted " + why);
        mMounted = false;
        mGait = Gait::Stand;
        mHorseLost = false;
        mDismountRequested = false;
        ++mDismountCount;
    }

    void Riding::update(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        if (!mEnabled)
            return;
        if (mStepActive)
        {
            mStepTime += seconds;
            if (mStepTime >= kHandoverBlendSeconds || !mMounted)
                mStepActive = false;
        }
        if (!mMounted && (mStopHorse || mRestorePending || mClearHorseClip))
        {
            if (mClearHorseClip)
            {
                clearHorseClip(dynamic_cast<openeden::sdk::game::RidingWriter*>(&writer));
                mClearHorseClip = false;
            }
            // Task 0255: what a dismount outside update() (a load door) or a load left for the next update.
            if (mStopHorse)
            {
                (void)writer.setMovement(mHorse, 0.f, 0.f);
                mStopHorse = false;
            }
            double px = 0.0, py = 0.0, pz = 0.0;
            auto* restorer = dynamic_cast<openeden::sdk::game::RidingWriter*>(&writer);
            if (mRestorePending && restorer != nullptr && writer.playerPosition(px, py, pz))
            {
                // He was saved on the saddle; he stands on the ground below it.
                (void)restorer->releasePlayer(px, py, pz - static_cast<double>(kSaddleZ));
                mRestorePending = false;
                log("RIDING restored on foot after a load");
            }
        }
        if (!mMounted && !mHasRequest)
            return;
        auto* player = dynamic_cast<openeden::sdk::game::RidingWriter*>(&writer);
        if (player == nullptr)
        {
            mHasRequest = false;
            return;
        }
        openeden::sdk::game::RiderControls controls;
        if (!player->riderControls(controls))
            controls = {};

        if (!mMounted)
        {
            const Row* row = rowOf(mRequest);
            double x = 0.0, y = 0.0, z = 0.0;
            if (row == nullptr || row->mHorse != 1 || !writer.actorPosition(mRequest, x, y, z))
            {
                mHasRequest = false;
                return;
            }
            // Task 0267: the paired mount clip, when the setting is on, the core can play it and it is read (a clip
            // that is still being read is waited for a few seconds; one that cannot be read mounts at once, as
            // before).
            openeden::sdk::game::PairedClipInfo clipInfo;
            auto* paired = mPaired ? dynamic_cast<openeden::sdk::game::PairedWriter*>(&writer) : nullptr;
            openeden::sdk::game::PairedStatus status = openeden::sdk::game::PairedStatus::Failed;
            if (paired != nullptr)
                status = paired->pairedClipInfo(kPairedMountClip, clipInfo);
            if (status == openeden::sdk::game::PairedStatus::NotReady)
            {
                mPairedWait += seconds;
                if (mPairedWait < kPairedWaitSeconds)
                    return; // the request is kept
                log("RIDING the mount clip was not read in time: mounting without it");
            }
            const bool usePaired = status == openeden::sdk::game::PairedStatus::Ready && clipInfo.duration > 0.f
                && std::isfinite(clipInfo.duration);
            mPairedWait = 0.f;
            mHasRequest = false;
            mHorse = mRequest;
            mHorseInfo = row->mInfo;
            mHeading = std::isfinite(controls.heading) ? controls.heading : 0.f;
            mGait = Gait::Stand;
            mJumpWasUp = !controls.jump;
            mHorseLost = false;
            mDismountRequested = false;
            mLastHorse = { x, y, z };
            mMounted = true;
            ++mMountCount;
            if (mBrainControl)
                mBrainControl(mHorse.reference, true);
            log("RIDING mounted horse " + std::to_string(mHorse.reference));
            mPhase = RidePhase::Ride;
            if (usePaired)
            {
                // The horse is turned to have the rider on its left flank (where the clip begins), stands still and
                // plays its side; the player's body is drawn at its origin and plays his.
                double px = 0.0, py = 0.0, pz = 0.0;
                if (writer.playerPosition(px, py, pz))
                    mHeading = headingForLeftFlank(x, y, px, py, mHeading);
                mPhase = RidePhase::Mounting;
                mPhaseTime = 0.f;
                mPairedInfo = clipInfo;
                (void)writer.setMovement(mHorse, 0.f, 0.f);
                (void)writer.setHeading(mHorse, mHeading);
                (void)paired->playPairedClip(mHorse, kPairedMountHorseClip);
                openeden::sdk::game::PairedClipInfo ahead;
                (void)paired->pairedClipInfo(kPairedDismountClip, ahead); // starts reading the dismount clip
                log("RIDING paired mount, " + std::to_string(clipInfo.duration) + " s");
                updatePaired(0.f, writer, *player);
                return;
            }
        }

        if (mHorseLost)
        {
            dismount(writer, player, "the horse is gone");
            return;
        }
        if (!controls.jump)
            mJumpWasUp = true;
        if (mPhase != RidePhase::Ride)
        {
            updatePaired(seconds, writer, *player);
            return;
        }
        if (mDismountRequested || (controls.jump && mJumpWasUp))
        {
            if (mPaired)
            {
                openeden::sdk::game::PairedClipInfo info;
                auto* paired = dynamic_cast<openeden::sdk::game::PairedWriter*>(&writer);
                if (paired != nullptr
                    && paired->pairedClipInfo(kPairedDismountClip, info) == openeden::sdk::game::PairedStatus::Ready
                    && info.duration > 0.f && std::isfinite(info.duration))
                {
                    beginDismount(writer, player, info);
                    return;
                }
            }
            dismount(writer, player, mDismountRequested ? "by activating the horse" : "by the jump control");
            return;
        }

        mHeading
            = nextRiderHeading(std::isfinite(controls.heading) ? controls.heading : mHeading, controls.turn, seconds);
        mGait = gaitFor(controls);
        const Velocity v = velocityFor(mGait, mHeading, mHorseInfo.mWalkSpeed, mHorseInfo.mRunSpeed);
        (void)writer.setMovement(mHorse, v.mX, v.mY);
        if (mGait == Gait::Stand)
            (void)writer.setHeading(mHorse, mHeading);
        double x = 0.0, y = 0.0, z = 0.0;
        if (!writer.actorPosition(mHorse, x, y, z))
        {
            dismount(writer, player, "the horse cannot be found");
            return;
        }
        mLastHorse = { x, y, z };
        const Point saddle = saddlePoint(x, y, z, mHeading);
        (void)player->carryPlayer(saddle.mX, saddle.mY, saddle.mZ, mHeading);
    }
}
