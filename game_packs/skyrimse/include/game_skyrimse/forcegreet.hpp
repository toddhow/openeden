#ifndef OPENEDEN_GAME_SKYRIMSE_FORCEGREET_H
#define OPENEDEN_GAME_SKYRIMSE_FORCEGREET_H

// Forced greetings by package (task 0264). A ForceGreet package that a scene gives an actor ends with the actor opening
// a conversation with the player: the greeting is the first INFO of the package's PDTO topic that passes, and its
// choices are that INFO's link-to topics (as the guard's arrest conversation, task 0258).
//
// The pack cannot open a conversation itself: the engine opens one when the player activates an actor. So the pack
// publishes the request and a global script (`forcegreet_global.lua`) activates the actor by the player, as the crime
// script does for the arrest conversation. This file is the pure bookkeeping plus the bindings namespace
// "skyrimse.forcegreet" (version 1.0, Read only):
//   pending_base() -> string   the base (decimal) of the actor that should open its conversation now, "" for none
//   pending_serial() -> int    changes with every new request (the script asks once per serial)
//
// A request is Waiting until the conversation begins (Open), then Done when it ends; one that is not opened within
// `kTimeoutChecks` checks (the scene runner checks once per update, so the count is deterministic: no clock) is
// Failed and counts as done, so a scene never waits for ever. Threads: main thread.

#include <game_skyrimse/records.hpp>

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace game_skyrimse
{
    class ForceGreet
    {
    public:
        enum class State : std::uint8_t
        {
            Waiting,
            Open,
            Done,
            Failed,
        };
        /// Updates the runner makes before a request that has not opened is given up (about 15 s at 60 updates).
        static constexpr std::uint32_t kTimeoutChecks = 900;
        static constexpr std::size_t kMaxRequests = 16;

        ForceGreet();
        ~ForceGreet();
        ForceGreet(const ForceGreet&) = delete;
        ForceGreet& operator=(const ForceGreet&) = delete;

        /// A request for the actor placed as `reference` (of base `base`) to say `topic`. False when the table is full
        /// or a request of the actor is live already.
        bool request(FormId reference, FormId base, FormId topic);
        /// Forgets a request (the scene ended or stopped).
        void cancel(FormId reference);
        /// The topic of a Waiting request of the actor (0 when it has none): what the conversation should open with.
        [[nodiscard]] FormId topicFor(FormId reference) const;
        void began(FormId reference);
        void ended(FormId reference);
        /// One check of whether the actor's package is finished: true for Done or Failed (the request is dropped), and
        /// counts a Waiting one towards its timeout.
        bool finished(FormId reference);
        [[nodiscard]] State stateOf(FormId reference) const;
        [[nodiscard]] std::uint32_t serial() const { return mSerial; }
        [[nodiscard]] std::string pendingBase() const;
        [[nodiscard]] std::uint64_t timeouts() const { return mTimeouts; }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Entry
        {
            FormId mBase = kNullFormId;
            FormId mTopic = kNullFormId;
            State mState = State::Waiting;
            std::uint32_t mChecks = 0;
        };
        std::map<FormId, Entry> mEntries;
        std::uint32_t mSerial = 0;
        std::uint64_t mTimeouts = 0;
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
