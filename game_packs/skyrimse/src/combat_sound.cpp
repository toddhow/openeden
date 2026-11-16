#include "game_skyrimse/combat_sound.hpp"

#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace game_skyrimse
{
    namespace
    {
        namespace sdkgame = openeden::sdk::game;

        std::string volumeText(float volume)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(volume));
            return buffer;
        }
    }

    CombatSounds::CombatSounds()
    {
        mQueue.reserve(kQueue);
        mDrawn.reserve(kMaxDrawn);
    }

    void CombatSounds::bind(const SoundLibrary* library, SoundFileProbe exists)
    {
        mLibrary = library;
        mProbe = std::move(exists);
        mCache.clear();
        mQueue.clear();
    }

    std::string CombatSounds::subjectName(sdkgame::CombatSubject who)
    {
        if (who.player)
            return "player";
        return "actor " + std::to_string(who.actor.reference) + "/" + std::to_string(who.actor.session);
    }

    const CombatSounds::Cached& CombatSounds::cached(FormId sound)
    {
        const auto found = mCache.find(sound);
        if (found != mCache.end())
            return found->second;
        Cached entry;
        entry.mOk = resolveSoundSet(*mLibrary, sound, mProbe, entry.mSet);
        return mCache.emplace(sound, std::move(entry)).first->second;
    }

    bool CombatSounds::play(
        sdkgame::CombatRequestWriter& writer, sdkgame::CombatSubject who, FormId sound, std::string_view what)
    {
        if (!enabled() || sound == kNullFormId || !who.valid())
            return false;
        const Cached& entry = cached(sound);
        if (!entry.mOk)
        {
            ++mUnresolved;
            return false;
        }
        const std::string& file = entry.mSet.mFiles[mCounter++ % entry.mSet.mFiles.size()];
        sdkgame::SoundRequest request;
        request.subject = who;
        request.file = file;
        request.name = entry.mSet.mName;
        request.volume = entry.mSet.mVolume;
        const bool ok = writer.playSound(request);
        ++(ok ? mPlayed : mRefused);
        if (mTrace)
            trace("CSOUND " + std::string(what) + " " + entry.mSet.mName + " " + file + " volume "
                + volumeText(entry.mSet.mVolume) + " at " + subjectName(who) + (ok ? "" : " refused"));
        return ok;
    }

    void CombatSounds::queueSwing(sdkgame::CombatSubject who, const WeaponRecord* weapon)
    {
        if (!enabled())
            return;
        if (mQueue.size() >= kQueue)
        {
            ++mDropped;
            return;
        }
        Queued q;
        q.mWho = who;
        q.mWeapon = weapon;
        mQueue.push_back(q);
    }

    void CombatSounds::queueNamed(sdkgame::CombatSubject who, std::string_view descriptorEditorId)
    {
        if (!enabled() || descriptorEditorId.empty() || descriptorEditorId.size() >= kNameBytes)
            return;
        if (mQueue.size() >= kQueue)
        {
            ++mDropped;
            return;
        }
        Queued q;
        q.mWho = who;
        q.mNamed = true;
        std::memcpy(q.mName.data(), descriptorEditorId.data(), descriptorEditorId.size());
        mQueue.push_back(q);
    }

    void CombatSounds::flush(sdkgame::CombatRequestWriter& writer)
    {
        for (std::size_t i = 0; i < mQueue.size(); ++i)
        {
            const Queued q = mQueue[i];
            if (q.mNamed)
            {
                const FormId sound = mLibrary->soundByEditorId(q.mName.data());
                if (sound == kNullFormId)
                    ++mUnresolved;
                else
                    play(writer, q.mWho, sound, "event");
            }
            else if (q.mWeapon != nullptr)
                play(writer, q.mWho, q.mWeapon->mSwingSound, "swing");
            else
                play(writer, q.mWho, mLibrary->soundByEditorId(kUnarmedSwingSound), "swing");
        }
        mQueue.clear();
    }

    void CombatSounds::weaponDraw(
        sdkgame::CombatRequestWriter& writer, sdkgame::CombatSubject who, const WeaponRecord* w)
    {
        if (!enabled() || w == nullptr || std::find(mDrawn.begin(), mDrawn.end(), who) != mDrawn.end())
            return;
        if (mDrawn.size() < kMaxDrawn)
            mDrawn.push_back(who);
        play(writer, who, w->mDrawSound, "draw");
    }

    void CombatSounds::hit(sdkgame::CombatRequestWriter& writer, const HitSoundInput& in)
    {
        if (!enabled())
            return;
        FormId set = kNullFormId;
        if (in.mBash)
            set = in.mWeapon != nullptr ? in.mWeapon->mBashImpactSet : in.mUnarmedImpactSet;
        else
            set = in.mWeapon != nullptr ? in.mWeapon->mImpactSet : in.mUnarmedImpactSet;
        FormId material = kNullFormId;
        const char* what = "hit";
        if (in.mBlocked && in.mBlockerWeapon != nullptr && in.mBlockerWeapon->mBlockMaterial != kNullFormId)
        {
            material = in.mBlockerWeapon->mBlockMaterial;
            what = "block";
        }
        else
        {
            material = targetMaterial(*mLibrary, in.mVictimBodyMaterial, in.mVictimBodyArmourType, true);
            if (in.mBash)
                what = "bash";
        }
        const ImpactSounds sounds = impactSoundFor(*mLibrary, set, material, in.mVictim.player);
        if (sounds.mImpact == kNullFormId)
        {
            ++mUnresolved;
            if (mTrace)
                trace(std::string("CSOUND ") + what + " no impact for set " + std::to_string(set) + " material "
                    + std::to_string(material));
            return;
        }
        play(writer, in.mVictim, sounds.mFirst, what);
        if (sounds.mSecond != kNullFormId)
            play(writer, in.mVictim, sounds.mSecond, what);
    }

    void CombatSounds::bowNock(sdkgame::CombatRequestWriter& writer, sdkgame::CombatSubject who)
    {
        if (enabled())
            play(writer, who, mLibrary->soundByEditorId(kBowNockSound), "nock");
    }

    void CombatSounds::bowRelease(
        sdkgame::CombatRequestWriter& writer, sdkgame::CombatSubject who, const WeaponRecord* w)
    {
        if (!enabled())
            return;
        const FormId own = w != nullptr ? w->mAttackSound : kNullFormId;
        play(writer, who, own != kNullFormId ? own : mLibrary->soundByEditorId(kBowFireSound), "release");
    }

    // ---- the glue of SkyrimCombat (declared in combat.hpp) ---------------------------------------------------------

    void SkyrimCombat::soundEvent(const sdkgame::ClipEvent& event)
    {
        if (!mSounds.enabled())
            return;
        if (event.kind == sdkgame::ClipEventKind::WeaponSwing)
        {
            const Fighter* fighter = find(event.subject);
            mSounds.queueSwing(event.subject, fighter != nullptr ? weaponFor(*fighter) : nullptr);
            return;
        }
        // A clip that names a sound: `SoundPlay.<descriptor editor id>` (and the NPC- prefixed form of the sidecars).
        constexpr std::string_view kPlay = "SoundPlay.";
        std::string_view name = event.name;
        if (name.starts_with("NPC"))
            name.remove_prefix(3);
        if (name.starts_with(kPlay) && name.size() > kPlay.size())
            mSounds.queueNamed(event.subject, name.substr(kPlay.size()));
    }

    void SkyrimCombat::soundHit(sdkgame::CombatRequestWriter& writer, const Fighter& attacker, const Fighter& victim,
        bool blocked, bool bash)
    {
        if (!mSounds.enabled())
            return;
        HitSoundInput input;
        input.mWeapon = weaponFor(attacker);
        input.mUnarmedImpactSet = attacker.mProfile.mUnarmedImpactSet;
        input.mVictimBodyMaterial = victim.mProfile.mBodyMaterial;
        for (const ArmourPiece& piece : victim.mProfile.mPieces)
            if ((piece.mSlotMask & ArmourSlot::Body) != 0 && (piece.mType == 0 || piece.mType == 1))
                input.mVictimBodyArmourType = static_cast<int>(piece.mType);
        input.mBlocked = blocked;
        input.mBash = bash;
        input.mBlockerWeapon = blocked ? weaponFor(victim) : nullptr;
        input.mVictim = victim.mSubject;
        mSounds.hit(writer, input);
    }
}
