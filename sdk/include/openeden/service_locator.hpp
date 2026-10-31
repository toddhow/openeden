#pragma once

#include <openeden/version.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Service locator interface read by extensions during resolution and execution.
     *
     * Provides access to contracts registered with the core.
     */
    class ServiceLocator
    {
    public:
        virtual ~ServiceLocator() = default;

        /**
         * @brief Looks up a contract implementation.
         * @tparam C Contract interface type declaring kId and kVersion.
         * @return Pointer to implementation if registered and compatible, or nullptr.
         */
        template <typename C>
        C* find() const
        {
            return static_cast<C*>(findService(C::kId, C::kVersion));
        }

        /**
         * @brief Looks up a required contract implementation, throwing if not found.
         * @tparam C Contract interface type declaring kId and kVersion.
         * @return Reference to implementation.
         * @throws std::runtime_error if contract is not registered or incompatible.
         */
        template <typename C>
        C& require() const
        {
            C* service = find<C>();
            if (!service)
            {
                throw std::runtime_error(
                    std::string("Required contract not found or incompatible: ") + std::string(C::kId));
            }
            return *service;
        }

    protected:
        /**
         * @brief Core service lookup mechanism implemented by the engine registry.
         */
        virtual void* findService(std::string_view contractId, Version minVersion) const = 0;
    };
} // namespace openeden::sdk
