#ifndef OPENEDEN_GAME_SKYRIMSE_CRIME_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_CRIME_SERVICE_H

// Skyrim crime in a game (task 0204): the ledger, its save chunk, the crime system over the game's hooks, and the
// script bindings namespace "skyrimse.crime" (version 1.0). Ids cross as text (8 lower-case hex digits), like the
// inventory's.
//
// Namespace skyrimse.crime 1.0 (Read unless marked):
//   state() -> string           "none", "approach", "offer", "jailed" or "resisted"
//   bounty() -> int             the player's bounty summed over every crime faction
//   offer_bounty() -> int       the bounty of the faction that makes the offer
//   guard_name() -> string      the guard that waits for the answer ("" when none)
//   can_pay() -> bool           an offer stands and the player has the gold
//   message() -> string         the line the window shows
//   stolen_count() -> int       items marked stolen
//   revision() -> int           changes with anything above
//   jail_available() -> bool    the faction of the offer (or jail) has a jail destination
//   jail_cell() -> string       the editor id of the jail's cell ("" when none)
//   jail_x(), jail_y(), jail_z(), jail_heading() -> double
//   jail_seconds() -> double    how long the player stays
//   choose(choice) [Mutate]     "pay", "jail" or "resist"
//   released()     [Mutate]     the jail time is over and the player was taken back
//   entered_cell(editorId)  [Mutate]   task 0213: the player's cell changed (trespass is judged, see enteredCell)
//   trespass_bed(placement) [Mutate]   task 0213: the player tried a bed that may not be his

#include <game_skyrimse/crime.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace game_skyrimse::crime
{
    /// What the service needs of the game; every function may be empty (it then answers "nothing").
    struct ServiceHooks
    {
        std::function<void(std::vector<CrimeActor>&)> mActors;
        std::function<bool(openeden::sdk::game::ActorHandle, CrimeActor&)> mActor;
        std::function<const CrimeData*(FormId faction)> mCrimeData;
        std::function<std::string(FormId faction)> mFactionName;
        std::function<std::int32_t(FormId item)> mItemValue;
        std::function<std::int32_t()> mGold;
        std::function<bool(std::int32_t amount)> mTakeGold;
        std::function<std::int32_t(FormId item, std::int32_t count)> mTakeItem;
        std::function<std::int32_t(FormId item)> mItemCount;
        std::function<void(openeden::sdk::game::ActorHandle, double, double, double, openeden::sdk::game::ActorSpeed)>
            mGoTo;
        std::function<void(openeden::sdk::game::ActorHandle)> mHold;
        std::function<void(openeden::sdk::game::ActorHandle)> mRelease;
        std::function<void(openeden::sdk::game::ActorHandle)> mAttack;
        std::function<bool(FormId faction, JailDestination& out)> mJail;
        /// Task 0213: what the CELL records say of a cell by editor id (false: unknown) and who owns a placement.
        std::function<bool(const std::string& editorId, CellAccess& out)> mCellAccess;
        std::function<FormId(FormId placement)> mPlacementOwner;
        std::function<void(const std::string&)> mLog;
    };

    class CrimeService final : public CrimeWorld
    {
    public:
        CrimeService(ServiceHooks hooks, Settings settings = {});
        ~CrimeService() override;
        CrimeService(const CrimeService&) = delete;
        CrimeService& operator=(const CrimeService&) = delete;

        [[nodiscard]] Ledger& ledger() { return mLedger; }
        [[nodiscard]] LedgerChunk& chunk() { return mChunk; }
        [[nodiscard]] CrimeSystem& system() { return mSystem; }

        /// The player took `count` of `item` out of a container somebody else owns.
        void theft(FormId item, std::int32_t count) { mSystem.onTheft(item, count); }
        /// Task 0213: the player's cell changed to this one (the Lua global script reports every change). The first
        /// report of a session only records the cell; a cell the jail sent the player to is no trespass; a private
        /// owned interior is (judged later by the witness rule). Entering any cell ends the previous visit.
        void enteredCell(std::string_view editorId);
        /// Task 0213: the player tried to use the bed at `placement`: trespass when its owner is not the player's.
        void trespassBed(FormId placement);
        /// The player's choice at an offer; a jail with no destination clears the bounty and says why.
        ChoiceStatus choose(std::string_view choice);
        /// How many of an item the player holds are stolen (never more than it holds).
        [[nodiscard]] std::int32_t stolenHeld(FormId item) const;
        /// The jail destination of a faction, cached (nothing for a faction with none).
        [[nodiscard]] const JailDestination& jailOf(FormId faction);

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        // ---- CrimeWorld
        void actors(std::vector<CrimeActor>& out) override;
        bool actor(openeden::sdk::game::ActorHandle handle, CrimeActor& out) override;
        const CrimeData* crimeData(FormId faction) override;
        std::string factionName(FormId faction) override;
        std::int32_t itemValue(FormId item) override;
        std::int32_t playerGold() override;
        bool takeGold(std::int32_t amount) override;
        std::int32_t takeItem(FormId item, std::int32_t count) override;
        std::int32_t itemCount(FormId item) override;
        void guardGoTo(openeden::sdk::game::ActorHandle guard, double x, double y, double z,
            openeden::sdk::game::ActorSpeed speed) override;
        void guardHold(openeden::sdk::game::ActorHandle guard) override;
        void guardRelease(openeden::sdk::game::ActorHandle guard) override;
        void guardAttack(openeden::sdk::game::ActorHandle guard) override;
        void log(const std::string& line) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        ServiceHooks mHooks;
        Ledger mLedger;
        LedgerChunk mChunk;
        CrimeSystem mSystem;
        std::map<FormId, JailDestination> mJail;
        std::string mLastCell; ///< the cell the player was last reported in, lower case (empty: none yet)
        bool mAfterJail = false; ///< the next cell reported is the way back from the jail
        JailDestination mNoJail;
    };
}

#endif
