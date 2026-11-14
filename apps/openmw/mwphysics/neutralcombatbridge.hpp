#ifndef OPENMW_MWPHYSICS_NEUTRALCOMBATBRIDGE_H
#define OPENMW_MWPHYSICS_NEUTRALCOMBATBRIDGE_H

// Task 0170 (decision 0018, milestone M-S5 stage S5-2): the engine's side of the combat hooks
// (sdk/include/openeden/game/combat_rules.hpp) for neutral actors: clip requests and clip events.
//
// The bridge turns CombatRequestWriter::playClip into a sink call (the renderer plays the clip once on the actor) and
// keeps a clip clock per playing clip. Once a frame, just before the pack's combatUpdate(), it advances every clock by
// the frame time and delivers the sidecar events that clock passed, in clip-time order (also several in one frame),
// then onClipFinished; interrupts are told at the start of the next frame. Events and the clip's length come from a
// ClipMeta (components/havokpack/clipevents.hpp) that a worker loads: a clip whose meta is not ready yet has no clock,
// and one that does not arrive within kMetaTimeoutSeconds ends as Failed.
//
// Threading: main thread only, like NeutralActorBridge, whose handles it uses. Allocation: update() and a request
// allocate nothing (the tables are reserved in the constructor); the meta is a shared_ptr copied from the provider.
// A pack function that throws is caught and counted. Only actors: the player has no handle (refused and counted).

#include "neutralactorbridge.hpp"

#include <components/havokpack/clipevents.hpp>

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace MWPhysics
{
    /// Where the clip goes. `key` is the actor key of the NeutralActorBridge.
    class NeutralClipSink
    {
    public:
        virtual ~NeutralClipSink() = default;
        virtual void playClip(const void* key, std::string_view clip, bool perSex) = 0;
        virtual void stopClip(const void* key) = 0;
    };

    /// How far the renderer is with the clip the actor behind `key` plays now: no meta while it is still being read (or
    /// when it failed), not started while the clip is being decoded.
    using NeutralClipMetaFn = std::function<HavokPack::ClipProgress(const void* key)>;

    /// The event kind of a sidecar event name (case-insensitive; Other for an unknown name).
    [[nodiscard]] openeden::sdk::game::ClipEventKind classifyClipEvent(std::string_view name);

    class NeutralCombatBridge
    {
    public:
        struct Stats
        {
            std::uint64_t mClipsStarted = 0;
            std::uint64_t mClipsCompleted = 0;
            std::uint64_t mClipsInterrupted = 0;
            std::uint64_t mClipsFailed = 0;
            std::uint64_t mEventsDelivered = 0;
            std::uint64_t mRefused = 0; ///< requests refused: stale or player subject, bad name or blend, slot None, outside update
            std::uint64_t mTableFull = 0;
            std::uint64_t mRulesFailures = 0; ///< a pack function that threw
            std::uint64_t mUpdates = 0;
        };

        static constexpr std::size_t kMaxPlaying = 64;
        static constexpr float kMetaTimeoutSeconds = 5.f;

        explicit NeutralCombatBridge(const NeutralActorBridge& actors);
        NeutralCombatBridge(const NeutralCombatBridge&) = delete;
        NeutralCombatBridge& operator=(const NeutralCombatBridge&) = delete;

        /// One frame: finishes told late, clocks, events, finishes, then rules->combatUpdate(seconds, writer). Does
        /// nothing for null rules (running clips are stopped and forgotten without telling anybody). A non-finite or
        /// negative `seconds` is passed as 0.
        void update(openeden::sdk::game::CombatRules* rules, float seconds, NeutralClipSink& sink,
            const NeutralClipMetaFn& meta);

        /// Forgets every clip without telling anybody (shutdown).
        void clear();

        [[nodiscard]] std::size_t playing() const { return mPlaying.size(); }
        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        struct Playing
        {
            openeden::sdk::game::CombatSubject mSubject;
            const void* mKey = nullptr;
            openeden::sdk::game::ClipSlot mSlot = openeden::sdk::game::ClipSlot::None;
            std::uint32_t mToken = 0;
            std::shared_ptr<const HavokPack::ClipMeta> mMeta;
            float mPrevious = -1.f; ///< clip clock before this frame's step
            float mCurrent = -1.f;
            float mWaited = 0.f; ///< seconds spent waiting for the meta
        };
        struct Finished
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken;
            openeden::sdk::game::ClipEnd mHow;
        };

        class Writer final : public openeden::sdk::game::CombatRequestWriter
        {
        public:
            explicit Writer(NeutralCombatBridge& bridge)
                : mBridge(bridge)
            {
            }
            bool playClip(openeden::sdk::game::CombatSubject subject,
                const openeden::sdk::game::ClipRequest& request) override;
            bool stopClip(openeden::sdk::game::CombatSubject subject) override;
            NeutralClipSink* mSink = nullptr; ///< non-null only while the pack's combatUpdate runs

        private:
            NeutralCombatBridge& mBridge;
        };

        std::size_t indexOf(openeden::sdk::game::CombatSubject subject) const;
        void tellFinished(openeden::sdk::game::CombatRules& rules, const Finished& finished);

        const NeutralActorBridge& mActors;
        std::vector<Playing> mPlaying;
        std::vector<Finished> mLate; ///< interrupts, told at the start of the next update
        Stats mStats;
        Writer mWriter;
    };
}

#endif
