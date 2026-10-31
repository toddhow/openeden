#pragma once

#include <openeden/entity.hpp>
#include <openeden/event_bus.hpp>

namespace openeden::sdk
{
    /**
     * @brief Game events extensions can subscribe to on the event bus (task 0017).
     *
     * The core delivers them at the start of Phase::Input, on the main thread, however the engine came to raise them.
     * Text is cut to fit and always ends in a NUL.
     */

    inline constexpr std::size_t kEventNameCapacity = 64;

    /// A new game started or a saved game finished loading.
    struct GameLoadedEvent
    {
        static constexpr EventId kEventId = 0x47450001;
    };

    /// A saved game was written.
    struct GameSavedEvent
    {
        static constexpr EventId kEventId = 0x47450002;
    };

    /// The player moved to another cell.
    struct CellChangedEvent
    {
        static constexpr EventId kEventId = 0x47450003;

        char cellName[kEventNameCapacity] = {};
    };

    /// A cell became active.
    struct CellLoadedEvent
    {
        static constexpr EventId kEventId = 0x47450004;

        char cellName[kEventNameCapacity] = {};
    };

    /// A cell stopped being active.
    struct CellUnloadedEvent
    {
        static constexpr EventId kEventId = 0x47450005;

        char cellName[kEventNameCapacity] = {};
    };

    /// An attack against an actor was resolved. An invalid attacker means none is known.
    struct ActorHitEvent
    {
        static constexpr EventId kEventId = 0x47450006;

        EntityId attacker{};
        EntityId target{};
        float damage = 0.0f;
        bool successful = false;
    };

    /// An entity's lifecycle changed, the same as EntityLifecycleListener hears. Cell kinds have an invalid entity.
    struct EntityLifecycleNotice
    {
        static constexpr EventId kEventId = 0x47450007;

        EntityLifecycleEvent kind = EntityLifecycleEvent::Spawned;
        EntityId entity{};
    };
} // namespace openeden::sdk
