#ifndef OPENEDEN_GAME_SKYRIMSE_CHARACTER_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_CHARACTER_SERVICE_H

// Task 0225: the character creation as a service of the pack. It keeps the working choice (`character::Creator`), the
// final choice, saves it in the chunk `character` v1 and declares the script namespace `skyrimse.character` 1.0.
//
// Namespace (Read unless marked; a part is the text "hair", "beard" or "eyes"):
//   ready() -> bool, needed() -> bool      (ready and the player has not finished the creation)
//   revision() -> int                      bumps with every change of the working choice
//   race_count() -> int, race_name(i), race_desc(i), race_skills(i) -> string, race_abilities(i) -> int
//   race_index() -> int, female() -> bool, name() -> string
//   part_count(part) -> int, part_name(part) -> string (the current), part_index(part) -> int
//   choose_race(i)       [Mutate]   a race by its index; the parts go back to its defaults
//   choose_female(bool)  [Mutate]
//   step_part(part, d)   [Mutate]   d places in the part's list, wrapping round
//   set_name(text)       [Mutate]
//   finish()             [Mutate]   keeps the choice: the chunk, the body, the race's skills and abilities
// Version 1.1 (task 0228), the race's powers (RACE SPLO spells of type power or lesser power; once a game day each):
//   power_count() -> int, power_name(i) -> string (the editor id), power_support(i) -> "works"|"status"|"none"
//   power_ready(i, now) -> bool            `now` is the game time in seconds (core.getGameTime())
//   use_power(i, now)    [Mutate]          casts the power through the magic runtime when it is ready, and starts its day
//
// The chunk (`character` version 1): done byte, then the choice's own blob (length and bytes). The saved choice is
// checked when the actor records are read (a race that is not playable, a part that is not an option: refused, and the
// creation is offered again).
//
// Main thread only (the game phase callback, the Read handlers and the queued Mutate calls all run there).

#include <game_skyrimse/character.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace game_skyrimse::character
{
    struct Access
    {
        const ActorIndex* mIndex = nullptr;
        TextFn mText;
    };

    /// How much of a power the magic runtime can do (SkyrimCombat::PowerSupport, repeated here so the service does not
    /// include the combat).
    enum class PowerSupport : std::uint8_t
    {
        Works,
        StatusOnly,
        Unsupported
    };
    struct PowerInfo
    {
        FormId mId = kNullFormId;
        std::string mName;
        PowerSupport mSupport = PowerSupport::Unsupported;
    };
    /// A power can be used once in this many game seconds (a game day; OURS: the records give no timing).
    inline constexpr std::int64_t kPowerCooldownSeconds = 24 * 60 * 60;

    struct Hooks
    {
        /// Fills the access when the actor records are read (false: ask again next frame).
        std::function<bool(Access& out)> mAccess;
        /// The body follows the choice: called on every change of the working choice and with the final one.
        std::function<void(const Choice&)> mApplyBody;
        /// The race of the character is known (a finished creation or a restored one): its abilities are the player's.
        /// `fresh` is true only at the moment the player finishes (the starting skills are applied then).
        std::function<void(const RaceOption&, bool fresh)> mRace;
        /// Task 0228: the player's chosen name (a finished creation, a restored choice): the engine's player record
        /// takes it.
        std::function<void(const std::string&)> mName;
        /// Task 0228: the powers among a race's spells, and the cast of one. Without them the race has no powers.
        std::function<std::vector<PowerInfo>(const std::vector<FormId>& raceSpells)> mPowers;
        std::function<bool(FormId)> mCastPower;
        /// Task 0241: the spells the player holds beyond his race's (the standing stones' powers); asked for the powers
        /// among them like the race's. Without it the list is the race's alone.
        std::function<std::vector<FormId>()> mExtraPowerSpells;
        std::function<void(const std::string&)> mLog;
    };

    class CharacterService
    {
    public:
        explicit CharacterService(Hooks hooks);
        ~CharacterService();
        CharacterService(const CharacterService&) = delete;
        CharacterService& operator=(const CharacterService&) = delete;

        /// Asks for the records until they are there, then builds the creator and applies a held restore. Never blocks.
        bool poll();
        [[nodiscard]] bool ready() const { return mCreator != nullptr; }
        [[nodiscard]] bool done() const { return mDone; }
        [[nodiscard]] bool needed() const { return ready() && !mDone; }
        [[nodiscard]] const Creator* creator() const { return mCreator.get(); }
        [[nodiscard]] Creator* creator() { return mCreator.get(); }
        /// The choice the body shows: the saved one once done, else the working one.
        [[nodiscard]] const Choice& current() const;
        /// A change counter for scripts: the creator's revision plus whether the creation is done.
        [[nodiscard]] std::uint64_t revision() const;

        /// The Mutate functions' work (also callable directly).
        bool chooseRace(std::size_t index);
        void chooseFemale(bool female);
        void stepPart(Part part, int delta);
        void setName(std::string_view name);
        /// Keeps the choice. False when not ready or already done.
        bool finish();

        /// Task 0228: the powers of the chosen race (empty before the creation is done).
        [[nodiscard]] std::vector<PowerInfo> powers() const;
        /// Ready: the creation is done, the index is valid, the power is not Unsupported and a day has passed.
        [[nodiscard]] bool powerReady(std::size_t index, std::int64_t nowSeconds) const;
        /// Casts power `index` when ready and starts its day; false (nothing changes) otherwise.
        bool usePower(std::size_t index, std::int64_t nowSeconds);

        [[nodiscard]] openeden::sdk::SaveChunkHandler& chunk();
        /// Task 0228: the powers' last use, a second chunk (`character_powers` v1) so the first stays as saved before.
        [[nodiscard]] openeden::sdk::SaveChunkHandler& powerChunk();
        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        /// Called by the chunk.
        void writeChunk(openeden::sdk::ChunkWriter& writer);
        bool readChunk(openeden::sdk::ChunkReader& reader, std::uint32_t version);
        void writePowerChunk(openeden::sdk::ChunkWriter& writer);
        bool readPowerChunk(openeden::sdk::ChunkReader& reader);

    private:
        void changed();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        std::unique_ptr<Creator> mCreator;
        Access mAccess;
        bool mDone = false;
        Choice mFinal;
        std::vector<std::pair<FormId, std::int64_t>> mPowerUsed; ///< power -> game second of its last use (saved)
        bool mHavePending = false;
        Choice mPending;
        std::uint64_t mLastSent = 0;
    };
}

#endif
