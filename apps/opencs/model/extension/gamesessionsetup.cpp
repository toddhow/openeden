#include "gamesessionsetup.hpp"

#include <utility>

#include <components/archivekinds/archivekinds.hpp>
#include <components/gamesession/gamesession.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include "editorextensionhost.hpp"

namespace CSMExtension
{
    GameSessionSetup::GameSessionSetup() = default;
    GameSessionSetup::~GameSessionSetup() = default;

    void GameSessionSetup::prepare(EditorExtensionHost& host, const Files::PathContainer& dataDirs,
        const std::vector<std::string>& archives, const std::vector<std::string>& contentFiles,
        ToUTF8::FromType encoding)
    {
        mProblems.clear();
        // Off means unchanged: no game pack, or a built-in one (Morrowind), builds nothing
        if (host.gamePackState() != EditorExtensionHost::GamePackState::Loaded)
            return;

        // The game builds the same VFS (apps/openmw/vsgengine.cpp, Engine::prepareEngine); archive names are decoded
        // with the content encoding, as there.
        mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(encoding);
        mVfs = std::make_unique<VFS::Manager>();
        VFS::registerBethesdaArchiveKinds();
        const Files::Collections collections(dataDirs);
        VFS::registerArchives(mVfs.get(), collections, archives, true, &mEncoder->getStatelessEncoder());

        openeden::extension::SessionContentCollection collected
            = GameSession::collectContent(contentFiles, collections);
        for (const std::string& name : collected.missing)
            mProblems.push_back(name + " is not in any data folder");
        for (const std::string& problem : collected.unreadable)
            mProblems.push_back(problem);
        host.setSessionContent(std::move(collected.entries), mVfs.get());
    }

    const VFS::Manager* GameSessionSetup::vfs() const
    {
        return mVfs.get();
    }
}
