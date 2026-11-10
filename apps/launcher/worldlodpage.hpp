#ifndef OPENEDEN_LAUNCHER_WORLDLODPAGE_H
#define OPENEDEN_LAUNCHER_WORLDLODPAGE_H

#include <QList>
#include <QString>
#include <QWidget>

#include <components/worldlod/bakestatus.hpp>
#include <components/worldlod/lodsettings.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    /// The World LOD page (task 0058): the [WorldLod] settings, what has been baked, and the bake commands that
    /// exist today (`openmw --bake-lod-terrain`, `openmw --bake-lod-objects`). It also reads status.json when a
    /// baker writes one (judge() decides what it says). The current bake commands report no progress and do not read
    /// a command file, so Pause and Resume are shown disabled with that reason and Stop ends the child process.
    /// Rules live in components/worldlod (tested without Qt).
    class WorldLodPage : public QWidget
    {
        Q_OBJECT

    public:
        explicit WorldLodPage(const Files::ConfigurationManager& cfg, QWidget* parent = nullptr);
        ~WorldLodPage() override;

        void loadSettings();
        /// Writes the changed rows through Settings::Manager (MainDialog::writeSettings saves settings.cfg).
        void saveSettings();

        /// How the page learns that the game is running (bakes and deleting baked data wait for it to close). The
        /// default looks for a process named openmw; tests replace it.
        void setGameRunningCheck(std::function<bool()> check) { mGameRunning = std::move(check); }

    signals:
        /// The page is about to start a bake that reads settings.cfg and openmw.cfg: write them first.
        void settingsNeedSaving();

    protected:
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private slots:
        void bakeTerrain();
        void bakeObjects();
        void stopBake();
        void deleteCache();
        void resetToDefaults();
        void refresh();
        void onProcessFinished();

    private:
        struct Row
        {
            const WorldLod::SettingSpec* mSpec = nullptr;
            QWidget* mControl = nullptr;
            QLabel* mLabel = nullptr;
        };

        void buildUi();
        void buildSettingsRows(QWidget* parent);
        QString currentText(const Row& row) const;
        void setControl(const Row& row, const QString& text);
        void updateNeeds();
        void startBake(WorldLod::PackKind kind);
        void refreshStatus();
        void refreshCache();
        bool featureEnabled() const;
        QString executablePath() const;

        const Files::ConfigurationManager& mCfgMgr;
        std::vector<Row> mRows;
        std::map<std::string, Row*> mRowByKey;
        bool mLoading = false;

        QLabel* mState = nullptr;
        QLabel* mDetails = nullptr;
        QProgressBar* mProgress = nullptr;
        QTableWidget* mPacks = nullptr;
        QPushButton* mBakeTerrain = nullptr;
        QPushButton* mBakeObjects = nullptr;
        QPushButton* mStop = nullptr;
        QPushButton* mPause = nullptr;
        QPushButton* mDeleteCache = nullptr;
        QLabel* mCacheInfo = nullptr;
        QPlainTextEdit* mLog = nullptr;

        QProcess* mProcess = nullptr;
        WorldLod::PackKind mRunningKind = WorldLod::PackKind::TerrainTiles;
        bool mStopRequested = false;
        QString mLastRun;
        std::vector<WorldLod::PackInfo> mPackInfo;
        WorldLod::CacheListing mCacheListing;
        bool mCacheListed = false;
        QTimer* mTimer = nullptr;
        QList<QThread*> mThreads;
        std::function<bool()> mGameRunning;
    };
}

#endif
