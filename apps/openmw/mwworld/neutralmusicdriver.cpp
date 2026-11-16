#include "neutralmusicdriver.hpp"

#include "cellstore.hpp"
#include "neutralmusic.hpp"
#include "ptr.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/worldmodel/store.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/soundmanager.hpp"

namespace MWWorld
{
    namespace
    {
        /// The game's sink: the sound manager, with the Neutral music type (see MWBase::SoundManager).
        class SoundManagerSink final : public NeutralMusicSink
        {
        public:
            void stream(const std::string& vfsPath, float fadeSeconds) override
            {
                if (MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager())
                    sound->streamMusic(VFS::Path::Normalized(vfsPath), MWSound::MusicType::Neutral, fadeSeconds);
            }
            void fadeOut(float fadeSeconds) override
            {
                if (MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager())
                    sound->streamMusic(VFS::Path::Normalized(std::string()), MWSound::MusicType::Neutral, fadeSeconds);
            }
            void claim() override { fadeOut(0.f); }
            bool playing() override
            {
                MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
                return sound != nullptr && sound->isMusicPlaying();
            }
            void log(const std::string& line) override { Log(Debug::Info) << line; }
        };
    }

    struct NeutralMusicDriver::Impl
    {
        explicit Impl(openeden::sdk::game::MusicSource& source)
            : mDirector(source, mSink)
        {
        }
        SoundManagerSink mSink;
        NeutralMusicDirector mDirector;
    };

    NeutralMusicDriver::NeutralMusicDriver(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    NeutralMusicDriver::~NeutralMusicDriver() = default;

    std::unique_ptr<NeutralMusicDriver> NeutralMusicDriver::make()
    {
        if (!Settings::game().mNeutralWorld.get())
            return nullptr;
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        if (store == nullptr)
            return nullptr;
        for (const std::string& gameId : store->gameIds())
        {
            auto* source = dynamic_cast<openeden::sdk::game::MusicSource*>(store->source(gameId));
            if (source == nullptr || !source->musicEnabled())
                continue;
            Log(Debug::Info) << "Neutral music: the music of " << gameId << " is on";
            return std::unique_ptr<NeutralMusicDriver>(new NeutralMusicDriver(std::make_unique<Impl>(*source)));
        }
        return nullptr;
    }

    void NeutralMusicDriver::update(float dt, const Ptr& player, float gameHour)
    {
        if (player.isEmpty() || player.getCell() == nullptr)
            return;
        const openeden::sdk::game::WorldCell* cell = player.getCell()->neutralCell();
        const openeden::sdk::game::WorldId id = cell != nullptr ? cell->id : openeden::sdk::game::kNoWorldId;
        mImpl->mDirector.update(dt, id, gameHour);
    }
}
