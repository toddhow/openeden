#include "modspage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/qtconversion.hpp>
#include <components/modorganizer/configfile.hpp>
#include <components/modorganizer/processcheck.hpp>
#include <components/modorganizer/repack.hpp>

#include "utils/backgroundjob.hpp"
#include "utils/textinputdialog.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    namespace MO = ModOrganizer;

    const int sBaseRow = -1; // the data of a base-layer row

    QString q(const std::string& text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    std::string s(const QString& text)
    {
        return text.toUtf8().toStdString();
    }

    std::string readText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    bool fileExists(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

    std::string lowerAscii(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return text;
    }

    constexpr std::size_t sMaxConflictRows = 5000;

    enum ModColumn
    {
        ModColumn_Name,
        ModColumn_Kind,
        ModColumn_Status,
        ModColumn_Path,
        ModColumn_Count,
    };

    enum PluginColumn
    {
        PluginColumn_Name,
        PluginColumn_Provider,
        PluginColumn_Hint,
        PluginColumn_Count,
    };

    QTableWidget* makeTable(QWidget* parent, const QStringList& headers, const QString& accessibleName)
    {
        auto* table = new QTableWidget(parent);
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->hide();
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAccessibleName(accessibleName);
        return table;
    }
}

Launcher::ModsPage::ModsPage(const Files::ConfigurationManager& cfg, QWidget* parent)
    : QWidget(parent)
    , mCfgMgr(cfg)
    , mPaths(MO::storePaths(cfg.getUserConfigPath()))
    , mGameRunning([] { return MO::isProcessRunning("openmw"); })
{
    setObjectName("ModsPage");
    buildUi();
}

Launcher::ModsPage::~ModsPage()
{
    waitForJobs(mThreads);
}

void Launcher::ModsPage::buildUi()
{
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Profile:"), this));
    mProfileBox = new QComboBox(this);
    mProfileBox->setAccessibleName(tr("Active profile"));
    mProfileBox->setMinimumContentsLength(18);
    top->addWidget(mProfileBox, 1);
    auto* newButton = new QPushButton(tr("New..."), this);
    mCopyProfile = new QPushButton(tr("Copy..."), this);
    mRenameProfile = new QPushButton(tr("Rename..."), this);
    mDeleteProfile = new QPushButton(tr("Delete"), this);
    for (QPushButton* b : { newButton, mCopyProfile, mRenameProfile, mDeleteProfile })
        top->addWidget(b);

    mNotice = new QLabel(this);
    mNotice->setWordWrap(true);
    mNotice->setTextInteractionFlags(Qt::TextSelectableByMouse);

    mTabs = new QTabWidget(this);
    mTabs->addTab(buildModsTab(), tr("Mods"));
    mTabs->addTab(buildPluginsTab(), tr("Plugins"));
    mTabs->addTab(buildConflictsTab(), tr("Conflicts"));
    mTabs->addTab(buildRepackTab(), tr("Repack check"));

    mApplyState = new QLabel(this);
    mApplyState->setWordWrap(true);
    mApply = new QPushButton(tr("Apply profile to openmw.cfg"), this);
    mApply->setToolTip(
        tr("Writes this profile's data folders, plugins, archives, groundcover and extensions into "
           "your openmw.cfg. A backup copy is made first. Your mods are never changed."));
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(mApplyState, 1);
    bottom->addWidget(mApply);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(mNotice);
    layout->addWidget(mTabs, 1);
    layout->addLayout(bottom);

    connect(mProfileBox, &QComboBox::activated, this, &ModsPage::onProfileChosen);
    connect(newButton, &QPushButton::clicked, this, &ModsPage::newProfile);
    connect(mCopyProfile, &QPushButton::clicked, this, &ModsPage::copyProfile);
    connect(mRenameProfile, &QPushButton::clicked, this, &ModsPage::renameProfile);
    connect(mDeleteProfile, &QPushButton::clicked, this, &ModsPage::deleteProfile);
    connect(mApply, &QPushButton::clicked, this, &ModsPage::applyProfile);
    connect(mTabs, &QTabWidget::currentChanged, this, [this](int) { refreshApplyState(); });
}

QWidget* Launcher::ModsPage::buildModsTab()
{
    auto* page = new QWidget(this);
    auto* hint = new QLabel(tr("Mods are listed in priority order: a later mod wins over an earlier one. Space "
                               "switches a mod on or off, Alt+Up and Alt+Down move it. Mods stay where they are; "
                               "removing one from the list never deletes files."),
        page);
    hint->setWordWrap(true);

    mModsTable = makeTable(
        page, { tr("Mod"), tr("Kind"), tr("Status"), tr("Path") }, tr("Mods in priority order, later wins"));
    auto* add = new QPushButton(tr("Add folder..."), page);
    auto* addPackButton = new QPushButton(tr("Add pack..."), page);
    mRemoveMod = new QPushButton(tr("Remove from list"), page);
    mModUp = new QPushButton(tr("Move up"), page);
    mModDown = new QPushButton(tr("Move down"), page);
    auto* buttons = new QHBoxLayout;
    for (QPushButton* b : { add, addPackButton, mRemoveMod, mModUp, mModDown })
        buttons->addWidget(b);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(page);
    layout->addWidget(hint);
    layout->addWidget(mModsTable, 1);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &ModsPage::addFolder);
    connect(addPackButton, &QPushButton::clicked, this, &ModsPage::addPack);
    connect(mRemoveMod, &QPushButton::clicked, this, &ModsPage::removeSelectedMod);
    connect(mModUp, &QPushButton::clicked, this, &ModsPage::moveModUp);
    connect(mModDown, &QPushButton::clicked, this, &ModsPage::moveModDown);
    connect(mModsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { onModItemChanged(); });
    connect(mModsTable, &QTableWidget::itemSelectionChanged, this, &ModsPage::onModSelectionChanged);

    auto* up = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), mModsTable, nullptr, nullptr, Qt::WidgetShortcut);
    auto* down = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), mModsTable, nullptr, nullptr, Qt::WidgetShortcut);
    auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), mModsTable, nullptr, nullptr, Qt::WidgetShortcut);
    connect(up, &QShortcut::activated, this, &ModsPage::moveModUp);
    connect(down, &QShortcut::activated, this, &ModsPage::moveModDown);
    connect(del, &QShortcut::activated, this, &ModsPage::removeSelectedMod);
    return page;
}

QWidget* Launcher::ModsPage::buildPluginsTab()
{
    auto* page = new QWidget(this);
    auto* hint = new QLabel(tr("The load order of the plugins (.esm, .esp, .omwaddon). The order is yours: hints about "
                               "masters never change it, and Sort by masters asks first."),
        page);
    hint->setWordWrap(true);
    mPluginsTable = makeTable(page, { tr("Plugin"), tr("Comes from"), tr("Hints") }, tr("Plugins in load order"));
    mPluginUp = new QPushButton(tr("Move up"), page);
    mPluginDown = new QPushButton(tr("Move down"), page);
    auto* addNew = new QPushButton(tr("Add plugins from mods"), page);
    auto* removeMissing = new QPushButton(tr("Remove missing"), page);
    mSortMasters = new QPushButton(tr("Sort by masters..."), page);
    auto* buttons = new QHBoxLayout;
    for (QPushButton* b : { mPluginUp, mPluginDown, addNew, removeMissing, mSortMasters })
        buttons->addWidget(b);
    buttons->addStretch(1);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(hint);
    layout->addWidget(mPluginsTable, 1);
    layout->addLayout(buttons);

    connect(mPluginUp, &QPushButton::clicked, this, &ModsPage::movePluginUp);
    connect(mPluginDown, &QPushButton::clicked, this, &ModsPage::movePluginDown);
    connect(addNew, &QPushButton::clicked, this, &ModsPage::addNewPlugins);
    connect(removeMissing, &QPushButton::clicked, this, &ModsPage::removeMissingPlugins);
    connect(mSortMasters, &QPushButton::clicked, this, &ModsPage::sortByMasters);
    connect(mPluginsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { onPluginItemChanged(); });

    auto* up = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), mPluginsTable, nullptr, nullptr, Qt::WidgetShortcut);
    auto* down
        = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), mPluginsTable, nullptr, nullptr, Qt::WidgetShortcut);
    connect(up, &QShortcut::activated, this, &ModsPage::movePluginUp);
    connect(down, &QShortcut::activated, this, &ModsPage::movePluginDown);
    return page;
}

QWidget* Launcher::ModsPage::buildConflictsTab()
{
    auto* page = new QWidget(this);
    auto* hint = new QLabel(tr("Which mod's file the game would use when several mods contain the same file. The "
                               "scan reads folder listings only and does not start the game."),
        page);
    hint->setWordWrap(true);
    mScanConflicts = new QPushButton(tr("Scan for conflicts"), page);
    mConflictStatus = new QLabel(tr("Not scanned yet."), page);
    mConflictStatus->setWordWrap(true);
    mConflictCounts
        = makeTable(page, { tr("Source"), tr("Overridden files"), tr("Overriding files"), tr("Identical copies") },
            tr("Conflict counts per source"));
    mConflictFilter = new QLineEdit(page);
    mConflictFilter->setPlaceholderText(tr("Filter files by name"));
    mConflictFilter->setAccessibleName(tr("Filter conflicting files"));
    mConflictFiles = makeTable(page, { tr("File"), tr("Used from"), tr("Also in") }, tr("Conflicting files"));

    auto* row = new QHBoxLayout;
    row->addWidget(mScanConflicts);
    row->addWidget(mConflictStatus, 1);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(hint);
    layout->addLayout(row);
    layout->addWidget(mConflictCounts, 1);
    layout->addWidget(mConflictFilter);
    layout->addWidget(mConflictFiles, 2);

    connect(mScanConflicts, &QPushButton::clicked, this, &ModsPage::scanConflicts);
    connect(mConflictFilter, &QLineEdit::textChanged, this, &ModsPage::applyConflictFilter);
    return page;
}

QWidget* Launcher::ModsPage::buildRepackTab()
{
    auto* page = new QWidget(this);
    auto* hint = new QLabel(tr("A repack would put a mod's loose files into one Eden Archive pack, which loads faster. "
                               "This check only measures: it reports sizes, free space and file names that cannot be "
                               "packed. It writes nothing and your mod stays as it is. Building the pack itself "
                               "needs the edenpack tool and is not started from here yet."),
        page);
    hint->setWordWrap(true);
    mRepackChoice = new QComboBox(page);
    mRepackChoice->setAccessibleName(tr("Mod to check"));
    mRepackCheck = new QPushButton(tr("Check"), page);
    mRepackOutput = new QPlainTextEdit(page);
    mRepackOutput->setReadOnly(true);
    mRepackOutput->setAccessibleName(tr("Repack check result"));
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Mod:"), page));
    row->addWidget(mRepackChoice, 1);
    row->addWidget(mRepackCheck);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(hint);
    layout->addLayout(row);
    layout->addWidget(mRepackOutput, 1);
    connect(mRepackCheck, &QPushButton::clicked, this, &ModsPage::checkRepack);
    return page;
}

// ---- load and save ------------------------------------------------------------------------------------------------

void Launcher::ModsPage::loadSettings()
{
    mStore = MO::loadStore(mPaths);

    const std::filesystem::path userCfg = mCfgMgr.getUserConfigPath() / Files::openmwCfgFile;
    if (mStore.mProfiles.empty() && !mStore.mLibraryReadOnly)
    {
        MO::ConfigSnapshot snapshot = MO::snapshotFromConfigText(fileExists(userCfg) ? readText(userCfg) : "");
        // When the installation's own openmw.cfg already names the game data, no user data line is the base layer
        const std::filesystem::path globalCfg = mCfgMgr.getGlobalPath() / Files::openmwCfgFile;
        if (fileExists(globalCfg))
            snapshot.mFirstDataIsBase = MO::snapshotFromConfigText(readText(globalCfg)).mData.empty();
        // Not written yet: opening the launcher must not create files. The import is saved with the first edit or
        // when the profile is applied.
        MO::importIfEmpty(mStore, snapshot);
    }
    mLoaded = true;
    refreshAll();
    startPluginReport();
}

void Launcher::ModsPage::saveSettings()
{
    // Edits are saved as they are made (changed()); this only retries a save that failed, so that opening and
    // closing the launcher writes nothing
    if (!mLoaded || !mSaveFailed)
        return;
    saveNow();
}

bool Launcher::ModsPage::saveNow()
{
    const auto errors = MO::saveStore(mPaths, mStore);
    mSaveFailed = !errors.empty();
    if (mSaveFailed)
        showResult(q(errors.front()), true);
    return !mSaveFailed;
}

void Launcher::ModsPage::changed()
{
    saveNow();
    refreshAll();
}

// ---- refresh --------------------------------------------------------------------------------------------------------

void Launcher::ModsPage::refreshAll()
{
    mRefreshing = true;
    refreshProfiles();
    refreshMods();
    refreshPlugins();
    refreshRepackChoices();
    mRefreshing = false;
    refreshApplyState();
}

void Launcher::ModsPage::refreshProfiles()
{
    mProfileBox->clear();
    for (const MO::Profile& profile : mStore.mProfiles)
        mProfileBox->addItem(q(profile.mName));
    const int index = mProfileBox->findText(q(mStore.mActive));
    mProfileBox->setCurrentIndex(index);

    const bool has = mStore.active() != nullptr;
    mCopyProfile->setEnabled(has);
    mRenameProfile->setEnabled(has && !mStore.isReadOnly(mStore.mActive));
    mDeleteProfile->setEnabled(has && mStore.mProfiles.size() > 1 && !mStore.isReadOnly(mStore.mActive));

    updateNotice();
}

QString Launcher::ModsPage::modName(const QString& id) const
{
    if (const MO::Mod* mod = mStore.mLibrary.find(s(id)))
        return q(mod->mName);
    return id;
}

void Launcher::ModsPage::refreshMods()
{
    const int selectedIndex = selectedModIndex();
    mModsTable->setRowCount(0);
    const MO::Profile* profile = mStore.active();
    const int baseCount = static_cast<int>(mStore.mBaseData.size());
    const int modCount = profile != nullptr ? static_cast<int>(profile->mMods.size()) : 0;
    mModsTable->setRowCount(baseCount + modCount);

    auto textItem = [](const QString& text) {
        auto* item = new QTableWidgetItem(text);
        item->setToolTip(text);
        return item;
    };

    for (int i = 0; i < baseCount; ++i)
    {
        auto* name = new QTableWidgetItem(tr("Game data"));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setData(Qt::UserRole, sBaseRow);
        mModsTable->setItem(i, ModColumn_Name, name);
        mModsTable->setItem(i, ModColumn_Kind, textItem(tr("Base")));
        mModsTable->setItem(i, ModColumn_Status, textItem(tr("Locked: always first")));
        mModsTable->setItem(i, ModColumn_Path, textItem(q(mStore.mBaseData[static_cast<std::size_t>(i)])));
    }
    for (int i = 0; i < modCount; ++i)
    {
        const MO::ProfileMod& entry = profile->mMods[static_cast<std::size_t>(i)];
        const MO::Mod* mod = mStore.mLibrary.find(entry.mId);
        auto* name = new QTableWidgetItem(mod != nullptr ? q(mod->mName) : q(entry.mId));
        name->setData(Qt::UserRole, i);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        name->setCheckState(entry.mEnabled ? Qt::Checked : Qt::Unchecked);
        const int row = baseCount + i;
        mModsTable->setItem(row, ModColumn_Name, name);
        QString kind;
        QString status;
        QString path;
        if (mod == nullptr)
        {
            kind = tr("Unknown");
            status = tr("Not in the library");
        }
        else
        {
            kind = mod->mKind == MO::ModKind::Pack ? tr("Pack") : tr("Folder");
            path = q(mod->mRoot);
            std::error_code ec;
            const std::filesystem::path root = Files::pathFromUnicodeString(mod->mRoot);
            if (!std::filesystem::exists(root, ec))
                status = tr("Missing on disk");
            else if (mod->mKind == MO::ModKind::Pack)
                status = tr("Ready");
            else if (!mod->mDataPath.empty())
                status = tr("Ready, data in \"%1\"").arg(q(mod->mDataPath));
            else
                status = tr("Ready");
            if (!entry.mEnabled)
                status = tr("Off. %1").arg(status);
        }
        mModsTable->setItem(row, ModColumn_Kind, textItem(kind));
        mModsTable->setItem(row, ModColumn_Status, textItem(status));
        mModsTable->setItem(row, ModColumn_Path, textItem(path));
    }
    mModsTable->resizeColumnsToContents();
    mModsTable->horizontalHeader()->setStretchLastSection(true);

    if (selectedIndex >= 0 && selectedIndex < modCount)
        mModsTable->selectRow(baseCount + selectedIndex);
    onModSelectionChanged();
}

void Launcher::ModsPage::refreshPlugins()
{
    const int selected = selectedPluginIndex();
    mPluginsTable->setRowCount(0);
    const MO::Profile* profile = mStore.active();
    if (profile == nullptr)
        return;
    mPluginsTable->setRowCount(static_cast<int>(profile->mPlugins.size()));

    std::map<std::string, const MO::PluginRow*> rows;
    for (const MO::PluginRow& row : mReport.mRows)
        rows[lowerAscii(row.mName)] = &row;

    for (std::size_t i = 0; i < profile->mPlugins.size(); ++i)
    {
        const MO::PluginEntry& plugin = profile->mPlugins[i];
        const int row = static_cast<int>(i);
        auto* name = new QTableWidgetItem(q(plugin.mName));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        name->setCheckState(plugin.mActive ? Qt::Checked : Qt::Unchecked);
        mPluginsTable->setItem(row, PluginColumn_Name, name);

        QString provider;
        QStringList hints;
        const auto found = rows.find(lowerAscii(plugin.mName));
        if (found != rows.end())
        {
            const MO::PluginRow& info = *found->second;
            if (!info.mProviderModId.empty())
                provider = modName(q(info.mProviderModId));
            else if (info.mFromBase)
                provider = tr("Game data");
            else
                provider = tr("Not found");
            if (!info.mFile.empty() && !info.mError.empty())
                hints << q(info.mError);
            else if (info.mFile.empty() && !info.mError.empty())
                hints << tr("Missing: the file is in no enabled mod or the game data.");
        }
        for (const MO::Hint& hint : mHints)
        {
            if (hint.mPlugin != plugin.mName)
                continue;
            switch (hint.mKind)
            {
                case MO::HintKind::MissingMaster:
                    hints << tr("Needs %1, which is not in the list.").arg(q(hint.mMaster));
                    break;
                case MO::HintKind::InactiveMaster:
                    hints << tr("Needs %1, which is switched off.").arg(q(hint.mMaster));
                    break;
                case MO::HintKind::MasterAfter:
                    hints << tr("Needs %1, which is listed after it.").arg(q(hint.mMaster));
                    break;
            }
        }
        mPluginsTable->setItem(row, PluginColumn_Provider, new QTableWidgetItem(provider));
        auto* hintItem = new QTableWidgetItem(hints.join(QLatin1Char(' ')));
        hintItem->setToolTip(hintItem->text());
        mPluginsTable->setItem(row, PluginColumn_Hint, hintItem);
    }
    mPluginsTable->resizeColumnsToContents();
    mPluginsTable->horizontalHeader()->setStretchLastSection(true);
    if (selected >= 0 && selected < mPluginsTable->rowCount())
        mPluginsTable->selectRow(selected);
}

void Launcher::ModsPage::refreshRepackChoices()
{
    const QString previous = mRepackChoice->currentData().toString();
    mRepackChoice->clear();
    const MO::Profile* profile = mStore.active();
    if (profile != nullptr)
    {
        for (const MO::ProfileMod& entry : profile->mMods)
        {
            const MO::Mod* mod = mStore.mLibrary.find(entry.mId);
            if (mod != nullptr && mod->mKind == MO::ModKind::Folder)
                mRepackChoice->addItem(q(mod->mName), q(mod->mId));
        }
    }
    const int index = mRepackChoice->findData(previous);
    if (index >= 0)
        mRepackChoice->setCurrentIndex(index);
    mRepackCheck->setEnabled(mRepackChoice->count() > 0);
}

void Launcher::ModsPage::refreshApplyState()
{
    if (mStore.active() == nullptr)
    {
        mApplyState->setText(tr("There is no profile yet."));
        mApply->setEnabled(false);
        return;
    }
    const std::filesystem::path cfg = mCfgMgr.getUserConfigPath() / Files::openmwCfgFile;
    const auto lines = MO::renderConfigLines(MO::activeLayout(mStore));
    if (!fileExists(cfg))
        mApplyState->setText(tr("openmw.cfg does not exist yet; Apply creates it."));
    else if (MO::differsFromProfile(readText(cfg), lines))
        mApplyState->setText(tr("This profile is not applied to openmw.cfg yet."));
    else
        mApplyState->setText(tr("openmw.cfg matches this profile."));
    mApply->setEnabled(mBusy == 0);
}

void Launcher::ModsPage::showResult(const QString& text, bool problem)
{
    mResult = problem ? tr("Problem: %1").arg(text) : text;
    mResultIsBusy = false;
    updateNotice();
}

void Launcher::ModsPage::updateNotice()
{
    QStringList notes;
    if (mStore.mLibraryReadOnly)
        notes << tr("The mod library was written by a newer version of this program. It is shown but not changed.");
    for (const std::string& name : mStore.mReadOnlyProfiles)
        notes << tr("The profile \"%1\" was written by a newer version and is not listed or changed.").arg(q(name));
    for (const std::string& problem : mStore.mProblems)
        notes << tr("Problem: %1").arg(q(problem));
    if (!mResult.isEmpty())
        notes << mResult;
    mNotice->setVisible(!notes.isEmpty());
    mNotice->setText(notes.join(QLatin1Char('\n')));
}

void Launcher::ModsPage::setBusy(bool busy, const QString& what)
{
    mBusy += busy ? 1 : -1;
    const bool idle = mBusy <= 0;
    mTabs->setEnabled(idle);
    mProfileBox->setEnabled(idle);
    mApply->setEnabled(idle);
    if (!what.isEmpty())
    {
        showResult(what, false);
        mResultIsBusy = true;
    }
    else if (idle && mResultIsBusy)
    {
        mResult.clear(); // the job is over: the message that said "working" goes with it
        mResultIsBusy = false;
        updateNotice();
    }
}

// ---- selection ----------------------------------------------------------------------------------------------------

int Launcher::ModsPage::selectedModIndex() const
{
    const int row = mModsTable->currentRow();
    if (row < 0 || mModsTable->selectedItems().isEmpty())
        return -1;
    const QTableWidgetItem* item = mModsTable->item(row, ModColumn_Name);
    return item != nullptr ? item->data(Qt::UserRole).toInt() : -1;
}

int Launcher::ModsPage::selectedPluginIndex() const
{
    if (mPluginsTable->selectedItems().isEmpty())
        return -1;
    return mPluginsTable->currentRow();
}

void Launcher::ModsPage::onModSelectionChanged()
{
    const int index = selectedModIndex();
    const MO::Profile* profile = mStore.active();
    const bool real = index >= 0 && profile != nullptr;
    mRemoveMod->setEnabled(real && !mStore.mLibraryReadOnly);
    mModUp->setEnabled(real && index > 0);
    mModDown->setEnabled(real && index + 1 < static_cast<int>(profile->mMods.size()));
}

void Launcher::ModsPage::onModItemChanged()
{
    if (mRefreshing)
        return;
    MO::Profile* profile = mStore.active();
    if (profile == nullptr || mStore.isReadOnly(mStore.mActive))
        return;
    bool any = false;
    for (int row = 0; row < mModsTable->rowCount(); ++row)
    {
        const QTableWidgetItem* item = mModsTable->item(row, ModColumn_Name);
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(profile->mMods.size()))
            continue;
        const bool enabled = item->checkState() == Qt::Checked;
        MO::ProfileMod& mod = profile->mMods[static_cast<std::size_t>(index)];
        if (mod.mEnabled != enabled)
        {
            mod.mEnabled = enabled;
            any = true;
        }
    }
    if (any)
    {
        changed();
        startPluginReport();
    }
}

void Launcher::ModsPage::onPluginItemChanged()
{
    if (mRefreshing)
        return;
    MO::Profile* profile = mStore.active();
    if (profile == nullptr || mStore.isReadOnly(mStore.mActive))
        return;
    bool any = false;
    for (int row = 0; row < mPluginsTable->rowCount() && row < static_cast<int>(profile->mPlugins.size()); ++row)
    {
        const bool active = mPluginsTable->item(row, PluginColumn_Name)->checkState() == Qt::Checked;
        MO::PluginEntry& plugin = profile->mPlugins[static_cast<std::size_t>(row)];
        if (plugin.mActive != active)
        {
            plugin.mActive = active;
            any = true;
        }
    }
    if (any)
    {
        changed();
        startPluginReport();
    }
}

// ---- profiles ------------------------------------------------------------------------------------------------------

bool Launcher::ModsPage::askName(const QString& title, const QString& label, QString& name, const QString& initial)
{
    TextInputDialog dialog(title, label, this);
    dialog.lineEdit()->setText(initial);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    name = dialog.lineEdit()->text().trimmed();
    if (!MO::isValidProfileName(s(name)))
    {
        QMessageBox::warning(
            this, title, tr("This is not a usable profile name. Use plain text without slashes, up to 64 characters."));
        return false;
    }
    return true;
}

void Launcher::ModsPage::onProfileChosen(int index)
{
    if (index < 0 || index >= static_cast<int>(mStore.mProfiles.size()))
        return;
    mStore.mActive = mStore.mProfiles[static_cast<std::size_t>(index)].mName;
    changed();
    startPluginReport();
}

void Launcher::ModsPage::newProfile()
{
    QString name;
    if (!askName(tr("New profile"), tr("Name of the new, empty profile:"), name))
        return;
    if (!MO::createProfile(mStore, s(name)))
    {
        QMessageBox::warning(this, tr("New profile"), tr("A profile with this name exists already."));
        return;
    }
    mStore.mActive = s(name);
    changed();
    startPluginReport();
}

void Launcher::ModsPage::copyProfile()
{
    QString name;
    const QString from = q(mStore.mActive);
    if (!askName(tr("Copy profile"), tr("Name of the copy of \"%1\":").arg(from), name, from + tr(" copy")))
        return;
    if (!MO::createProfile(mStore, s(name), s(from)))
    {
        QMessageBox::warning(this, tr("Copy profile"), tr("A profile with this name exists already."));
        return;
    }
    mStore.mActive = s(name);
    changed();
}

void Launcher::ModsPage::renameProfile()
{
    QString name;
    const QString from = q(mStore.mActive);
    if (!askName(tr("Rename profile"), tr("New name of \"%1\":").arg(from), name, from))
        return;
    if (!MO::renameProfile(mStore, s(from), s(name)))
    {
        QMessageBox::warning(this, tr("Rename profile"), tr("A profile with this name exists already."));
        return;
    }
    changed();
}

void Launcher::ModsPage::deleteProfile()
{
    const QString name = q(mStore.mActive);
    const auto answer = QMessageBox::question(this, tr("Delete profile"),
        tr("Delete the profile \"%1\"? Your mods and your openmw.cfg are not changed.").arg(name));
    if (answer != QMessageBox::Yes)
        return;
    if (MO::deleteProfile(mStore, s(name)))
    {
        changed();
        startPluginReport();
    }
}

// ---- mods ----------------------------------------------------------------------------------------------------------

void Launcher::ModsPage::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Select a mod folder"));
    if (folder.isEmpty())
        return;
    setBusy(true, tr("Looking at the folder..."));
    MO::Store copy = mStore;
    const std::filesystem::path path = Files::pathFromQString(folder);
    startBackgroundJob(
        this, mThreads,
        [copy = std::move(copy), path]() mutable {
            const MO::EditResult result = MO::addFolderMod(copy, path);
            return std::make_pair(std::move(copy), result);
        },
        [this](std::pair<MO::Store, MO::EditResult> outcome) {
            setBusy(false);
            if (!outcome.second.mOk)
            {
                showResult(q(outcome.second.mError), true);
                return;
            }
            mStore = std::move(outcome.first);
            changed();
            QString text = tr("Added \"%1\". Its plugins are in the Plugins tab, switched off.")
                               .arg(modName(q(outcome.second.mId)));
            if (!outcome.second.mWarnings.empty())
                text += QLatin1Char(' ')
                    + tr("%1 file names could not be used; the first: %2")
                          .arg(outcome.second.mWarnings.size())
                          .arg(q(outcome.second.mWarnings.front()));
            showResult(text, false);
            startPluginReport();
        });
}

void Launcher::ModsPage::addPack()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select an Eden Archive pack"), QString(), tr("Eden Archive (*.edenpak)"));
    if (file.isEmpty())
        return;
    const MO::EditResult result = MO::addPackMod(mStore, Files::pathFromQString(file));
    if (!result.mOk)
    {
        showResult(q(result.mError), true);
        return;
    }
    changed();
    showResult(tr("Added \"%1\".").arg(modName(q(result.mId))), false);
}

void Launcher::ModsPage::removeSelectedMod()
{
    const int index = selectedModIndex();
    const MO::Profile* profile = mStore.active();
    if (index < 0 || profile == nullptr || index >= static_cast<int>(profile->mMods.size()))
        return;
    const QString id = q(profile->mMods[static_cast<std::size_t>(index)].mId);
    const auto answer = QMessageBox::question(
        this, tr("Remove mod"), tr("Remove \"%1\" from the list? The mod's files are not deleted.").arg(modName(id)));
    if (answer != QMessageBox::Yes)
        return;
    if (MO::removeMod(mStore, s(id)))
    {
        changed();
        startPluginReport();
    }
}

void Launcher::ModsPage::moveMod(int delta)
{
    MO::Profile* profile = mStore.active();
    const int index = selectedModIndex();
    if (profile == nullptr || index < 0 || mStore.isReadOnly(mStore.mActive))
        return;
    const int target = index + delta;
    if (target < 0 || !MO::moveMod(*profile, static_cast<std::size_t>(index), static_cast<std::size_t>(target)))
        return;
    changed();
    mModsTable->selectRow(static_cast<int>(mStore.mBaseData.size()) + target);
    startPluginReport();
}

void Launcher::ModsPage::moveModUp()
{
    moveMod(-1);
}

void Launcher::ModsPage::moveModDown()
{
    moveMod(1);
}

// ---- plugins -------------------------------------------------------------------------------------------------------

void Launcher::ModsPage::movePlugin(int delta)
{
    MO::Profile* profile = mStore.active();
    const int index = selectedPluginIndex();
    if (profile == nullptr || index < 0 || mStore.isReadOnly(mStore.mActive))
        return;
    const int target = index + delta;
    if (target < 0 || !MO::movePlugin(*profile, static_cast<std::size_t>(index), static_cast<std::size_t>(target)))
        return;
    changed();
    mPluginsTable->selectRow(target);
    startPluginReport();
}

void Launcher::ModsPage::movePluginUp()
{
    movePlugin(-1);
}

void Launcher::ModsPage::movePluginDown()
{
    movePlugin(1);
}

void Launcher::ModsPage::startPluginReport()
{
    if (mStore.active() == nullptr)
        return;
    const unsigned generation = ++mReportGeneration;
    MO::Store copy = mStore;
    startBackgroundJob(
        this, mThreads,
        [copy = std::move(copy)]() {
            return MO::buildPluginReport(copy,
                [](const std::filesystem::path& file, std::string& error) { return MO::readMasters(file, error); });
        },
        [this, generation](MO::PluginReport report) {
            if (generation != mReportGeneration)
                return; // a newer request is on its way
            mReport = std::move(report);
            std::vector<MO::PluginInfo> infos;
            for (const MO::PluginRow& row : mReport.mRows)
                infos.push_back({ row.mName, row.mActive, row.mMasters });
            mHints = MO::masterHints(infos, mReport.mBasePlugins);
            mRefreshing = true;
            refreshPlugins();
            mRefreshing = false;
        });
}

void Launcher::ModsPage::addNewPlugins()
{
    if (mStore.active() == nullptr || mStore.isReadOnly(mStore.mActive))
        return;
    setBusy(true, tr("Looking for plugins in the enabled mods..."));
    const MO::ModLibrary library = mStore.mLibrary;
    const MO::Profile profile = *mStore.active();
    startBackgroundJob(
        this, mThreads, [library, profile]() { return MO::inventoryPlugins(library, profile); },
        [this](MO::PluginInventory inventory) {
            setBusy(false);
            MO::Profile* current = mStore.active();
            if (current == nullptr)
                return;
            const std::size_t added = MO::appendNewPlugins(*current, inventory);
            changed();
            showResult(tr("%n plugin(s) added, switched off.", nullptr, static_cast<int>(added)), false);
            startPluginReport();
        });
}

void Launcher::ModsPage::removeMissingPlugins()
{
    if (mStore.active() == nullptr || mStore.isReadOnly(mStore.mActive))
        return;
    setBusy(true, tr("Looking for plugins in the enabled mods..."));
    const MO::ModLibrary library = mStore.mLibrary;
    const MO::Profile profile = *mStore.active();
    const std::vector<std::string> bases = mStore.mBaseData;
    startBackgroundJob(
        this, mThreads,
        [library, profile, bases]() {
            std::pair<MO::PluginInventory, std::vector<std::string>> result;
            result.first = MO::inventoryPlugins(library, profile);
            for (const std::string& base : bases)
            {
                const MO::ScanResult scan = MO::scanDataRoot(Files::pathFromUnicodeString(base));
                result.second.insert(result.second.end(), scan.mPlugins.begin(), scan.mPlugins.end());
            }
            return result;
        },
        [this](std::pair<MO::PluginInventory, std::vector<std::string>> found) {
            setBusy(false);
            MO::Profile* current = mStore.active();
            if (current == nullptr)
                return;
            if (found.first.mTruncated)
            {
                showResult(tr("A mod folder is very large and was only partly scanned; nothing was removed."), true);
                return;
            }
            const auto dropped = MO::dropMissingPlugins(*current, found.first, found.second);
            changed();
            showResult(
                tr("%n missing plugin(s) removed from the list.", nullptr, static_cast<int>(dropped.size())), false);
            startPluginReport();
        });
}

void Launcher::ModsPage::sortByMasters()
{
    MO::Profile* profile = mStore.active();
    if (profile == nullptr || mStore.isReadOnly(mStore.mActive))
        return;
    if (mReport.mRows.size() != profile->mPlugins.size())
    {
        showResult(tr("The plugin details are still loading. Try again in a moment."), false);
        startPluginReport();
        return;
    }
    std::vector<MO::PluginInfo> infos;
    for (const MO::PluginRow& row : mReport.mRows)
        infos.push_back({ row.mName, row.mActive, row.mMasters });
    const auto order = MO::suggestSortedOrder(infos);
    if (!order)
    {
        QMessageBox::information(this, tr("Sort by masters"),
            tr("The plugins name each other as masters in a circle, so no order can satisfy them."));
        return;
    }
    std::vector<MO::PluginEntry> sorted;
    for (const std::string& name : *order)
        for (const MO::PluginEntry& plugin : profile->mPlugins)
            if (lowerAscii(plugin.mName) == lowerAscii(name))
            {
                sorted.push_back(plugin);
                break;
            }
    int moved = 0;
    for (std::size_t i = 0; i < sorted.size() && i < profile->mPlugins.size(); ++i)
        if (sorted[i].mName != profile->mPlugins[i].mName)
            ++moved;
    if (sorted.size() != profile->mPlugins.size() || moved == 0)
    {
        QMessageBox::information(this, tr("Sort by masters"), tr("Every plugin is already after its masters."));
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Sort by masters"),
        tr("Put every plugin after its masters? %n position(s) change; everything else keeps its order.", nullptr,
            moved));
    if (answer != QMessageBox::Yes)
        return;
    profile->mPlugins = std::move(sorted);
    changed();
    startPluginReport();
}

// ---- conflicts -----------------------------------------------------------------------------------------------------

void Launcher::ModsPage::scanConflicts()
{
    if (mStore.active() == nullptr)
        return;
    setBusy(true);
    mConflictStatus->setText(tr("Scanning..."));
    const MO::Layout layout = MO::activeLayout(mStore);
    startBackgroundJob(
        this, mThreads, [layout]() { return MO::computeConflicts(layout, false); },
        [this](MO::ConflictReport report) {
            setBusy(false);
            mConflicts = std::move(report);
            QString status
                = tr("%n file(s) exist in more than one place.", nullptr, static_cast<int>(mConflicts.mFiles.size()));
            if (!mConflicts.mErrors.empty())
                status += QLatin1Char(' ') + tr("Problem: %1").arg(q(mConflicts.mErrors.front()));
            mConflictStatus->setText(status);

            auto label = [this](const std::string& source) {
                if (source == "base")
                    return tr("Game data");
                const std::string id = MO::sourceModId(source);
                if (id.empty())
                    return q(source);
                const QString name = modName(q(id));
                return source.rfind("shim:", 0) == 0 ? tr("%1 (files kept on disk)").arg(name) : name;
            };
            mConflictCounts->setRowCount(static_cast<int>(mConflicts.mCounts.size()));
            int row = 0;
            for (const MO::ModCounts& counts : mConflicts.mCounts)
            {
                mConflictCounts->setItem(row, 0, new QTableWidgetItem(label(counts.mSource)));
                mConflictCounts->setItem(row, 1, new QTableWidgetItem(QString::number(counts.mOverridden)));
                mConflictCounts->setItem(row, 2, new QTableWidgetItem(QString::number(counts.mOverriding)));
                mConflictCounts->setItem(row, 3, new QTableWidgetItem(QString::number(counts.mIdenticalDuplicates)));
                ++row;
            }
            mConflictCounts->resizeColumnsToContents();
            mConflictCounts->horizontalHeader()->setStretchLastSection(true);
            applyConflictFilter();
        });
}

void Launcher::ModsPage::applyConflictFilter()
{
    const std::string filter = lowerAscii(s(mConflictFilter->text()));
    auto label = [this](const std::string& source) {
        if (source == "base")
            return tr("Game data");
        const std::string id = MO::sourceModId(source);
        return id.empty() ? q(source) : modName(q(id));
    };
    std::size_t shown = 0;
    std::size_t matching = 0;
    mConflictFiles->setRowCount(0);
    for (const MO::ConflictFile& file : mConflicts.mFiles)
    {
        if (!filter.empty() && file.mPath.find(filter) == std::string::npos)
            continue;
        ++matching;
        if (shown >= sMaxConflictRows)
            continue;
        QStringList losers;
        for (const std::string& loser : file.mLosers)
            losers << label(loser);
        const int row = static_cast<int>(shown++);
        mConflictFiles->insertRow(row);
        mConflictFiles->setItem(row, 0, new QTableWidgetItem(q(file.mPath)));
        mConflictFiles->setItem(row, 1, new QTableWidgetItem(label(file.mWinner)));
        mConflictFiles->setItem(row, 2, new QTableWidgetItem(losers.join(QStringLiteral(", "))));
    }
    mConflictFiles->resizeColumnsToContents();
    mConflictFiles->horizontalHeader()->setStretchLastSection(true);
    if (matching > shown)
        mConflictStatus->setText(tr("%1 files match; the first %2 are shown. Type in the filter to narrow the list.")
                                     .arg(matching)
                                     .arg(shown));
}

// ---- repack check
// ----------------------------------------------------------------------------------------------------

void Launcher::ModsPage::checkRepack()
{
    const MO::Mod* mod = mStore.mLibrary.find(s(mRepackChoice->currentData().toString()));
    if (mod == nullptr)
        return;
    setBusy(true, tr("Measuring..."));
    mRepackOutput->setPlainText(tr("Measuring \"%1\"...").arg(q(mod->mName)));
    const std::filesystem::path dataRoot = Files::pathFromUnicodeString(MO::modDataPath(*mod));
    const std::filesystem::path target = mCfgMgr.getCachePath() / "openeden" / "repack";
    const QString name = q(mod->mName);
    startBackgroundJob(
        this, mThreads, [dataRoot, target]() { return MO::planRepack(dataRoot, target); },
        [this, name](MO::DryRun plan) {
            setBusy(false);
            const QLocale locale;
            QStringList lines;
            lines << tr("Repack check for \"%1\"").arg(name);
            lines << tr("Files that would go into the pack: %1 (%2)")
                         .arg(plan.mPackFileCount)
                         .arg(locale.formattedDataSize(static_cast<qint64>(plan.mPackSourceBytes)));
            lines << tr("Estimated pack size: %1")
                         .arg(locale.formattedDataSize(static_cast<qint64>(plan.mEstimatedPackBytes)));
            if (plan.mProbablyLarger)
                lines << tr(
                    "The estimate is larger than the loose files: many small files cost a block each. A repack "
                    "may not be worth it.");
            if (plan.mFreeBytes != 0)
                lines << tr("Free space on the target drive: %1. %2")
                             .arg(locale.formattedDataSize(static_cast<qint64>(plan.mFreeBytes)))
                             .arg(plan.mFits ? tr("Enough.") : tr("Not enough."));
            lines << tr("Plugins and archives that stay on disk: %1").arg(plan.mStayOnDisk.size());
            for (const MO::CollidingNames& collision : plan.mCollisions)
                lines << tr("Names that differ only in case, so they cannot be packed together: %1")
                             .arg(q(collision.mNormalized));
            for (const MO::ScanProblem& problem : plan.mProblems)
                lines << tr("Cannot be packed: %1 (%2)").arg(q(problem.mPath)).arg(q(problem.mMessage));
            if (plan.mTruncated)
                lines << tr("The folder is very large; only part of it was measured.");
            lines << (plan.canRepack() ? tr("Result: this mod can be repacked.")
                                       : tr("Result: this mod cannot be repacked."));
            mRepackOutput->setPlainText(lines.join(QLatin1Char('\n')));
        });
}

// ---- apply
// -----------------------------------------------------------------------------------------------------------

bool Launcher::ModsPage::doApply(bool acceptHandEdits)
{
    MO::ApplyOptions options;
    options.mGlobalConfig = mCfgMgr.getGlobalPath() / Files::openmwCfgFile;
    options.mGameRunning = mGameRunning;
    const std::filesystem::path cfg = mCfgMgr.getUserConfigPath() / Files::openmwCfgFile;
    const MO::ApplyOutcome outcome = MO::applyActiveProfile(mPaths, mStore, cfg, options, acceptHandEdits);

    if (outcome.mHandEdited)
    {
        const auto answer = QMessageBox::question(this, tr("Apply profile"),
            tr("openmw.cfg was changed since this profile was last applied (by hand, or by the Data Files page). "
               "Applying replaces its data, content, archive, groundcover and extension lines with this profile's. "
               "A backup copy is made first.\n\nApply anyway?"));
        return answer == QMessageBox::Yes && doApply(true);
    }
    if (!outcome.mOk)
    {
        showResult(q(outcome.mError), true);
        QMessageBox::warning(this, tr("Apply profile"), q(outcome.mError));
        return false;
    }
    QString text = outcome.mWritten ? tr("openmw.cfg was updated from the profile \"%1\".").arg(q(mStore.mActive))
                                    : tr("openmw.cfg already matched the profile \"%1\".").arg(q(mStore.mActive));
    if (!outcome.mBackup.empty())
        text += QLatin1Char(' ') + tr("Backup: %1").arg(Files::pathToQString(outcome.mBackup));
    if (!outcome.mProblems.empty())
        text += QLatin1Char(' ') + tr("Problem: %1").arg(q(outcome.mProblems.front()));
    showResult(text, !outcome.mProblems.empty());
    return true;
}

void Launcher::ModsPage::applyProfile()
{
    const MO::Profile* profile = mStore.active();
    if (profile == nullptr)
        return;
    int missing = 0;
    for (const MO::ProfileMod& entry : profile->mMods)
    {
        const MO::Mod* mod = mStore.mLibrary.find(entry.mId);
        std::error_code ec;
        if (entry.mEnabled && mod != nullptr && !std::filesystem::exists(Files::pathFromUnicodeString(mod->mRoot), ec))
            ++missing;
    }
    if (missing > 0)
    {
        const auto answer = QMessageBox::question(this, tr("Apply profile"),
            tr("%n enabled mod(s) are missing on disk (a drive may be away). The game will not find their files. "
               "Apply anyway?",
                nullptr, missing));
        if (answer != QMessageBox::Yes)
            return;
    }
    if (!saveNow())
        return;
    if (doApply(false))
    {
        refreshApplyState();
        emit profileApplied();
    }
}
