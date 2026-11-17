#ifndef OPENEDEN_GAME_SKYRIMSE_TRIGGERS_H
#define OPENEDEN_GAME_SKYRIMSE_TRIGGERS_H

// Trigger volumes for Papyrus (task 0256, word walls). A placed reference that carries a primitive (REFR XPRM) is a
// trigger volume: a script on it gets OnTriggerEnter and OnTriggerLeave when the player goes in and out. Pure: no
// engine, no clock; the host feeds it the player's place and position once a frame.
//
// Only the SPHERE primitive (XPRM type 2) is built. That is the only kind the 56 word wall triggers of the five
// official masters use (VERIFIED by a scan: XPRM is 32 bytes, three equal bounds, three colour floats, one more float,
// then the type; type 2 in all 56, bounds 1000 to 3994 units, 2064.8 in 50 of them), and the radius is the bound
// (RECALLED-VERIFY: the equal three bounds fit a radius, and 2064.8 is just over the 2000 the word wall script's
// farthest band reaches). A box, plane, line or ellipsoid volume never fires and is counted.

#include <game_skyrimse/records.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace game_skyrimse
{
    inline constexpr std::uint32_t kPrimitiveSphere = 2; ///< XPRM's last field

    struct TriggerVolume
    {
        std::uint32_t mType = 0;
        double mBounds[3] = {};
        double mPosition[3] = {};
        FormId mCell = kNullFormId; ///< the cell the reference is placed in
        bool mInterior = false; ///< ... and whether that cell is an interior
    };

    /// Is this a volume the tracker can test (a sphere with a positive, finite radius and a finite place)?
    [[nodiscard]] bool triggerSupported(const TriggerVolume& volume);
    /// Is the point inside (on the surface counts)? False for a volume that is not supported or a point that is not
    /// finite.
    [[nodiscard]] bool volumeContains(const TriggerVolume& volume, double x, double y, double z);

    /// The Actor.HasLOS answer for the player, approximated by where he looks (there is no ray from a Papyrus call):
    /// the angle between the view direction (heading about Z as the engine stores it, 0 faces +Y; pitch up positive)
    /// and the direction from the eye to the target is at most `halfAngle` radians. A target at the eye is in view.
    [[nodiscard]] bool inViewCone(double eyeX, double eyeY, double eyeZ, float heading, float pitch, double targetX,
        double targetY, double targetZ, double halfAngle);

    /// OURS: the eye is this far above the player's feet, and the view cone is this wide (a half angle, 45 degrees).
    inline constexpr double kLosEyeHeight = 120.0;
    inline constexpr double kViewHalfAngle = 0.7853981633974483;

    struct TriggerPlace
    {
        bool mKnown = false;
        bool mInterior = false;
        FormId mCell = kNullFormId; ///< the interior cell the player is in (when mInterior)
    };

    struct TriggerEdge
    {
        FormId mReference = kNullFormId;
        bool mEntered = false;
    };

    /// The volumes in the world and whether the player is inside each. An interior volume counts only while the player
    /// is in that interior cell; an exterior volume only while he is in an exterior (the volume reaches into
    /// neighbouring squares, as the engine's own trigger does). The player's place not being known changes nothing.
    class TriggerTracker
    {
    public:
        static constexpr std::size_t kMaxTriggers = 8192;

        /// False (and counted) when the volume is not supported or the tracker is full. Adding a reference again
        /// replaces its volume and keeps whether the player is inside.
        bool add(FormId reference, const TriggerVolume& volume);
        /// The edges since the last call, in ascending reference order; `edges` is appended to.
        void update(const TriggerPlace& place, double x, double y, double z, std::vector<TriggerEdge>& edges);
        [[nodiscard]] std::size_t count() const { return mVolumes.size(); }
        [[nodiscard]] std::size_t unsupported() const { return mUnsupported; }
        [[nodiscard]] bool inside(FormId reference) const;
        /// Forget whether the player is inside (a load): the next update reports every volume he stands in as entered.
        void resetInside();

    private:
        struct Entry
        {
            TriggerVolume mVolume;
            bool mInside = false;
        };
        std::map<FormId, Entry> mVolumes;
        std::size_t mUnsupported = 0;
    };
}

#endif
