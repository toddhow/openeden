#ifndef OPENEDEN_GAME_SKYRIMSE_CART_RIDES_H
#define OPENEDEN_GAME_SKYRIMSE_CART_RIDES_H

// Task 0274 (the Helgen cart ride): carts that follow a horse and the people who sit on them. Pure: no engine, no
// clock, no allocation per step beyond the output vectors the caller keeps.
//
// What Skyrim does (read from the game's own scripts, task 0274 section 1): MQ101's stage 12 fragment calls
// `Cart.TetherToHorse(horse)` (a Havok constraint between the cart and its horse) and `Actor.SetVehicle(cart)` for
// every rider, the player included. The horse then walks its Travel packages along the road and the cart is dragged
// behind it. This engine has no constraints, so the cart is a TRAILER of the horse, HAND-WRITTEN here:
//   * the cart sits behind the horse at the distance it had when it was tethered, on the line of the horse's recent
//     travel direction (the direction turns toward the horse's motion at most `kTrailerTurnRate` radians a second);
//   * the cart turns by the angle that line has turned since the tether;
//   * a rider keeps the place it had on the cart when it was first seen (rigid, in the cart's frame).
// Headings are the engine's: radians about Z, 0 faces +Y, positive turns clockwise seen from above.

#include <game_skyrimse/records.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace game_skyrimse
{
    struct CartPose
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f;
    };

    /// OURS: how fast the line from the cart to the horse may turn toward the horse's travel direction.
    inline constexpr double kTrailerTurnRate = 1.5;
    /// OURS: the horse counts as moving above this speed (units a second); a standing horse keeps the line.
    inline constexpr double kTrailerMinSpeed = 10.0;

    class CartRides
    {
    public:
        static constexpr std::size_t kMaxRigs = 16;
        static constexpr std::size_t kMaxRiders = 64;

        /// Where a reference is now (a horse, a rider, the player as 0x14). False: unknown, nothing is done for it.
        using Lookup = std::function<bool(FormId reference, double& x, double& y, double& z)>;

        struct CartMove
        {
            FormId mCart = kNullFormId;
            CartPose mPose;
        };

        struct RiderMove
        {
            FormId mRider = kNullFormId;
            FormId mCart = kNullFormId;
            CartPose mPose;
            bool mFirst = false; ///< the first placement since the rider was added (the caller seats it)
        };

        struct Result
        {
            std::vector<CartMove> mCarts;
            std::vector<RiderMove> mRiders;
        };

        /// Tethers `cart` (standing at `cartPose`) to `horse`; a cart tethered again changes horse and starts over.
        /// False (counted) when 16 rigs are in use or an argument is not finite.
        bool tether(FormId cart, FormId horse, const CartPose& cartPose);
        /// `rider` sits on `cart` (Actor.SetVehicle). A rider on another cart moves over. False when 64 riders are in
        /// use or the cart is not tethered (a rider of an untethered cart is remembered until it is: not done, false).
        bool addRider(FormId rider, FormId cart);
        /// The rider gets off (SetVehicle(None) or an exit). The caller stands it up. Returns the cart it was on.
        FormId removeRider(FormId rider);
        /// Does this rider sit on a cart now?
        [[nodiscard]] bool isRider(FormId rider) const;
        [[nodiscard]] bool isTethered(FormId cart) const;
        /// Is this reference the horse of a rig?
        [[nodiscard]] bool isHorse(FormId reference) const;
        [[nodiscard]] std::size_t rigCount() const { return mRigs.size(); }
        [[nodiscard]] std::size_t riderCount() const { return mRiders.size(); }
        /// The horses of the rigs (for the trigger subjects), in ascending reference order.
        [[nodiscard]] std::vector<FormId> horses() const;
        /// The riders of one cart, ascending.
        [[nodiscard]] std::vector<FormId> ridersOf(FormId cart) const;
        /// The cart's pose now (false: not tethered or not captured yet).
        [[nodiscard]] bool cartPose(FormId cart, CartPose& out) const;

        /// One frame: `seconds` is the frame's time. Appends what moved to `out`.
        void step(double seconds, const Lookup& where, Result& out);

        void clear();

    private:
        struct Offset
        {
            bool mCaptured = false;
            double mX = 0.0, mY = 0.0, mZ = 0.0; ///< in the cart's frame at the tether, rotation undone
            bool mPlaced = false;
        };
        struct Rig
        {
            FormId mHorse = kNullFormId;
            CartPose mStart;
            bool mCaptured = false;
            double mDistance = 0.0; ///< horizontal cart to horse
            double mDx = 0.0, mDy = 1.0; ///< unit vector from the cart toward the horse
            double mStartHeading = 0.0; ///< heading of that line at the tether
            double mDz = 0.0; ///< cart z minus horse z
            double mLastHorseX = 0.0, mLastHorseY = 0.0;
            bool mHaveLast = false;
            CartPose mNow;
        };
        std::map<FormId, Rig> mRigs; ///< by cart
        std::map<FormId, FormId> mRiders; ///< rider -> cart
        std::map<FormId, Offset> mOffsets; ///< by rider
    };
}

#endif
