#ifndef OPENMW_MWPHYSICS_NEUTRALACTORBRIDGE_H
#define OPENMW_MWPHYSICS_NEUTRALACTORBRIDGE_H

// Task 0135 (decision 0017, stage S3): the engine's side of the actor hooks contract
// (sdk/include/openeden/game/actor_rules.hpp). The bridge makes the session handle of each neutral actor the pack is
// told about, remembers which engine key (the physics system's reference pointer, opaque here) stands behind it, calls
// the pack once a frame, and hands every valid movement request to a sink. It knows no Jolt, Ptr or ESM type, so a
// test drives it with a fake pack and a fake sink.
//
// Threading: main thread only (World::doPhysics, before stepSimulation; Neutral::insertActorRendering; the physics
// system's remove). Allocation: add() may grow the table (reserved with the physics system's own growth); update()
// allocates nothing, and neither does a request.
//
// Exceptions: a pack function that throws is caught here (nothing crosses the core / pack boundary); it is counted
// in Stats::mRulesFailures.

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/paired_animation.hpp>
#include <openeden/game/riding_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace MWPhysics
{
    /// Receives the requests that survived validation. `key` is the value given to add().
    class NeutralRequestSink
    {
    public:
        virtual ~NeutralRequestSink() = default;
        virtual void setRequest(const void* key, float velX, float velY) = 0;
        // Stage S4 (defaults do nothing, so a sink that only knows velocities still works):
        virtual void setGoal(const void* key, const openeden::sdk::game::ActorGoal& goal)
        {
            (void)key;
            (void)goal;
        }
        virtual void cancelGoal(const void* key) { (void)key; }
        virtual void setHeading(const void* key, float heading)
        {
            (void)key;
            (void)heading;
        }
        // Stage S6b (furniture use): `use.clip` is valid during the call only, a sink that keeps it copies it.
        virtual void useFurniture(const void* key, const openeden::sdk::game::FurnitureUse& use)
        {
            (void)key;
            (void)use;
        }
        virtual void leaveFurniture(const void* key, double x, double y, double z)
        {
            (void)key;
            (void)x;
            (void)y;
            (void)z;
        }
        // Stage S6f: moves an actor that is on furniture without changing its pose; where an actor is now.
        virtual bool placeSeated(const void* key, double x, double y, double z, float heading)
        {
            (void)key;
            (void)x;
            (void)y;
            (void)z;
            (void)heading;
            return false;
        }
        // Task 0254: puts a dead actor back where it lay (a loaded save): its position and heading, no movement request.
        virtual bool placeCorpse(const void* key, double x, double y, double z, float heading)
        {
            (void)key;
            (void)x;
            (void)y;
            (void)z;
            (void)heading;
            return false;
        }
        virtual bool positionOf(const void* key, double& x, double& y, double& z) const
        {
            (void)key;
            (void)x;
            (void)y;
            (void)z;
            return false;
        }
    };

    /// Task 0251 (riding): what the writer's RidingWriter interface does. Set by the world; any of them may be empty
    /// (the writer then refuses). Called on the main thread, inside the pack's update().
    struct NeutralRidingHooks
    {
        std::function<bool(openeden::sdk::game::RiderControls&)> mControls;
        std::function<bool(double, double, double, float)> mCarry;
        std::function<bool(double, double, double)> mRelease;
        /// Task 0267: the paired clips (sdk/include/openeden/game/paired_animation.hpp): what a clip holds, and
        /// playing a side of one (an empty name clears it) on the actor behind a key.
        std::function<openeden::sdk::game::PairedStatus(std::string_view, openeden::sdk::game::PairedClipInfo&)>
            mPairedInfo;
        std::function<bool(const void*, std::string_view)> mPairedPlay;
    };

    class NeutralActorBridge
    {
    public:
        struct Stats
        {
            std::uint64_t mAdded = 0;
            std::uint64_t mRemoved = 0;
            std::uint64_t mRequests = 0; ///< requests passed to the sink
            std::uint64_t mStale = 0; ///< requests for a handle the bridge does not hold, or with a wrong session
            std::uint64_t mBadValues = 0; ///< requests with a value that is not finite
            std::uint64_t mOutsideUpdate = 0; ///< requests written when no update() was running
            std::uint64_t mRulesFailures = 0; ///< a pack function that threw
            std::uint64_t mGoals = 0; ///< go-to requests passed to the sink (stage S4)
            std::uint64_t mCancels = 0;
            std::uint64_t mHeadings = 0;
            std::uint64_t mFurnitureUses = 0; ///< furniture-use requests passed to the sink (stage S6b)
            std::uint64_t mFurnitureLeaves = 0;
            std::uint64_t mPlacements = 0; ///< placeFurniture requests the sink accepted (stage S6f)
            std::uint64_t mResultsDelivered = 0; ///< goal results told to the pack
            std::uint64_t mResultsDropped = 0; ///< goal results whose actor had gone by the time they were told
        };

        NeutralActorBridge();
        NeutralActorBridge(const NeutralActorBridge&) = delete;
        NeutralActorBridge& operator=(const NeutralActorBridge&) = delete;

        /// Makes a handle for the actor standing behind `key`, records it and tells `rules` (when not null). Returns
        /// the invalid handle (and records nothing) for a null key, a missing placement id, a key that is already
        /// registered, or when the table is full (kMaxActors).
        openeden::sdk::game::ActorHandle add(openeden::sdk::game::ActorRules* rules, const void* key,
            openeden::sdk::game::WorldId reference, openeden::sdk::game::WorldId base, std::string_view editorId,
            double x, double y, double z, float heading);

        /// Forgets the actor behind `key` and tells `rules` (when not null). Unknown keys do nothing.
        void remove(openeden::sdk::game::ActorRules* rules, const void* key);

        /// Forgets every actor without telling anybody (shutdown: the pack may be gone already). Sessions go on
        /// counting up.
        void clear();

        /// Stage S4: the core reports that the go-to of the actor behind `key` ended. It is told to the pack at the
        /// start of the next update() (ActorRules::onGoalFinished), if the actor is still registered then. Does not
        /// allocate while fewer than kMaxActors results wait (the list is reserved).
        void reportGoal(const void* key, openeden::sdk::game::GoalResult result);

        /// Calls rules->update(seconds, writer); every valid request goes to `sink`. Does nothing for null rules or
        /// an empty table. A non-finite or negative `seconds` is passed as 0.
        void update(openeden::sdk::game::ActorRules* rules, float seconds, NeutralRequestSink& sink);

        /// Stage S5: the game clock the pack reads through ActorRequestWriter::clock() during update(). Set by the
        /// caller before each update(); the default is an invalid clock. A clock whose hour is not in [0, 24) or whose
        /// day is negative is stored as invalid.
        void setClock(const openeden::sdk::game::ActorClock& clock);

        /// Task 0196 (followers): where the player stands, read by the pack through
        /// ActorRequestWriter::playerPosition() during update(). Set by the caller before each update(); a value that
        /// is not finite makes the position invalid (the writer then refuses).
        void setPlayerPosition(bool valid, double x, double y, double z);

        /// Task 0251: the hooks behind the writer's RidingWriter; null (the default) refuses every riding call. The
        /// pointed-to struct must outlive the bridge's use of it.
        void setRidingHooks(const NeutralRidingHooks* hooks) { mRidingHooks = hooks; }

        /// Task 0255: asks the pack's riding rules to get the rider off NOW, outside update() (a load door is about to
        /// take the player). While `rules.dismountNow` runs, the writer's riding calls reach the hooks as they do inside
        /// update(); afterwards they are refused again. Main thread.
        void dismountRider(openeden::sdk::game::RidingRules& rules);

        [[nodiscard]] std::size_t size() const { return mEntries.size(); }
        [[nodiscard]] const Stats& stats() const { return mStats; }

        /// The most actors the table holds.
        static constexpr std::size_t kMaxActors = 4096;

        /// The handle registered for `key`; invalid when there is none.
        [[nodiscard]] openeden::sdk::game::ActorHandle handleOf(const void* key) const;

        /// Task 0170: the key behind a handle, or null when the handle is not (or no longer) registered. Unlike the
        /// writer's lookup it counts nothing and works outside update().
        [[nodiscard]] const void* keyOfHandle(openeden::sdk::game::ActorHandle handle) const;

    private:
        struct Entry
        {
            openeden::sdk::game::ActorHandle mHandle;
            const void* mKey;
        };

        class Writer final : public openeden::sdk::game::ActorRequestWriter,
                             public openeden::sdk::game::RidingWriter,
                             public openeden::sdk::game::PairedWriter
        {
        public:
            explicit Writer(NeutralActorBridge& bridge)
                : mBridge(bridge)
            {
            }
            bool setMovement(openeden::sdk::game::ActorHandle handle, float velX, float velY) override;
            bool goTo(openeden::sdk::game::ActorHandle handle, const openeden::sdk::game::ActorGoal& goal) override;
            bool cancelGoal(openeden::sdk::game::ActorHandle handle) override;
            bool setHeading(openeden::sdk::game::ActorHandle handle, float heading) override;
            bool useFurniture(
                openeden::sdk::game::ActorHandle handle, const openeden::sdk::game::FurnitureUse& use) override;
            bool leaveFurniture(openeden::sdk::game::ActorHandle handle, double x, double y, double z) override;
            bool placeFurniture(
                openeden::sdk::game::ActorHandle handle, double x, double y, double z, float heading) override;
            bool placeCorpse(
                openeden::sdk::game::ActorHandle handle, double x, double y, double z, float heading) override;
            bool actorPosition(openeden::sdk::game::ActorHandle handle, double& x, double& y, double& z) const override;
            [[nodiscard]] openeden::sdk::game::ActorClock clock() const override { return mBridge.mClock; }
            bool playerPosition(double& x, double& y, double& z) const override;
            // Task 0251 (RidingWriter): valid during update() only.
            bool riderControls(openeden::sdk::game::RiderControls& out) const override;
            bool carryPlayer(double x, double y, double z, float heading) override;
            bool releasePlayer(double x, double y, double z) override;
            // Task 0267 (PairedWriter): valid during update() and during dismountRider().
            openeden::sdk::game::PairedStatus pairedClipInfo(
                std::string_view clipName, openeden::sdk::game::PairedClipInfo& out) override;
            bool playPairedClip(openeden::sdk::game::ActorHandle actor, std::string_view clipName) override;
            NeutralRequestSink* mSink = nullptr; ///< non-null only while update() runs
            bool mSyncRiding = false; ///< true only while dismountRider() runs (task 0255)

        private:
            /// The key behind a valid handle while update() runs; counts the refusal otherwise.
            const void* keyOf(openeden::sdk::game::ActorHandle handle);
            NeutralActorBridge& mBridge;
        };

        struct PendingGoal
        {
            const void* mKey;
            openeden::sdk::game::GoalResult mResult;
        };
        std::vector<PendingGoal> mPendingGoals;

        // Sorted by session number: they are handed out in ascending order and never reused.
        std::vector<Entry> mEntries;
        std::uint32_t mNextSession = 1;
        Stats mStats;
        openeden::sdk::game::ActorClock mClock;
        bool mPlayerValid = false;
        double mPlayerX = 0.0;
        double mPlayerY = 0.0;
        double mPlayerZ = 0.0;
        const NeutralRidingHooks* mRidingHooks = nullptr;
        Writer mWriter;
    };
}

#endif
