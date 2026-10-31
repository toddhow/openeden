#pragma once

#include <openeden/phases.hpp>
#include <openeden/version.hpp>

#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Registration interface filled by a plugin during its registerServices() stage.
     */
    class Registrar
    {
    public:
        virtual ~Registrar() = default;

        /**
         * @brief Offers impl as the provider of contract C.
         *
         * The plugin owns impl and keeps it alive until its stop() method completes.
         * The registry holds a non-owning pointer.
         * @tparam C Contract interface type declaring kId and kVersion.
         * @param impl Reference to the contract implementation.
         */
        template <typename C>
        void provide(C& impl)
        {
            provideService(C::kId, C::kVersion, &impl);
        }

        /**
         * @brief Registers a callback to be called during a frame phase.
         * @param phase Frame phase (e.g. Input, PreSimulation, PostSimulation, Animation, PreRender, Ui).
         * @param callback Function to call during the phase.
         * @param parallel If true, promises callback touches only its own data and data passed in.
         */
        virtual void registerPhaseCallback(Phase phase, PhaseCallback callback, bool parallel = false) = 0;

    protected:
        /**
         * @brief Core service registration mechanism implemented by the engine registry.
         */
        virtual void provideService(std::string_view contractId, Version version, void* implPointer) = 0;
    };
} // namespace openeden::sdk
