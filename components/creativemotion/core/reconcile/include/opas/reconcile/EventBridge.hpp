#pragma once

// Bidirectional Event Bus for game-to-physics event dispatching and
// physics-to-game state event synthesis
// (Milestone C7 Stage C7b, docs/tasks/0026-c7-reconciliation-and-ownership.md).

#include "opas/reconcile/CapsuleReconciler.hpp"
#include "opas/reconcile/Events.hpp"
#include "opas/reconcile/FixedEventQueue.hpp"

#include "opas/arbiter/Arbiter.hpp"
#include "opas/behaviors/BalanceDriver.hpp"
#include "opas/behaviors/GetUpController.hpp"
#include "opas/behaviors/GrabController.hpp"
#include "opas/behaviors/StumbleController.hpp"
#include "opas/sensors/ImpactSensor.hpp"

#include <cstddef>
#include <optional>

namespace opas::reconcile
{

    class EventBridge
    {
    public:
        static constexpr std::size_t kDefaultCapacity = 64;

        EventBridge() noexcept = default;

        [[nodiscard]] FixedEventQueue<GameEventIn, kDefaultCapacity>& inboundQueue() noexcept { return inboundQueue_; }

        [[nodiscard]] const FixedEventQueue<GameEventIn, kDefaultCapacity>& inboundQueue() const noexcept
        {
            return inboundQueue_;
        }

        [[nodiscard]] FixedEventQueue<StateEventOut, kDefaultCapacity>& outboundQueue() noexcept
        {
            return outboundQueue_;
        }

        [[nodiscard]] const FixedEventQueue<StateEventOut, kDefaultCapacity>& outboundQueue() const noexcept
        {
            return outboundQueue_;
        }

        bool postInbound(const GameEventIn& event) { return inboundQueue_.push(event); }

        std::optional<StateEventOut> pollOutbound() { return outboundQueue_.pop(); }

        void reset() noexcept;

        // Dispatches queued inbound events to sensory and behavior components.
        // Zero dynamic allocations.
        void dispatchInbound(sensors::ImpactSensor* impactSensor = nullptr,
            behaviors::BalanceDriver* balanceDriver = nullptr, arbiter::Arbiter* arbiter = nullptr);

        // Inspects character state machines, controllers, and capsule reconciler,
        // synthesizing edge-triggered outbound state events. Zero dynamic allocations.
        void updateOutbound(const arbiter::Arbiter& arbiter, const behaviors::StumbleController* stumble = nullptr,
            const behaviors::GetUpController* getUp = nullptr, const CapsuleReconciler* capsule = nullptr,
            const behaviors::GrabController* grab = nullptr);

    private:
        FixedEventQueue<GameEventIn, kDefaultCapacity> inboundQueue_;
        FixedEventQueue<StateEventOut, kDefaultCapacity> outboundQueue_;

        // Internal state tracking for edge-triggered event transitions
        std::optional<arbiter::ArbiterState> prevArbiterState_ = std::nullopt;
        bool prevStumbleActive_ = false;
        bool prevGetUpResting_ = false;
        bool prevGetUpBlending_ = false;
        bool prevGetUpCompleted_ = false;
        bool prevGrabHolding_ = false;
        bool prevCapsuleClamped_ = false;
        bool prevDied_ = false;
    };

} // namespace opas::reconcile
