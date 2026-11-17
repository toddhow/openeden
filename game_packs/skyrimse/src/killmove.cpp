#include "game_skyrimse/killmove.hpp"

#include <cmath>
#include <numbers>

namespace game_skyrimse::killmove
{
    namespace
    {
        constexpr double kPi = std::numbers::pi;

        float wrap(double angle)
        {
            if (!std::isfinite(angle))
                return 0.f;
            double a = std::fmod(angle, 2.0 * kPi);
            if (a > kPi)
                a -= 2.0 * kPi;
            else if (a <= -kPi)
                a += 2.0 * kPi;
            return static_cast<float>(a);
        }
    }

    bool eligible(const Candidate& c)
    {
        return c.mAttackerIsPlayer && !c.mVictimIsPlayer && c.mWeaponAnimType == kSwordAnimType && !c.mVictimIsCreature
            && !c.mVictimEssential && !c.mVictimDead && !c.mBash && !c.mActive;
    }

    float roll(std::uint64_t seed, std::uint64_t counter)
    {
        // splitmix64 (public domain) over seed and counter
        std::uint64_t z = seed + counter * 0x9E3779B97F4A7C15ull + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0));
    }

    bool chosen(std::uint64_t seed, std::uint64_t counter, float chance)
    {
        if (!(chance > 0.f))
            return false;
        if (chance >= 1.f)
            return true;
        return roll(seed, counter) < chance;
    }

    float headingToward(double fromX, double fromY, double toX, double toY, float fallback)
    {
        if (!std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) || !std::isfinite(toY))
            return fallback;
        const double dx = toX - fromX;
        const double dy = toY - fromY;
        if (std::hypot(dx, dy) < 1e-3)
            return fallback;
        return static_cast<float>(std::atan2(dx, dy));
    }

    WorldPoint toWorld(const Anchor& a, double x, double y, double z)
    {
        const double s = std::sin(static_cast<double>(a.mHeading));
        const double c = std::cos(static_cast<double>(a.mHeading));
        // x is to the killer's right, y forward (the horse frame's rule, riding.cpp horseFramePoint)
        return { a.mX + x * c + y * s, a.mY - x * s + y * c, a.mZ + z };
    }

    Anchor anchorFor(double victimX, double victimY, double victimZ, float killerHeading,
        const openeden::sdk::game::PairedPose& root)
    {
        const double s = std::sin(static_cast<double>(killerHeading));
        const double c = std::cos(static_cast<double>(killerHeading));
        Anchor a;
        a.mHeading = killerHeading;
        a.mX = victimX - (root.x * c + root.y * s);
        a.mY = victimY - (-root.x * s + root.y * c);
        a.mZ = victimZ - root.z;
        return a;
    }

    float victimHeading(float killerHeading, double secondYaw)
    {
        return wrap(static_cast<double>(killerHeading) - secondYaw);
    }

    bool KillMoves::offer(const Candidate& candidate, openeden::sdk::game::ActorHandle victim)
    {
        if (!mEnabled || !victim.valid())
            return false;
        Candidate c = candidate;
        c.mActive = c.mActive || mPhase != Phase::Idle;
        if (!eligible(c))
            return false;
        const std::uint64_t counter = mCounter++;
        if (!chosen(mSeed ^ (static_cast<std::uint64_t>(victim.reference) * 0xD6E8FEB86659FD93ull), counter, mChance))
            return false;
        mVictim = victim;
        mPhase = Phase::Asking;
        mWait = 0.f;
        mTime = 0.f;
        ++mStarted;
        if (mHold)
            mHold(victim.reference, true);
        if (mLog)
            mLog("KILLMOVE chosen for " + std::to_string(victim.reference));
        return true;
    }

    void KillMoves::reset()
    {
        mPhase = Phase::Idle;
        mVictim = {};
    }

    void KillMoves::finish(openeden::sdk::game::ActorRequestWriter* writer, const char* why)
    {
        using namespace openeden::sdk::game;
        if (writer != nullptr && mPhase == Phase::Playing)
        {
            // The killer is let go at the clip's last pose; the victim keeps it (the death takes it over).
            const WorldPoint end = toWorld(mAnchor, mInfo.end.x, mInfo.end.y, mInfo.end.z);
            if (auto* player = dynamic_cast<RidingWriter*>(writer))
                (void)player->releasePlayer(end.mX, end.mY, end.mZ);
        }
        else if (writer != nullptr && mPhase == Phase::Asking)
        {
            // nothing was taken from the player yet
        }
        const ActorHandle victim = mVictim;
        mPhase = Phase::Idle;
        mVictim = {};
        ++mFinished;
        if (mLog)
            mLog(std::string("KILLMOVE finished (") + why + ")");
        if (mFinish)
            mFinish(victim);
    }

    void KillMoves::update(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        using namespace openeden::sdk::game;
        if (mPhase == Phase::Idle)
            return;
        double vx = 0.0, vy = 0.0, vz = 0.0;
        if (!writer.actorPosition(mVictim, vx, vy, vz))
        {
            finish(&writer, "the victim cannot be found");
            return;
        }
        auto* paired = dynamic_cast<PairedWriter*>(&writer);
        auto* player = dynamic_cast<RidingWriter*>(&writer);
        if (paired == nullptr || player == nullptr)
        {
            finish(&writer, "the core cannot play paired clips");
            return;
        }
        if (mPhase == Phase::Asking)
        {
            PairedClipInfo info;
            const PairedStatus status = paired->pairedClipInfo(kKillerClip, info);
            if (status == PairedStatus::NotReady)
            {
                mWait += seconds;
                if (mWait >= kClipWaitSeconds)
                    finish(&writer, "the clip was not ready");
                return;
            }
            if (status == PairedStatus::Failed || !info.hasSecond || !(info.duration > 0.f))
            {
                finish(&writer, "the clip cannot be used");
                return;
            }
            double px = 0.0, py = 0.0, pz = 0.0;
            if (!writer.playerPosition(px, py, pz))
            {
                finish(&writer, "no player");
                return;
            }
            const float heading = headingToward(px, py, vx, vy, 0.f);
            mInfo = info;
            mAnchor = anchorFor(vx, vy, vz, heading, info.secondStart);
            mVictimHeading = victimHeading(heading, info.secondStart.yaw);
            mPhase = Phase::Playing;
            mTime = 0.f;
            (void)paired->playPairedClip(mVictim, kVictimClip);
            if (mLog)
                mLog("KILLMOVE playing " + std::to_string(info.duration) + " s anchor " + std::to_string(mAnchor.mX) + " "
                    + std::to_string(mAnchor.mY) + " heading " + std::to_string(mAnchor.mHeading));
        }
        // Playing: the victim stands still where the blow left him, the killer is held at the anchor.
        (void)writer.setMovement(mVictim, 0.f, 0.f);
        (void)writer.setHeading(mVictim, mVictimHeading);
        (void)player->carryPlayer(mAnchor.mX, mAnchor.mY, mAnchor.mZ, mAnchor.mHeading);
        mTime += seconds;
        if (mTime >= mInfo.duration)
            finish(&writer, "the clip is over");
    }
}
