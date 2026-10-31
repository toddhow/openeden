#include "pluginspage.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <components/config/gamesettings.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/qtconversion.hpp>
#include <components/plugin/plugin_loader.hpp>

namespace
{
    enum Column
    {
        Column_Name,
        Column_Version,
        Column_Kind,
        Column_Licence,
        Column_Problem,
        Column_Count,
    };

    const QLatin1String sPluginKey("plugin");
}

Launcher::PluginsPage::PluginsPage(
    const Files::ConfigurationManager& cfg, Config::GameSettings& gameSettings, QWidget* parent)
    : QWidget(parent)
    , mCfgMgr(cfg)
    , mGameSettings(gameSettings)
    , mTable(new QTableWidget(this))
{
    setObjectName("PluginsPage");

    mTable->setColumnCount(Column_Count);
    mTable->setHorizontalHeaderLabels({ tr("Plugin"), tr("Version"), tr("Kind"), tr("Licence"), tr("Problem") });
    mTable->horizontalHeader()->setStretchLastSection(true);
    mTable->verticalHeader()->hide();
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* notice = new QLabel(
        tr("Plugins are programs that run with the same rights as the game. Install them only from sources you trust."),
        this);
    notice->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(notice);
    layout->addWidget(mTable);
}

void Launcher::PluginsPage::loadSettings()
{
    // The same places, in the same order, as OMW::Engine::prepareEngine
    const std::vector<openeden::plugin::InstalledPlugin> plugins = openeden::plugin::listInstalledPlugins(
        { mCfgMgr.getLocalPath() / "plugins", mCfgMgr.getUserDataPath() / "plugins" });

    QStringList enabled;
    for (const Config::SettingValue& value : mGameSettings.values(sPluginKey))
        enabled.append(value.value);

    // What was last written: GameSettings keeps the global file's lines, so it cannot say that
    mSavedEnabled = enabled;

    mTable->setRowCount(static_cast<int>(plugins.size()));
    int row = 0;
    for (const openeden::plugin::InstalledPlugin& plugin : plugins)
    {
        const QString id = QString::fromStdString(plugin.manifest.id);
        const QString name
            = plugin.manifest.name.empty() ? id : QString::fromStdString(plugin.manifest.name);

        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, id);
        nameItem->setToolTip(Files::pathToQString(plugin.directory));
        if (plugin.error.empty())
        {
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            nameItem->setCheckState(enabled.contains(id) ? Qt::Checked : Qt::Unchecked);
        }
        else
            nameItem->setFlags(Qt::NoItemFlags); // A plugin with a problem cannot be enabled

        mTable->setItem(row, Column_Name, nameItem);
        mTable->setItem(row, Column_Version, new QTableWidgetItem(QString::fromStdString(plugin.manifest.pluginVersion)));
        mTable->setItem(row, Column_Kind, new QTableWidgetItem(QString::fromStdString(plugin.manifest.kind)));
        mTable->setItem(row, Column_Licence, new QTableWidgetItem(QString::fromStdString(plugin.manifest.licence)));
        auto* problem = new QTableWidgetItem(QString::fromStdString(plugin.error));
        problem->setToolTip(problem->text());
        mTable->setItem(row, Column_Problem, problem);
        ++row;
    }
    mTable->resizeColumnsToContents();
    mLoaded = true;
}

void Launcher::PluginsPage::saveSettings()
{
    if (!mLoaded)
        return;

    QStringList wanted;
    QStringList listed;
    for (int row = 0; row < mTable->rowCount(); ++row)
    {
        const QTableWidgetItem* item = mTable->item(row, Column_Name);
        const QString id = item->data(Qt::UserRole).toString();
        // A plugin with a problem has no checkbox, and the player did not turn it off
        if (!(item->flags() & Qt::ItemIsUserCheckable))
            continue;
        listed.append(id);
        if (item->checkState() == Qt::Checked)
            wanted.append(id);
    }

    // A line for a plugin that is not installed now is kept: it may be on a drive that is away
    for (const QString& id : mSavedEnabled)
    {
        if (!listed.contains(id) && !wanted.contains(id))
            wanted.append(id);
    }

    QStringList sortedSaved = mSavedEnabled;
    sortedSaved.sort();
    QStringList sortedWanted = wanted;
    sortedWanted.sort();
    if (sortedSaved == sortedWanted)
        return; // Writes nothing unless the player changed something, so the default stays the installed one
    mSavedEnabled = wanted;

    // replace= drops the lines from the global openmw.cfg, which enables the compatibility pack by default;
    // otherwise a plugin enabled there could not be turned off.
    mGameSettings.remove(sPluginKey);
    bool hasReplace = false;
    for (const Config::SettingValue& value : mGameSettings.values(QLatin1String("replace")))
        hasReplace = hasReplace || value.value == sPluginKey;
    if (!hasReplace)
        mGameSettings.setMultiValue(QLatin1String("replace"), Config::SettingValue{ sPluginKey });
    for (const QString& id : wanted)
        mGameSettings.setMultiValue(sPluginKey, Config::SettingValue{ id });
}
