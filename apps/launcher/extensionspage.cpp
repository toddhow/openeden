#include "extensionspage.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <components/config/gamesettings.hpp>
#include <components/extension/extension_loader.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/qtconversion.hpp>

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

    const QLatin1String sExtensionKey("extension");
}

Launcher::ExtensionsPage::ExtensionsPage(
    const Files::ConfigurationManager& cfg, Config::GameSettings& gameSettings, QWidget* parent)
    : QWidget(parent)
    , mCfgMgr(cfg)
    , mGameSettings(gameSettings)
    , mTable(new QTableWidget(this))
{
    setObjectName("ExtensionsPage");

    mTable->setColumnCount(Column_Count);
    mTable->setHorizontalHeaderLabels({ tr("Extension"), tr("Version"), tr("Kind"), tr("Licence"), tr("Problem") });
    mTable->horizontalHeader()->setStretchLastSection(true);
    mTable->verticalHeader()->hide();
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* notice = new QLabel(tr("Extensions are programs that run with the same rights as the game. Install them only "
                                 "from sources you trust."),
        this);
    notice->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(notice);
    layout->addWidget(mTable);
}

void Launcher::ExtensionsPage::loadSettings()
{
    // The same places, in the same order, as OMW::Engine::prepareEngine
    const std::vector<openeden::extension::InstalledExtension> extensions
        = openeden::extension::listInstalledExtensions(
            { mCfgMgr.getLocalPath() / "extensions", mCfgMgr.getUserDataPath() / "extensions" });

    QStringList enabled;
    for (const Config::SettingValue& value : mGameSettings.values(sExtensionKey))
        enabled.append(value.value);

    // What was last written: GameSettings keeps the global file's lines, so it cannot say that
    mSavedEnabled = enabled;

    mTable->setRowCount(static_cast<int>(extensions.size()));
    int row = 0;
    for (const openeden::extension::InstalledExtension& extension : extensions)
    {
        const QString id = QString::fromStdString(extension.manifest.id);
        const QString name = extension.manifest.name.empty() ? id : QString::fromStdString(extension.manifest.name);

        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, id);
        nameItem->setToolTip(Files::pathToQString(extension.directory));
        if (extension.error.empty())
        {
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            nameItem->setCheckState(enabled.contains(id) ? Qt::Checked : Qt::Unchecked);
        }
        else
            nameItem->setFlags(Qt::NoItemFlags); // An extension with a problem cannot be enabled

        mTable->setItem(row, Column_Name, nameItem);
        mTable->setItem(
            row, Column_Version, new QTableWidgetItem(QString::fromStdString(extension.manifest.extensionVersion)));
        mTable->setItem(row, Column_Kind, new QTableWidgetItem(QString::fromStdString(extension.manifest.kind)));
        mTable->setItem(row, Column_Licence, new QTableWidgetItem(QString::fromStdString(extension.manifest.licence)));
        auto* problem = new QTableWidgetItem(QString::fromStdString(extension.error));
        problem->setToolTip(problem->text());
        mTable->setItem(row, Column_Problem, problem);
        ++row;
    }
    mTable->resizeColumnsToContents();
    mLoaded = true;
}

void Launcher::ExtensionsPage::saveSettings()
{
    if (!mLoaded)
        return;

    QStringList wanted;
    QStringList listed;
    for (int row = 0; row < mTable->rowCount(); ++row)
    {
        const QTableWidgetItem* item = mTable->item(row, Column_Name);
        const QString id = item->data(Qt::UserRole).toString();
        // An extension with a problem has no checkbox, and the player did not turn it off
        if (!(item->flags() & Qt::ItemIsUserCheckable))
            continue;
        listed.append(id);
        if (item->checkState() == Qt::Checked)
            wanted.append(id);
    }

    // A line for an extension that is not installed now is kept: it may be on a drive that is away
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
    // otherwise an extension enabled there could not be turned off.
    mGameSettings.remove(sExtensionKey);
    bool hasReplace = false;
    for (const Config::SettingValue& value : mGameSettings.values(QLatin1String("replace")))
        hasReplace = hasReplace || value.value == sExtensionKey;
    if (!hasReplace)
        mGameSettings.setMultiValue(QLatin1String("replace"), Config::SettingValue{ sExtensionKey });
    for (const QString& id : wanted)
        mGameSettings.setMultiValue(sExtensionKey, Config::SettingValue{ id });
}
