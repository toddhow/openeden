#include "extensionmanager.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <apps/opencs/model/extension/editorextensionhost.hpp>

#include <components/files/qtconversion.hpp>

namespace
{
    using CSMExtension::ExtensionInfo;

    QString stateText(ExtensionInfo::State state)
    {
        switch (state)
        {
            case ExtensionInfo::State::Active:
                return QObject::tr("Active");
            case ExtensionInfo::State::Failed:
                return QObject::tr("Failed");
            case ExtensionInfo::State::Installed:
                break;
        }
        return QObject::tr("Not loaded");
    }
}

CSVDoc::ExtensionManagerDialog::ExtensionManagerDialog(const CSMExtension::EditorExtensionHost* host, QWidget* parent)
    : QDialog(parent)
    , mHost(host)
    , mList(new QTreeWidget(this))
{
    setWindowTitle(tr("Extension Manager"));
    resize(820, 360);

    mList->setObjectName("extensionList");
    mList->setRootIsDecorated(false);
    mList->setUniformRowHeights(true);
    mList->setHeaderLabels({ tr("Name"), tr("ID"), tr("Version"), tr("Enabled"), tr("State"), tr("Details") });

    QLabel* note = new QLabel(
        tr("Extensions are listed in openmw.cfg as extension=<id> lines (the game reads the same lines). "
           "Changes apply the next time Eden Studio starts. Extensions are loaded read-only here: "
           "they cannot change a document."),
        this);
    note->setWordWrap(true);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(mList, 1);
    layout->addWidget(note);
    layout->addWidget(buttons);

    refresh();
}

void CSVDoc::ExtensionManagerDialog::refresh()
{
    mList->clear();
    if (mHost == nullptr)
        return;

    for (const ExtensionInfo& info : mHost->extensions())
    {
        QString details = QString::fromStdString(info.mMessage);
        if (details.isEmpty() && !info.mDirectory.empty())
            details = Files::pathToQString(info.mDirectory);

        QTreeWidgetItem* item = new QTreeWidgetItem(mList,
            { QString::fromStdString(info.mName.empty() ? info.mId : info.mName), QString::fromStdString(info.mId),
                QString::fromStdString(info.mVersion), info.mEnabled ? tr("Yes") : tr("No"), stateText(info.mState),
                details });
        if (info.mState == ExtensionInfo::State::Failed)
            item->setForeground(4, Qt::red);
        item->setToolTip(5, details);
    }
    for (int column = 0; column < mList->columnCount() - 1; ++column)
        mList->resizeColumnToContents(column);
}
