#include "neutraldisplayname.hpp"

namespace MWWorld
{
    std::string_view NeutralDisplayNames::nameOf(
        const NeutralBase& base, openeden::sdk::game::DisplayNameSource* source)
    {
        // A placement with no base form, or no source: the editor id (what getName answered before this task).
        if (source == nullptr || base.mBase == openeden::sdk::game::kNoWorldId)
            return base.mEditorId;
        std::string key = base.mGameId;
        key += ':';
        key += std::to_string(base.mBase);
        const std::lock_guard lock(mMutex);
        auto it = mNames.find(key);
        if (it == mNames.end())
        {
            std::string name = source->displayNameOf(base.mBase);
            if (name.empty())
                name = base.mEditorId;
            it = mNames.emplace(std::move(key), std::move(name)).first;
        }
        return it->second;
    }

    void NeutralDisplayNames::clear()
    {
        const std::lock_guard lock(mMutex);
        mNames.clear();
    }
}
