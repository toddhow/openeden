#include "startup.hpp"

#include <components/misc/scalableicon.hpp>

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QVBoxLayout>

QPushButton* CSVDoc::StartupDialogue::addButton(const QString& label, const QString& icon)
{
    int column = mColumn--;

    QPushButton* button = new QPushButton(this);

    button->setIcon(Misc::ScalableIcon::load(icon));

    button->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));

    mLayout->addWidget(button, 0, column);

    mLayout->addWidget(new QLabel(label, this), 1, column, Qt::AlignCenter);

    int width = mLayout->itemAtPosition(1, column)->widget()->sizeHint().width();

    if (width > mWidth)
        mWidth = width;

    return button;
}

QWidget* CSVDoc::StartupDialogue::createButtons()
{
    QWidget* widget = new QWidget(this);

    mLayout = new QGridLayout(widget);

    /// \todo add icons
    QPushButton* loadDocument = addButton("Edit A Content File", ":startup/edit-content");
    mLoadDocument = loadDocument;
    connect(loadDocument, &QPushButton::clicked, this, &StartupDialogue::loadDocument);

    QPushButton* createAddon = addButton("Create A New Addon", ":startup/create-addon");
    mCreateAddon = createAddon;
    connect(createAddon, &QPushButton::clicked, this, &StartupDialogue::createAddon);

    QPushButton* createGame = addButton("Create A New Game", ":startup/create-game");
    mCreateGame = createGame;
    connect(createGame, &QPushButton::clicked, this, &StartupDialogue::createGame);

    for (int i = 0; i < 3; ++i)
        mLayout->setColumnMinimumWidth(i, mWidth);

    mLayout->setRowMinimumHeight(0, mWidth);

    mLayout->setSizeConstraint(QLayout::SetMinimumSize);
    mLayout->setHorizontalSpacing(32);

    mLayout->setContentsMargins(16, 16, 16, 8);

    loadDocument->setIconSize(QSize(mWidth, mWidth));
    createGame->setIconSize(QSize(mWidth, mWidth));
    createAddon->setIconSize(QSize(mWidth, mWidth));

    widget->setLayout(mLayout);

    return widget;
}

QWidget* CSVDoc::StartupDialogue::createTools()
{
    QWidget* widget = new QWidget(this);

    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setDirection(QBoxLayout::RightToLeft);
    layout->setContentsMargins(4, 4, 4, 4);

    QPushButton* config = new QPushButton(widget);

    config->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    config->setIcon(Misc::ScalableIcon::load(":startup/configure"));
    config->setToolTip("Open user settings");

    layout->addWidget(config);

    // Task 0173: shown only when the session's game pack is not Morrowind (setGamePack)
    mBrowseWorld = new QPushButton(tr("Browse the world"), widget);
    mBrowseWorld->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    mBrowseWorld->setVisible(false);
    layout->addWidget(mBrowseWorld);
    connect(mBrowseWorld, &QPushButton::clicked, this, &StartupDialogue::browseWorld);

    layout->addWidget(new QWidget, 1); // dummy widget; stops buttons from taking all the space

    widget->setLayout(layout);

    connect(config, &QPushButton::clicked, this, &StartupDialogue::editConfig);

    return widget;
}

CSVDoc::StartupDialogue::StartupDialogue()
    : mWidth(0)
    , mColumn(2)
{
    setWindowTitle("Eden Studio");

    QVBoxLayout* layout = new QVBoxLayout(this);

    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(createButtons());
    layout->addWidget(createTools());

    /// \todo remove this label once we are feature complete and convinced that this thing is
    /// working properly.
    QLabel* warning = new QLabel(
        "<font color=Red>WARNING: Eden Studio is in alpha stage.<p>The editor is not feature complete and not "
        "sufficiently tested.<br>In theory your data should be safe. But we strongly advise to make backups regularly "
        "if you are working with live data.</font color>");

    QFont font;
    font.setPointSize(12);
    font.setBold(true);

    warning->setFont(font);
    warning->setWordWrap(true);

    layout->addWidget(warning, 1);

    setLayout(layout);

    QRect scr = QGuiApplication::primaryScreen()->geometry();
    QRect rect = geometry();
    move(scr.center().x() - rect.center().x(), scr.center().y() - rect.center().y());
}

void CSVDoc::StartupDialogue::setGamePack(const QString& name, bool worldAvailable)
{
    setWindowTitle(tr("Eden Studio - %1").arg(name));
    const QString reason = tr("This game pack has no record editor yet");
    for (QPushButton* button : { mLoadDocument, mCreateAddon, mCreateGame })
    {
        if (button == nullptr)
            continue;
        button->setEnabled(false);
        button->setToolTip(reason);
    }
    mBrowseWorld->setVisible(true);
    mBrowseWorld->setEnabled(worldAvailable);
    mBrowseWorld->setToolTip(worldAvailable ? tr("Open a cell of the world of %1").arg(name)
                                            : tr("%1 registered no world: see the Output Log").arg(name));
}
