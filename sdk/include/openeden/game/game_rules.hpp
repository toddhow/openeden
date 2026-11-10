#pragma once

#include <memory>
#include <string_view>

#include <openeden/game/crafting.hpp>
#include <openeden/version.hpp>

// The single place a game pack hands its rules to the core (task 0084 leftovers, lane I stage I1;
// docs/design/game-packs.md section 5.5). Game packs only, main thread only.
//
// A pack provides one GameRules object through the registry (Registrar::provide<GameRules>), found by its id and
// version like any contract. The core asks it for each group of rules once, when it installs them, and owns what it is
// given. The interface carries plain values and the other game-pack contracts of this folder; it has no engine,
// renderer, physics or Qt type, and the core never learns the pack's name.
//
// What it holds today is crafting. Further groups (mechanics hooks derived from MWBase::MechanicsManager, the journal
// and dialogue) join as their contracts exist; each is one more virtual function at the END with a default, so a pack
// written for an older version keeps compiling and keeps meaning "this game has no such rules".

namespace openeden::sdk::game
{
    /// This header's own version; the SDK as a whole is versioned by kSdkVersion (openeden/version.hpp).
    ///
    /// Version policy (the same as crafting.hpp). An additive change (a new virtual function at the END of a class,
    /// with a default; a new type) raises the minor number. A change to the layout or meaning of anything that exists
    /// (a removed or reordered virtual function, a changed signature) raises the major number. CI
    /// (CI/check_extension_sdk.sh) keeps this paragraph in place, and a test pins the number.
    inline constexpr Version kGameRulesVersion{ 1, 0 };

    /**
     * @brief The rules of one game, as the core consumes them. Implemented by a game pack; the pack owns this object
     * and keeps it alive until its stop() completes.
     */
    class GameRules
    {
    public:
        static constexpr std::string_view kId = "openeden.game.rules";
        static constexpr Version kVersion = kGameRulesVersion;

        virtual ~GameRules() = default;

        /// Canonical lower-case id of the game ("morrowind"); equal to the pack's `game=` id.
        [[nodiscard]] virtual std::string_view gameId() const noexcept = 0;

        /// A new crafting rule set, which the core then owns, or null when the game has no crafting rules (the core
        /// then runs its inert ones). Called once per installation, on the main thread.
        [[nodiscard]] virtual std::unique_ptr<CraftingRules> makeCraftingRules() { return nullptr; }
    };
} // namespace openeden::sdk::game
