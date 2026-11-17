#ifndef OPENEDEN_GAME_SKYRIMSE_WEREWOLF_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_WEREWOLF_SERVICE_H

// Task 0259 (werewolf): what the scripts see of the beast form, the namespace "skyrimse.werewolf" (version 1.0). The
// pack owns the form; this holds the numbers the scripts read and the one call they make.
//
// Namespace skyrimse.werewolf 1.0:
//   active() -> int            1 while the player is a werewolf
//   seconds_left() -> int      whole seconds left on the form (0 when not one)
//   notification_count() -> int, notification(n) -> string   what the form announced (numbered from 1)
//   feed(placement, base) [Mutate]   the player fed on the corpse of an actor (ids as 8 hex digits, a placement 1 to
//   16)
//
// Threads: main thread.

#include <game_skyrimse/records.hpp>

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace game_skyrimse::werewolf
{
    class WerewolfService
    {
    public:
        /// Feeds on the corpse: true when it counted (a corpse is fed on once).
        using FeedHook = std::function<bool(std::uint64_t placement, FormId base)>;

        explicit WerewolfService(FeedHook feed);
        ~WerewolfService();
        WerewolfService(const WerewolfService&) = delete;
        WerewolfService& operator=(const WerewolfService&) = delete;

        void setState(bool active, float secondsLeft);
        void announce(std::string text);
        [[nodiscard]] bool active() const { return mActive; }
        [[nodiscard]] std::size_t noticeCount() const { return mNotices.size(); }
        [[nodiscard]] const std::string& noticeAt(std::size_t number) const; ///< 1-based; empty when out of range

        /// What a feed call does; false when the form is off or the hook refused.
        bool feed(std::uint64_t placement, FormId base);

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        FeedHook mFeed;
        bool mActive = false;
        float mSecondsLeft = 0.f;
        std::vector<std::string> mNotices;
    };
}

#endif
