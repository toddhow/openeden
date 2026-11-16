#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_SOUND_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_SOUND_H

// The sounds of a Skyrim SE fight (task 0193, milestone M-S5 stage S5-10; the records and the rules are in
// combat_impact.hpp). `CombatSounds` decides WHAT plays and when it is asked for; the core plays it
// (CombatRequestWriter::playSound). Pure: no engine type, no clock, no random numbers (the files of a descriptor take
// turns by a counter, so a run repeats byte for byte).
//
// Events: a weapon swing (the clip's weaponSwing event: the weapon's swing sound), a weapon drawn (once per fighter, as
// it starts to fight), a hit (the impact of the attacker's impact data set against the target's material), a bash, a
// block (the blocker's weapon material), a bow's nock and release and an arrow's end (the bow's impact data set against
// the target's material, or a stone wall). A clip's `SoundPlay.<editor id>` event plays that descriptor.
//
// Threading: main thread only. `queueSwing` and `queueNamed` are called from the clip event callback, where a writer is
// not valid, and are played by `flush` inside combatUpdate; the other calls take the writer.

#include "game_skyrimse/combat_stats.hpp"

#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    /// What the hit rule needs to know about the two sides.
    struct HitSoundInput
    {
        const WeaponRecord* mWeapon = nullptr; ///< the attacker's (null: bare hands)
        FormId mUnarmedImpactSet = kNullFormId; ///< the attacker's race NAM5
        FormId mVictimBodyMaterial = kNullFormId; ///< the victim's race NAM4
        int mVictimBodyArmourType = -1; ///< ArmourPiece::mType of the worn body piece, -1 for none
        const WeaponRecord* mBlockerWeapon = nullptr; ///< the blocker's weapon when `mBlocked`
        bool mBlocked = false;
        bool mBash = false;
        openeden::sdk::game::CombatSubject mVictim;
    };

    class CombatSounds
    {
    public:
        CombatSounds();

        /// The records and the file probe (the session's VFS). A null library turns every call into a no-op.
        void bind(const SoundLibrary* library, SoundFileProbe exists);
        /// `combat sounds` (default true).
        void setEnabled(bool on) { mEnabled = on; }
        [[nodiscard]] bool enabled() const { return mEnabled && mLibrary != nullptr; }
        void setTrace(std::function<void(const std::string&)> trace) { mTrace = std::move(trace); }

        /// From the clip event callback (no writer there). A full queue drops the sound and counts it.
        void queueSwing(openeden::sdk::game::CombatSubject who, const WeaponRecord* weapon);
        void queueNamed(openeden::sdk::game::CombatSubject who, std::string_view descriptorEditorId);
        /// Plays the queue (inside combatUpdate).
        void flush(openeden::sdk::game::CombatRequestWriter& writer);

        /// The weapon is drawn, once per subject.
        void weaponDraw(
            openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::CombatSubject who,
            const WeaponRecord* w);
        /// A melee or arrow hit (see HitSoundInput); the sound plays at the victim.
        void hit(openeden::sdk::game::CombatRequestWriter& writer, const HitSoundInput& input);
        /// A bow is drawn / lets go. A weapon with its own attack sound (a crossbow) uses it for the release.
        void bowNock(openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::CombatSubject who);
        void bowRelease(
            openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::CombatSubject who,
            const WeaponRecord* w);

        [[nodiscard]] std::size_t played() const { return mPlayed; }
        [[nodiscard]] std::size_t refused() const { return mRefused; }
        [[nodiscard]] std::size_t unresolved() const { return mUnresolved; }
        [[nodiscard]] std::size_t dropped() const { return mDropped; }

        static constexpr std::size_t kQueue = 128;
        static constexpr std::size_t kMaxDrawn = 512;
        static constexpr std::size_t kNameBytes = 48;

    private:
        struct Queued
        {
            openeden::sdk::game::CombatSubject mWho;
            const WeaponRecord* mWeapon = nullptr;
            bool mNamed = false;
            std::array<char, kNameBytes> mName{};
        };
        struct Cached
        {
            bool mOk = false;
            ResolvedSoundSet mSet;
        };

        bool play(openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::CombatSubject who,
            FormId sound, std::string_view what);
        const Cached& cached(FormId sound);
        void trace(const std::string& line) const
        {
            if (mTrace)
                mTrace(line);
        }
        static std::string subjectName(openeden::sdk::game::CombatSubject who);

        const SoundLibrary* mLibrary = nullptr;
        SoundFileProbe mProbe;
        bool mEnabled = true;
        std::function<void(const std::string&)> mTrace;
        std::vector<Queued> mQueue;
        std::vector<openeden::sdk::game::CombatSubject> mDrawn;
        std::unordered_map<FormId, Cached> mCache;
        std::uint32_t mCounter = 0;
        std::size_t mPlayed = 0;
        std::size_t mRefused = 0;
        std::size_t mUnresolved = 0;
        std::size_t mDropped = 0;
    };
}

#endif
