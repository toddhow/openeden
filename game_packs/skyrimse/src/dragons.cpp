#include "game_skyrimse/dragons.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace game_skyrimse::dragons
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTwoPi = 2.0 * kPi;
        /// Seconds the cruise takes to settle on the circle's radius (ours).
        constexpr double kRadiusSettleSeconds = 3.0;
        /// The circle is never tighter than this (ours): a dragon does not turn on the spot.
        constexpr double kMinRadius = 300.0;
        constexpr double kMaxStep = 1.0; ///< a frame longer than this is clamped

        double wrapPi(double a)
        {
            a = std::fmod(a, kTwoPi);
            if (a > kPi)
                a -= kTwoPi;
            else if (a <= -kPi)
                a += kTwoPi;
            return a;
        }

        double smooth(double t)
        {
            t = std::clamp(t, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        bool finite(double v)
        {
            return std::isfinite(v);
        }
    }

    const CreatureProfile& dragonProfile()
    {
        // The row lives in the shared creature table (task 0262).
        return *creatureProfileFor("actors/dragon/character assets/skeleton.nif");
    }

    const CreatureProfile* dragonProfileFor(std::string_view skeletonPath)
    {
        const CreatureProfile* row = creatureProfileFor(skeletonPath);
        return isDragon(row) ? row : nullptr;
    }

    bool isDragon(const CreatureProfile* profile)
    {
        return profile != nullptr && profile->mName == "dragon";
    }

    Flight::Flight(FlightSettings settings)
        : mSettings(settings)
    {
    }

    FlightOutput Flight::step(const FlightInput& in)
    {
        FlightOutput out;
        out.mPhase = mPhase;
        if (!std::isfinite(in.mDt) || in.mDt < 0.f || !finite(in.mX) || !finite(in.mY) || !finite(in.mZ)
            || !std::isfinite(in.mHeading) || !finite(in.mPlayerX) || !finite(in.mPlayerY) || !finite(in.mPlayerZ))
            return out;
        const double dt = std::min(static_cast<double>(in.mDt), kMaxStep);

        // Where the player is from the dragon, for facing.
        const double toPlayer = std::atan2(in.mPlayerX - in.mX, in.mPlayerY - in.mY);
        const auto turnTo = [&](double desired) {
            if (!mHeadingSet)
            {
                mHeading = in.mHeading;
                mHeadingSet = true;
            }
            const double current = mHeading;
            const double delta = wrapPi(desired - current);
            const double limit = static_cast<double>(std::max(mSettings.mTurnRate, 0.01f)) * dt;
            mHeading = static_cast<float>(wrapPi(current + std::clamp(delta, -limit, limit)));
        };
        const auto place = [&](double x, double y, double z) {
            out.mPlace = true;
            out.mX = x;
            out.mY = y;
            out.mZ = z;
            out.mHeading = mHeading;
            mX = x;
            mY = y;
            mZ = z;
        };

        if (mPhase == Phase::Ground)
        {
            if (in.mDead || !mSettings.mEnabled)
                return out;
            if (in.mEngaged && !in.mBusy)
                mGround += dt;
            if (!(in.mEngaged && !in.mBusy && mGround >= static_cast<double>(mSettings.mGroundSeconds)))
                return out;
            mPhase = Phase::Takeoff;
            mPhaseTime = 0.0;
            mGround = 0.0;
            mOriginX = in.mX;
            mOriginY = in.mY;
            mOriginZ = in.mZ;
            mTopZ = in.mZ + static_cast<double>(mSettings.mCruiseHeight);
            mHeading = in.mHeading;
            mHeadingSet = true;
            ++mFlights;
            out.mPhase = mPhase;
            out.mStartPose = true;
            out.mClip = kClipTakeoff;
            place(mOriginX, mOriginY, mOriginZ);
            return out;
        }

        // In the air.
        if (in.mDead)
        {
            out.mLand = true;
            out.mX = mX;
            out.mY = mY;
            out.mZ = mZ;
            out.mHeading = mHeading;
            mPhase = Phase::Ground;
            mGround = 0.0;
            out.mPhase = mPhase;
            return out;
        }
        mPhaseTime += dt;

        if (mPhase == Phase::Takeoff)
        {
            if (mPhaseTime < static_cast<double>(kTakeoffSeconds))
            {
                turnTo(toPlayer);
                out.mClip = kClipTakeoff;
                place(mOriginX, mOriginY,
                    mOriginZ + (mTopZ - mOriginZ) * smooth(mPhaseTime / static_cast<double>(kTakeoffSeconds)));
                return out;
            }
            // Over: the cruise begins where the dragon is, on its bearing and distance from the player.
            const double dx = mOriginX - in.mPlayerX;
            const double dy = mOriginY - in.mPlayerY;
            mAngle = std::atan2(dx, dy);
            mStartRadius = std::max(std::hypot(dx, dy), 1.0);
            mRadius = mStartRadius;
            mPhase = Phase::Cruise;
            mPhaseTime = 0.0;
            out.mStartPose = true;
            out.mClip = kClipCruise;
            out.mPhase = mPhase;
            turnTo(wrapPi(mAngle + kPi / 2.0));
            place(mOriginX, mOriginY, mTopZ);
            return out;
        }

        if (mPhase == Phase::Cruise)
        {
            if (mPhaseTime >= static_cast<double>(mSettings.mAirSeconds) || !in.mEngaged)
            {
                // Approach: the landing point is on the bearing the dragon is at, nearer the player.
                const double dx = mX - in.mPlayerX;
                const double dy = mY - in.mPlayerY;
                const double bearing = std::hypot(dx, dy) > 1.0 ? std::atan2(dx, dy) : mAngle;
                mLandX = in.mPlayerX + std::sin(bearing) * static_cast<double>(mSettings.mLandDistance);
                mLandY = in.mPlayerY + std::cos(bearing) * static_cast<double>(mSettings.mLandDistance);
                mLandZ = in.mPlayerZ;
                mApproachX = mX;
                mApproachY = mY;
                mApproachZ = mZ;
                mPhase = Phase::Approach;
                mPhaseTime = 0.0;
                out.mStartPose = true;
                out.mClip = kClipApproach;
                out.mPhase = mPhase;
                turnTo(toPlayer);
                place(mApproachX, mApproachY, mApproachZ);
                return out;
            }
            const double settle = smooth(mPhaseTime / kRadiusSettleSeconds);
            mRadius = mStartRadius + (static_cast<double>(mSettings.mCircleRadius) - mStartRadius) * settle;
            mAngle = wrapPi(mAngle + static_cast<double>(kGlideSpeed) * dt / std::max(mRadius, kMinRadius));
            out.mClip = kClipCruise;
            turnTo(in.mFaceTarget ? toPlayer : wrapPi(mAngle + kPi / 2.0));
            place(in.mPlayerX + std::sin(mAngle) * mRadius, in.mPlayerY + std::cos(mAngle) * mRadius, mTopZ);
            return out;
        }

        if (mPhase == Phase::Approach)
        {
            if (mPhaseTime >= static_cast<double>(kApproachSeconds))
            {
                mPhase = Phase::Touchdown;
                mPhaseTime = 0.0;
                out.mStartPose = true;
                out.mClip = kClipTouchdown;
                out.mPhase = mPhase;
                turnTo(toPlayer);
                place(mLandX, mLandY, mLandZ + static_cast<double>(mSettings.mLandHover));
                return out;
            }
            const double f = smooth(mPhaseTime / static_cast<double>(kApproachSeconds));
            turnTo(toPlayer);
            out.mClip = kClipApproach;
            place(mApproachX + (mLandX - mApproachX) * f, mApproachY + (mLandY - mApproachY) * f,
                mApproachZ + (mLandZ + static_cast<double>(mSettings.mLandHover) - mApproachZ) * f);
            return out;
        }

        // Touchdown
        if (mPhaseTime >= static_cast<double>(kTouchdownSeconds))
        {
            mPhase = Phase::Ground;
            mGround = 0.0;
            out.mPhase = mPhase;
            out.mLand = true;
            out.mX = mLandX;
            out.mY = mLandY;
            out.mZ = mLandZ;
            out.mHeading = mHeading;
            return out;
        }
        turnTo(toPlayer);
        out.mClip = kClipTouchdown;
        const double f = smooth(mPhaseTime / static_cast<double>(kTouchdownSeconds));
        place(mLandX, mLandY, mLandZ + static_cast<double>(mSettings.mLandHover) * (1.0 - f));
        return out;
    }

    Cone breathCone(double ox, double oy, double oz, double tx, double ty, double tz, float range, float halfAngle)
    {
        Cone cone;
        cone.mOx = ox;
        cone.mOy = oy;
        cone.mOz = oz;
        cone.mRange = 0.f;
        cone.mHalfAngle = halfAngle;
        if (!finite(ox) || !finite(oy) || !finite(oz) || !finite(tx) || !finite(ty) || !finite(tz)
            || !std::isfinite(range) || !(range > 0.f) || !std::isfinite(halfAngle))
            return cone;
        const double dx = tx - ox, dy = ty - oy, dz = tz - oz;
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(length > 1e-6))
            return cone;
        cone.mAx = dx / length;
        cone.mAy = dy / length;
        cone.mAz = dz / length;
        cone.mRange = range;
        return cone;
    }

    bool coneContains(const Cone& cone, double x, double y, double z)
    {
        if (!(cone.mRange > 0.f) || !finite(x) || !finite(y) || !finite(z))
            return false;
        const double vx = x - cone.mOx, vy = y - cone.mOy, vz = z - cone.mOz;
        const double distance = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (distance > static_cast<double>(cone.mRange))
            return false;
        if (distance < 1e-9)
            return true;
        const double cosine = (vx * cone.mAx + vy * cone.mAy + vz * cone.mAz) / distance;
        return cosine >= std::cos(static_cast<double>(cone.mHalfAngle));
    }

    bool soulAbsorbs(float secondsSinceDeath, double playerDistance)
    {
        if (!std::isfinite(secondsSinceDeath) || !finite(playerDistance) || secondsSinceDeath < 0.f
            || playerDistance < 0.0)
            return false;
        return secondsSinceDeath >= kSoulDelaySeconds && playerDistance <= static_cast<double>(kSoulRange);
    }
}
