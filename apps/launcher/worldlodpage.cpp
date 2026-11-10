#include "worldlodpage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/qtconversion.hpp>
#include <components/modorganizer/processcheck.hpp>
#include <components/settings/settings.hpp>

#include "utils/backgroundjob.hpp"

#include <filesystem>

namespace
{
    namespace WL = WorldLod;

    QString q(std::string_view text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    std::string s(const QString& text)
    {
        return text.toUtf8().toStdString();
    }

    std::string categoryName()
    {
        return std::string(WL::sCategory);
    }
}

Launcher::WorldLodPage::WorldLodPage(const Files::ConfigurationManager& cfg, QWidget* parent)
    : QWidget(parent)
    , mCfgMgr(cfg)
    , mGameRunning([] { return ModOrganizer::isProcessRunning("openmw"); })
{
    setObjectName("WorldLodPage");
    buildUi();
    mTimer = new QTimer(this);
    mTimer->setInterval(2000);
    connect(mTimer, &QTimer::timeout, this, &WorldLodPage::refresh);
}

Launcher::WorldLodPage::~WorldLodPage()
{
    if (mProcess != nullptr)
    {
        mProcess->disconnect(this);
        mProcess->kill();
        mProcess->waitForFinished(3000);
    }
    waitForJobs(mThreads);
}

// ---- layout --------------------------------------------------------------------------------------------------------

void Launcher::WorldLodPage::buildUi()
{
    auto* outer = new QVBoxLayout(this);

    // The status
    auto* statusBox = new QGroupBox(tr("Status"), this);
    mState = new QLabel(statusBox);
    mState->setWordWrap(true);
    mState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = mState->font();
    bold.setBold(true);
    mState->setFont(bold);
    mState->setAccessibleName(tr("World LOD status"));
    mProgress = new QProgressBar(statusBox);
    mProgress->setAccessibleName(tr("Bake progress"));
    mProgress->setVisible(false);
    mDetails = new QLabel(statusBox);
    mDetails->setWordWrap(true);
    mDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addWidget(mState);
    statusLayout->addWidget(mProgress);
    statusLayout->addWidget(mDetails);
    outer->addWidget(statusBox);

    // What has been baked, and the bake commands
    auto* bakeBox = new QGroupBox(tr("Baked data"), this);
    mPacks = new QTableWidget(bakeBox);
    mPacks->setColumnCount(4);
    mPacks->setHorizontalHeaderLabels({ tr("Data"), tr("State"), tr("Size"), tr("Baked") });
    mPacks->horizontalHeader()->setStretchLastSection(true);
    mPacks->verticalHeader()->hide();
    mPacks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mPacks->setSelectionMode(QAbstractItemView::NoSelection);
    mPacks->setAccessibleName(tr("Baked world LOD data"));
    mPacks->setMaximumHeight(110);

    mBakeTerrain = new QPushButton(tr("Bake terrain tiles"), bakeBox);
    mBakeObjects = new QPushButton(tr("Bake object clusters"), bakeBox);
    mPause = new QPushButton(tr("Pause"), bakeBox);
    mStop = new QPushButton(tr("Stop"), bakeBox);
    mDeleteCache = new QPushButton(tr("Delete baked data..."), bakeBox);
    mPause->setEnabled(false);
    mPause->setToolTip(tr("Not available: the bake commands run once, report no progress and read no pause request."));
    auto* buttons = new QHBoxLayout;
    for (QPushButton* b : { mBakeTerrain, mBakeObjects, mPause, mStop, mDeleteCache })
        buttons->addWidget(b);
    buttons->addStretch(1);
    mCacheInfo = new QLabel(bakeBox);
    mCacheInfo->setWordWrap(true);
    mLog = new QPlainTextEdit(bakeBox);
    mLog->setReadOnly(true);
    mLog->setMaximumBlockCount(500);
    mLog->setMaximumHeight(70);
    mLog->setPlaceholderText(tr("Output of the bake appears here."));
    mLog->setAccessibleName(tr("Bake output"));
    auto* bakeLayout = new QVBoxLayout(bakeBox);
    bakeLayout->addWidget(mPacks);
    bakeLayout->addLayout(buttons);
    bakeLayout->addWidget(mCacheInfo);
    bakeLayout->addWidget(mLog);
    outer->addWidget(bakeBox);

    // The settings, in a scroll area
    auto* settingsBox = new QGroupBox(tr("Settings"), this);
    auto* scroll = new QScrollArea(settingsBox);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    buildSettingsRows(inner);
    scroll->setWidget(inner);
    auto* reset = new QPushButton(tr("Reset all to defaults"), settingsBox);
    auto* settingsLayout = new QVBoxLayout(settingsBox);
    settingsLayout->addWidget(scroll, 1);
    settingsLayout->addWidget(reset, 0, Qt::AlignLeft);
    outer->addWidget(settingsBox, 1);

    connect(mBakeTerrain, &QPushButton::clicked, this, &WorldLodPage::bakeTerrain);
    connect(mBakeObjects, &QPushButton::clicked, this, &WorldLodPage::bakeObjects);
    connect(mStop, &QPushButton::clicked, this, &WorldLodPage::stopBake);
    connect(mDeleteCache, &QPushButton::clicked, this, &WorldLodPage::deleteCache);
    connect(reset, &QPushButton::clicked, this, &WorldLodPage::resetToDefaults);
}

void Launcher::WorldLodPage::buildSettingsRows(QWidget* parent)
{
    // Every label and description is a literal tr() so that lupdate finds it
    struct Text
    {
        QString mLabel;
        QString mHelp;
    };
    const std::map<std::string, Text> texts = {
        { "enabled",
            { tr("Enable world LOD"),
                tr("Draws baked far terrain tiles. With this off none of the world LOD code runs and the game plays "
                   "as before. Bake the terrain tiles below first.") } },
        { "terrain tile threshold",
            { tr("Terrain tile error (pixels)"),
                tr("Largest error of a baked terrain tile on screen before the live terrain is used. Smaller is "
                   "finer and costs more.") } },
        { "terrain fade time",
            { tr("Terrain tile fade (seconds)"),
                tr("How long a baked tile takes to dither in or out. 0 swaps in one frame.") } },
        { "view distance",
            { tr("View distance (units, or auto)"),
                tr("How far the world is drawn while the world LOD is on. A number of units, or auto (the reach of the "
                   "loaded land, with Whole map view).") } },
        { "map view",
            { tr("Whole map view"),
                tr("Draws the baked tiles and clusters out to the view distance, so from high up the whole map shows. "
                   "Needs distant terrain.") } },
        { "max view distance",
            { tr("Largest auto view distance (units)"),
                tr("A budget for weaker hardware: auto never goes beyond it.") } },
        { "haze", { tr("Distance haze"), tr("Far land fades into the colour of the air.") } },
        { "haze visibility",
            { tr("Haze visibility (units)"),
                tr("How far a ray along the ground keeps 10 percent of its light in clear weather.") } },
        { "haze height",
            { tr("Haze height (units)"), tr("The height over which the air thins out by a factor of e.") } },
        { "haze weather gain",
            { tr("Haze weather gain"), tr("How strongly fog and storms thicken the haze. 0 ignores the weather.") } },
        { "tier",
            { tr("Hardware tier"),
                tr("auto picks from your graphics card and memory. Choose another to see what a weaker or stronger "
                   "machine would get.") } },
        { "far levels",
            { tr("Highest tile level kept (0: the tier's)"),
                tr("The highest baked terrain tile level kept in memory, 1 to 6.") } },
        { "far texture max size",
            { tr("Far object picture size (0: the tier's)"),
                tr("The longest side of the pictures of far object clusters, in pixels.") } },
        { "far memory budget",
            { tr("Far object memory (MiB, 0: the tier's)"), tr("What the far object clusters may hold in memory.") } },
        { "object clusters",
            { tr("Far object clusters"),
                tr("Draws baked clusters of distant towers, rocks, trees and buildings. Bake the object clusters below "
                   "first.") } },
        { "object cluster threshold",
            { tr("Cluster error (pixels)"),
                tr("Largest error of an object cluster on screen before finer objects are used.") } },
        { "object cluster fade time",
            { tr("Cluster fade (seconds)"), tr("How long a cluster takes to dither in or out.") } },
        { "object cluster effects",
            { tr("Moving textures on far objects"),
                tr("Bakes the scrolling of lava, waterfalls and similar into the clusters. Needs a new bake.") } },
        { "object cluster scenery",
            { tr("Include plain scenery objects"),
                tr("Lets the bake take unnamed, unscripted activators such as lava planes into the clusters. Needs a "
                   "new bake.") } },
        { "object cluster sway",
            { tr("Swaying far foliage"), tr("Far trees and plants bend in the wind. Needs a new bake.") } },
        { "object cluster sway models",
            { tr("Words that mark swaying models"),
                tr("Comma separated words matched anywhere in a model's path. Needs a new bake.") } },
        { "object cluster sway lean",
            { tr("Sway lean"),
                tr("How far the top of an object leans at full wind, as a share of its height. Needs a new bake.") } },
        { "object cluster sway wind",
            { tr("Sway wind strength"), tr("How strongly the weather's wind drives the sway.") } },
    };

    auto* layout = new QVBoxLayout(parent);
    const auto& specs = WL::settingSpecs();
    mRows.reserve(specs.size());

    std::string group;
    QFormLayout* form = nullptr;
    for (const WL::SettingSpec& spec : specs)
    {
        if (form == nullptr || spec.mGroup != group)
        {
            group = std::string(spec.mGroup);
            QString title = q(group);
            if (group == "General")
                title = tr("General");
            else if (group == "Whole map view")
                title = tr("Whole map view");
            else if (group == "Far objects")
                title = tr("Far objects");
            auto* box = new QGroupBox(title, parent);
            form = new QFormLayout(box);
            layout->addWidget(box);
        }

        Row row;
        row.mSpec = &spec;
        const auto text = texts.find(std::string(spec.mKey));
        const QString label = text != texts.end() ? text->second.mLabel : q(spec.mKey);
        QString help = text != texts.end() ? text->second.mHelp : QString();
        help += QLatin1Char(' ') + tr("(Setting: [WorldLod] %1, default %2)").arg(q(spec.mKey), q(spec.mDefault));

        switch (spec.mKind)
        {
            case WL::SettingKind::Bool:
            {
                auto* box = new QCheckBox(label, parent);
                row.mControl = box;
                connect(box, &QCheckBox::toggled, this, [this] {
                    updateNeeds();
                    refresh();
                });
                break;
            }
            case WL::SettingKind::Float:
            {
                auto* spin = new QDoubleSpinBox(parent);
                spin->setLocale(QLocale::c());
                spin->setDecimals(spec.mMax <= 1 ? 3 : (spec.mMax <= 100 ? 2 : 1));
                spin->setRange(spec.mMin, spec.mMax);
                spin->setSingleStep(spec.mMax <= 1 ? 0.01 : (spec.mMax <= 100 ? 0.1 : 1.0));
                spin->setKeyboardTracking(false);
                row.mControl = spin;
                break;
            }
            case WL::SettingKind::Int:
            {
                auto* spin = new QSpinBox(parent);
                spin->setRange(static_cast<int>(spec.mMin), static_cast<int>(spec.mMax));
                spin->setKeyboardTracking(false);
                row.mControl = spin;
                break;
            }
            case WL::SettingKind::Choice:
            {
                auto* combo = new QComboBox(parent);
                for (const std::string_view choice : spec.mChoices)
                    combo->addItem(q(choice));
                row.mControl = combo;
                break;
            }
            case WL::SettingKind::Text:
            case WL::SettingKind::DistanceOrAuto:
            {
                auto* edit = new QLineEdit(parent);
                row.mControl = edit;
                break;
            }
        }
        row.mControl->setToolTip(help);
        row.mControl->setAccessibleName(label);
        row.mControl->setAccessibleDescription(help);
        if (spec.mKind == WL::SettingKind::Bool)
            form->addRow(row.mControl);
        else
        {
            row.mLabel = new QLabel(label, parent);
            row.mLabel->setBuddy(row.mControl);
            row.mLabel->setToolTip(help);
            form->addRow(row.mLabel, row.mControl);
        }
        mRows.push_back(row);
    }
    layout->addStretch(1);

    // The rows are in place: link them by key and connect the controls that decide which rows work
    for (Row& row : mRows)
    {
        mRowByKey[std::string(row.mSpec->mKey)] = &row;
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(row.mControl))
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this] { refresh(); });
        else if (auto* edit = qobject_cast<QLineEdit*>(row.mControl))
            connect(edit, &QLineEdit::editingFinished, this, [this] { refresh(); });
    }
}

// ---- settings ------------------------------------------------------------------------------------------------------

QString Launcher::WorldLodPage::currentText(const Row& row) const
{
    if (auto* box = qobject_cast<QCheckBox*>(row.mControl))
        return box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(row.mControl))
        return QString::number(spin->value(), 'f', 4);
    if (auto* intSpin = qobject_cast<QSpinBox*>(row.mControl))
        return QString::number(intSpin->value());
    if (auto* combo = qobject_cast<QComboBox*>(row.mControl))
        return combo->currentText();
    if (auto* edit = qobject_cast<QLineEdit*>(row.mControl))
        return edit->text();
    return {};
}

void Launcher::WorldLodPage::setControl(const Row& row, const QString& text)
{
    const WL::Normalized value = WL::normalizeSetting(*row.mSpec, s(text));
    const QString shown = value.mOk ? q(value.mText) : q(row.mSpec->mDefault);
    if (auto* box = qobject_cast<QCheckBox*>(row.mControl))
        box->setChecked(shown == QLatin1String("true"));
    else if (auto* spin = qobject_cast<QDoubleSpinBox*>(row.mControl))
        spin->setValue(shown.toDouble());
    else if (auto* intSpin = qobject_cast<QSpinBox*>(row.mControl))
        intSpin->setValue(shown.toInt());
    else if (auto* combo = qobject_cast<QComboBox*>(row.mControl))
        combo->setCurrentIndex(std::max(0, combo->findText(shown)));
    else if (auto* edit = qobject_cast<QLineEdit*>(row.mControl))
        edit->setText(shown);
}

void Launcher::WorldLodPage::updateNeeds()
{
    auto valueOf = [this](std::string_view key) {
        const auto it = mRowByKey.find(std::string(key));
        return it == mRowByKey.end() ? std::string() : s(currentText(*it->second));
    };
    for (Row& row : mRows)
    {
        const std::string unmet = WL::unmetNeed(*row.mSpec, valueOf);
        row.mControl->setEnabled(unmet.empty());
        if (row.mLabel != nullptr)
            row.mLabel->setEnabled(unmet.empty());
        if (!unmet.empty())
        {
            const auto needed = mRowByKey.find(unmet);
            const QString name = needed != mRowByKey.end() ? needed->second->mControl->accessibleName() : q(unmet);
            row.mControl->setStatusTip(tr("Has no effect until \"%1\" is switched on.").arg(name));
        }
        else
            row.mControl->setStatusTip(QString());
    }
}

bool Launcher::WorldLodPage::featureEnabled() const
{
    const auto it = mRowByKey.find("enabled");
    return it != mRowByKey.end() && currentText(*it->second) == QLatin1String("true");
}

void Launcher::WorldLodPage::loadSettings()
{
    mLoading = true;
    for (const Row& row : mRows)
    {
        std::string value(row.mSpec->mDefault);
        try
        {
            value = Settings::Manager::getString(row.mSpec->mKey, categoryName());
        }
        catch (const std::exception&)
        {
            // A build whose defaults file lacks the key: show the default of the table
        }
        setControl(row, q(value));
    }
    mLoading = false;
    updateNeeds();
    refresh();
    refreshCache();
}

void Launcher::WorldLodPage::saveSettings()
{
    for (const Row& row : mRows)
    {
        const WL::Normalized wanted = WL::normalizeSetting(*row.mSpec, s(currentText(row)));
        if (!wanted.mOk)
            continue;
        std::string current(row.mSpec->mDefault);
        try
        {
            current = Settings::Manager::getString(row.mSpec->mKey, categoryName());
        }
        catch (const std::exception&)
        {
        }
        const WL::Normalized old = WL::normalizeSetting(*row.mSpec, current);
        if (old.mOk && old.mText == wanted.mText)
            continue; // the player did not change this row: the file keeps what it has
        Settings::Manager::setString(row.mSpec->mKey, categoryName(), wanted.mText);
    }
}

void Launcher::WorldLodPage::resetToDefaults()
{
    for (const Row& row : mRows)
        setControl(row, q(row.mSpec->mDefault));
    updateNeeds();
    refresh();
}

// ---- status --------------------------------------------------------------------------------------------------------

void Launcher::WorldLodPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    refreshCache();
    mTimer->start();
}

void Launcher::WorldLodPage::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    mTimer->stop();
}

QString Launcher::WorldLodPage::executablePath() const
{
    QString name = QStringLiteral("openmw");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(name);
}

void Launcher::WorldLodPage::refresh()
{
    if (mLoading || mRows.empty())
        return;
    refreshStatus();
}

void Launcher::WorldLodPage::refreshStatus()
{
    const std::filesystem::path userData = mCfgMgr.getUserDataPath();
    mPackInfo = WL::describePacks(userData);
    const bool running = mProcess != nullptr;

    // The status file, when a baker writes one
    const std::filesystem::path statusFile = WL::lodFolder(userData) / "status.json";
    std::error_code ec;
    const bool statusExists = std::filesystem::is_regular_file(statusFile, ec);
    WL::JudgeInput input;
    input.mFeatureEnabled = featureEnabled();
    input.mStatusFileExists = statusExists;
    input.mNow = QDateTime::currentSecsSinceEpoch();
    if (statusExists)
    {
        std::string error;
        if (const auto text = WL::readSmallFile(statusFile, WL::sMaxStatusBytes, error))
        {
            WL::StatusError parseError;
            input.mStatus = WL::readStatus(*text, "status.json", parseError);
            if (!input.mStatus)
                input.mUnreadableReason = parseError.format();
        }
        else
            input.mUnreadableReason = error;
    }

    const WL::Summary summary = WL::summarize(input.mFeatureEnabled, running, statusExists, mPackInfo);
    QString state;
    QString details;
    bool showProgress = false;
    int progressMax = 0;
    int progressValue = 0;
    switch (summary)
    {
        case WL::Summary::Off:
            state = tr("Off");
            details = tr(
                "The world LOD is switched off, so nothing here is used by the game. Tick \"Enable world LOD\" "
                "below to use baked data.");
            break;
        case WL::Summary::RunningHere:
            state = tr("Building");
            details = tr(
                "A bake started from this page is running. The bake commands report no progress, so the bar "
                "only shows that work is going on.");
            showProgress = true;
            break;
        case WL::Summary::FromStatusFile:
        {
            const WL::Judgement judgement = WL::judge(input);
            switch (judgement.mState)
            {
                case WL::PageState::Off:
                    state = tr("Off");
                    break;
                case WL::PageState::NotBuilt:
                    state = tr("Not built");
                    break;
                case WL::PageState::Building:
                    state = tr("Building");
                    break;
                case WL::PageState::Paused:
                    state = tr("Paused");
                    break;
                case WL::PageState::UpToDate:
                    state = tr("Up to date");
                    break;
                case WL::PageState::OutOfDate:
                    state = tr("Out of date");
                    break;
                case WL::PageState::Failed:
                    state = tr("Failed");
                    break;
                case WL::PageState::Unknown:
                    state = tr("Unknown");
                    break;
            }
            if (!judgement.mReason.empty())
                details = q(judgement.mReason);
            if (input.mStatus)
            {
                const WL::BakeStatus& status = *input.mStatus;
                QStringList lines;
                if (!status.mActivity.empty())
                    lines << tr("Working on: %1").arg(q(status.mActivity));
                if (status.mLevelsTotal > 0)
                    lines << tr("Level %1 of %2").arg(status.mLevel).arg(status.mLevelsTotal);
                if (status.mUnitsTotal > 0)
                    lines << tr("%1 of %2 units done").arg(status.mUnitsDone).arg(status.mUnitsTotal);
                if (status.mErrorsTotal > 0)
                    lines << tr("%n error(s)", nullptr, static_cast<int>(status.mErrorsTotal));
                if (const auto eta = WL::shownEtaSeconds(status))
                    lines << tr("About %1 minute(s) left (an estimate)").arg((*eta + 59) / 60);
                if (status.mCacheBytes > 0)
                    lines << tr("Cache: %1").arg(QLocale().formattedDataSize(status.mCacheBytes));
                if (!lines.isEmpty())
                    details
                        += (details.isEmpty() ? QString() : QStringLiteral("\n")) + lines.join(QStringLiteral(". "));
                if ((judgement.mState == WL::PageState::Building || judgement.mState == WL::PageState::Paused)
                    && status.mUnitsTotal > 0)
                {
                    showProgress = true;
                    progressMax = static_cast<int>(std::min<std::int64_t>(status.mUnitsTotal, 1000000));
                    progressValue = static_cast<int>(std::min<std::int64_t>(status.mUnitsDone, progressMax));
                }
            }
            break;
        }
        case WL::Summary::Baked:
            state = tr("Baked");
            details = tr(
                "Terrain tiles and object clusters are present. The bake commands write no status, so this "
                "page cannot tell whether they are out of date: bake again after changing your mods or the "
                "object settings.");
            break;
        case WL::Summary::PartlyBaked:
            state = tr("Partly baked");
            details = tr("Only one of the two kinds of data exists. Bake the other one to use everything.");
            break;
        case WL::Summary::NotBaked:
            state = tr("Not baked yet");
            details = tr("Use the buttons below to bake. A bake needs your game data and takes a few minutes.");
            break;
    }
    if (!mLastRun.isEmpty())
        details += (details.isEmpty() ? QString() : QStringLiteral("\n")) + mLastRun;
    mState->setText(state);
    mDetails->setText(details);
    mProgress->setVisible(showProgress);
    if (showProgress)
    {
        mProgress->setRange(0, progressMax);
        mProgress->setValue(progressValue);
    }

    // The table of what exists
    mPacks->setRowCount(static_cast<int>(mPackInfo.size()));
    int row = 0;
    for (const WL::PackInfo& pack : mPackInfo)
    {
        const QString what = pack.mKind == WL::PackKind::TerrainTiles ? tr("Terrain tiles") : tr("Object clusters");
        mPacks->setItem(row, 0, new QTableWidgetItem(what));
        mPacks->setItem(row, 1, new QTableWidgetItem(pack.mExists ? tr("Present") : tr("Not baked")));
        mPacks->setItem(row, 2,
            new QTableWidgetItem(
                pack.mExists ? QLocale().formattedDataSize(static_cast<qint64>(pack.mBytes)) : QString()));
        mPacks->setItem(row, 3,
            new QTableWidgetItem(pack.mExists && pack.mModifiedSeconds > 0
                    ? QLocale().toString(QDateTime::fromSecsSinceEpoch(pack.mModifiedSeconds), QLocale::ShortFormat)
                    : QString()));
        ++row;
    }
    mPacks->resizeColumnsToContents();
    mPacks->horizontalHeader()->setStretchLastSection(true);

    // Which buttons work, and why not
    const bool exeFound = QFileInfo::exists(executablePath());
    const bool gameRunning = mGameRunning();
    QString whyNot;
    if (running)
        whyNot = tr("A bake is already running.");
    else if (!exeFound)
        whyNot = tr("The game executable was not found next to the launcher.");
    else if (gameRunning)
        whyNot = tr("The game is running; close it first.");
    for (QPushButton* button : { mBakeTerrain, mBakeObjects })
    {
        button->setEnabled(whyNot.isEmpty());
        button->setToolTip(whyNot.isEmpty()
                ? tr("Starts the game's bake command on your game data and settings. Your settings are saved first.")
                : whyNot);
    }
    mStop->setEnabled(running);
    mDeleteCache->setEnabled(!running && mCacheListed && !mCacheListing.mFiles.empty());
}

void Launcher::WorldLodPage::refreshCache()
{
    const std::filesystem::path folder = WL::lodFolder(mCfgMgr.getUserDataPath());
    startBackgroundJob(
        this, mThreads, [folder]() { return WL::listCacheFiles(folder); },
        [this](WL::CacheListing listing) {
            mCacheListing = std::move(listing);
            mCacheListed = true;
            mCacheInfo->setText(
                tr("Baked data folder: %1. %n file(s), %2.", nullptr, static_cast<int>(mCacheListing.mFiles.size()))
                    .arg(Files::pathToQString(WL::lodFolder(mCfgMgr.getUserDataPath())),
                        QLocale().formattedDataSize(static_cast<qint64>(mCacheListing.mBytes))));
            refreshStatus();
        });
}

// ---- running a bake ------------------------------------------------------------------------------------------------

void Launcher::WorldLodPage::bakeTerrain()
{
    startBake(WL::PackKind::TerrainTiles);
}

void Launcher::WorldLodPage::bakeObjects()
{
    startBake(WL::PackKind::ObjectClusters);
}

void Launcher::WorldLodPage::startBake(WL::PackKind kind)
{
    if (mProcess != nullptr)
        return;
    emit settingsNeedSaving();
    mRunningKind = kind;
    mStopRequested = false;
    mLastRun.clear();
    mLog->clear();

    QStringList arguments;
    for (const std::string& argument : WL::bakeArguments(kind))
        arguments << q(argument);

    mProcess = new QProcess(this);
    mProcess->setProcessChannelMode(QProcess::MergedChannels);
    mProcess->setWorkingDirectory(QCoreApplication::applicationDirPath());
    connect(mProcess, &QProcess::readyRead, this, [this] {
        if (mProcess != nullptr)
            mLog->appendPlainText(QString::fromLocal8Bit(mProcess->readAll()).trimmed());
    });
    connect(mProcess, &QProcess::finished, this, [this] { onProcessFinished(); });
    connect(mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && mProcess != nullptr)
        {
            mLastRun = tr("The bake could not be started: %1").arg(mProcess->errorString());
            mProcess->deleteLater();
            mProcess = nullptr;
            refreshStatus();
        }
    });
    mLog->appendPlainText(tr("Starting: %1 %2").arg(executablePath(), arguments.join(QLatin1Char(' '))));
    mProcess->start(executablePath(), arguments);
    refreshStatus();
}

void Launcher::WorldLodPage::onProcessFinished()
{
    if (mProcess == nullptr)
        return;
    mLog->appendPlainText(QString::fromLocal8Bit(mProcess->readAll()).trimmed());
    const int code = mProcess->exitCode();
    const bool crashed = mProcess->exitStatus() == QProcess::CrashExit;
    const QString what = mRunningKind == WL::PackKind::TerrainTiles ? tr("terrain tiles") : tr("object clusters");
    if (mStopRequested)
        mLastRun = tr("The bake of the %1 was stopped.").arg(what);
    else if (crashed || code != 0)
        mLastRun
            = tr("The bake of the %1 failed (exit code %2). See the output above and openmw.log.").arg(what).arg(code);
    else
        mLastRun = tr("The bake of the %1 finished.").arg(what);
    mProcess->deleteLater();
    mProcess = nullptr;
    refreshStatus();
    refreshCache();
}

void Launcher::WorldLodPage::stopBake()
{
    if (mProcess == nullptr)
        return;
    const auto answer = QMessageBox::question(this, tr("Stop the bake"),
        tr("Stop the running bake? Data written so far may be incomplete; bake again to replace it."));
    if (answer != QMessageBox::Yes || mProcess == nullptr)
        return;
    mStopRequested = true;
    mProcess->kill();
}

void Launcher::WorldLodPage::deleteCache()
{
    const std::filesystem::path folder = WL::lodFolder(mCfgMgr.getUserDataPath());
    if (!mCacheListed || mCacheListing.mFiles.empty())
        return;
    const auto answer = QMessageBox::question(this, tr("Delete baked data"),
        tr("Delete %n baked file(s) (%1) in %2? They can be baked again; your game, mods and settings are not "
           "touched. This cannot be undone.",
            nullptr, static_cast<int>(mCacheListing.mFiles.size()))
            .arg(QLocale().formattedDataSize(static_cast<qint64>(mCacheListing.mBytes)), Files::pathToQString(folder)));
    if (answer != QMessageBox::Yes)
        return;
    const std::string error = WL::deleteCache(folder, mCacheListing, mProcess != nullptr, mGameRunning());
    if (!error.empty())
        QMessageBox::warning(this, tr("Delete baked data"), q(error));
    refreshStatus();
    refreshCache();
}
