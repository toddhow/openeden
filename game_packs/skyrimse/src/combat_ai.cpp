#include "game_skyrimse/combat_ai.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

        std::uint64_t splitmix64(std::uint64_t z)
        {
            z += kGolden;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        constexpr double kPi = 3.14159265358979323846;

        // The flee directions, as turns away from the straight line away from the target, in degrees.
        constexpr double kFleeTurnDegrees[kAiFleeAttempts] = { 0, 45, -45, 90, -90, 135, -135, 180 };

        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }
    }

    float fleeHealthFraction(std::uint8_t confidence)
    {
        switch (confidence)
        {
            case 0:
                return 1.0f;
            case 1:
                return 0.5f;
            case 2:
                return 0.25f;
            case 3:
                return 0.1f;
            case 4:
                return 0.f;
            default:
                return 0.25f;
        }
    }

    std::uint64_t aiSeed(std::uint64_t reference, std::uint32_t engagement)
    {
        return splitmix64(reference * kGolden + engagement);
    }

    float aiRoll(AiActorState& state)
    {
        const std::uint64_t bits = splitmix64(state.mSeed + static_cast<std::uint64_t>(state.mRolls++) * kGolden);
        return static_cast<float>(bits >> 40) / 16777216.f; // 24 bits: exactly representable, in [0, 1)
    }

    void aiEngage(AiActorState& state, std::uint64_t seed, float now)
    {
        state = AiActorState();
        state.mSeed = seed;
        state.mMode = AiMode::Advance;
        state.mNextDecision = now;
        state.mCircleDir = aiRoll(state) < 0.5f ? 1 : -1;
    }

    AiOutput aiThink(AiActorState& state, const AiView& v, const AiProfile& profile)
    {
        AiOutput out;
        if (state.mMode == AiMode::Calm)
            return out;
        const CombatStyleRecord& style = profile.mStyle;
        auto setMode = [&](AiMode mode) {
            if (state.mMode != mode)
            {
                state.mMode = mode;
                out.mModeChanged = true;
            }
        };
        auto lowerGuard = [&]() {
            if (state.mGuardUp)
            {
                state.mGuardUp = false;
                out.mAction = AiAction::LowerGuard;
            }
        };
        if (!v.mTargetAlive)
        {
            lowerGuard();
            setMode(AiMode::Calm);
            out.mLost = true;
            return out;
        }
        const bool rise = v.mTargetSwinging && !state.mPrevTargetSwinging;
        state.mPrevTargetSwinging = v.mTargetSwinging;

        // Flee: at or below the health fraction of its confidence, unless it is cornered (a coward at full health
        // flees at once).
        const float fleeAt = fleeHealthFraction(profile.mConfidence);
        if (state.mMode != AiMode::Retreat && !state.mCornered && fleeAt > 0.f && v.mHealthFraction <= fleeAt
            && v.mHealthFraction < state.mRecoveredAt - kAiRecoverMargin)
        {
            lowerGuard();
            setMode(AiMode::Retreat);
            state.mFleeTry = 0;
            state.mFleeStuck = false;
            state.mFleeStartHealth = v.mHealthFraction;
        }
        if (state.mMode == AiMode::Retreat)
        {
            if (state.mCornered)
                setMode(AiMode::Duel);
            else if (state.mFleeStartHealth < kAiRestoreHealthFraction && v.mHealthFraction >= kAiRestoreHealthFraction)
            {
                // The health is back: it stops running (and does not run again until it is hurt anew).
                setMode(AiMode::Duel);
                state.mRecoveredAt = v.mHealthFraction;
                state.mNextDecision = v.mNow;
                out.mRecovered = true;
            }
            else if (v.mDistance >= v.mFleeDistance)
            {
                setMode(AiMode::Calm);
                out.mLost = true;
                return out;
            }
            else if (state.mFleeStuck)
            {
                if (v.mDistance <= kAiCorneredDistance)
                {
                    state.mCornered = true;
                    out.mBecameCornered = true;
                    setMode(AiMode::Duel);
                }
                else
                {
                    out.mMove = AiMove::None; // nowhere to go: it waits for the target to come
                    return out;
                }
            }
            else
            {
                out.mMove = AiMove::Flee;
                return out;
            }
        }

        const bool ranged = v.mRange > 0.f;
        const float attackDistance = ranged ? v.mRange * kAiRangedFraction : v.mReach;
        if (!(v.mDistance <= attackDistance))
        {
            // Too far: close in (a caster may cast on the way).
            lowerGuard();
            setMode(AiMode::Advance);
            if (v.mCanCast && !v.mBusy && v.mNow >= state.mNextDecision && v.mDistance > v.mReach + kAiCastMinDistance)
            {
                state.mNextDecision = v.mNow + kAiDecisionPeriod;
                ++state.mDecisions;
                const float chance
                    = std::min(kAiCastMaxChance, kAiCastBaseChance * std::max(0.f, finiteOr(style.mMagicScore, 1.f)));
                if (aiRoll(state) < chance)
                {
                    out.mAction = AiAction::Cast;
                    return out;
                }
            }
            out.mMove = v.mBusy ? AiMove::None : AiMove::Approach;
            return out;
        }
        if (state.mMode == AiMode::Advance)
        {
            setMode(AiMode::Duel);
            state.mNextDecision = v.mNow; // arrived: decide at once
        }
        if (rise && (state.mMode == AiMode::Circle || state.mMode == AiMode::Fallback))
        {
            setMode(AiMode::Duel);
            state.mNextDecision = v.mNow;
        }
        // A ranged fighter that has been closed in on steps back.
        if (ranged && v.mDistance < kAiRangedMinDistance && state.mMode != AiMode::Fallback && !v.mBusy)
        {
            lowerGuard();
            setMode(AiMode::Fallback);
            state.mUntil = v.mNow + kAiFallbackHold;
        }

        switch (state.mMode)
        {
            case AiMode::Block:
                if (v.mNow < state.mUntil)
                {
                    out.mMove = AiMove::None;
                    return out;
                }
                lowerGuard();
                setMode(AiMode::Duel);
                state.mNextDecision = v.mNow;
                break;
            case AiMode::Circle:
                if (v.mNow < state.mUntil)
                {
                    out.mMove = AiMove::Circle;
                    return out;
                }
                setMode(AiMode::Duel);
                state.mNextDecision = v.mNow;
                break;
            case AiMode::Fallback:
                if (v.mNow < state.mUntil)
                {
                    out.mMove = AiMove::Away;
                    return out;
                }
                setMode(AiMode::Duel);
                state.mNextDecision = v.mNow;
                break;
            default:
                break;
        }

        // Duel: a decision when one is due (a swing of the target makes one due at once).
        if (v.mBusy)
            return out;
        if (rise)
            state.mNextDecision = v.mNow;
        if (v.mNow < state.mNextDecision)
            return out;
        ++state.mDecisions;
        const float stamina = std::clamp(finiteOr(v.mStaminaFraction, 1.f), 0.f, 1.f);
        const bool dueling = (style.mFlags & CombatStyleFlag::Dueling) != 0;
        const float wAttack = std::max(kAiMinAttackWeight, finiteOr(style.mOffensive, 0.24f))
            * (stamina > kAiLowAttackStamina ? 1.f : 0.2f);
        const float wBlock = (v.mCanBlock && stamina > kAiNoBlockStamina)
            ? std::max(0.f, finiteOr(style.mDefensive, 0.24f)) * (v.mTargetSwinging ? 2.f : 0.5f)
            : 0.f;
        const float wCircle = ranged ? 0.f
                                     : std::max(0.f, finiteOr(style.mCircle, 0.22f)) * (dueling ? 1.f : 0.5f);
        const float wFallback = (dueling && !ranged)
            ? std::max(0.f, finiteOr(style.mFallback, 0.11f)) * (stamina < kAiTiredStamina ? 2.f : 0.5f)
            : 0.f;
        const float total = wAttack + wBlock + wCircle + wFallback + kAiWaitWeight;
        const float pick = aiRoll(state) * total;
        float edge = wAttack;
        if (pick < edge)
        {
            const float rollBash = aiRoll(state);
            const float rollPower = aiRoll(state);
            const float bash = std::max(0.f, finiteOr(style.mBash, 0.76f));
            const float chanceBash = v.mTargetBlocking ? std::min(0.9f, kAiBashChanceBlocked * bash)
                                                       : std::min(0.5f, kAiBashChance * bash);
            const float chancePower = (stamina > kAiPowerStamina ? kAiPowerChance : kAiPowerChanceTired)
                * (v.mTargetBlocking ? std::max(0.f, finiteOr(style.mPowerAttackBlocking, 0.72f)) : 1.f);
            if (!ranged && rollBash < chanceBash)
                out.mAction = AiAction::Bash;
            else if (!ranged && rollPower < chancePower)
                out.mAction = AiAction::PowerAttack;
            else
                out.mAction = AiAction::Attack;
            state.mNextDecision = v.mNow + kAiDecisionPeriod;
            return out;
        }
        edge += wBlock;
        if (pick < edge)
        {
            const float hold = kAiBlockHoldMin + (kAiBlockHoldMax - kAiBlockHoldMin) * aiRoll(state);
            setMode(AiMode::Block);
            state.mUntil = v.mNow + hold;
            state.mNextDecision = state.mUntil;
            state.mGuardUp = true;
            out.mAction = AiAction::RaiseGuard;
            out.mGuardHold = hold;
            return out;
        }
        edge += wCircle;
        if (pick < edge)
        {
            if (aiRoll(state) < 0.5f)
                state.mCircleDir = -state.mCircleDir;
            const float hold = kAiCircleHoldMin + (kAiCircleHoldMax - kAiCircleHoldMin) * aiRoll(state);
            setMode(AiMode::Circle);
            state.mUntil = v.mNow + hold;
            out.mMove = AiMove::Circle;
            return out;
        }
        edge += wFallback;
        if (pick < edge)
        {
            setMode(AiMode::Fallback);
            state.mUntil = v.mNow + kAiFallbackHold;
            out.mMove = AiMove::Away;
            return out;
        }
        state.mNextDecision = v.mNow + kAiDecisionPeriod; // wait
        return out;
    }

    AiPoint aiApproachPoint(AiPoint self, AiPoint target, double standOff)
    {
        const double dx = self.x - target.x;
        const double dy = self.y - target.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 1e-6) || length <= standOff)
            return self;
        return { target.x + dx / length * standOff, target.y + dy / length * standOff };
    }

    AiPoint aiCirclePoint(AiPoint self, AiPoint target, double radius, int direction, double arc)
    {
        const double dx = self.x - target.x;
        const double dy = self.y - target.y;
        if (!(radius > 1e-6))
            return self;
        double angle = (dx * dx + dy * dy) > 1e-12 ? std::atan2(dy, dx) : 0.0;
        angle += (direction >= 0 ? 1.0 : -1.0) * arc / radius;
        return { target.x + radius * std::cos(angle), target.y + radius * std::sin(angle) };
    }

    AiPoint aiAwayPoint(AiPoint self, AiPoint target, double distance, int attempt)
    {
        double dx = self.x - target.x;
        double dy = self.y - target.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length > 1e-6)
        {
            dx /= length;
            dy /= length;
        }
        else
        {
            dx = 1.0;
            dy = 0.0;
        }
        const int index = std::clamp(attempt, 0, kAiFleeAttempts - 1);
        const double turn = kFleeTurnDegrees[index] * kPi / 180.0;
        const double c = std::cos(turn);
        const double s = std::sin(turn);
        return { self.x + (dx * c - dy * s) * distance, self.y + (dx * s + dy * c) * distance };
    }
}
