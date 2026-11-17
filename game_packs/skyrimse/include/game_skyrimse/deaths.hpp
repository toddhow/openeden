#ifndef OPENEDEN_GAME_SKYRIMSE_DEATHS_H
#define OPENEDEN_GAME_SKYRIMSE_DEATHS_H

// Dead stays dead across a load (task 0254, a bug of the neutral saves of task 0177: an actor is built again from the
// pack alive at every load, and nothing saved that it had died).
//
// The pack keeps a LEDGER of the dead: for each actor placement that died in the session, the game hour it died, where
// its body lay and whether a Reanimate spell hid its corpse. The ledger is a save chunk of its own (`deaths`). After a
// load, when an actor with an entry appears, the DeathKeeper
//   1. decides whether it respawns (not unique, not essential, and its cell would have reset: iHoursToRespawnCell game
//      hours since it died; the entry is dropped and the actor stays the live one the pack built),
//   2. otherwise puts the body where it lay (ActorRequestWriter::placeCorpse), keeps the package brain from walking it,
//      and
//   3. marks it dead (SkyrimCombat::restoreDead: health 0, the death clip played at ten times its speed, the death
//      reported to the core so that its loot opens).
// The report in step 3 goes to the core's own writer, never through the script, crime or HUD wrappers: nothing that
// fired at the death fires again (no OnDeath, no quest kill, no murder).
//
// The raised corpse of task 0250 is a placement that was disabled; its entry has `mRaised`, and after a load the
// keeper enables the placement again so that the original corpse is back (the raised actor itself is not saved).
//
// No engine, renderer or Jolt type. Main thread only. Every container is ordered (determinism).

#include <game_skyrimse/records.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    class SkyrimCombat;

    /// The most dead actors the ledger remembers (a save cannot grow without bound).
    inline constexpr std::size_t kMaxDeathEntries = 4096;
    /// iHoursToRespawnCell of Skyrim.esm (240 game hours: 10 days; the value is read from the data when the pack has
    /// it, its use as the cell reset is RECALLED-VERIFY).
    inline constexpr double kDefaultRespawnHours = 240.0;
    /// After a death the body may still move (a ragdoll settles): its position is read again every kDeathRefreshEvery
    /// seconds for kDeathRefreshSpan seconds (real seconds of updates).
    inline constexpr float kDeathRefreshEvery = 0.5f;
    inline constexpr float kDeathRefreshSpan = 20.f;
    /// After that span the position is still read, every kDeathRefreshLate seconds (something may push a corpse).
    inline constexpr float kDeathRefreshLate = 5.f;

    struct DeathEntry
    {
        // Saved
        double mHours = 0.0; ///< game hours (days * 24 + hour) when it died
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        float mHeading = 0.f;
        bool mHasPosition = false;
        bool mRaised = false; ///< a Reanimate spell hid the corpse (the placement was disabled)
        // Not saved: what this session did with the entry
        bool mPlaced = false; ///< the body was put back (or it died in this session)
        bool mRestored = false; ///< the actor was marked dead (or it died in this session)
        float mRefreshLeft = 0.f;
        float mSinceRefresh = 0.f;
    };

    /// Respawn rule: a unique or essential actor never respawns; the others do once `respawnHours` game hours have gone
    /// by since `deathHours`. A clock that went back (a negative interval) never respawns anybody.
    [[nodiscard]] bool deathRespawns(
        double nowHours, double deathHours, double respawnHours, bool unique, bool essential);

    class DeathLedger
    {
    public:
        /// A new entry for the placement. False for a null id, a created actor (summons are not saved), a full ledger,
        /// a non-finite hour or a placement that has an entry already.
        bool add(FormId reference, double hours);
        bool setPosition(FormId reference, double x, double y, double z, float heading);
        bool markRaised(FormId reference);
        bool erase(FormId reference);
        void clear();
        [[nodiscard]] const DeathEntry* find(FormId reference) const;
        [[nodiscard]] DeathEntry* find(FormId reference);
        [[nodiscard]] std::size_t size() const { return mEntries.size(); }
        [[nodiscard]] const std::map<FormId, DeathEntry>& entries() const { return mEntries; }
        [[nodiscard]] std::map<FormId, DeathEntry>& entries() { return mEntries; }
        /// What a save keeps (the runtime fields are cleared).
        [[nodiscard]] std::map<FormId, DeathEntry> data() const;
        /// Replaces the ledger with a saved one: false (nothing changed) for a null or created id, an entry that is not
        /// finite or more than kMaxDeathEntries. Every entry then waits to be put back.
        bool restore(const std::map<FormId, DeathEntry>& saved);
        /// The actor of the placement went (its cell unloaded): it is put back again when it returns.
        void actorGone(FormId reference);

    private:
        std::map<FormId, DeathEntry> mEntries;
    };

    /// Chunk `deaths`, version 1: u32 count; each entry in strictly ascending placement order: u32 placement, u64 hours
    /// (the bits of a double), u8 flags (1 position, 2 raised), and with the position flag three u64 (the bits of the
    /// doubles x, y, z) and a u32 (the bits of the heading float).
    class DeathsChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "deaths";

        using Getter = std::function<std::map<FormId, DeathEntry>()>;
        using Setter = std::function<bool(const std::map<FormId, DeathEntry>&)>;

        DeathsChunk(Getter get, Setter set)
            : mGet(std::move(get))
            , mSet(std::move(set))
        {
        }

        [[nodiscard]] std::string_view chunkName() const override { return kName; }
        [[nodiscard]] std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

    private:
        Getter mGet;
        Setter mSet;
        std::string mRefusal;
    };

    /// What the keeper needs from the world; every function may be empty (that part of the work is then skipped).
    struct DeathHooks
    {
        /// The actor of a placement while the core has it (an invalid handle otherwise).
        std::function<openeden::sdk::game::ActorHandle(openeden::sdk::game::WorldId)> handleOf;
        /// The package brain stands the actor still (true) or lets it go on (false).
        std::function<void(openeden::sdk::game::WorldId, bool)> hold;
        /// Enables a placement a Reanimate spell had hidden.
        std::function<void(openeden::sdk::game::WorldId)> enable;
        /// iHoursToRespawnCell
        std::function<double()> respawnHours;
    };

    class DeathKeeper
    {
    public:
        DeathKeeper(DeathLedger& ledger, DeathHooks hooks)
            : mLedger(ledger)
            , mHooks(std::move(hooks))
        {
            mScratch.reserve(64);
            mScratchIds.reserve(64);
        }

        /// The game hour (days * 24 + hour), once a frame from the actor update. `valid` false: the core has no clock.
        void setClock(double hours, bool valid)
        {
            mHours = hours;
            mClockValid = valid;
        }
        /// After the combat update (the core's own writer): every dead fighter gets an entry; a body that may still
        /// move is read again. `seconds` is the update's length.
        void noteDeaths(float seconds, openeden::sdk::game::CombatRequestWriter& writer, SkyrimCombat& combat);
        /// In the actor update: an actor of the ledger that appeared is put back or respawns.
        void placeCorpses(openeden::sdk::game::ActorRequestWriter& writer, SkyrimCombat& combat);
        /// In the combat update, with the core's own writer, before anything else is done: an actor that was put back
        /// is marked dead.
        void restoreDeaths(openeden::sdk::game::CombatRequestWriter& writer, SkyrimCombat& combat);
        /// The actor of this placement went (its cell unloaded): its entry waits to be put back again.
        void actorGone(openeden::sdk::game::WorldId reference) { mLedger.actorGone(static_cast<FormId>(reference)); }
        /// A Reanimate spell hid the corpse of this placement.
        void corpseRaised(openeden::sdk::game::WorldId reference);
        /// After the ledger was replaced by a saved one: the hidden corpses are enabled again.
        void ledgerRestored();
        [[nodiscard]] std::size_t respawns() const { return mRespawns; }
        /// One line for each death noted, body put back, respawn and restored death (empty: none).
        void setTrace(std::function<void(const std::string&)> trace) { mTrace = std::move(trace); }
        [[nodiscard]] std::size_t restored() const { return mRestoredCount; }

    private:
        DeathLedger& mLedger;
        DeathHooks mHooks;
        double mHours = 0.0;
        bool mClockValid = false;
        std::size_t mRespawns = 0;
        std::function<void(const std::string&)> mTrace;
        std::size_t mRestoredCount = 0;
        std::vector<openeden::sdk::game::CombatSubject> mScratch; // reserved once: no allocation in a steady update
        std::vector<FormId> mScratchIds;
    };
}

#endif
