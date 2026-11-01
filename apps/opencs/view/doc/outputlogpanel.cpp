#include "outputlogpanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "logtap.hpp"

namespace
{
    constexpr int pollIntervalMs = 250;
}

CSVDoc::OutputLogPanel::OutputLogPanel(QWidget* parent)
    : QWidget(parent)
    , mText(new QPlainTextEdit(this))
    , mTimer(new QTimer(this))
{
    mText->setReadOnly(true);
    mText->setObjectName("outputLogText");
    mText->setMaximumBlockCount(static_cast<int>(LogTap::maxLines));
    mText->setLineWrapMode(QPlainTextEdit::NoWrap);

    QPushButton* clear = new QPushButton(tr("Clear"), this);
    connect(clear, &QPushButton::clicked, mText, &QPlainTextEdit::clear);

    QLabel* hint = new QLabel(tr("Verifier results: File > Verify. Game output: Simulation > Run Log."), this);

    QHBoxLayout* top = new QHBoxLayout;
    top->addWidget(hint, 1);
    top->addWidget(clear);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(mText, 1);

    mTimer->setInterval(pollIntervalMs);
    connect(mTimer, &QTimer::timeout, this, &OutputLogPanel::refresh);
}

void CSVDoc::OutputLogPanel::refresh()
{
    LogTap* tap = LogTap::get();
    if (tap == nullptr)
        return;

    std::vector<std::string> lines;
    tap->linesSince(mCursor, lines);
    for (const std::string& line : lines)
        mText->appendPlainText(QString::fromUtf8(line.data(), static_cast<int>(line.size())));
}

void CSVDoc::OutputLogPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    mTimer->start();
}

void CSVDoc::OutputLogPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    mTimer->stop();
}
