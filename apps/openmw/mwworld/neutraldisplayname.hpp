#ifndef OPENMW_MWWORLD_NEUTRALDISPLAYNAME_H
#define OPENMW_MWWORLD_NEUTRALDISPLAYNAME_H

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <openeden/game/display_name_source.hpp>

#include "neutralstore.hpp"

namespace MWWorld
{
    /// Task 0150: the name the player sees for a neutral object (tooltip, activation caption). The pack gives it
    /// through the optional DisplayNameSource of its registered world source; without one, or when the base has no
    /// name, it is the editor id as before. The answer is cached by (game, base) because `Class::getName` returns a
    /// view and is asked every frame while a tooltip is up; cached strings never move (node based map, entries are only
    /// added). Owned by the process (one static in MWClass::Neutral); safe from any thread.
    class NeutralDisplayNames
    {
    public:
        /// `source` may be null. The returned view stays valid until `clear()`.
        [[nodiscard]] std::string_view nameOf(const NeutralBase& base, openeden::sdk::game::DisplayNameSource* source);

        /// Task 0224: keeps a text the pack gave for ONE placement (a depleted vein's name) and returns a view that stays
        /// valid until `clear()`; the same text gives the same view. Used for names that change with the object's state.
        [[nodiscard]] std::string_view intern(std::string_view text);

        /// Forgets every cached name (a new session may load other content).
        void clear();

    private:
        std::mutex mMutex;
        std::unordered_map<std::string, std::string> mNames;
        std::unordered_set<std::string> mInterned; ///< node based: element addresses never move
    };
}

#endif
