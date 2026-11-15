#include "neutralplayerattack.hpp"

#include <cmath>

namespace MWPhysics
{
    NeutralPlayerAttackInput::Event NeutralPlayerAttackInput::update(bool held, bool enabled, float dt)
    {
        const float step = (std::isfinite(dt) && dt > 0.f) ? dt : 0.f;
        if (!enabled)
        {
            // A hold in progress is forgotten; a key still down when the controls come back is not a new press.
            mHolding = false;
            mFired = false;
            mHeld = 0.f;
            mIgnoreUntilRelease = held;
            return Event::None;
        }
        if (mIgnoreUntilRelease)
        {
            if (!held)
                mIgnoreUntilRelease = false;
            return Event::None;
        }
        if (held)
        {
            if (!mHolding)
            {
                mHolding = true;
                mFired = false;
                mHeld = 0.f;
                return Event::None; // the press frame adds no time
            }
            mHeld += step;
            if (!mFired && mHeld >= kPowerHoldSeconds)
            {
                mFired = true;
                return Event::PowerAttack;
            }
            return Event::None;
        }
        if (!mHolding)
            return Event::None;
        const bool powerTold = mFired;
        mHolding = false;
        mFired = false;
        mHeld = 0.f;
        return powerTold ? Event::None : Event::Attack;
    }
}
