// Task 0299 (dungeon traps): a trap's shot in the combat rules.
//  - A script's Weapon.Fire (the DartTrap script of the Bleak Falls Barrow dart ports) reaches the combat as a TrapShot:
//    a projectile that nobody fired (ProjectileRequest::ownerless) from the source reference's place along its facing.
//    The weapon and the ammunition are looked up by form id; the shot flies at the ammunition's PROJ speed, gravity and
//    range, and does (weapon damage + ammunition damage) x 1 when it lands, less the victim's armour: a trap has no
//    skill and no draw. The victim recoils or dies by the same rules as a hit (copied in applyTrapDamage, which shares
//    nothing with applyHit because a trap has no attacker: no perks, no sneak bonus, no provocation).
//  - trapAimOf turns a placement's stored Euler angles into the engine's heading and pitch.
// Main thread only, in combatUpdate. No clock but the combat clock.
#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
        std::string num(double value)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof buffer, "%.3f", value);
            return buffer;
        }

        std::string who(const CombatSubject& subject)
        {
            if (subject.player)
                return "player";
            return "actor" + std::to_string(subject.actor.reference) + "/" + std::to_string(subject.actor.session);
        }

        constexpr float kHalfPi = 1.5707963f;
    }

    TrapAim trapAimOf(double rx, double ry, double rz)
    {
        // R = Rx(-rx) * Ry(-ry) * Rz(-rz) applied to the model's forward axis +Y (the angles are negated as
        // eulerToQuat does: Skyrim turns an object clockwise for a positive stored angle).
        if (!std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz))
            return {};
        double x = 0.0, y = 1.0, z = 0.0;
        // about Z first
        {
            const double a = -rz;
            const double nx = x * std::cos(a) - y * std::sin(a);
            const double ny = x * std::sin(a) + y * std::cos(a);
            x = nx;
            y = ny;
        }
        // then about Y
        {
            const double a = -ry;
            const double nx = x * std::cos(a) + z * std::sin(a);
            const double nz = -x * std::sin(a) + z * std::cos(a);
            x = nx;
            z = nz;
        }
        // then about X
        {
            const double a = -rx;
            const double ny = y * std::cos(a) - z * std::sin(a);
            const double nz = y * std::sin(a) + z * std::cos(a);
            y = ny;
            z = nz;
        }
        TrapAim aim;
        const double flat = std::hypot(x, y);
        aim.heading = flat > 1.0e-9 ? static_cast<float>(std::atan2(x, y)) : 0.f;
        aim.pitch = static_cast<float>(std::atan2(z, flat));
        return aim;
    }

    void trapMuzzle(const double (&from)[3], const TrapAim& aim, double (&out)[3])
    {
        const double h = static_cast<double>(aim.heading), p = static_cast<double>(aim.pitch);
        out[0] = from[0] + std::sin(h) * std::cos(p) * kTrapMuzzleForward;
        out[1] = from[1] + std::cos(h) * std::cos(p) * kTrapMuzzleForward;
        out[2] = from[2] + std::sin(p) * kTrapMuzzleForward;
    }

    bool SkyrimCombat::fireTrapShot(CombatRequestWriter& writer, const TrapShot& shot)
    {
        if (mData == nullptr || !std::isfinite(shot.x) || !std::isfinite(shot.y) || !std::isfinite(shot.z)
            || !std::isfinite(shot.heading) || !std::isfinite(shot.pitch) || !(std::abs(shot.pitch) < kHalfPi))
            return false;
        if (!mHaveSettings)
            loadSettings();
        const WeaponRecord* weapon = mData->weaponById(shot.mWeapon);
        const AmmoRecord* ammo = mData->ammoById(shot.mAmmo);
        const ProjectileRecord* projectile = ammo != nullptr ? mData->projectileOf(*ammo) : nullptr;
        if (weapon == nullptr || !weapon->mHasData || ammo == nullptr || !ammo->mHasData || projectile == nullptr
            || !projectile->mHasData)
        {
            if (mTrace)
                trace("COMBAT trap shot refused: weapon " + std::to_string(shot.mWeapon) + " ammo "
                    + std::to_string(shot.mAmmo) + (weapon == nullptr ? " (no such weapon)" : "")
                    + (ammo == nullptr ? " (no such ammo)" : "") + (projectile == nullptr ? " (no projectile)" : ""));
            return false;
        }
        ProjectileRequest request;
        request.token = mNextToken++;
        request.x = shot.x;
        request.y = shot.y;
        request.z = shot.z;
        request.heading = shot.heading;
        request.pitch = shot.pitch;
        request.speed = projectile->mSpeed;
        request.gravity = std::max(0.f, projectile->mGravity) * kProjectileGravityUnits;
        request.range = projectile->mRange;
        request.stickDepth = mProjectileSettings.mMissileStickDepth;
        request.stickSeconds = mProjectileSettings.mAttachedArrowAgeMax;
        const std::string model = projectileModelPath(projectile->mModel);
        request.model = model;
        request.ownerless = true;
        if (!writer.launchProjectile(CombatSubject{}, request))
            return false;
        if (mTrapShots.size() >= kMaxShotsRemembered)
            mTrapShots.erase(mTrapShots.begin()); // a dart whose end never came: the oldest is forgotten
        mTrapShots.push_back({ request.token, shot.mWeapon, shot.mAmmo });
        if (mTrace)
            trace("COMBAT trap shot launch token " + std::to_string(request.token) + " from " + num(request.x) + " "
                + num(request.y) + " " + num(request.z) + " heading " + num(request.heading) + " pitch "
                + num(request.pitch) + " speed " + num(request.speed));
        return true;
    }

    bool SkyrimCombat::resolveTrapShot(CombatRequestWriter& writer, const ProjectileResult& result)
    {
        if (result.owner.valid())
            return false;
        const auto found = std::find_if(
            mTrapShots.begin(), mTrapShots.end(), [&](const TrapFlight& s) { return s.mToken == result.token; });
        if (found == mTrapShots.end())
            return false;
        const TrapFlight flight = *found;
        mTrapShots.erase(found);
        if (mTrace)
            trace("COMBAT trap shot ended token " + std::to_string(result.token) + " fate "
                + std::to_string(static_cast<int>(result.fate)) + " at " + num(result.x) + " " + num(result.y) + " "
                + num(result.z));
        if (result.fate != ProjectileFate::HitActor && result.fate != ProjectileFate::HitPlayer)
            return true;
        Fighter* victim = result.fate == ProjectileFate::HitPlayer ? find(CombatSubject::ofPlayer())
                                                                    : find(CombatSubject::of(result.actor));
        const WeaponRecord* weapon = mData != nullptr ? mData->weaponById(flight.mWeapon) : nullptr;
        const AmmoRecord* ammo = mData != nullptr ? mData->ammoById(flight.mAmmo) : nullptr;
        if (victim == nullptr || victim->mDead || !ready(*victim) || weapon == nullptr || ammo == nullptr)
            return true;
        BowInput input;
        input.mBow = weapon;
        input.mAmmo = ammo;
        input.mDrawFraction = 1.f;
        input.mArcherySkill = 0.f;
        input.mTargetMitigationPercent = victim->mProfile.mMitigationPercent;
        const BowHit hit = computeBowHit(input, mCombatSettings);
        applyTrapDamage(writer, *victim, hit.mDamageAfter, result.x, result.y, result.z);
        return true;
    }

    void SkyrimCombat::applyTrapDamage(
        CombatRequestWriter& writer, Fighter& victim, float damage, double x, double y, double z)
    {
        if (mKillMoveActive && (victim.mSubject.player || victim.mSubject.actor == mKillMoveVictim))
            return;
        float current = victim.mHealth;
        float maximum = victim.mProfile.mHealthMax;
        if (victim.mSubject.player)
        {
            // The player's health is the engine's: read it when the engine has one.
            SubjectState state;
            if (writer.subjectState(victim.mSubject, state) && state.healthMax > 0.f)
            {
                current = state.health;
                maximum = state.healthMax;
            }
        }
        damage = std::isfinite(damage) ? std::max(0.f, damage) : 0.f;
        float after = std::max(0.f, current - damage);
        const bool collapsed = mEssentialBleedout && after <= 0.f && !victim.mSubject.player
            && victim.mProfile.mEssential && !victim.mDead;
        if (collapsed)
            after = 1.f; // an essential actor collapses into bleedout instead of dying
        victim.mHealth = after;
        if (collapsed && !victim.mBleedout)
        {
            victim.mBleedout = true;
            if (mBleedoutSink)
                mBleedoutSink(victim.mSubject.actor);
        }
        victim.mCombatUntil = mClock + kCombatLingerSeconds;
        ++mHitsResolved;

        DamageReport report;
        report.attackerIsPlayer = false;
        report.victimIsPlayer = victim.mSubject.player;
        report.victim = victim.mSubject.actor;
        report.amount = damage;
        report.healthAfter = after;
        report.healthMax = maximum;
        report.x = x;
        report.y = y;
        report.z = z;
        writer.reportDamage(report);
        if (mTrace)
            trace("COMBAT trap damage -> " + who(victim.mSubject) + " " + num(damage) + " health " + num(current)
                + " -> " + num(after) + " of " + num(maximum));

        if (after > 0.f)
        {
            if (!victim.mSubject.player)
            {
                const std::string_view clip = meleeClip(ClipSlot::Recoil, victim, false);
                if (!clip.empty() && !isParalyzed(victim))
                {
                    ClipRequest request;
                    request.slot = ClipSlot::Recoil;
                    request.clip = clip;
                    request.blendSeconds = 0.05f;
                    request.token = mNextToken++;
                    if (writer.playClip(victim.mSubject, request))
                    {
                        victim.mSwinging = false;
                        victim.mStaggered = false;
                    }
                }
            }
            return;
        }
        if (victim.mDead)
            return;
        victim.mDead = true;
        victim.mStaggered = false;
        if (mTrace)
            trace("COMBAT death " + who(victim.mSubject));
        if (!victim.mSubject.player)
        {
            std::string_view clip = clipOf(victim, ClipSlot::Death);
            if (clip.empty())
                clip = meleeClip(ClipSlot::Stagger, victim, false);
            if (!clip.empty())
            {
                ClipRequest request;
                request.slot = ClipSlot::Death;
                request.clip = clip;
                request.blendSeconds = 0.05f;
                request.token = mNextToken++;
                (void)writer.playClip(victim.mSubject, request);
            }
            victim.mSwinging = false;
            (void)writer.reportDeath(victim.mSubject.actor);
        }
    }
}
