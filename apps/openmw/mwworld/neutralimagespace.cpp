#include "neutralimagespace.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwrender/rendermanager.hpp"

namespace MWWorld
{
    namespace
    {
        // A source is looked for this often while none was found (a session of another game, or the pack not up yet).
        constexpr unsigned kLookupPeriodFrames = 60;
    }

    Render::ImageSpace::Modifier neutralModifierFrom(openeden::sdk::game::PlayerImageSpaceSource& source)
    {
        openeden::sdk::game::PlayerImageSpaceState state;
        Render::ImageSpace::Modifier out;
        if (!source.playerImageSpace(state) || !state.active)
            return out;
        out.active = true;
        out.saturationMult = state.saturationMult;
        out.saturationAdd = state.saturationAdd;
        out.brightnessMult = state.brightnessMult;
        out.brightnessAdd = state.brightnessAdd;
        out.contrastMult = state.contrastMult;
        out.contrastAdd = state.contrastAdd;
        out.tintAmount = state.tintAmount;
        out.tint = state.tint;
        return out;
    }

    NeutralImageSpaceDriver::~NeutralImageSpaceDriver() = default;

    std::unique_ptr<NeutralImageSpaceDriver> NeutralImageSpaceDriver::make()
    {
        if (!Settings::game().mNeutralWorld.get() || !Settings::game().mNeutralImageSpaceEffects.get())
            return nullptr;
        return std::unique_ptr<NeutralImageSpaceDriver>(new NeutralImageSpaceDriver());
    }

    void NeutralImageSpaceDriver::update(MWRender::RenderManager& rendering)
    {
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        if (store == nullptr)
            return;
        openeden::sdk::game::PlayerImageSpaceSource* source = nullptr;
        if (!mGameId.empty())
            source = dynamic_cast<openeden::sdk::game::PlayerImageSpaceSource*>(store->source(mGameId));
        else if (mLookupFrames++ % kLookupPeriodFrames == 0)
        {
            for (const std::string& id : store->gameIds())
                if (auto* found = dynamic_cast<openeden::sdk::game::PlayerImageSpaceSource*>(store->source(id)))
                {
                    mGameId = id;
                    source = found;
                    break;
                }
        }
        if (source == nullptr)
            return;
        const Render::ImageSpace::Modifier modifier = neutralModifierFrom(*source);
        if (modifier.active != mWasActive)
        {
            mWasActive = modifier.active;
            Log(Debug::Info) << "Neutral image space: the player's modifier is " << (modifier.active ? "on" : "off");
        }
        rendering.setImageSpaceModifier(modifier);
    }
}
