#ifndef OPENMW_MWRENDER_PLAYERBODYTRACK_HPP
#define OPENMW_MWRENDER_PLAYERBODYTRACK_HPP

// Task 0222 (the player has a Skyrim body): the pure rules of the player's drawn body. Nothing here reads a file or
// touches the renderer, so a test runs them as they are.
//
//   * GroundSpeed turns the player's position over frames into the speed the gait blends by (the same number the
//     physics side gives a neutral actor, task 0148): the horizontal distance over the frame time. A frame time that
//     is zero, negative or not finite (a paused game) changes nothing; a jump over kTeleportUnits in one frame (a door,
//     a teleport) is no walk: speed 0 and the place is taken over.
//   * SwingWatch tells the frame a swing began, from the token the pack counts up (PlayerBodyState::swingToken).
//   * RebuildGate says when the body must be made again: the pack's revision changed to something other than 0 ("not
//     ready") and it is not the one already built or already failed.
//
// Threads: main thread only. No allocation.

#include <cmath>
#include <cstdint>

namespace MWRender::PlayerBodyTrack
{
    /// A move of more than this in one frame is a teleport, not a walk. Game units (a run is about 350 a second, so a
    /// frame of a second stays under it; the longest frame the game allows is 0.2 s).
    inline constexpr double kTeleportUnits = 1000.0;

    class GroundSpeed
    {
    public:
        float update(double x, double y, float dt)
        {
            if (!(dt > 0.f) || !std::isfinite(dt))
                return mSpeed;
            if (!std::isfinite(x) || !std::isfinite(y))
            {
                mSpeed = 0.f;
                return mSpeed;
            }
            if (!mHave)
            {
                mHave = true;
                mX = x;
                mY = y;
                mSpeed = 0.f;
                return mSpeed;
            }
            const double dx = x - mX;
            const double dy = y - mY;
            mX = x;
            mY = y;
            const double distance = std::sqrt(dx * dx + dy * dy);
            mSpeed = distance > kTeleportUnits ? 0.f : static_cast<float>(distance / static_cast<double>(dt));
            return mSpeed;
        }

        void reset()
        {
            mHave = false;
            mSpeed = 0.f;
        }

    private:
        bool mHave = false;
        double mX = 0.0;
        double mY = 0.0;
        float mSpeed = 0.f;
    };

    class SwingWatch
    {
    public:
        /// True once for each change of the token to a number above the one seen last.
        bool poll(std::uint32_t token)
        {
            if (token == mSeen)
                return false;
            const bool grew = token > mSeen;
            mSeen = token;
            return grew;
        }
        /// Takes the token as already seen (a body made while a swing is underway does not play it from the start).
        void adopt(std::uint32_t token) { mSeen = token; }

    private:
        std::uint32_t mSeen = 0;
    };

    class RebuildGate
    {
    public:
        [[nodiscard]] bool wants(std::uint32_t revision) const { return revision != 0 && revision != mHandled; }
        void built(std::uint32_t revision) { mHandled = revision; }
        /// The build of this revision failed: it is not tried again until the revision changes.
        void failed(std::uint32_t revision) { mHandled = revision; }

    private:
        std::uint32_t mHandled = 0;
    };
}

#endif
