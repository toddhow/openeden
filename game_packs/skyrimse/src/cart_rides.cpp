#include <game_skyrimse/cart_rides.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        bool finite(double v)
        {
            return std::isfinite(v);
        }

        /// The heading (radians, 0 faces +Y, clockwise positive) of a horizontal direction.
        double headingOf(double dx, double dy)
        {
            return std::atan2(dx, dy);
        }

        /// The turn from `from` to `to`, in (-pi, pi].
        double turnBetween(double from, double to)
        {
            double d = std::fmod(to - from, 2.0 * kPi);
            if (d > kPi)
                d -= 2.0 * kPi;
            else if (d <= -kPi)
                d += 2.0 * kPi;
            return d;
        }

        /// A cart-frame offset turned clockwise by `delta` (the data's heading convention).
        void turned(double lx, double ly, double delta, double& wx, double& wy)
        {
            const double c = std::cos(delta), s = std::sin(delta);
            wx = lx * c + ly * s;
            wy = -lx * s + ly * c;
        }
    }

    bool CartRides::tether(FormId cart, FormId horse, const CartPose& cartPose)
    {
        if (!finite(cartPose.x) || !finite(cartPose.y) || !finite(cartPose.z) || cart == kNullFormId
            || horse == kNullFormId)
            return false;
        const auto it = mRigs.find(cart);
        if (it == mRigs.end() && mRigs.size() >= kMaxRigs)
            return false;
        Rig rig;
        rig.mHorse = horse;
        rig.mStart = cartPose;
        rig.mNow = cartPose;
        mRigs[cart] = rig; // a cart tethered again changes horse and starts over
        return true;
    }

    bool CartRides::addRider(FormId rider, FormId cart)
    {
        if (rider == kNullFormId || mRigs.find(cart) == mRigs.end())
            return false;
        if (mRiders.find(rider) == mRiders.end() && mRiders.size() >= kMaxRiders)
            return false;
        mRiders[rider] = cart;
        mOffsets[rider] = Offset(); // placed when it is first seen
        return true;
    }

    FormId CartRides::removeRider(FormId rider)
    {
        const auto it = mRiders.find(rider);
        if (it == mRiders.end())
            return kNullFormId;
        const FormId cart = it->second;
        mRiders.erase(it);
        mOffsets.erase(rider);
        return cart;
    }

    bool CartRides::isRider(FormId rider) const
    {
        return mRiders.count(rider) != 0;
    }

    bool CartRides::isTethered(FormId cart) const
    {
        return mRigs.count(cart) != 0;
    }

    bool CartRides::isHorse(FormId reference) const
    {
        for (const auto& [cart, rig] : mRigs)
            if (rig.mHorse == reference)
                return true;
        return false;
    }

    std::vector<FormId> CartRides::horses() const
    {
        std::vector<FormId> out;
        for (const auto& [cart, rig] : mRigs)
            out.push_back(rig.mHorse);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<FormId> CartRides::ridersOf(FormId cart) const
    {
        std::vector<FormId> out;
        for (const auto& [rider, on] : mRiders) // a std::map: ascending
            if (on == cart)
                out.push_back(rider);
        return out;
    }

    bool CartRides::cartPose(FormId cart, CartPose& out) const
    {
        const auto it = mRigs.find(cart);
        if (it == mRigs.end())
            return false;
        out = it->second.mNow;
        return true;
    }

    void CartRides::clear()
    {
        mRigs.clear();
        mRiders.clear();
        mOffsets.clear();
    }

    void CartRides::step(double seconds, const Lookup& where, Result& out)
    {
        if (!where)
            return;
        if (!finite(seconds) || seconds < 0.0)
            seconds = 0.0;
        for (auto& [cart, rig] : mRigs)
        {
            double hx = 0.0, hy = 0.0, hz = 0.0;
            if (!where(rig.mHorse, hx, hy, hz) || !finite(hx) || !finite(hy) || !finite(hz))
                continue;
            if (!rig.mCaptured)
            {
                // the line from the cart to the horse, as it was when the cart was tethered
                const double dx = hx - rig.mStart.x, dy = hy - rig.mStart.y;
                rig.mDistance = std::hypot(dx, dy);
                if (rig.mDistance > 1e-6)
                {
                    rig.mDx = dx / rig.mDistance;
                    rig.mDy = dy / rig.mDistance;
                }
                else
                {
                    rig.mDistance = 0.0;
                    rig.mDx = 0.0;
                    rig.mDy = 1.0;
                }
                rig.mStartHeading = headingOf(rig.mDx, rig.mDy);
                if (!finite(static_cast<double>(rig.mStart.heading)))
                    rig.mStart.heading = static_cast<float>(rig.mStartHeading); // a cart faces its horse
                rig.mDz = rig.mStart.z - hz;
                rig.mLastHorseX = hx;
                rig.mLastHorseY = hy;
                rig.mHaveLast = true;
                rig.mCaptured = true;
            }
            else if (seconds > 0.0)
            {
                const double mx = hx - rig.mLastHorseX, my = hy - rig.mLastHorseY;
                const double speed = std::hypot(mx, my) / seconds;
                if (speed > kTrailerMinSpeed)
                {
                    const double turn = turnBetween(headingOf(rig.mDx, rig.mDy), headingOf(mx, my));
                    const double limit = kTrailerTurnRate * seconds;
                    const double step = std::clamp(turn, -limit, limit);
                    const double h = headingOf(rig.mDx, rig.mDy) + step;
                    rig.mDx = std::sin(h);
                    rig.mDy = std::cos(h);
                }
                rig.mLastHorseX = hx;
                rig.mLastHorseY = hy;
            }
            const double delta = turnBetween(rig.mStartHeading, headingOf(rig.mDx, rig.mDy));
            CartPose pose;
            pose.x = hx - rig.mDistance * rig.mDx;
            pose.y = hy - rig.mDistance * rig.mDy;
            pose.z = hz + rig.mDz;
            pose.heading = static_cast<float>(static_cast<double>(rig.mStart.heading) + delta);
            rig.mNow = pose;
            out.mCarts.push_back({ cart, pose });

            // the riders of this cart, ascending
            for (const auto& [rider, on] : mRiders)
            {
                if (on != cart)
                    continue;
                Offset& offset = mOffsets[rider];
                if (!offset.mCaptured)
                {
                    double rx = 0.0, ry = 0.0, rz = 0.0;
                    if (!where(rider, rx, ry, rz) || !finite(rx) || !finite(ry) || !finite(rz))
                        continue;
                    // the place on the cart as it is now, with the cart's turn undone
                    double lx = 0.0, ly = 0.0;
                    turned(rx - pose.x, ry - pose.y, -delta, lx, ly);
                    offset.mX = lx;
                    offset.mY = ly;
                    offset.mZ = rz - pose.z;
                    offset.mCaptured = true;
                }
                double wx = 0.0, wy = 0.0;
                turned(offset.mX, offset.mY, delta, wx, wy);
                RiderMove move;
                move.mRider = rider;
                move.mCart = cart;
                move.mPose.x = pose.x + wx;
                move.mPose.y = pose.y + wy;
                move.mPose.z = pose.z + offset.mZ;
                move.mPose.heading = pose.heading;
                move.mFirst = !offset.mPlaced;
                offset.mPlaced = true;
                out.mRiders.push_back(move);
            }
        }
    }
}
