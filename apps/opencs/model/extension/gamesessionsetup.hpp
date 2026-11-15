#ifndef CSM_EXTENSION_GAMESESSIONSETUP_H
#define CSM_EXTENSION_GAMESESSIONSETUP_H

#include <memory>
#include <string>
#include <vector>

#include <components/files/collections.hpp>
#include <components/toutf8/toutf8.hpp>

namespace VFS
{
    class Manager;
}

namespace CSMExtension
{
    class EditorExtensionHost;

    /// What a game pack needs before the editor host starts it (task 0173, stage c): a VFS of the session's data
    /// folders and archives (the game builds the same one in Engine::prepareEngine) and the session's content list.
    /// Owns the VFS, which must outlive the host: destroy the host first.
    class GameSessionSetup
    {
    public:
        GameSessionSetup();
        ~GameSessionSetup();

        /// When `host` has a loaded game pack that is not built in (gamePackState() is Loaded): builds the VFS
        /// (Bethesda archive kinds registered), collects the content list through the data folders
        /// (GameSession::collectContent) and hands both to host.setSessionContent. Otherwise does nothing and builds
        /// nothing. Call it before host.start().
        void prepare(EditorExtensionHost& host, const Files::PathContainer& dataDirs,
            const std::vector<std::string>& archives, const std::vector<std::string>& contentFiles,
            ToUTF8::FromType encoding);

        /// Null until prepare() built one.
        const VFS::Manager* vfs() const;

        /// The content names the last prepare() could not find or read (for the log).
        const std::vector<std::string>& problems() const { return mProblems; }

    private:
        std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
        std::unique_ptr<VFS::Manager> mVfs;
        std::vector<std::string> mProblems;
    };
}

#endif
