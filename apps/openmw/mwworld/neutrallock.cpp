#include "neutrallock.hpp"

#include <charconv>
#include <cstdlib>

namespace MWWorld
{
    using openeden::sdk::game::kNoWorldId;
    using openeden::sdk::game::WorldId;
    using openeden::sdk::game::WorldObject;

    NeutralLockOutcome decideNeutralLock(
        const WorldObject& object, bool unlockAll, bool alreadyUnlocked, const std::function<bool(WorldId)>& hasKey)
    {
        if (!object.lock || !object.lock->locked)
            return NeutralLockOutcome::NotLocked;
        // A level 0 lock that names no key is no lock (toddhow, 2026-11-12; the Angarvunde draugr chests).
        if (object.lock->level == 0 && object.lock->key == kNoWorldId)
            return NeutralLockOutcome::NotLocked;
        if (unlockAll || alreadyUnlocked)
            return NeutralLockOutcome::Open;
        if (object.lock->key != kNoWorldId && hasKey && hasKey(object.lock->key))
            return NeutralLockOutcome::UnlockedByKey;
        return NeutralLockOutcome::Locked;
    }

    void NeutralLockState::giveKey(std::string_view gameId, WorldId key)
    {
        mKeys.emplace(std::string(gameId), static_cast<std::uint32_t>(key));
    }

    bool NeutralLockState::hasKey(std::string_view gameId, WorldId key) const
    {
        return mKeys.count(Entry(std::string(gameId), static_cast<std::uint32_t>(key))) != 0;
    }

    void NeutralLockState::unlock(std::string_view gameId, WorldId placement)
    {
        mUnlocked.emplace(std::string(gameId), static_cast<std::uint32_t>(placement));
    }

    bool NeutralLockState::isUnlocked(std::string_view gameId, WorldId placement) const
    {
        return mUnlocked.count(Entry(std::string(gameId), static_cast<std::uint32_t>(placement))) != 0;
    }

    void NeutralLockState::clear()
    {
        mKeys.clear();
        mUnlocked.clear();
    }

    std::size_t NeutralLockState::giveKeysFromText(std::string_view text)
    {
        std::size_t given = 0;
        while (!text.empty())
        {
            const std::size_t comma = text.find(',');
            std::string_view item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            const std::size_t colon = item.find(':');
            if (colon == std::string_view::npos || colon == 0)
                continue;
            const std::string_view game = item.substr(0, colon);
            std::string_view number = item.substr(colon + 1);
            if (number.size() > 2 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X'))
                number.remove_prefix(2);
            std::uint32_t value = 0;
            const auto result = std::from_chars(number.data(), number.data() + number.size(), value, 16);
            if (number.empty() || result.ec != std::errc() || result.ptr != number.data() + number.size()
                || value == static_cast<std::uint32_t>(kNoWorldId))
                continue;
            giveKey(game, static_cast<WorldId>(value));
            ++given;
        }
        return given;
    }

    NeutralLockState& neutralLockState()
    {
        static NeutralLockState state;
        return state;
    }

    std::size_t resetNeutralLockState(std::string_view keysText)
    {
        NeutralLockState& state = neutralLockState();
        state.clear();
        return state.giveKeysFromText(keysText);
    }

    std::size_t resetNeutralLockState()
    {
        const char* text = std::getenv("OPENEDEN_NEUTRAL_KEYS");
        return resetNeutralLockState(text != nullptr ? std::string_view(text) : std::string_view());
    }

    std::string_view neutralLockMessageKey(NeutralLockOutcome outcome)
    {
        switch (outcome)
        {
            case NeutralLockOutcome::Locked:
                return "NeutralDoorLocked";
            case NeutralLockOutcome::UnlockedByKey:
                return "NeutralLockUnlocked";
            default:
                return {};
        }
    }
}
