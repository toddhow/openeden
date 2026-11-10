#pragma once

#include <string>

#include <openeden/game/world_source.hpp>
#include <openeden/version.hpp>

// The slot through which a game pack hands its WorldCellSource to the host (decision 0016, task 0103 stage N3a-1;
// docs/design/neutral-world-model.md 10.5). Reached by Host::getWorldSourceService(), which only a pack of kind "game"
// gets (every other kind gets nullptr and a recorded denial, like the bindings service). The host, if it consumes
// sources at all, keeps them in the core's neutral world store (components/worldmodel); a host that does not (Eden
// Studio, a test host) says so through consumerEnabled(), and a pack then does not register.
//
// This header is separate from world_source.hpp on purpose: that header and world_model.hpp are frozen at 1.0, and a
// new type there would raise their minor number. Carries no implementation and no engine, renderer, Jolt, Qt or game
// type. Main thread only, like the source interface.

namespace openeden::sdk::game
{
    /// Version policy: a new virtual function goes at the END of WorldSourceService with a default that means "not
    /// supported" and raises the minor number; anything else is a major change. Host::getWorldSourceService() is the
    /// last virtual function of Host, so adding it left every earlier slot where it was (kSdkVersion stays 4.0).
    inline constexpr Version kWorldSourceServiceVersion{ 1, 0 };

    class WorldSourceService
    {
    public:
        virtual ~WorldSourceService() = default;

        /// Hands the host a source. The pack keeps ownership and must withdraw the source before it is destroyed (in
        /// stop() and in its destructor; withdrawing twice is harmless). False with a located `error` when the host
        /// refuses it: another source has the same game id, the same object is already registered, or the host's
        /// store is full. A refused source is not registered.
        [[nodiscard]] virtual bool registerSource(WorldCellSource& source, std::string& error) = 0;

        /// Takes the source back. Does nothing for a source that is not registered. After it returns the host holds no
        /// pointer to the source and none to anything the source built.
        virtual void withdrawSource(WorldCellSource& source) = 0;

        /// True when the host will read the sources it is given (the engine with `[Game] neutral world` on). A pack
        /// registers only when it is true; when it is false the pack behaves as it did before the service existed.
        [[nodiscard]] virtual bool consumerEnabled() const = 0;
    };
}
