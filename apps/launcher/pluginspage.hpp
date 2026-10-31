#ifndef OPENEDEN_LAUNCHER_PLUGINSPAGE_H
#define OPENEDEN_LAUNCHER_PLUGINSPAGE_H

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
    /// Lists installed OpenEden plugins and enables them by writing plugin= lines to openmw.cfg (task 0018). It reads
    /// manifests only: no plugin code runs in the launcher, and a plugin's settings are not shown here yet.
    class PluginsPage : public QWidget
    {
        Q_OBJECT

    public:
        PluginsPage(const Files::ConfigurationManager& cfg, Config::GameSettings& gameSettings, QWidget* parent = nullptr);

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
