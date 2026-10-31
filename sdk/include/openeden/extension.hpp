#pragma once

#include <openeden/host.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <memory>
#include <string_view>

#if defined(_WIN32) || defined(__CYGWIN__)
#define OPENEDEN_EXTENSION_EXPORT __declspec(dllexport)
#else
#define OPENEDEN_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

namespace openeden::sdk
{
    /**
     * @brief Abstract base class implemented by each extension.
     *
     * Owned by the extension lifecycle manager from creation until unload.
     */
    class Extension
    {
    public:
        virtual ~Extension() = default;

        /**
         * @brief Called in load order to offer contract implementations and register callbacks.
         */
        virtual void registerServices(Registrar& registrar) = 0;

        /**
         * @brief Called in load order to find and cache required and optional contracts.
         * @return True if resolution succeeded; false if extension cannot run.
         */
        virtual bool resolve(const ServiceLocator& locator) = 0;

        /**
         * @brief Called in load order to start the extension and initialize runtime state.
         */
        virtual void start(Host& host) = 0;

        /**
         * @brief Called in reverse load order to gracefully stop the extension before destruction.
         */
        virtual void stop() = 0;
    };

    /**
     * @brief Static extension descriptor returned by openeden_extension_entry.
     */
    struct ExtensionDescriptor
    {
        Version sdkVersion;
        std::string_view id;
        std::unique_ptr<Extension> (*create)();
    };
} // namespace openeden::sdk

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* host);
