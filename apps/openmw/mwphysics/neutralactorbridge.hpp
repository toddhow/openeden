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

#include <cstddef>
#include <cstdint>
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

        /// Calls rules->update(seconds, writer); every valid request goes to `sink`. Does nothing for null rules or
        /// an empty table. A non-finite or negative `seconds` is passed as 0.
        void update(openeden::sdk::game::ActorRules* rules, float seconds, NeutralRequestSink& sink);

        [[nodiscard]] std::size_t size() const { return mEntries.size(); }
        [[nodiscard]] const Stats& stats() const { return mStats; }

        /// The most actors the table holds.
        static constexpr std::size_t kMaxActors = 4096;

        /// The handle registered for `key`; invalid when there is none.
        [[nodiscard]] openeden::sdk::game::ActorHandle handleOf(const void* key) const;

    private:
        struct Entry
        {
            openeden::sdk::game::ActorHandle mHandle;
            const void* mKey;
        };

        class Writer final : public openeden::sdk::game::ActorRequestWriter
        {
        public:
            explicit Writer(NeutralActorBridge& bridge)
                : mBridge(bridge)
            {
            }
            bool setMovement(openeden::sdk::game::ActorHandle handle, float velX, float velY) override;
            NeutralRequestSink* mSink = nullptr; ///< non-null only while update() runs

        private:
            NeutralActorBridge& mBridge;
        };

        // Sorted by session number: they are handed out in ascending order and never reused.
        std::vector<Entry> mEntries;
        std::uint32_t mNextSession = 1;
        Stats mStats;
        Writer mWriter;
    };
}

#endif
