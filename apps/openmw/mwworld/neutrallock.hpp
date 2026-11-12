#ifndef OPENMW_MWWORLD_NEUTRALLOCK_H
#define OPENMW_MWWORLD_NEUTRALLOCK_H

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <openeden/game/world_model.hpp>

// Locks in a neutral cell (task 0127, milestone M-S1): the pure part.
//
// A placement's LockComponent says it is locked and which key form opens it. What the player may do about it is decided
// here from three facts the engine supplies: whether the lock was already opened this session, whether the player holds
// the key, and the developer aid that opens everything. Lock picking is NOT modelled: the engine's lock picking
// (game_packs/morrowind security rules) reads Morrowind GMSTs, skills and probe items and cannot be applied to a Skyrim
// lock, so a lock with no usable key stays locked (task 0127, decision 1).
//
// NeutralLockState is the session's memory: the keys held and the locks opened. Main thread only. Not saved (saving is
// refused in a neutral session until stage N6).

namespace MWWorld
{
    enum class NeutralLockOutcome
    {
        NotLocked, ///< no lock, or the lock is not set: nothing to decide
        Open, ///< locked, but opened earlier this session or by the developer aid
        UnlockedByKey, ///< locked and the player holds the key: opens now (and stays open)
        Locked, ///< locked and no key is held (lock picking is not supported)
    };

    /// `alreadyUnlocked`: this placement was opened earlier this session. `unlockAll`: OPENEDEN_NEUTRAL_UNLOCK_DOORS.
    /// `hasKey` is asked at most once, and only for a lock that names a key.
    [[nodiscard]] NeutralLockOutcome decideNeutralLock(const openeden::sdk::game::WorldObject& object, bool unlockAll,
        bool alreadyUnlocked, const std::function<bool(openeden::sdk::game::WorldId)>& hasKey);

    class NeutralLockState
    {
    public:
        /// The player holds the key form `key` of game `gameId` (a stand-in for an inventory, which cannot hold a
        /// neutral item before stage N5).
        void giveKey(std::string_view gameId, openeden::sdk::game::WorldId key);
        [[nodiscard]] bool hasKey(std::string_view gameId, openeden::sdk::game::WorldId key) const;

        void unlock(std::string_view gameId, openeden::sdk::game::WorldId placement);
        [[nodiscard]] bool isUnlocked(std::string_view gameId, openeden::sdk::game::WorldId placement) const;

        /// Forgets everything (a new game).
        void clear();

        /// Reads "game:id,game:id" (id in hexadecimal, with or without 0x) and gives each key. Returns the number of
        /// keys given; malformed entries are skipped.
        std::size_t giveKeysFromText(std::string_view text);

        [[nodiscard]] std::size_t keyCount() const { return mKeys.size(); }
        [[nodiscard]] std::size_t unlockedCount() const { return mUnlocked.size(); }

    private:
        using Entry = std::pair<std::string, std::uint32_t>;
        std::set<Entry> mKeys;
        std::set<Entry> mUnlocked;
    };

    /// A new game: forgets keys and opened locks, then gives the keys in `keysText` (the text of OPENEDEN_NEUTRAL_KEYS,
    /// see giveKeysFromText). The no-argument form reads that environment variable. Returns the keys given.
    std::size_t resetNeutralLockState(std::string_view keysText);
    std::size_t resetNeutralLockState();

    /// The session's lock state.
    [[nodiscard]] NeutralLockState& neutralLockState();

    /// OMWEngine l10n keys.
    [[nodiscard]] std::string_view neutralLockMessageKey(NeutralLockOutcome outcome);
}

#endif
