#include "neutralplayername.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

namespace MWWorld
{
    namespace
    {
        // A source is looked for this often while none was found (a session of another game, or the pack not up yet).
        constexpr std::uint32_t kLookupPeriodFrames = 60;
    }

    bool NeutralPlayerNameSync::poll(openeden::sdk::game::PlayerIdentitySource& source, std::string& name)
    {
        openeden::sdk::game::PlayerIdentityState state;
        if (!source.playerIdentity(state) || state.revision == 0 || state.revision == mSeen || state.name.empty())
            return false;
        mSeen = state.revision;
        name = std::move(state.name);
        return true;
    }

    NeutralPlayerNameDriver::~NeutralPlayerNameDriver() = default;

    std::unique_ptr<NeutralPlayerNameDriver> NeutralPlayerNameDriver::make()
    {
        if (!Settings::game().mNeutralWorld.get())
            return nullptr;
        return std::unique_ptr<NeutralPlayerNameDriver>(new NeutralPlayerNameDriver());
    }

    void NeutralPlayerNameDriver::update()
    {
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        if (store == nullptr)
            return;
        openeden::sdk::game::PlayerIdentitySource* source = nullptr;
        if (!mGameId.empty())
            source = dynamic_cast<openeden::sdk::game::PlayerIdentitySource*>(store->source(mGameId));
        else if (mLookupFrames++ % kLookupPeriodFrames == 0)
        {
            for (const std::string& id : store->gameIds())
                if (auto* found = dynamic_cast<openeden::sdk::game::PlayerIdentitySource*>(store->source(id)))
                {
                    mGameId = id;
                    source = found;
                    break;
                }
        }
        if (source == nullptr)
            return;
        std::string name;
        if (!mSync.poll(*source, name))
            return;
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        if (mechanics == nullptr)
            return;
        mechanics->setPlayerName(name);
        Log(Debug::Info) << "Neutral player name: '" << name << "'";
    }
}
