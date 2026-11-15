#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::SweepHit;
        using openeden::sdk::game::SweepTarget;

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        // Clip names of Skyrim - Animations.bsa, meshes/actors/character/animations (read from the listing; events in
        // animationdata/defaultmale.txt: AttackRight has preHitFrame 0.233, weaponSwing 0.333, HitFrame 0.367, attackStop
        // 1.287 playback seconds). Case as in the sidecar's generator names, the file system of the VFS ignores case.
        constexpr SlotTable::Entry sHumanoid[] = {
            { ClipSlot::Attack, kWeaponAnim1HSword, "1HM_AttackRight" },
            { ClipSlot::AttackLeft, kWeaponAnim1HSword, "1HM_AttackLeft" },
            { ClipSlot::PowerAttack, kWeaponAnim1HSword, "1HM_AttackPower" },
            { ClipSlot::BlockHit, kWeaponAnim1HSword, "1HM_BlockHit" },
            { ClipSlot::Bash, kWeaponAnim1HSword, "1HM_BlockBash" },
            { ClipSlot::Recoil, kWeaponAnim1HSword, "1HM_RecoilRight" },
            { ClipSlot::Stagger, kWeaponAnim1HSword, "1HM_StaggerBackMedium" },
        };

        std::string fmt(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(value));
            return buffer;
        }

        std::string name(const CombatSubject& subject)
        {
            if (subject.player)
                return "player";
            return "actor" + std::to_string(subject.actor.reference) + "/" + std::to_string(subject.actor.session);
        }
    }

    SlotTable SlotTable::humanoid()
    {
        SlotTable table;
        table.mEntries = sHumanoid;
        table.mCount = std::size(sHumanoid);
        return table;
    }

    SkyrimCombat::SkyrimCombat()
        : mSlots(SlotTable::humanoid())
    {
        mFighters.reserve(256);
        mPendingHits.reserve(32);
        mAwaiting.reserve(32);
        mDeliveries.reserve(32);
        Fighter player;
        player.mSubject = CombatSubject::ofPlayer();
        mFighters.push_back(player);
    }

    void SkyrimCombat::setData(CombatData* data)
    {
        mData = data;
        mHaveSettings = false;
        mWeaponLooked = false;
        mWeapon = nullptr;
        for (Fighter& f : mFighters)
            f.mReady = false;
    }

    bool SkyrimCombat::setTestSwing(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestSwing.clear();
            return false;
        }
        mTestSwing = lowerAscii(editorId);
        return true;
    }

    bool SkyrimCombat::setTestHostile(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestHostile.clear();
            return false;
        }
        mTestHostile = lowerAscii(editorId);
        return true;
    }

    bool SkyrimCombat::setTestWeapon(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestWeapon.clear();
            return false;
        }
        mTestWeapon = lowerAscii(editorId);
        mWeaponLooked = false;
        mWeapon = nullptr;
        return true;
    }

    void SkyrimCombat::setTestPlayerSwing(float seconds)
    {
        mPlayerAutoPeriod = (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
    }

    SkyrimCombat::Fighter* SkyrimCombat::find(CombatSubject subject)
    {
        for (Fighter& f : mFighters)
            if (f.mSubject == subject)
                return &f;
        return nullptr;
    }

    const SkyrimCombat::Fighter* SkyrimCombat::find(CombatSubject subject) const
    {
        for (const Fighter& f : mFighters)
            if (f.mSubject == subject)
                return &f;
        return nullptr;
    }

    bool SkyrimCombat::fighterView(CombatSubject subject, FighterView& out) const
    {
        out = {};
        const Fighter* f = find(subject);
        if (f == nullptr || !f->mReady || !f->mHasProfile)
            return false;
        out.mHealth = f->mHealth;
        out.mHealthMax = f->mProfile.mHealthMax;
        out.mStamina = f->mStamina;
        out.mStaminaMax = f->mProfile.mStaminaMax;
        out.mDead = f->mDead;
        return true;
    }

    bool SkyrimCombat::ready(Fighter& fighter)
    {
        if (mData == nullptr)
            return false;
        if (!mHaveSettings)
        {
            mCombatSettings = mData->settings();
            mHaveSettings = true;
        }
        if (!mWeaponLooked)
        {
            mWeaponLooked = true;
            mWeapon = mTestWeapon.empty() ? nullptr : mData->weaponByEditorId(mTestWeapon);
        }
        if (!fighter.mReady)
        {
            fighter.mReady = true;
            fighter.mHasProfile = fighter.mSubject.player ? true : mData->fighter(fighter.mBase, fighter.mProfile);
            if (fighter.mSubject.player)
                fighter.mProfile = mData->playerFighter();
            fighter.mHealth = fighter.mProfile.mHealthMax;
            fighter.mStamina = fighter.mProfile.mStaminaMax;
        }
        return fighter.mHasProfile;
    }

    void SkyrimCombat::onActorAdded(const openeden::sdk::game::ActorInfo& info)
    {
        if (info.handle.valid())
        {
            // The same placement again with a new session replaces the old fighter (its state starts again).
            std::erase_if(mFighters,
                [&](const Fighter& f) { return !f.mSubject.player && f.mSubject.actor.reference == info.handle.reference; });
            if (mFighters.size() < kMaxCombatFighters)
            {
                Fighter fighter;
                fighter.mSubject = CombatSubject::of(info.handle);
                fighter.mBase = info.base;
                mFighters.push_back(fighter);
            }
        }
        const std::string id = lowerAscii(info.editorId);
        if (!info.handle.valid())
            return;
        if (!mTestSwing.empty() && id == mTestSwing)
        {
            std::erase_if(mSwingers, [&](const ActorHandle& h) { return h.reference == info.handle.reference; });
            mSwingers.push_back(info.handle);
        }
        if (!mTestHostile.empty() && id == mTestHostile)
        {
            std::erase_if(mHostiles, [&](const ActorHandle& h) { return h.reference == info.handle.reference; });
            mHostiles.push_back(info.handle);
        }
    }

    void SkyrimCombat::onActorRemoved(ActorHandle handle)
    {
        std::erase(mSwingers, handle);
        std::erase(mHostiles, handle);
        std::erase_if(mFighters, [&](const Fighter& f) { return !f.mSubject.player && f.mSubject.actor == handle; });
    }

    void SkyrimCombat::onPlayerAttack(bool power)
    {
        mPlayerAttackPending = true;
        mPlayerPower = power;
    }

    void SkyrimCombat::onClipEvent(const openeden::sdk::game::ClipEvent& event)
    {
        ++mEventsSeen;
        const bool attackSlot = event.slot == ClipSlot::Attack || event.slot == ClipSlot::PowerAttack
            || event.slot == ClipSlot::AttackLeft;
        if (mData == nullptr || event.kind != openeden::sdk::game::ClipEventKind::Hit || !attackSlot)
        {
            if (event.kind == openeden::sdk::game::ClipEventKind::Hit)
                ++mRefusedEvents; // a hit with no rule to act on it
            return;
        }
        if (mPendingHits.size() < mPendingHits.capacity())
            mPendingHits.push_back({ event.subject, event.token, event.slot == ClipSlot::PowerAttack });
    }

    void SkyrimCombat::onClipFinished(CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how)
    {
        ++mClipsFinished;
        if (how == openeden::sdk::game::ClipEnd::Completed)
            ++mClipsCompleted;
        if (Fighter* f = find(subject); f != nullptr && f->mSwinging && f->mSwingToken == token)
            f->mSwinging = false;
    }

    void SkyrimCombat::onSweepResult(CombatSubject subject, std::uint32_t token, const SweepHit* hits, std::size_t count)
    {
        const auto waiting = std::find_if(mAwaiting.begin(), mAwaiting.end(),
            [&](const PendingHit& p) { return p.mSubject == subject && p.mToken == token; });
        if (waiting == mAwaiting.end() || mDeliveries.size() >= mDeliveries.capacity())
        {
            ++mRefusedSweepResults;
            return;
        }
        SweepDelivery delivery;
        delivery.mWho = *waiting;
        delivery.mCount = std::min(count, openeden::sdk::game::kMaxSweepHits);
        for (std::size_t i = 0; i < delivery.mCount && hits != nullptr; ++i)
            delivery.mHits[i] = hits[i];
        mAwaiting.erase(waiting);
        mDeliveries.push_back(delivery);
    }

    void SkyrimCombat::regenerate(float seconds)
    {
        for (Fighter& f : mFighters)
        {
            if (!f.mReady || !f.mHasProfile || f.mDead)
                continue;
            const bool inCombat = mClock < f.mCombatUntil;
            if (!f.mSubject.player)
                f.mHealth = std::min(f.mProfile.mHealthMax,
                    f.mHealth
                        + regenAmount(f.mProfile.mHealthMax, f.mProfile.mHealthRegenPercent,
                            mCombatSettings.mCombatHealthRegenRateMult, inCombat, seconds));
            f.mStamina = std::min(f.mProfile.mStaminaMax,
                f.mStamina
                    + regenAmount(f.mProfile.mStaminaMax, f.mProfile.mStaminaRegenPercent,
                        mCombatSettings.mCombatStaminaRegenRateMult, inCombat, seconds));
        }
    }

    void SkyrimCombat::beginAttack(CombatRequestWriter& writer, Fighter& fighter, bool power)
    {
        if (fighter.mSubject.player)
        {
            if (mPlayerSwingActive)
                return;
            // The player's clip is never played (no first-person arms): its timing is the sidecar's, asked for by the
            // clip the slot would play. Until it is ready there is no swing (stage S5-3b: no fallback number).
            const std::string_view playerClip
                = mSlots.clipFor(power ? ClipSlot::PowerAttack : ClipSlot::Attack, kWeaponAnim1HSword);
            openeden::sdk::game::ClipTiming timing;
            if (playerClip.empty() || !writer.clipTiming(playerClip, false, timing) || !timing.hasHit
                || !(timing.length > 0.f))
            {
                if (mPlayerTimingSince < 0.f)
                    mPlayerTimingSince = mClock;
                else if (mClock - mPlayerTimingSince >= kPlayerTimingTimeoutSeconds && mPlayerTimingTimeouts == 0)
                {
                    ++mPlayerTimingTimeouts;
                    const std::string line = "COMBAT player swing timing never arrived for '" + std::string(playerClip)
                        + "': the player cannot swing";
                    if (mWarning)
                        mWarning(line);
                    trace(line);
                }
                return;
            }
            mPlayerTimingSince = -1.f;
            mPlayerHitTime = timing.hitTime;
            mPlayerSwingLength = timing.length;
            mPlayerSwingActive = true;
            mPlayerHitDone = false;
            mPlayerSwingStart = mClock;
            mPlayerSwingToken = mNextToken++;
            mPlayerPower = power;
            ++mSwingsRequested;
            trace("COMBAT swing player token " + std::to_string(mPlayerSwingToken) + (power ? " power" : "") + " hit at "
                + std::to_string(mPlayerHitTime) + " length " + std::to_string(mPlayerSwingLength));
            return;
        }
        if (fighter.mDead || fighter.mSwinging)
            return;
        const ClipSlot slot = power ? ClipSlot::PowerAttack : ClipSlot::Attack;
        const std::string_view clip = mSlots.clipFor(slot, kWeaponAnim1HSword);
        if (clip.empty())
            return;
        openeden::sdk::game::ClipRequest request;
        request.slot = slot;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.1f;
        request.token = mNextToken++;
        if (writer.playClip(fighter.mSubject, request))
        {
            fighter.mSwinging = true;
            fighter.mSwingToken = request.token;
            ++mSwingsRequested;
            trace("COMBAT swing " + name(fighter.mSubject) + " token " + std::to_string(request.token));
        }
    }

    void SkyrimCombat::startHostiles(CombatRequestWriter& writer)
    {
        if (mHostiles.empty())
            return;
        openeden::sdk::game::SubjectState player;
        bool havePlayer = false;
        for (const ActorHandle& handle : mHostiles)
        {
            Fighter* f = find(CombatSubject::of(handle));
            if (f == nullptr || f->mDead || f->mSwinging || mClock < f->mNextAttack || !ready(*f))
                continue;
            if (!havePlayer)
            {
                havePlayer = writer.subjectState(CombatSubject::ofPlayer(), player);
                if (!havePlayer)
                    return;
            }
            openeden::sdk::game::SubjectState self;
            if (!writer.subjectState(f->mSubject, self))
                continue;
            const double dx = player.x - self.x;
            const double dy = player.y - self.y;
            const float reach = meleeReachUnits(mWeapon, f->mProfile.mUnarmedReach, mCombatSettings);
            // The sweep reaches `reach` from the actor to the player's capsule: close enough when the centres are within
            // that and a body radius, less a little so a hit is certain.
            if (std::sqrt(dx * dx + dy * dy) > static_cast<double>(reach + kBodyRadius - 4.f))
                continue;
            writer.faceHeading(f->mSubject, static_cast<float>(std::atan2(dx, dy)));
            f->mNextAttack = mClock + kHostileAttackPeriod;
            beginAttack(writer, *f, false);
        }
    }

    void SkyrimCombat::runPlayerSwing(CombatRequestWriter& writer)
    {
        if (mData == nullptr)
            return;
        Fighter& player = mFighters.front();
        if (!ready(player))
            return;
        if (!mPlayerTimingKicked)
        {
            // Start reading the sidecars of the player's attack clips now, long before the first swing needs them (the
            // answer is ignored here; the swing asks again). Reading is on the core's worker.
            mPlayerTimingKicked = true;
            openeden::sdk::game::ClipTiming ignored;
            for (const ClipSlot slot : { ClipSlot::Attack, ClipSlot::PowerAttack })
            {
                const std::string_view clip = mSlots.clipFor(slot, kWeaponAnim1HSword);
                if (!clip.empty())
                    (void)writer.clipTiming(clip, false, ignored);
            }
        }
        // A press while a swing runs is dropped (no buffering in this stage).
        const bool pressed = mPlayerAttackPending;
        mPlayerAttackPending = false;
        if (!mPlayerSwingActive)
        {
            bool start = false;
            bool power = false;
            bool automatic = false;
            if (pressed)
            {
                start = true;
                power = mPlayerPower;
            }
            else if (mPlayerAutoPeriod > 0.f && mClock >= mNextPlayerAuto)
            {
                start = true;
                mNextPlayerAuto = mClock + mPlayerAutoPeriod;
                automatic = true;
            }
            if (start)
            {
                beginAttack(writer, player, power);
                // An automatic swing whose timing is not ready yet asks again in the next frame, not a period later.
                if (automatic && !mPlayerSwingActive)
                    mNextPlayerAuto = mClock;
            }
        }
        if (mPlayerSwingActive)
        {
            const float elapsed = mClock - mPlayerSwingStart;
            if (!mPlayerHitDone && elapsed >= mPlayerHitTime)
            {
                mPlayerHitDone = true;
                processHit(writer, { player.mSubject, mPlayerSwingToken, mPlayerPower });
            }
            if (elapsed >= mPlayerSwingLength)
                mPlayerSwingActive = false;
        }
    }

    void SkyrimCombat::processHit(CombatRequestWriter& writer, const PendingHit& hit)
    {
        Fighter* attacker = find(hit.mSubject);
        if (attacker == nullptr || attacker->mDead || !ready(*attacker))
            return;
        MeleeInput input;
        input.mWeapon = mWeapon;
        input.mUnarmedDamage = attacker->mProfile.mUnarmedDamage;
        input.mPowerAttack = hit.mPower;
        if (mWeapon != nullptr && mWeapon->skillSlot() < attacker->mProfile.mSkills.size())
            input.mAttackerSkill = attacker->mProfile.mSkills[mWeapon->skillSlot()];
        const MeleeHit cost = computeMeleeHit(input, mCombatSettings);
        attacker->mStamina = std::max(0.f, attacker->mStamina - cost.mStaminaCost);
        attacker->mCombatUntil = mClock + kCombatLingerSeconds;
        openeden::sdk::game::SubjectState where;
        if (!writer.subjectState(attacker->mSubject, where))
            return;
        openeden::sdk::game::SweepRequest request;
        request.token = hit.mToken;
        request.x = where.x;
        request.y = where.y;
        request.z = where.z;
        request.heading = where.heading;
        request.reach = meleeReachUnits(mWeapon, attacker->mProfile.mUnarmedReach, mCombatSettings);
        request.halfAngle = kStrikeHalfAngle;
        request.heightBelow = kSweepHeightBelow;
        request.heightAbove = kSweepHeightAbove;
        trace("COMBAT hitframe " + name(attacker->mSubject) + " token " + std::to_string(hit.mToken) + " stamina "
            + fmt(attacker->mStamina) + " of " + fmt(attacker->mProfile.mStaminaMax) + " reach " + fmt(request.reach));
        if (writer.requestSweep(attacker->mSubject, request))
        {
            if (mAwaiting.size() < mAwaiting.capacity())
                mAwaiting.push_back(hit);
        }
    }

    void SkyrimCombat::applyHit(CombatRequestWriter& writer, Fighter& attacker, bool power, const SweepHit& hit, Fighter& victim)
    {
        MeleeInput input;
        input.mWeapon = mWeapon;
        input.mUnarmedDamage = attacker.mProfile.mUnarmedDamage;
        input.mPowerAttack = power;
        input.mTargetMitigationPercent = victim.mProfile.mMitigationPercent;
        if (mWeapon != nullptr && mWeapon->skillSlot() < attacker.mProfile.mSkills.size())
            input.mAttackerSkill = attacker.mProfile.mSkills[mWeapon->skillSlot()];
        const MeleeHit result = computeMeleeHit(input, mCombatSettings);

        float current = victim.mHealth;
        float maximum = victim.mProfile.mHealthMax;
        if (victim.mSubject.player)
        {
            // The player's health is the engine's: read it when the engine has one.
            openeden::sdk::game::SubjectState state;
            if (writer.subjectState(victim.mSubject, state) && state.healthMax > 0.f)
            {
                current = state.health;
                maximum = state.healthMax;
            }
        }
        const float after = std::max(0.f, current - result.mDamageAfter);
        victim.mHealth = after;
        victim.mCombatUntil = mClock + kCombatLingerSeconds;
        ++mHitsResolved;

        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = attacker.mSubject.player;
        report.attacker = attacker.mSubject.actor;
        report.victimIsPlayer = victim.mSubject.player;
        report.victim = victim.mSubject.actor;
        report.amount = result.mDamageAfter;
        report.healthAfter = after;
        report.healthMax = maximum;
        report.x = hit.x;
        report.y = hit.y;
        report.z = hit.z;
        writer.reportDamage(report);
        trace("COMBAT damage " + name(attacker.mSubject) + " -> " + name(victim.mSubject) + " before armour "
            + fmt(result.mDamageBefore) + " after armour " + fmt(result.mDamageAfter) + " (mitigation "
            + fmt(victim.mProfile.mMitigationPercent) + "%) health " + fmt(current) + " -> " + fmt(after) + " of "
            + fmt(maximum) + (power ? " power" : ""));

        if (after > 0.f)
        {
            if (!victim.mSubject.player)
            {
                const std::string_view clip = mSlots.clipFor(ClipSlot::Recoil, kWeaponAnim1HSword);
                if (!clip.empty())
                {
                    openeden::sdk::game::ClipRequest request;
                    request.slot = ClipSlot::Recoil;
                    request.clip = clip;
                    request.blendSeconds = 0.05f;
                    request.token = mNextToken++;
                    if (writer.playClip(victim.mSubject, request))
                    {
                        victim.mSwinging = false; // the reaction replaced its swing
                        trace("COMBAT reaction " + name(victim.mSubject) + " clip " + std::string(clip));
                    }
                }
            }
            return;
        }
        if (victim.mDead)
            return;
        victim.mDead = true;
        trace("COMBAT death " + name(victim.mSubject));
        if (!victim.mSubject.player)
        {
            // The stagger-back clip lays the body down; as a Death-slot clip the core holds its last pose until the ragdoll
            // (stage S5-5) exists. (The table has no Death entry: its test pins that, so the Stagger entry is used.)
            const std::string_view clip = mSlots.clipFor(ClipSlot::Stagger, kWeaponAnim1HSword);
            if (!clip.empty())
            {
                openeden::sdk::game::ClipRequest request;
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

    void SkyrimCombat::resolveSweep(CombatRequestWriter& writer, const SweepDelivery& delivery)
    {
        Fighter* attacker = find(delivery.mWho.mSubject);
        if (attacker == nullptr || !ready(*attacker))
            return;
        for (std::size_t i = 0; i < delivery.mCount; ++i)
        {
            const SweepHit& hit = delivery.mHits[i];
            if (hit.target == SweepTarget::World)
                continue;
            Fighter* victim = hit.target == SweepTarget::Player ? find(CombatSubject::ofPlayer())
                                                               : find(CombatSubject::of(hit.actor));
            if (victim == nullptr || victim == attacker || victim->mDead || !ready(*victim))
                continue;
            applyHit(writer, *attacker, delivery.mWho.mPower, hit, *victim);
            return; // one target a swing: the nearest valid one
        }
        trace("COMBAT miss " + name(attacker->mSubject) + " token " + std::to_string(delivery.mWho.mToken));
    }

    void SkyrimCombat::combatUpdate(float seconds, CombatRequestWriter& writer)
    {
        ++mUpdates;
        const float step = (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
        mClock += step;
        if (mData != nullptr)
        {
            if (!mHaveSettings)
                mCombatSettings = mData->settings(), mHaveSettings = true;
            regenerate(step);
            // Results cast last frame, then the hits the clips reached since.
            for (const SweepDelivery& delivery : mDeliveries)
                resolveSweep(writer, delivery);
            mDeliveries.clear();
            // processHit may push to mAwaiting but never to mPendingHits, so iterating a copy of the count is enough.
            for (std::size_t i = 0; i < mPendingHits.size(); ++i)
                processHit(writer, mPendingHits[i]);
            mPendingHits.clear();
            startHostiles(writer);
            runPlayerSwing(writer);
        }
        if (mSwingers.empty())
            return;
        if (mClock < mNextSwing)
            return;
        mNextSwing = mClock + kTestSwingPeriod;
        const std::string_view clip = mSlots.clipFor(ClipSlot::Attack, kWeaponAnim1HSword);
        for (const ActorHandle& handle : mSwingers)
        {
            openeden::sdk::game::ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = clip;
            request.clipPerSex = false;
            request.blendSeconds = 0.1f;
            request.token = mNextToken++;
            if (writer.playClip(CombatSubject::of(handle), request))
            {
                ++mSwingsRequested;
                if (Fighter* f = find(CombatSubject::of(handle)))
                {
                    f->mSwinging = true;
                    f->mSwingToken = request.token;
                }
            }
        }
    }
}
