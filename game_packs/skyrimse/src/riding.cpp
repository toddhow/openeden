#include "game_skyrimse/riding.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

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
        return row != nullptr && row->mHorse == 1 && row->mOwned && row->mInfo.mWalkSpeed > 0.f;
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
    }

    void Riding::dismount(
        openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter* player, const std::string& why)
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
        if (!mEnabled || (!mMounted && !mHasRequest))
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
            mHasRequest = false;
            if (row == nullptr || row->mHorse != 1)
                return;
            double x = 0.0, y = 0.0, z = 0.0;
            if (!writer.actorPosition(mRequest, x, y, z))
                return;
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
        }

        if (mHorseLost)
        {
            dismount(writer, player, "the horse is gone");
            return;
        }
        if (!controls.jump)
            mJumpWasUp = true;
        if (mDismountRequested || (controls.jump && mJumpWasUp))
        {
            dismount(writer, player, mDismountRequested ? "by activating the horse" : "by the jump control");
            return;
        }

        mHeading = nextRiderHeading(std::isfinite(controls.heading) ? controls.heading : mHeading, controls.turn, seconds);
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
