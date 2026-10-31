#ifndef OPENEDEN_LAUNCHER_EXTENSIONSPAGE_H
#define OPENEDEN_LAUNCHER_EXTENSIONSPAGE_H

#include <QStringList>
#include <QWidget>

class QTableWidget;

namespace Files
{
    struct ConfigurationManager;
}

namespace Config
{
    class GameSettings;
}

namespace Launcher
{
    /// Lists installed OpenEden extensions and enables them by writing extension= lines to openmw.cfg (task 0018). It
    /// reads manifests only: no extension code runs in the launcher, and an extension's settings are not shown here
    /// yet.
    class ExtensionsPage : public QWidget
    {
        Q_OBJECT

    public:
        ExtensionsPage(
            const Files::ConfigurationManager& cfg, Config::GameSettings& gameSettings, QWidget* parent = nullptr);

        void loadSettings();
        void saveSettings();

    private:
        const Files::ConfigurationManager& mCfgMgr;
        Config::GameSettings& mGameSettings;
        QTableWidget* mTable;
        QStringList mSavedEnabled;
        bool mLoaded = false;
    };
}

#endif
