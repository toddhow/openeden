#include "neutraldoorswing.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics::NeutralDoor
{
    bool isApproached(float centreX, float centreY, float centreZ, float walkerX, float walkerY, float walkerZ)
    {
        const float dx = walkerX - centreX;
        const float dy = walkerY - centreY;
        const float dz = walkerZ - centreZ;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
            return false;
        return dx * dx + dy * dy <= kTriggerRadius * kTriggerRadius && std::abs(dz) <= kTriggerHeight;
    }

    State step(State state, bool walkerNear, float dt)
    {
        if (!(dt > 0.f) || !std::isfinite(dt))
            return state;
        if (walkerNear)
        {
            state.mHold = kHoldSeconds;
            state.mProgress = std::min(1.f, state.mProgress + dt / kOpenSeconds);
            return state;
        }
        if (state.mProgress <= 0.f)
        {
            state.mHold = 0.f;
            return state;
        }
        // The hold is used up first; the rest of the frame closes the door.
        const float held = std::min(dt, state.mHold);
        state.mHold -= held;
        state.mProgress = std::max(0.f, state.mProgress - (dt - held) / kCloseSeconds);
        return state;
    }

    float sinkOffset(const State& state)
    {
        return state.mProgress * kSinkDepth;
    }
}
