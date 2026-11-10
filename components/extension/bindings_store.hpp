#pragma once

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief One declared function, as the core keeps it. Instances never move or die while the store lives (the
     * script host's closures point at them); retiring a pack only clears `handler`.
     */
    struct BoundFunction
    {
        std::string name;
        std::string qualifiedName; ///< "<namespace>.<name>", for messages
        std::vector<openeden::sdk::BindingType> params;
        bool hasResult = false;
        openeden::sdk::BindingType result = openeden::sdk::BindingType::Bool;
        openeden::sdk::BindingAccess access = openeden::sdk::BindingAccess::Read;
        openeden::sdk::BindingHandler* handler = nullptr; ///< Not owned: the pack's. Null once the pack is retired.
        std::uint32_t maxCostMicroseconds = 0;
        std::uint32_t escapedExceptions = 0; ///< Exceptions that left the handler (the pack broke its contract)
        std::uint32_t overCostCalls = 0;
    };

    /// What invoking a BoundFunction produced, with the facts the caller reports once.
    struct Invoked
    {
        openeden::sdk::BindingResult result;
        bool firstEscapedException = false; ///< This call was the first on this function to leak an exception
        bool firstOverCost = false; ///< This call was the first on this function to pass its stated maximum cost
    };

    struct BoundNamespace
    {
        std::string name; ///< "<game id>.<area>"
        std::string packId;
        openeden::sdk::Version version;
        std::vector<BoundFunction> functions; ///< Declaration order; never resized after the declaration
    };

    /**
     * @brief The namespaces the game pack declared (task 0097). Main thread only to declare; the script host reads it
     * on the thread that runs scripts. Owned by HostImpl.
     *
     * Everything is kept in declaration order. A namespace outlives its pack as a retired shell, so a script that
     * still holds a function gets a clear error, never a call into unloaded code.
     */
    class BindingsStore
    {
    public:
        /// Checks the descriptor and stores a copy of it. `gameId` is the session's game (the pack's id).
        [[nodiscard]] openeden::sdk::DeclareResult declare(
            std::string_view packId, std::string_view gameId, const openeden::sdk::NamespaceDescriptor& descriptor);

        [[nodiscard]] const std::vector<std::unique_ptr<BoundNamespace>>& namespaces() const noexcept
        {
            return mNamespaces;
        }

        [[nodiscard]] std::size_t functionCount() const noexcept;

        /// The pack is going away: its handlers are cleared, so nothing can call into it. The namespaces stay.
        void retirePack(std::string_view packId) noexcept;
        void retireAll() noexcept;

        /// Calls the handler: refuses a retired function, catches whatever leaks out and checks the result's type.
        [[nodiscard]] static Invoked invoke(BoundFunction& function, std::span<const openeden::sdk::BindingValue> args);

    private:
        std::vector<std::unique_ptr<BoundNamespace>> mNamespaces;
    };

    /// True for [a-z][a-z0-9_]*.
    [[nodiscard]] bool isBindingIdentifier(std::string_view text) noexcept;
} // namespace openeden::extension
